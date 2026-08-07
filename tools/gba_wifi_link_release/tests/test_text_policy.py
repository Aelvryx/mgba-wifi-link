"""Shared public-text privacy classification tests."""

from urllib.parse import quote
import unittest

from tools.gba_wifi_link_release.text_policy import classify_private_text


class TextPolicyTest(unittest.TestCase):
    def test_ordinary_public_repository_path_is_allowed(self):
        self.assertIsNone(
            classify_private_text(
                "Read https://github.com/Aelvryx/mgba-wifi-link/issues/1 for context."
            )
        )

    def test_url_authority_and_path_bypasses_are_category_only_paths(self):
        quadruple_encoded = quote(
            quote(quote(quote("/private/SYNTHETIC_ESCAPE", safe=""), safe=""), safe=""),
            safe="",
        )
        cases = (
            "https://username:password@github.com/Aelvryx/mgba-wifi-link/issues/1",
            "https://github.com/%2Fprivate%2FSYNTHETIC_SINGLE_PATH",
            "https://github.com/%252Fprivate%252FSYNTHETIC_DOUBLE_PATH",
            "https://github.com/Aelvryx/../private/SYNTHETIC_TRAVERSAL",
            "https://github.com/" + quadruple_encoded,
            "https://github.com/?path=" + "x" * 4_097,
        )
        for value in cases:
            with self.subTest(value=value):
                self.assertEqual(classify_private_text(value), "PATH")
