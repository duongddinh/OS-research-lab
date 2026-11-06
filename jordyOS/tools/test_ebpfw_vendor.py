"""Verify upstream provenance and fail closed on unreviewed source changes."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from embed_ebpfw import generate

MANIFEST = Path("net/bpf/upstream.json")
VENDOR = Path(os.environ.get("EBPFW_DIR", "../vendor/ebpfw"))


class Vendor(unittest.TestCase):
    def test_exported_xdp_is_original_except_section(self):
        outputs = generate(VENDOR, MANIFEST)
        source = (VENDOR / "bpf/stateful.c").read_text()
        body = outputs["ebpfw_early.h"].split('SEC("xdp")', 1)[1]
        self.assertIn('SEC("xdp.frags")' + body, source)
        for helper in ["be16", "be32", "prefix", "name_equal", "matches"]:
            self.assertIn(helper + "(", outputs["ebpfw_shared.h"])
        self.assertIn("INLINE void count(", outputs["ebpfw_count.h"])

    def test_unreviewed_source_change_is_rejected(self):
        pin = json.loads(MANIFEST.read_text())
        with tempfile.TemporaryDirectory(prefix="jordyos-vendor-") as directory:
            vendor = Path(directory)
            path = vendor / pin["source"]
            path.parent.mkdir(parents=True)
            path.write_bytes((VENDOR / pin["source"]).read_bytes() + b"\n/* changed */\n")
            with self.assertRaisesRegex(ValueError, "reviewed hash"):
                generate(vendor, MANIFEST)


if __name__ == "__main__":
    unittest.main()
