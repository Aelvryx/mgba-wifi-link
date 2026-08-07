"""Release package construction and verified-artifact publication commands."""

import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from .admission import (CANONICAL_REPOSITORY, REQUIRED_GATES, REQUIRED_WORKFLOW,
                        admit_release, verify_remote_tag)
from .github import GhClient
from .model import (ActualBuildEvidence, BuildEvidence, GateResult, ReleaseAsset,
                    REQUIRED_BUILD_CONFIGURATION, ReleaseContext)
from .packager import PackageInputs, build_release
from .render import render_release_body
from .publisher import publish_release
from .provenance import bind_actual_builds
from .tag_policy import (load_tag_policy, read_tag_rulesets,
                         validate_tag_policy_response)
from .verifier import verify_release


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/gba_wifi_link_release/fixtures/synthetic"
TAG_POLICY = ROOT / ".github/rulesets/gba-wifi-link-release-tags.json"
_AT_FDCWD = -100
_RENAME_NOREPLACE = 1


def _identity_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("CLI_BUILD_IDENTITIES")
        value[key] = item
    return value


def _rename_noreplace(source: Path, destination: Path) -> None:
    """Atomically install a sibling directory only when destination is absent."""
    try:
        renameat2 = ctypes.CDLL(None, use_errno=True).renameat2
    except AttributeError as error:
        raise OSError(errno.ENOSYS, "renameat2 unavailable") from error
    renameat2.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
                          ctypes.c_char_p, ctypes.c_uint)
    renameat2.restype = ctypes.c_int
    if renameat2(_AT_FDCWD, os.fsencode(source), _AT_FDCWD,
                 os.fsencode(destination), _RENAME_NOREPLACE) != 0:
        error_number = ctypes.get_errno()
        raise OSError(error_number, os.strerror(error_number), destination)


def _context_dict(context: ReleaseContext) -> dict[str, object]:
    assert context.build is not None
    return {
        "build": {"actual_builds": [_actual_build_dict(build)
                                      for build in context.build.actual_builds],
                  "configuration": dict(context.build.configuration),
                  "pinned_actions": list(context.build.pinned_actions),
                  "pinned_toolchains": list(context.build.pinned_toolchains),
                  "runner_image": context.build.runner_image},
        "commit": context.commit,
        "gates": [gate.__dict__ for gate in context.gates],
        "notes_sha256": context.notes_sha256,
        "prerelease": context.prerelease,
        "repository": context.repository,
        "source_date_epoch": context.source_date_epoch,
        "tag": context.tag,
        "tag_object": context.tag_object,
        "version": context.version,
    }


def _actual_build_dict(build: ActualBuildEvidence) -> dict[str, object]:
    return {
        "cmake_version": build.cmake_version,
        "compiler_sha256": build.compiler_sha256,
        "compiler_version": build.compiler_version,
        "configuration": dict(build.configuration),
        "core": {"name": build.core.name, "sha256": build.core.sha256,
                 "size": build.core.size},
        "job_id": build.job_id,
        "ndk_revision": build.ndk_revision,
        "ndk_source_properties_sha256": build.ndk_source_properties_sha256,
        "ninja_version": build.ninja_version,
        "pinned_actions": list(build.pinned_actions),
        "role": build.role,
        "run_id": build.run_id,
        "runner_image_os": build.runner_image_os,
        "runner_image_version": build.runner_image_version,
        "source_commit": build.source_commit,
        "source_date_epoch": build.source_date_epoch,
    }


def _actual_build_from_dict(value: object) -> ActualBuildEvidence:
    required = {
        "cmake_version", "compiler_sha256", "compiler_version", "configuration",
        "core", "job_id", "ndk_revision", "ndk_source_properties_sha256",
        "ninja_version", "pinned_actions", "role", "run_id", "runner_image_os",
        "runner_image_version", "source_commit", "source_date_epoch",
    }
    if not isinstance(value, dict) or set(value) != required:
        raise ValueError("CLI_BUILD_IDENTITIES")
    core = value["core"]
    configuration = value["configuration"]
    if not isinstance(core, dict) or set(core) != {"name", "sha256", "size"}:
        raise ValueError("CLI_BUILD_IDENTITIES")
    if not isinstance(configuration, dict):
        raise ValueError("CLI_BUILD_IDENTITIES")
    try:
        return ActualBuildEvidence(
            role=value["role"], run_id=value["run_id"], job_id=value["job_id"],
            runner_image_os=value["runner_image_os"],
            runner_image_version=value["runner_image_version"],
            ndk_revision=value["ndk_revision"],
            ndk_source_properties_sha256=value["ndk_source_properties_sha256"],
            compiler_sha256=value["compiler_sha256"],
            compiler_version=value["compiler_version"],
            cmake_version=value["cmake_version"], ninja_version=value["ninja_version"],
            source_commit=value["source_commit"],
            source_date_epoch=value["source_date_epoch"],
            configuration=tuple(configuration.items()),
            core=ReleaseAsset(core["name"], core["size"], core["sha256"]),
            pinned_actions=tuple(value["pinned_actions"]),
        )
    except (KeyError, TypeError) as error:
        raise ValueError("CLI_BUILD_IDENTITIES") from error


def _context_from_dict(value: object) -> ReleaseContext:
    if not isinstance(value, dict):
        raise ValueError("CLI_CONTEXT")
    try:
        raw_build = value["build"]
        if not isinstance(raw_build, dict):
            raise ValueError
        build = BuildEvidence(
            raw_build["runner_image"], tuple(raw_build["pinned_actions"]),
            tuple(raw_build["pinned_toolchains"]), tuple(raw_build["configuration"].items()),
            tuple(_actual_build_from_dict(item) for item in raw_build.get("actual_builds", ())),
        )
        gates = tuple(GateResult(item["name"], item["workflow"], item["run_id"], item["job_id"], item["conclusion"])
                      for item in value["gates"])
        return ReleaseContext(value["repository"], value["tag"], value["tag_object"], value["commit"],
                              value["version"], value["source_date_epoch"], value["prerelease"], gates,
                              value["notes_sha256"], build)
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("CLI_CONTEXT") from error


def _synthetic_context() -> ReleaseContext:
    raw = json.loads((FIXTURE / "input/context.json").read_text(encoding="utf-8"))
    notes = (FIXTURE / "release-notes/v9.8.7.md").read_bytes()
    core_path = FIXTURE / "input/mgba_libretro_android.so"
    core = ReleaseAsset(
        core_path.name, core_path.stat().st_size,
        hashlib.sha256(core_path.read_bytes()).hexdigest(),
    )
    checkout = "actions/checkout@v6+sha:0123456789abcdef0123456789abcdef01234567"
    download = "actions/download-artifact@v5+sha:" + "1" * 40
    upload = "actions/upload-artifact@v4+sha:89abcdef0123456789abcdef0123456789abcdef"
    common = {
        "run_id": 900,
        "runner_image_os": "synthetic-linux",
        "runner_image_version": "20260125.1",
        "ndk_revision": "27.2.12479018",
        "ndk_source_properties_sha256": "d" * 64,
        "compiler_sha256": "e" * 64,
        "compiler_version": "Android clang version 18.0.3",
        "cmake_version": "cmake version 3.31.6",
        "ninja_version": "1.12.1",
        "source_commit": raw["commit"],
        "source_date_epoch": raw["source_date_epoch"],
        "configuration": REQUIRED_BUILD_CONFIGURATION,
        "core": core,
    }
    actual_builds = (
        ActualBuildEvidence(role="protected", job_id=901,
                            pinned_actions=(checkout, download, upload), **common),
        ActualBuildEvidence(role="independent", job_id=902,
                            pinned_actions=(checkout, upload), **common),
    )
    return ReleaseContext(
        repository=raw["repository"], tag=raw["tag"], tag_object=raw["tag_object"], commit=raw["commit"],
        version=raw["tag"][1:], source_date_epoch=raw["source_date_epoch"], prerelease=True,
        gates=tuple(GateResult(name, REQUIRED_WORKFLOW, 100 + index, 200 + index, "success")
                    for index, name in enumerate(REQUIRED_GATES)),
        notes_sha256=hashlib.sha256(notes).hexdigest(),
        build=BuildEvidence(
            raw["runner_image"],
            (
                "actions/checkout@0123456789abcdef0123456789abcdef01234567",
                "actions/download-artifact@" + "1" * 40,
                "actions/upload-artifact@89abcdef0123456789abcdef0123456789abcdef",
            ),
            ("android-ndk@27.2.12479018+sha256:" + "d" * 64,),
            (("android_abi", "arm64-v8a"), ("android_api", "21")),
            actual_builds,
        ),
    )


def _load_context(args: argparse.Namespace) -> ReleaseContext:
    if args.fixture == "synthetic":
        return _synthetic_context()
    if not args.context:
        raise ValueError("CLI_CONTEXT")
    return _context_from_dict(json.loads(Path(args.context).read_text(encoding="utf-8")))


def _inputs(args: argparse.Namespace) -> PackageInputs:
    if args.fixture == "synthetic":
        source = FIXTURE / "input"
        return PackageInputs(source / "mgba_libretro_android.so", source / "gba-link-test.gba",
                             source / "gba-link-continuous.gba", source / "LICENSE",
                             ROOT / "packaging/gba-wifi-link/release/templates/INSTALL-AND-USAGE.md.in",
                             ROOT / "packaging/gba-wifi-link/release/templates/SOURCE-AND-PROVENANCE.md.in",
                             (FIXTURE / "release-notes/v9.8.7.md").read_bytes())
    paths = (args.core, args.test_fixture, args.continuous_fixture, args.licence,
             args.install_template, args.source_template, args.notes)
    if any(path is None for path in paths):
        raise ValueError("CLI_INPUT")
    return PackageInputs(*(Path(path) for path in paths[:-1]), Path(paths[-1]).read_bytes())


def _build_atomic(context: ReleaseContext, inputs: PackageInputs, output: Path,
                  *, before_install=None, renamer=_rename_noreplace) -> None:
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("CLI_OUTPUT")
    staging_parent = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    staging = staging_parent / output.name
    try:
        build_release(context, inputs, staging)
        if before_install is not None:
            before_install()
        try:
            renamer(staging, output)
        except OSError as error:
            raise ValueError("CLI_INSTALL") from error
        verify_release(output, context)
    finally:
        shutil.rmtree(staging_parent, ignore_errors=True)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="gba-wifi-link-release")
    commands = parser.add_subparsers(dest="command", required=True)
    admit = commands.add_parser("admit")
    admit.add_argument("--repo", required=True)
    admit.add_argument("--tag", required=True)
    admit.add_argument("--evidence", required=True)
    admit.add_argument("--allow-existing-release", action="store_true")
    for name in ("build", "verify", "render-body"):
        command = commands.add_parser(name)
        command.add_argument("--fixture", choices=("synthetic",))
        command.add_argument("--context")
    build = commands.choices["build"]
    build.add_argument("--output", required=True)
    for option in ("core", "test-fixture", "continuous-fixture", "licence", "install-template", "source-template", "notes"):
        build.add_argument("--" + option)
    verify = commands.choices["verify"]
    verify.add_argument("--output", required=True)
    render = commands.choices["render-body"]
    render.add_argument("--notes")
    publish = commands.add_parser("publish")
    publish.add_argument("--context", required=True)
    publish.add_argument("--output", required=True)
    publish.add_argument("--body", required=True)
    publish.add_argument("--repository", required=True)
    publish.add_argument("--gh-bin")
    publish.add_argument("--test-mode", action="store_true", help=argparse.SUPPRESS)
    verify_tag = commands.add_parser("verify-tag")
    verify_tag.add_argument("--context", required=True)
    verify_tag.add_argument("--repository", required=True)
    bind_builds = commands.add_parser("bind-builds")
    bind_builds.add_argument("--context", required=True)
    bind_builds.add_argument("--identities", required=True)
    commands.add_parser("verify-tag-policy")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "admit":
            context = admit_release(Path(args.repo), args.tag,
                                    json.loads(Path(args.evidence).read_text(encoding="utf-8")),
                                    allow_existing_release=args.allow_existing_release)
            sys.stdout.write(json.dumps(_context_dict(context), sort_keys=True,
                                        separators=(",", ":")) + "\n")
        elif args.command == "build":
            _build_atomic(_load_context(args), _inputs(args), Path(args.output))
        elif args.command == "verify":
            verify_release(Path(args.output), _load_context(args))
        elif args.command == "render-body":
            context = _load_context(args)
            if not args.fixture and not args.notes:
                raise ValueError("CLI_INPUT")
            notes = (FIXTURE / "release-notes/v9.8.7.md").read_bytes() if args.fixture else Path(args.notes).read_bytes()
            sys.stdout.write(render_release_body(context, notes.decode("utf-8")).decode("utf-8"))
        elif args.command == "verify-tag":
            context = _context_from_dict(json.loads(Path(args.context).read_text(encoding="utf-8")))
            verify_remote_tag(context, args.repository)
        elif args.command == "bind-builds":
            context = _context_from_dict(json.loads(Path(args.context).read_text(encoding="utf-8")))
            raw = json.loads(
                Path(args.identities).read_text(encoding="utf-8"),
                object_pairs_hook=_identity_object,
            )
            if not isinstance(raw, dict) or set(raw) != {"protected", "independent"}:
                raise ValueError("CLI_BUILD_IDENTITIES")
            identities = tuple(_actual_build_from_dict(raw[role])
                               for role in ("protected", "independent"))
            bound = bind_actual_builds(context, identities)
            sys.stdout.write(json.dumps(_context_dict(bound), sort_keys=True,
                                        separators=(",", ":")) + "\n")
        elif args.command == "verify-tag-policy":
            expected = load_tag_policy(TAG_POLICY)
            validate_tag_policy_response(read_tag_rulesets(CANONICAL_REPOSITORY), expected)
            sys.stdout.write("tag policy verified\n")
        else:
            if args.repository != CANONICAL_REPOSITORY:
                raise ValueError("CLI_REPOSITORY")
            if args.gh_bin and not args.test_mode:
                raise ValueError("CLI_GH_BIN")
            context = _context_from_dict(json.loads(Path(args.context).read_text(encoding="utf-8")))
            if context.repository != args.repository:
                raise ValueError("CLI_REPOSITORY")
            release_set = verify_release(Path(args.output), context)
            verify_remote_tag(context, args.repository)
            result = publish_release(
                GhClient(args.repository, gh=args.gh_bin or "gh",
                         source_digest=context.commit), release_set,
                Path(args.body).read_bytes(),
            )
            sys.stdout.write(json.dumps({"release_id": result.release_id, "reused": result.reused},
                                        sort_keys=True, separators=(",", ":")) + "\n")
        return 0
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(str(error) if str(error) else "CLI_ERROR", file=sys.stderr)
        return 2
