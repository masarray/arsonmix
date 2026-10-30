# SPDX-License-Identifier: AGPL-3.0-only
"""FactoryFX offline acceptance tests; never reach the Internet or require JUCE."""
import hashlib
import importlib.util
import json
import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "factoryfx_release_bundle", ROOT / "scripts/factoryfx_release_bundle.py")
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)
WATCH_SPEC = importlib.util.spec_from_file_location(
    "factoryfx_upstream_watch", ROOT / "scripts/factoryfx_upstream_watch.py")
watch = importlib.util.module_from_spec(WATCH_SPEC)
WATCH_SPEC.loader.exec_module(watch)


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.output = self.root / "dist"
        self.output.mkdir()
        self.notices = self.root / "notices"
        self.notices.mkdir()
        (self.notices / "arson-eq.txt").write_text(
            "Reviewed EQ redistributable notice; fixture only", encoding="utf-8")
        self.archive = self.root / "eq.zip"
        with zipfile.ZipFile(self.archive, "w") as z:
            z.writestr("VST3/ArSonEQ.vst3/Contents/x86_64-win/ArSonEQ.vst3", b"fake-DSP-for-packager-test")
            z.writestr("VST3/ArSonEQ.vst3/Contents/Resources/moduleinfo.json", b"{}")
        self.approved = {
            "productId": "arson-eq", "version": "1.0.0", "releaseTag": "v1.0.0",
            "assetName": "ArSonEQ-Windows-x64-VST3.zip",
            "artifactSha256": hashlib.sha256(self.archive.read_bytes()).hexdigest(),
            "sourceRepo": "masarray/arson-eq", "sourceCommit": "a" * 40,
            "vst3ClassId": "b" * 32, "hostUniqueId": 12345,
            "licenseReview": "approved", "entitlementPolicy": "unrestricted",
            "hostQualification": "passed",
        }
        self.lock = self.root / "lock.json"
        self.save_lock(self.approved)

    def save_lock(self, *plugins):
        self.lock.write_text(
            json.dumps({"schemaVersion": 1, "plugins": list(plugins)}), encoding="utf-8")

    def test_empty_lock_keeps_existing_app_only_distribution(self):
        self.save_lock()
        self.assertEqual(bundle.read_lock(self.lock), [])
        bundle.stage([], self.output, self.notices)
        bundle.verify([], self.output, self.notices)
        self.assertFalse((self.output / "FactoryFX").exists())

    def test_license_and_host_qualification_are_hard_gates(self):
        for key, val in (("licenseReview", "pending"), ("hostQualification", "pending"),
                         ("artifactSha256", "0" * 63), ("hostUniqueId", "not-verified"),
                         ("sourceRepo", "another/repo")):
            p = dict(self.approved, **{key: val})
            self.save_lock(p)
            with self.subTest(key=key), self.assertRaises(ValueError):
                bundle.read_lock(self.lock)

    def test_duplicate_products_rejected(self):
        self.save_lock(self.approved, self.approved)
        with self.assertRaises(ValueError):
            bundle.read_lock(self.lock)

    def test_staged_directory_with_manifest_and_hashes_then_tamper(self):
        with patch.object(bundle, "download_release",
                          side_effect=lambda _p, target: shutil.copyfile(self.archive, target)):
            bundle.stage(bundle.read_lock(self.lock), self.output, self.notices)
        bundle.verify(bundle.read_lock(self.lock), self.output, self.notices)
        manifest = json.loads((self.output / "FactoryFX/factoryfx-manifest.json").read_text())
        self.assertEqual(manifest["distribution"], "engineering-partial")
        self.assertEqual(manifest["plugins"][0]["hostUniqueId"], 12345)
        target = self.output / "FactoryFX/ArSonEQ/ArSonEQ.vst3/Contents/x86_64-win/ArSonEQ.vst3"
        target.write_bytes(b"tampered")
        with self.assertRaises(ValueError):
            bundle.verify(bundle.read_lock(self.lock), self.output, self.notices)

    def test_archive_rejects_traversal_duplicate_and_missing_module(self):
        for names in [
            ["VST3/ArSonEQ.vst3/Contents/../bad.vst3"],
            ["VST3/ArSonEQ.vst3/Contents/x86_64-win/ArSonEQ.vst3"] * 2,
            ["VST3/ArSonEQ.vst3/Resources/info.json"],
        ]:
            p = self.root / "bad.zip"
            with zipfile.ZipFile(p, "w") as z:
                for name in names:
                    z.writestr(name, b"stub")
            with zipfile.ZipFile(p) as z, self.assertRaises(ValueError):
                bundle.zip_members(z, "ArSonEQ.vst3")

    def test_extra_factory_file_is_rejected(self):
        with patch.object(bundle, "download_release",
                          side_effect=lambda _p, target: shutil.copyfile(self.archive, target)):
            bundle.stage(bundle.read_lock(self.lock), self.output, self.notices)
        (self.output / "FactoryFX" / "rogue.txt").write_text("not approved")
        with self.assertRaises(ValueError):
            bundle.verify(bundle.read_lock(self.lock), self.output, self.notices)


class WatchTests(unittest.TestCase):
    def test_latest_is_metadata_only_and_absent_release_not_a_plugin(self):
        def fake(repo):
            if repo == "masarray/vst-enhancer":
                return dict(tag_name="v0.5.26", published_at="2026-08-23T23:40:39Z",
                            assets=[{"name": "ArSonKuPik-v0.5.26-Windows-x64-VST3.zip"}])
            return None
        snapshot = watch.snapshot(fake)
        self.assertEqual(len(snapshot["observed"]), 3)
        self.assertIsNone(snapshot["observed"][0]["releaseTag"])
        self.assertEqual(snapshot["observed"][2]["releaseTag"], "v0.5.26")


if __name__ == "__main__":
    unittest.main()
