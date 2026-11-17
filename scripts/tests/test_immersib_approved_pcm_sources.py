# SPDX-License-Identifier: AGPL-3.0-only
"""The acoustic source lock must be verifiable and fail on any hidden retune."""
import copy
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from verify_immersib_approved_pcm_sources import (  # noqa: E402
    REQUIRED_ROOTS, SCHEMA, github_blob_hash, validate,
)


class ApprovedImmerSIBSourceLockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(
            (ROOT / "config/immersib_approved_pcm_sources.json").read_text(encoding="utf-8")
        )

    def test_frozen_29_source_files_match_public_owner_accepted_alpha26(self):
        self.assertEqual(self.manifest["schema"], SCHEMA)
        self.assertEqual(validate(ROOT, self.manifest), 29)
        self.assertTrue(REQUIRED_ROOTS.issubset(
            {item["path"] for item in self.manifest["files"]}
        ))

    def test_sha_uses_git_blob_framing_and_changes_with_one_byte(self):
        self.assertEqual(github_blob_hash(b"test\n"), "9daeafb9864cf43055ae93beb0afd6c7d144bfa4")
        self.assertNotEqual(github_blob_hash(b"test\n"), github_blob_hash(b"test!"))

    def test_missing_duplicate_and_untrusted_entries_are_rejected(self):
        truncated = copy.deepcopy(self.manifest)
        truncated["files"].pop()
        with self.assertRaisesRegex(ValueError, "LOCK_INCOMPLETE|FILES_MISMATCH"):
            validate(ROOT, truncated)
        repeated = copy.deepcopy(self.manifest)
        repeated["files"][-1] = repeated["files"][0]
        with self.assertRaisesRegex(ValueError, "LOCK_DUPLICATE"):
            validate(ROOT, repeated)
        outsider = copy.deepcopy(self.manifest)
        outsider["files"][0]["path"] = "../../outside.cpp"
        with self.assertRaisesRegex(ValueError, "LOCK_UNTRUSTED_PATH"):
            validate(ROOT, outsider)

    def test_one_bit_of_dsp_source_mutation_fails_even_if_test_hash_is_unmodified(self):
        with tempfile.TemporaryDirectory() as directory:
            temp_root = Path(directory)
            for item in self.manifest["files"]:
                destination = temp_root / item["path"]
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / item["path"], destination)
            self.assertEqual(validate(temp_root, self.manifest), 29)
            p = temp_root / "engine/builtin_fx/sonku/SonKuSpatialCore.h"
            p.write_bytes(p.read_bytes() + b"\n// attempted unapproved DSP retune\n")
            with self.assertRaisesRegex(ValueError, "IMMERSIB_APPROVED_PCM_SOURCE_CHANGED"):
                validate(temp_root, self.manifest)


if __name__ == "__main__":
    unittest.main()
