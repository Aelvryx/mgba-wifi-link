"""Tests for deterministic, generated release-body metadata."""

import unittest
import hashlib
from dataclasses import replace

from tools.gba_wifi_link_release.admission import AdmissionError, REQUIRED_GATES
from tools.gba_wifi_link_release.model import GateResult, ReleaseContext
from tools.gba_wifi_link_release.render import render_release_body


CONTEXT = ReleaseContext(
    repository="Aelvryx/mgba-wifi-link",
    tag="v9.8.7",
    tag_object="a" * 40,
    commit="b" * 40,
    version="9.8.7",
    source_date_epoch=1_700_000_000,
    prerelease=True,
    gates=tuple(
        GateResult(name, "GBA Wi-Fi Link", index + 100, index + 200, "success")
        for index, name in enumerate(REQUIRED_GATES)
    ),
    notes_sha256="",
)


class RenderTest(unittest.TestCase):
    def test_body_is_utf8_lf_and_keeps_reviewed_notes_byte_for_byte(self):
        notes = "Reviewed release scope.\n\nA second reviewed paragraph.\n"

        body = render_release_body(
            replace(CONTEXT, notes_sha256=hashlib.sha256(notes.encode("utf-8")).hexdigest()),
            notes,
        )

        self.assertEqual(body, body.decode("utf-8").encode("utf-8"))
        self.assertTrue(body.endswith(b"\n"))
        self.assertNotIn(b"\r", body)
        self.assertIn(notes.encode("utf-8"), body)
        self.assertLess(body.index(b"Repository:"), body.index(b"Annotated tag object:"))
        self.assertLess(body.index(b"Annotated tag object:"), body.index(b"Peeled commit:"))
        self.assertIn(b"## Workflow evidence\n", body)
        self.assertIn(
            b"- `Complete normal mGBA suite`: workflow `GBA Wi-Fi Link`, run `100`, job `200`, conclusion `success`\n",
            body,
        )
        self.assertIn(b"## Release assets\n", body)
        self.assertIn(b"- `mgba-gba-wifi-link-v9.8.7-android-arm64.zip`\n", body)
        self.assertNotIn(b"{{", body)

    def test_notes_cannot_supply_generated_identity_or_placeholder(self):
        with self.assertRaisesRegex(AdmissionError, "NOTES_GENERATED_FIELD"):
            render_release_body(CONTEXT, "Peeled commit: bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n")
        with self.assertRaisesRegex(AdmissionError, "NOTES_PLACEHOLDER"):
            render_release_body(CONTEXT, "TODO: write notes\n")
        with self.assertRaisesRegex(AdmissionError, "NOTES_GENERATED_FIELD"):
            render_release_body(CONTEXT, "Compatibility: custom protocol claim\n")
        with self.assertRaisesRegex(AdmissionError, "NOTES_PRIVACY_ADDRESS"):
            render_release_body(CONTEXT, "Contact 192.0.2.1 for setup\n")
        with self.assertRaisesRegex(AdmissionError, "NOTES_GENERATED_FIELD"):
            render_release_body(CONTEXT, "## Qualification\n")

    def test_render_rejects_notes_other_than_admitted_tracked_content(self):
        notes = "Reviewed release scope.\n"
        admitted = replace(
            CONTEXT,
            notes_sha256=hashlib.sha256(notes.encode("utf-8")).hexdigest(),
        )

        with self.assertRaisesRegex(AdmissionError, "NOTES_CONTENT"):
            render_release_body(admitted, "Other reviewed prose.\n")
