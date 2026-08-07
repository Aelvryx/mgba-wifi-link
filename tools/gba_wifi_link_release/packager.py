"""Deterministic construction of the schema-v1 public release set."""

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import stat
import time
import zipfile

from .model import ReleaseAsset, ReleaseContext, ReleaseSet, load_contract
from .provenance import build_provenance, release_provenance


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = ROOT / "packaging/gba-wifi-link/release/contract-v1.json"


class PackageError(ValueError):
    """A bounded release-package construction rejection."""


@dataclass(frozen=True)
class PackageInputs:
    core: Path
    test_fixture: Path
    continuous_fixture: Path
    licence: Path
    install_template: Path
    source_template: Path
    release_notes: bytes


def _normal_text(data: bytes) -> bytes:
    """Return UTF-8, LF-only text with trailing horizontal space removed."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise PackageError("PACKAGE_INPUT") from error
    if "\x00" in text:
        raise PackageError("PACKAGE_INPUT")
    return ("\n".join(line.rstrip(" \t") for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n")).rstrip("\n") + "\n").encode("utf-8")


def _read_regular(path: Path, *, text: bool = False) -> bytes:
    try:
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or stat.S_ISLNK(status.st_mode):
            raise PackageError("PACKAGE_INPUT")
        data = path.read_bytes()
    except (OSError, PackageError) as error:
        if isinstance(error, PackageError):
            raise
        raise PackageError("PACKAGE_INPUT") from error
    return _normal_text(data) if text else data


def _asset(name: str, data: bytes) -> ReleaseAsset:
    return ReleaseAsset(name, len(data), hashlib.sha256(data).hexdigest())


def _sums(items: tuple[tuple[str, bytes], ...]) -> bytes:
    return b"".join(hashlib.sha256(data).hexdigest().encode("ascii") + b"  " + name.encode("utf-8") + b"\n"
                    for name, data in items)


def _render_template(template: bytes, context: ReleaseContext) -> bytes:
    values = {
        "repository": context.repository,
        "tag": context.tag,
        "tag_object": context.tag_object,
        "commit": context.commit,
        "source_date_epoch": str(context.source_date_epoch),
    }
    text = _normal_text(template).decode("utf-8")
    for name, value in values.items():
        text = text.replace("{{" + name + "}}", value)
    if "{{" in text or "}}" in text:
        raise PackageError("PACKAGE_TEMPLATE")
    return _normal_text(text.encode("utf-8"))


def zip_timestamp(epoch: int) -> tuple[int, int, int, int, int, int]:
    """Encode an epoch as ZIP's canonical two-second-resolution UTC stamp."""
    if type(epoch) is not int or epoch < 315532800 or epoch > 4354819199:
        raise PackageError("PACKAGE_EPOCH")
    stamp = time.gmtime(epoch)[:6]
    if stamp[0] < 1980 or stamp[0] > 2107:
        raise PackageError("PACKAGE_EPOCH")
    return (*stamp[:5], stamp[5] & ~1)


def zip_info(name: str, epoch: int) -> zipfile.ZipInfo:
    """Make one byte-stable Unix regular-file ZIP member descriptor."""
    info = zipfile.ZipInfo(name, zip_timestamp(epoch))
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    info.extra = b""
    info.comment = b""
    return info


def _write(path: Path, data: bytes) -> None:
    path.write_bytes(data)
    os.chmod(path, 0o644)


def _archive(path: Path, members: tuple[tuple[str, bytes], ...], epoch: int) -> bytes:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9, strict_timestamps=True) as archive:
        archive.comment = b""
        for name, data in members:
            archive.writestr(zip_info(name, epoch), data, compress_type=zipfile.ZIP_DEFLATED,
                             compresslevel=9)
    os.chmod(path, 0o644)
    return path.read_bytes()


def _contract_names(context: ReleaseContext) -> tuple[str, ...]:
    return tuple(name.replace("{tag}", context.tag) for name in load_contract(CONTRACT_PATH).public_assets)


def canonical_license(contract) -> bytes:
    """Return the contract-pinned, normalized tracked MPL-2.0 text."""
    try:
        data = _normal_text((ROOT / "LICENSE").read_bytes())
    except OSError as error:
        raise PackageError("PACKAGE_LICENCE") from error
    if hashlib.sha256(data).hexdigest() != contract.license_sha256:
        raise PackageError("PACKAGE_LICENCE")
    return data


def build_release(context: ReleaseContext, inputs: PackageInputs, output_dir: Path) -> ReleaseSet:
    """Build exactly the contract's release files in an initially absent directory."""
    contract = load_contract(CONTRACT_PATH)
    if output_dir.exists() or output_dir.is_symlink():
        raise PackageError("PACKAGE_OUTPUT")
    try:
        output_dir.mkdir(parents=False)
    except OSError as error:
        raise PackageError("PACKAGE_OUTPUT") from error
    core = _read_regular(inputs.core)
    test = _read_regular(inputs.test_fixture)
    continuous = _read_regular(inputs.continuous_fixture)
    licence = _read_regular(inputs.licence, text=True)
    expected_licence = canonical_license(contract)
    if licence != expected_licence:
        raise PackageError("PACKAGE_LICENCE")
    install = _render_template(_read_regular(inputs.install_template, text=True), context)
    source = _render_template(_read_regular(inputs.source_template, text=True), context)
    if (
        _normal_text(inputs.release_notes) != inputs.release_notes
        or hashlib.sha256(inputs.release_notes).hexdigest() != context.notes_sha256
    ):
        raise PackageError("PACKAGE_INPUT")

    raw = {
        "mgba_libretro_android.so": core,
        "gba-link-test.gba": test,
        "gba-link-continuous.gba": continuous,
        "INSTALL-AND-USAGE.md": install,
        "LICENSE": expected_licence,
        "SOURCE-AND-PROVENANCE.md": source,
    }
    siblings = tuple(_asset(name, raw[name]) for name in contract.build_provenance_siblings)
    raw["BUILD-PROVENANCE.json"] = build_provenance(context, siblings)
    internal_scope = tuple((name, raw[name]) for name in contract.archive_sha256_members)
    raw["SHA256SUMS"] = _sums(internal_scope)
    archive_members = tuple((name, raw[name]) for name in contract.archive_members)
    archive_name = next(name for name in _contract_names(context) if name.endswith(".zip"))
    archive_data = _archive(output_dir / archive_name, archive_members, context.source_date_epoch)
    raw[archive_name] = archive_data

    public_names = _contract_names(context)
    for name in public_names[:4]:
        _write(output_dir / name, raw[name])
    # The archive was emitted above to make release provenance acyclic.
    payloads = tuple(_asset(name, raw[name]) for name in
                     tuple(name.replace("{tag}", context.tag) for name in contract.release_provenance_assets))
    raw["RELEASE-PROVENANCE.json"] = release_provenance(context, payloads)
    standalone_scope = tuple((name, raw[name]) for name in
                             tuple(name.replace("{tag}", context.tag) for name in contract.standalone_sha256_assets))
    raw["SHA256SUMS"] = _sums(standalone_scope)
    _write(output_dir / "RELEASE-PROVENANCE.json", raw["RELEASE-PROVENANCE.json"])
    _write(output_dir / "SHA256SUMS", raw["SHA256SUMS"])
    from .verifier import verify_release
    return verify_release(output_dir, context)
