"""Deterministic rendering of reviewed release notes and generated facts."""

from pathlib import Path
import hashlib

from .admission import (
    AdmissionError,
    REQUIRED_GATES,
    REQUIRED_WORKFLOW,
    validate_notes_text,
)
from .model import ReleaseContext, load_contract


_CONTRACT = (
    Path(__file__).resolve().parents[2]
    / "packaging/gba-wifi-link/release/contract-v1.json"
)


def _validate_context(context: ReleaseContext) -> None:
    if tuple(gate.name for gate in context.gates) != REQUIRED_GATES:
        raise AdmissionError("GATE_SET")
    if any(
        type(gate.run_id) is not int
        or gate.run_id <= 0
        or type(gate.job_id) is not int
        or gate.job_id <= 0
        or gate.workflow != REQUIRED_WORKFLOW
        or gate.conclusion != "success"
        for gate in context.gates
    ):
        raise AdmissionError("GATE_CONCLUSION")


def render_release_body(context: ReleaseContext, notes: str) -> bytes:
    """Render reviewed prose with canonical source, workflow, and asset facts."""
    _validate_context(context)
    if not isinstance(notes, str):
        raise AdmissionError("NOTES_FORMAT")
    validate_notes_text(
        notes,
        context.tag,
        (context.tag_object, context.commit),
    )
    if hashlib.sha256(notes.encode("utf-8")).hexdigest() != context.notes_sha256:
        raise AdmissionError("NOTES_CONTENT")
    contract = load_contract(_CONTRACT)
    assets = tuple(name.replace("{tag}", context.tag) for name in contract.public_assets)
    facts = [
        f"# mGBA GBA Wi-Fi Link {context.tag}\n\n",
        notes,
        "\n" if notes.endswith("\n") else "\n\n",
        "## Source and verification\n\n",
        f"- Repository: `{context.repository}`\n",
        f"- Tag: `{context.tag}`\n",
        f"- Annotated tag object: `{context.tag_object}`\n",
        f"- Peeled commit: `{context.commit}`\n",
        "- Release provenance: `RELEASE-PROVENANCE.json`\n",
        "- Checksums: `SHA256SUMS`\n\n",
        "## Workflow evidence\n\n",
    ]
    facts.extend(
        f"- `{gate.name}`: workflow `{gate.workflow}`, run `{gate.run_id}`, job `{gate.job_id}`, "
        f"conclusion `{gate.conclusion}`\n"
        for gate in context.gates
    )
    facts.append("\n## Release assets\n\n")
    facts.extend(f"- `{asset}`\n" for asset in assets)
    return "".join(facts).encode("utf-8")
