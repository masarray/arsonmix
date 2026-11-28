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
    REQUIRED_ROOTS, SCHEMA, APPROVED_SHA, APPROVED_FILE_COUNT, github_blob_hash, validate,
    APPROVED_ORIGINAL_BLOB, OPTIMIZED_BLOB, OPTIMIZED_PATH,
    OPTIMIZED_PROVENANCE_PATH, SONKU_CONTROL_PATH, SONKU_CONTROL_ORIGINAL, SONKU_CONTROL_VARIANT,
    SONKU_FIELD_PINS, SONKU_FIELD_RECEIPT,
)


class ApprovedImmerSIBSourceLockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(
            (ROOT / "config/immersib_approved_pcm_sources.json").read_text(encoding="utf-8")
        )

    def test_frozen_sources_match_owner_accepted_p102_3(self):
        self.assertEqual(self.manifest["schema"], SCHEMA)
        self.assertEqual(self.manifest["approvedSourceSha"], APPROVED_SHA)
        self.assertEqual(self.manifest["approval"]["pr"], 863)
        self.assertEqual(self.manifest["approval"]["status"], "owner_approved_for_main_integration")
        self.assertIn("engine/builtin_fx/sonku/SonKuFixedPaMusicalEq.h", {x["path"] for x in self.manifest["files"]})
        self.assertEqual(validate(ROOT, self.manifest), APPROVED_FILE_COUNT)
        historic = next(item for item in self.manifest["files"]
                      if item["path"] == OPTIMIZED_PATH)
        self.assertEqual(historic["blob"], APPROVED_ORIGINAL_BLOB)
        self.assertEqual(
            github_blob_hash((ROOT / OPTIMIZED_PATH).read_bytes().replace(b"\r\n", b"\n")),
            SONKU_FIELD_PINS[OPTIMIZED_PATH][1],
        )
        self.assertNotEqual(OPTIMIZED_BLOB, APPROVED_ORIGINAL_BLOB)
        self.assertEqual(next(x["blob"] for x in self.manifest["files"]
                             if x["path"]==SONKU_CONTROL_PATH), SONKU_CONTROL_ORIGINAL)
        self.assertEqual(github_blob_hash((ROOT/SONKU_CONTROL_PATH).read_bytes().replace(b"\r\n",b"\n")),
                         SONKU_FIELD_PINS[SONKU_CONTROL_PATH][1])
        self.assertTrue(REQUIRED_ROOTS.issubset(
            {item["path"] for item in self.manifest["files"]}
        ))

    def test_historical_alpha26_manifest_preserved(self):
        historical=json.loads((ROOT/"config/archive/immersib_approved_pcm_sources_alpha26.json").read_text(encoding="utf-8"))
        self.assertEqual(historical["approvedSourceSha"], "8ec5b8a3e07837c85fb6e06c952cff1621fecc51")
        self.assertEqual(len(historical["files"]),29)
        with self.assertRaisesRegex(ValueError,"INVALID_MANIFEST"):
            validate(ROOT,historical)

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

    def test_optimized_source_and_provenance_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            temp_root = Path(directory)
            for item in self.manifest["files"]:
                p = temp_root / item["path"]
                p.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / item["path"], p)
            # A candidate source without explicit pinned provenance is rejected.
            with self.assertRaisesRegex(ValueError, "PROVENANCE_MISSING"):
                validate(temp_root, self.manifest)
            dest = temp_root / OPTIMIZED_PROVENANCE_PATH
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / OPTIMIZED_PROVENANCE_PATH, dest)
            with self.assertRaisesRegex(ValueError, "SONKU_FIELD_PROVENANCE_MISSING"):
                validate(temp_root, self.manifest)
            field = temp_root / SONKU_FIELD_RECEIPT
            shutil.copyfile(ROOT / SONKU_FIELD_RECEIPT, field)
            self.assertEqual(validate(temp_root, self.manifest), APPROVED_FILE_COUNT)
            receipt = json.loads(field.read_text(encoding="utf-8"))
            receipt["files"][OPTIMIZED_PATH] = "f" * 40
            field.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "SONKU_FIELD_PROVENANCE_TAMPERED"):
                validate(temp_root, self.manifest)
            shutil.copyfile(ROOT / SONKU_FIELD_RECEIPT, field)
            document = json.loads(dest.read_text(encoding="utf-8"))
            document["optimizedImplementationBlob"] = "f" * 40
            dest.write_text(json.dumps(document), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "PROVENANCE_TAMPERED"):
                validate(temp_root, self.manifest)
            shutil.copyfile(ROOT / OPTIMIZED_PROVENANCE_PATH, dest)
            # No additional or alternate acoustic source revision is allowed.
            early = temp_root / OPTIMIZED_PATH
            early.write_bytes(early.read_bytes() + b"\n// unapproved DSP edit\n")
            with self.assertRaisesRegex(ValueError, "SOURCE_CHANGED"):
                validate(temp_root, self.manifest)

    def test_one_bit_of_dsp_source_mutation_fails_even_if_test_hash_is_unmodified(self):
        with tempfile.TemporaryDirectory() as directory:
            temp_root = Path(directory)
            for item in self.manifest["files"]:
                destination = temp_root / item["path"]
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / item["path"], destination)
            # The separately pinned implementation receipt is required when
            # one historical acoustic source blob is optimized bit-exactly.
            p = temp_root / OPTIMIZED_PROVENANCE_PATH
            p.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / OPTIMIZED_PROVENANCE_PATH, p)
            shutil.copyfile(ROOT / SONKU_FIELD_RECEIPT, temp_root / SONKU_FIELD_RECEIPT)
            self.assertEqual(validate(temp_root, self.manifest), APPROVED_FILE_COUNT)
            for path in SONKU_FIELD_PINS:
                p = temp_root / path
                original = p.read_bytes()
                p.write_bytes(original + b"\n// attempted unapproved DSP retune\n")
                with self.assertRaisesRegex(ValueError, "IMMERSIB_APPROVED_PCM_SOURCE_CHANGED"):
                    validate(temp_root, self.manifest)
                p.write_bytes(original)
            # A mixed old/new variant is not an admitted candidate.
            p = temp_root / "engine/builtin_fx/sonku/SonKuSpatialCore.h"
            # Changing the manifest to bless a different blob must not admit
            # a partial P106 set, even when the source's bytes match it.
            mixed = copy.deepcopy(self.manifest)
            entry = next(x for x in mixed["files"] if x["path"] == p.relative_to(temp_root).as_posix())
            entry["blob"] = github_blob_hash(p.read_bytes().replace(b"\r\n", b"\n"))
            with self.assertRaisesRegex(ValueError, "SONKU_FIELD_VARIANT_SET_INCOMPLETE"):
                validate(temp_root, mixed)
            # Even resealing ALL four candidate blobs, eliminating the
            # candidate variant branch entirely, cannot bless changed PCM.
            resealed = copy.deepcopy(self.manifest)
            for entry in resealed["files"]:
                if entry["path"] in SONKU_FIELD_PINS:
                    entry["blob"] = SONKU_FIELD_PINS[entry["path"]][1]
            with self.assertRaisesRegex(ValueError, "HISTORICAL_PINSET_CHANGED"):
                validate(temp_root, resealed)


if __name__ == "__main__":
    unittest.main()
