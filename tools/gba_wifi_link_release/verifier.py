"""Fail-closed verification for a schema-v1 deterministic release set."""

from pathlib import Path
import stat
import zipfile

from .model import ReleaseAsset, ReleaseContext, ReleaseSet, load_contract
from .packager import (CONTRACT_PATH, PackageError, _asset, _render_template,
                       _sums, canonical_license, zip_timestamp)
from .privacy import PrivacyError, validate_public_tree
from .provenance import build_provenance, release_provenance
from .text_policy import classify_private_text


class VerificationError(ValueError):
    """A bounded release verification rejection."""


def _fail(category: str) -> None:
    raise VerificationError(category)


def _read_regular(path: Path) -> bytes:
    try:
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or stat.S_ISLNK(status.st_mode):
            _fail("VERIFY_TYPE")
        if stat.S_IMODE(status.st_mode) != 0o644:
            _fail("VERIFY_MODE")
        return path.read_bytes()
    except VerificationError:
        raise
    except OSError as error:
        raise VerificationError("VERIFY_FILE_SET") from error


def _names(context: ReleaseContext) -> tuple[str, ...]:
    return tuple(name.replace("{tag}", context.tag) for name in load_contract(CONTRACT_PATH).public_assets)


def _check_sums(data: bytes, expected: tuple[tuple[str, bytes], ...]) -> None:
    if data != _sums(expected):
        _fail("VERIFY_CHECKSUM")


def _check_archive(data: bytes, context: ReleaseContext, public: dict[str, bytes]) -> None:
    contract = load_contract(CONTRACT_PATH)
    try:
        expected_stamp = zip_timestamp(context.source_date_epoch)
    except PackageError as error:
        raise VerificationError("VERIFY_ARCHIVE") from error
    try:
        from io import BytesIO
        with zipfile.ZipFile(BytesIO(data)) as archive:
            if archive.comment != b"":
                _fail("VERIFY_ARCHIVE")
            entries = archive.infolist()
            names = tuple(entry.filename for entry in entries)
            if names != contract.archive_members or len(set(names)) != len(names):
                _fail("VERIFY_ARCHIVE")
            raw: dict[str, bytes] = {}
            for entry in entries:
                if (
                    entry.date_time != expected_stamp or entry.create_system != 3
                    or (entry.external_attr >> 16) != 0o100644
                    or entry.extra != b"" or entry.comment != b""
                    or entry.compress_type != zipfile.ZIP_DEFLATED
                    or entry.is_dir()
                ):
                    _fail("VERIFY_ARCHIVE")
                raw[entry.filename] = archive.read(entry)
    except (OSError, zipfile.BadZipFile) as error:
        raise VerificationError("VERIFY_ARCHIVE") from error
    for name in ("BUILD-PROVENANCE.json", "INSTALL-AND-USAGE.md", "SHA256SUMS",
                 "SOURCE-AND-PROVENANCE.md"):
        try:
            text = raw[name].decode("utf-8")
        except UnicodeDecodeError as error:
            raise VerificationError("VERIFY_PRIVACY") from error
        if "\r" in text or classify_private_text(text):
            _fail("VERIFY_PRIVACY")
    try:
        expected_licence = canonical_license(contract)
    except PackageError as error:
        raise VerificationError("VERIFY_LICENCE") from error
    if raw["LICENSE"] != expected_licence:
        _fail("VERIFY_LICENCE")
    if raw["mgba_libretro_android.so"] != public["mgba_libretro_android.so"] or raw["gba-link-test.gba"] != public["gba-link-test.gba"] or raw["gba-link-continuous.gba"] != public["gba-link-continuous.gba"] or raw["INSTALL-AND-USAGE.md"] != public["INSTALL-AND-USAGE.md"]:
        _fail("VERIFY_ARCHIVE")
    expected_source = _render_template(
        (Path(__file__).resolve().parents[2] / "packaging/gba-wifi-link/release/templates/SOURCE-AND-PROVENANCE.md.in").read_bytes(), context)
    if raw["SOURCE-AND-PROVENANCE.md"] != expected_source:
        _fail("VERIFY_STALE")
    siblings = tuple(_asset(name, raw[name]) for name in contract.build_provenance_siblings)
    if raw["BUILD-PROVENANCE.json"] != build_provenance(context, siblings):
        _fail("VERIFY_PROVENANCE")
    _check_sums(raw["SHA256SUMS"], tuple((name, raw[name]) for name in contract.archive_sha256_members))


def verify_release(output_dir: Path, context: ReleaseContext) -> ReleaseSet:
    """Verify exact public and archive membership, metadata, checksums, and provenance."""
    contract = load_contract(CONTRACT_PATH)
    try:
        root_status = output_dir.lstat()
        if not stat.S_ISDIR(root_status.st_mode) or stat.S_ISLNK(root_status.st_mode):
            _fail("VERIFY_ROOT")
        observed = tuple(sorted(path.name for path in output_dir.iterdir()))
        expected_names = _names(context)
        if observed != tuple(sorted(expected_names)):
            _fail("VERIFY_FILE_SET")
        public = {name: _read_regular(output_dir / name) for name in expected_names}
        expected_install = _render_template(
            (Path(__file__).resolve().parents[2] / "packaging/gba-wifi-link/release/templates/INSTALL-AND-USAGE.md.in").read_bytes(), context)
        if public["INSTALL-AND-USAGE.md"] != expected_install:
            _fail("VERIFY_STALE")
        archive_name = next(name for name in expected_names if name.endswith(".zip"))
        _check_archive(public[archive_name], context, public)
        payloads = tuple(_asset(name, public[name]) for name in
                         tuple(name.replace("{tag}", context.tag) for name in contract.release_provenance_assets))
        if public["RELEASE-PROVENANCE.json"] != release_provenance(context, payloads):
            _fail("VERIFY_PROVENANCE")
        _check_sums(public["SHA256SUMS"], tuple((name, public[name]) for name in
                                                  tuple(name.replace("{tag}", context.tag) for name in contract.standalone_sha256_assets)))
        try:
            validate_public_tree(output_dir, contract)
        except PrivacyError as error:
            raise VerificationError("VERIFY_PRIVACY") from error
        return ReleaseSet(context, tuple(_asset(name, public[name]) for name in expected_names), output_dir)
    except VerificationError:
        raise
    except OSError as error:
        raise VerificationError("VERIFY_FILE_SET") from error
