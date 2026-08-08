"""Bounded parsing and archive resource-budget contracts."""

import unittest
from unittest.mock import patch

from tools.gba_wifi_link_release.resource_limits import (
    CONTRACT_PATH,
    ResourceLimitError,
    bounded_canonical_json,
)
from tools.gba_wifi_link_release.model import load_contract


class ResourceLimitTest(unittest.TestCase):
    def test_json_byte_ceiling_is_checked_before_parsing(self):
        oversized = b'{"value":"' + (b"a" * 1_048_576) + b'"}\n'
        contract = load_contract(CONTRACT_PATH)
        with patch("tools.gba_wifi_link_release.resource_limits.json.loads") as loads:
            with self.assertRaisesRegex(ResourceLimitError, "^RESOURCE_JSON_SIZE$"):
                bounded_canonical_json(oversized, required={"value"}, contract=contract)
        loads.assert_not_called()

    def test_duplicate_and_overdeep_json_are_rejected_with_bounded_categories(self):
        with self.assertRaisesRegex(ResourceLimitError, "^RESOURCE_JSON_DUPLICATE$"):
            bounded_canonical_json(b'{"value":1,"value":2}\n', required={"value"})
        nested = b'{"value":' + (b"[" * 21) + b"0" + (b"]" * 21) + b"}\n"
        with self.assertRaisesRegex(ResourceLimitError, "^RESOURCE_JSON_DEPTH$"):
            bounded_canonical_json(nested, required={"value"})
        too_many = b'{"value":[' + b",".join(b"0" for _ in range(4096)) + b"]}\n"
        with self.assertRaisesRegex(ResourceLimitError, "^RESOURCE_JSON_NODES$"):
            bounded_canonical_json(too_many, required={"value"})


if __name__ == "__main__":
    unittest.main()
