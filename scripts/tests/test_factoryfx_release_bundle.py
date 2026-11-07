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

ROOT = Path(__file__).resolve().parents[2]
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


    def test_comp_plugin_archive_layout_excludes_standalone(self):
        with zipfile.ZipFile(self.root / "comp.zip", "w") as z:
            z.writestr("Plugin/ArSonComp.vst3/Contents/x86_64-win/ArSonComp.vst3",
                       b"mock-comp-PE")
            z.writestr("Standalone/ArSonComp.exe", b"NEVER SHIP")
        with zipfile.ZipFile(self.root / "comp.zip") as z:
            selected = bundle.zip_members(z, "ArSonComp.vst3")
        self.assertEqual(len(selected), 1)
        self.assertEqual(selected[0][1], "Contents/x86_64-win/ArSonComp.vst3")

    def test_four_product_production_gate_and_full_atomic_staging(self):
        archives, releases = {}, []
        for pid in ("arson-eq", "arson-comp", "arson-verb", "arsonkupik-enhancer"):
            repo, display, filename, _category = bundle.OWNED[pid]
            archive = self.root / (pid + ".zip")
            with zipfile.ZipFile(archive, "w") as z:
                base = "Plugin" if pid == "arson-comp" else "VST3"
                z.writestr(f"{base}/{filename}/Contents/x86_64-win/{filename}",
                           pid.encode("ascii") + b"-DSP-test")
            archives[pid] = archive
            (self.notices / (pid + ".txt")).write_text(
                "Reviewed release fixture " + pid, encoding="utf-8")
            releases.append(dict(
                self.approved, productId=pid,
                sourceRepo="masarray/askp-vst" if pid == "arsonkupik-enhancer" else repo,
                assetName=display + "-Windows-x64-VST3.zip",
                artifactSha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                hostUniqueId=12345 + len(releases)))
        self.save_lock(*releases)
        locked = bundle.read_lock(self.lock)
        self.assertEqual(bundle.distribution_for(locked), "production-four")
        with patch.object(bundle, "download_release",
                          side_effect=lambda p, target: shutil.copyfile(archives[p["productId"]], target)):
            bundle.stage(locked, self.output, self.notices)
        bundle.verify(locked, self.output, self.notices)
        manifest_path = self.output / "FactoryFX/factoryfx-manifest.json"
        manifest = json.loads(manifest_path.read_text())
        self.assertEqual(manifest["distribution"], "production-four")
        self.assertEqual(len(manifest["plugins"]), 4)
        self.assertFalse(list(self.output.glob(".factoryfx-incoming-*")))
        # Fake production branding cannot turn a three-effect package into four.
        manifest["plugins"].pop()
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaises(ValueError):
            bundle.verify(locked, self.output, self.notices)
        self.assertEqual(bundle.distribution_for(releases[:3]), "engineering-partial")

    def test_failed_second_artifact_never_publishes_partial_factory(self):
        p = dict(self.approved, productId="arson-verb",
                 sourceRepo="masarray/arsonverb",
                 assetName="ArSonVerb-Windows-x64-VST3.zip")
        (self.notices / "arson-verb.txt").write_text(
            "Reviewed Reverb fixture", encoding="utf-8")
        self.save_lock(self.approved, p)
        def fake_download(release, target):
            if release["productId"] != "arson-eq":
                raise ValueError("download failed")
            shutil.copyfile(self.archive, target)
        with patch.object(bundle, "download_release", side_effect=fake_download):
            with self.assertRaisesRegex(ValueError, "download failed"):
                bundle.stage(bundle.read_lock(self.lock), self.output, self.notices)
        self.assertFalse((self.output / "FactoryFX").exists())
        self.assertFalse(list(self.output.glob(".factoryfx-incoming-*")))

    def test_production_names_are_exact_not_count_only(self):
        refs = [dict(self.approved, productId=pid) for pid in
                ("arson-eq", "arson-comp", "arson-harmony", "arson-color")]
        self.assertEqual(bundle.distribution_for(refs), "engineering-partial")


class WatchTests(unittest.TestCase):
    def test_four_suppliers_are_monitored_and_missing_is_not_approved(self):
        def fake(repo):
            if repo == "masarray/vst-enhancer":
                return dict(tag_name="v0.5.26", published_at="2026-08-23T23:40:39Z",
                            prerelease=False,
                            assets=[{"name": "ArSonKuPik-v0.5.26-Windows-x64-VST3.zip"}])
            return None
        snapshot = watch.snapshot(fake)
        self.assertEqual([x["productId"] for x in snapshot["observed"]],
                         ["arson-eq", "arson-comp", "arson-verb", "arsonkupik-enhancer"])
        self.assertIsNone(snapshot["observed"][0]["releaseTag"])
        self.assertEqual(snapshot["observed"][3]["releaseTag"], "v0.5.26")
        self.assertNotIn("approved", json.dumps(snapshot))

    def test_standalone_stable_cannot_hide_newer_usable_integration_vst3(self):
        releases = [
            dict(tag_name="v0.2.0", published_at="2026-11-01T07:59:05Z",
                 draft=False, prerelease=False,
                 assets=[{"name": "ArSonComp-0.2.0-Windows-x64-Setup.exe"}]),
            dict(tag_name="v0.2.0-sc220-vst3-test",
                 published_at="2026-11-03T23:42:19Z", draft=False,
                 prerelease=True,
                 assets=[{"name": "ArSonComp-v0.2.0-Windows-x64-VST3.zip"}]),
        ]
        selected = watch.select_candidate(releases, "masarray/arson-comp")
        self.assertEqual(selected["tag_name"], "v0.2.0-sc220-vst3-test")
        snap = watch.snapshot(lambda repo: selected if repo == "masarray/arson-comp" else None)
        self.assertTrue(snap["observed"][1]["prerelease"])
        self.assertEqual(snap["observed"][1]["windowsVst3Asset"],
                         "ArSonComp-v0.2.0-Windows-x64-VST3.zip")

    def test_ambiguous_binaries_and_drafts_fail_closed(self):
        releases = [
            dict(tag_name="draft", published_at="2026-11-09", draft=True,
                 assets=[{"name": "ArSonComp-Windows-x64-VST3.zip"}]),
            dict(tag_name="bad", published_at="2026-11-08", draft=False, assets=[
                {"name": "one-Windows-x64-VST3.zip"},
                {"name": "two-Windows-x64-VST3.zip"}])]
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            watch.select_candidate(releases, "masarray/arson-comp")
        self.assertIsNone(watch.select_candidate(releases[:1], "masarray/arson-comp"))


if __name__ == "__main__":
    unittest.main()
