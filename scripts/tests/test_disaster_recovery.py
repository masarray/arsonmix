# SPDX-License-Identifier: AGPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "dr", Path(__file__).resolve().parents[1] / "disaster_recovery.py")
dr = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dr)


class DisasterRecovery(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        dr.git("init", "-b", "main", self.source)
        (self.source / "track.txt").write_text("one", encoding="utf-8")
        dr.git("-C", self.source, "add", "track.txt")
        dr.git("-C", self.source, "-c", "user.name=Fixture",
               "-c", "user.email=fixture@example.invalid", "commit", "-m", "initial")
        dr.git("-C", self.source, "branch", "older")
        dr.git("-C", self.source, "-c", "user.name=Fixture",
               "-c", "user.email=fixture@example.invalid", "tag", "-a", "v1", "-m", "v1")
        (self.source / "track.txt").write_text("two", encoding="utf-8")
        dr.git("-C", self.source, "-c", "user.name=Fixture",
               "-c", "user.email=fixture@example.invalid", "commit", "-am", "second")
        self.archive = self.root / "archive"

    def make(self):
        dr.backup(str(self.source), self.archive)

    def test_offline_restore_history_and_refs(self):
        expected = dr.inventory(self.source)
        self.make()
        # Remove the original from its known path: recovery must not use it.
        self.source.rename(self.root / "unavailable")
        out = self.root / "restored.git"
        dr.restore(self.archive, out)
        self.assertEqual(dr.inventory(out), expected)
        self.assertEqual(dr.git("-C", out, "show", "main:track.txt"), "two")
        self.assertEqual(dr.git("-C", out, "show", "older:track.txt"), "one")
        self.assertEqual(dr.git("-C", out, "remote"), "")

    def test_existing_destinations_preserved(self):
        self.make()
        before = dr.digest(self.archive / "manifest.json")
        with self.assertRaises(FileExistsError):
            self.make()
        with self.assertRaises(FileExistsError):
            dr.restore(self.archive, self.source)
        self.assertEqual(dr.digest(self.archive / "manifest.json"), before)

    def test_corruption_and_extra_fail_closed(self):
        self.make()
        bundle = self.archive / "source.bundle"
        original = bundle.read_bytes()
        bundle.write_bytes(original + b"tampered")
        out = self.root / "restored.git"
        with self.assertRaises(ValueError):
            dr.restore(self.archive, out)
        self.assertFalse(out.exists())
        bundle.write_bytes(original)
        (self.archive / "extra").write_text("extra", encoding="utf-8")
        with self.assertRaises(ValueError):
            dr.verify(self.archive)

    def test_duplicate_manifest_key(self):
        self.make()
        manifest = self.archive / "manifest.json"
        manifest.write_text('{"schema":1,' + manifest.read_text()[1:], encoding="utf-8")
        with self.assertRaises(ValueError):
            dr.verify(self.archive)

    def test_failed_drill_does_not_publish(self):
        with patch.object(dr, "restore_into", side_effect=ValueError("simulated failure")):
            with self.assertRaises(ValueError):
                self.make()
        self.assertFalse(self.archive.exists())

    def test_release_inventory_pages_assets_and_ignores_drafts(self):
        with patch.object(dr, "gh_json", side_effect=[
                [[{"id": 1, "draft": False, "assets": []}], [{"id": 2, "draft": True}]],
                [[{"id": 101}], [{"id": 102}]]]) as api:
            releases = dr.release_inventory()
        self.assertEqual([a["id"] for a in releases[0]["assets"]], [101, 102])
        self.assertEqual(len(releases), 1)
        self.assertEqual(api.call_count, 2)

    def test_release_asset_digest_failure(self):
        target = self.root / "release-fixture"
        target.mkdir()
        asset = {"id": 7, "size": 3, "digest": "sha256:" + "0"*64}
        with patch.object(dr, "release_inventory", return_value=[{"assets": [asset]}]):
            def fake_download(*args, **kw):
                kw["stdout"].write(b"bad")
            with patch.object(dr.subprocess, "run", side_effect=fake_download):
                with self.assertRaisesRegex(ValueError, "published digest"):
                    dr.archive_releases(target)

    def test_symlink_manifest_rejected(self):
        self.make()
        manifest = self.archive / "manifest.json"
        external = self.root / "external.json"
        manifest.rename(external)
        try:
            manifest.symlink_to(external)
        except OSError:
            self.skipTest("symlink unavailable")
        with self.assertRaises(ValueError):
            dr.verify(self.archive)


if __name__ == "__main__":
    unittest.main()
