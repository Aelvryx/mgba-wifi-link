"""Shared public-text privacy classification tests."""

from urllib.parse import quote
import unittest

from tools.gba_wifi_link_release.text_policy import classify_private_text


class TextPolicyTest(unittest.TestCase):
    def test_schema_v1_project_issue_and_release_urls_are_allowed(self):
        urls = (
            "https://github.com/Aelvryx/mgba-wifi-link",
            "https://github.com/Aelvryx/mgba-wifi-link/",
            "https://github.com/Aelvryx/mgba-wifi-link/issues/1",
            "https://github.com/Aelvryx/mgba-wifi-link/releases",
            "https://github.com/Aelvryx/mgba-wifi-link/releases/tag/v9.8.7",
        )
        for index, url in enumerate(urls):
            with self.subTest(case=index):
                self.assertIsNone(classify_private_text(f"Read {url} for context."))

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
        for index, value in enumerate(cases):
            with self.subTest(case=index):
                self.assertEqual(classify_private_text(value), "PATH")

    def test_schema_v1_rejects_filesystem_material_in_url_wrappers(self):
        raw_private_paths = (
            "/root/SYNTHETIC_RAW_ROOT",
            "/mnt/SYNTHETIC_RAW_MNT",
        )
        repeated_private_paths = tuple(
            encoded
            for path in raw_private_paths
            for encoded in (
                quote(path, safe=""),
                quote(quote(path, safe=""), safe=""),
            )
        )
        windows_paths = (
            r"C:\Users\SYNTHETIC_RAW_DRIVE",
            quote(r"C:\Users\SYNTHETIC_ENCODED_DRIVE", safe=""),
            quote(quote(r"C:\Users\SYNTHETIC_DOUBLE_DRIVE", safe=""), safe=""),
            r"\\host\share\SYNTHETIC_RAW_UNC",
            quote(r"\\host\share\SYNTHETIC_ENCODED_UNC", safe=""),
            quote(quote(r"\\host\share\SYNTHETIC_DOUBLE_UNC", safe=""), safe=""),
        )
        traversal_paths = (
            "Aelvryx/../root/SYNTHETIC_RAW_TRAVERSAL",
            "Aelvryx/%2E%2E/root/SYNTHETIC_ENCODED_TRAVERSAL",
            "Aelvryx/%252E%252E/root/SYNTHETIC_DOUBLE_TRAVERSAL",
        )
        cases = tuple(f"https://github.com{path}" for path in raw_private_paths)
        cases += tuple(f"https://github.com/{path}" for path in repeated_private_paths)
        cases += tuple(f"https://github.com/{path}" for path in windows_paths)
        cases += tuple(f"https://github.com/{path}" for path in traversal_paths)
        for index, value in enumerate(cases):
            with self.subTest(case=index):
                self.assertEqual(classify_private_text(value), "PATH")

    def test_schema_v1_rejects_non_allowlisted_url_forms(self):
        quadruple_encoded = quote(
            quote(quote(quote("/root/SYNTHETIC_UNRESOLVED", safe=""), safe=""), safe=""),
            safe="",
        )
        oversized_encoded = quote("/root/" + "x" * 4_097, safe="")
        cases = (
            "https://username:password@github.com/Aelvryx/mgba-wifi-link/issues/1",
            "http://github.com/Aelvryx/mgba-wifi-link/releases",
            "https://example.com/Aelvryx/mgba-wifi-link/releases",
            "https://github.com/other/project/releases",
            "https://github.com/Aelvryx/mgba-wifi-link?path=public",
            "https://github.com/Aelvryx/mgba-wifi-link#public",
            "https://github.com/Aelvryx/mgba-wifi-link/%ZZ",
            "https://github.com/" + quadruple_encoded,
            "https://github.com/" + oversized_encoded,
        )
        for index, value in enumerate(cases):
            with self.subTest(case=index):
                self.assertEqual(classify_private_text(value), "PATH")
