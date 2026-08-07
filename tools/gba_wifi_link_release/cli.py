"""Non-publishing command line entry points for release package construction."""

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

from .admission import REQUIRED_GATES, REQUIRED_WORKFLOW, admit_release
from .model import BuildEvidence, GateResult, ReleaseContext
from .packager import PackageInputs, build_release
from .render import render_release_body
from .verifier import verify_release


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/gba_wifi_link_release/fixtures/synthetic"
_AT_FDCWD = -100
_RENAME_NOREPLACE = 1


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
        "build": {"configuration": dict(context.build.configuration),
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
    return ReleaseContext(
        repository=raw["repository"], tag=raw["tag"], tag_object=raw["tag_object"], commit=raw["commit"],
        version=raw["tag"][1:], source_date_epoch=raw["source_date_epoch"], prerelease=True,
        gates=tuple(GateResult(name, REQUIRED_WORKFLOW, 100 + index, 200 + index, "success")
                    for index, name in enumerate(REQUIRED_GATES)),
        notes_sha256=hashlib.sha256(notes).hexdigest(),
        build=BuildEvidence(
            raw["runner_image"],
            ("actions/checkout@0123456789abcdef0123456789abcdef01234567",),
            ("android-ndk@27.2.12479018+sha256:" + "d" * 64,),
            (("android_abi", "arm64-v8a"), ("android_api", "21")),
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
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "admit":
            context = admit_release(Path(args.repo), args.tag,
                                    json.loads(Path(args.evidence).read_text(encoding="utf-8")))
            sys.stdout.write(json.dumps(_context_dict(context), sort_keys=True,
                                        separators=(",", ":")) + "\n")
        elif args.command == "build":
            _build_atomic(_load_context(args), _inputs(args), Path(args.output))
        elif args.command == "verify":
            verify_release(Path(args.output), _load_context(args))
        else:
            context = _load_context(args)
            if not args.fixture and not args.notes:
                raise ValueError("CLI_INPUT")
            notes = (FIXTURE / "release-notes/v9.8.7.md").read_bytes() if args.fixture else Path(args.notes).read_bytes()
            sys.stdout.write(render_release_body(context, notes.decode("utf-8")).decode("utf-8"))
        return 0
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(str(error) if str(error) else "CLI_ERROR", file=sys.stderr)
        return 2
