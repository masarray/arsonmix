# SPDX-License-Identifier: AGPL-3.0-only
import copy
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
with patch.object(sys, "path", [str(ROOT / "scripts"), *sys.path]):
    import factoryfx_candidate_probe as probe


class CandidateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = ROOT / "config/factoryfx-qualification-candidates.json"

    def test_exact_four_candidates_without_approval(self):
        rows = probe.read_candidates(self.path)
        self.assertEqual(len(rows), 4)
        self.assertEqual({r["productId"] for r in rows}, probe.bundle.FOUR_IDS)
        self.assertTrue(all("hostQualification" not in r and "licenseReview" not in r for r in rows))

    def test_duplicate_or_foreign_candidate_rejected(self):
        obj = json.loads(self.path.read_text())
        for variant in ("duplicate", "supplier", "digest"):
            rows = copy.deepcopy(obj)
            if variant == "duplicate":
                rows["candidates"][1] = rows["candidates"][0]
            if variant == "supplier":
                rows["candidates"][0]["distributionRepo"] = "other/repo"
            if variant == "digest":
                rows["candidates"][0]["artifactSha256"] = "not-a-digest"
            pin = self.root / "pins.json"
            pin.write_text(json.dumps(rows))
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                probe.read_candidates(pin)

    def test_pe_architecture_rejects_wrong_or_truncated_binary(self):
        file = self.root / "test.vst3"
        image = bytearray(128)
        image[0:2] = b"MZ"
        struct.pack_into("<I", image, 60, 64)
        image[64:68] = b"PE\0\0"
        struct.pack_into("<H", image, 68, 0x8664)
        struct.pack_into("<H", image, 88, 0x20b)
        file.write_bytes(image)
        probe.check_pe(file)
        struct.pack_into("<H", image, 68, 0x14c)
        file.write_bytes(image)
        with self.assertRaises(ValueError):
            probe.check_pe(file)
        file.write_bytes(b"MZ")
        with self.assertRaises(ValueError):
            probe.check_pe(file)

    def test_sdk_moduleinfo_trailing_comma_preserves_strings(self):
        path = self.root / "moduleinfo.json"
        path.write_text(r'{"Classes":[{"Name":"example, }","CID":"a",},],}')
        self.assertEqual(probe.read_module_info(path), {"Classes":[{"Name":"example, }","CID":"a"}]})
        path.write_text('{"bad": unquoted}')
        with self.assertRaises(json.JSONDecodeError):
            probe.read_module_info(path)
