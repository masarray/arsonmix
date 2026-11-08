"""Strict offline release-catalog fixtures; never query GitHub in tests."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "release_catalog", ROOT / "scripts/build_release_catalog.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def asset(tag, name, size=3_000_000, digest="a" * 64):
    return {
        "name": name, "size": size, "state": "uploaded",
        "digest": "sha256:" + digest,
        "browser_download_url": f"https://github.com/masarray/arsonmix/releases/download/{tag}/{name}",
    }


def release(tag="v0.5.0-alpha.1", published="2026-11-08T00:46:00Z", suffixes=None):
    if suffixes is None:
        suffixes = [("Windows-x64-Setup.exe", "INSTALLER_PROVENANCE.json")]
    files = [asset(tag, "SHA256SUMS.txt", size=230)]
    for suffix, provenance in suffixes:
        files.append(asset(tag, f"ArSonMix-{tag}-{suffix}"))
        files.append(asset(tag, provenance, size=450))
    return dict(tag_name=tag, draft=False, prerelease=True,
                published_at=published, target_commitish="f" * 40, assets=files)


class ReleaseCatalogContract(unittest.TestCase):
    def test_actual_windows_alpha_is_not_a_fake_three_platform_release(self):
        output = mod.catalog([release()])
        self.assertEqual(output["schemaVersion"], 1)
        self.assertEqual(output["platforms"]["windows"]["tag"], "v0.5.0-alpha.1")
        self.assertIsNone(output["platforms"]["linux"])
        self.assertIsNone(output["platforms"]["macos"])

    def test_latest_per_platform_and_no_silent_fallback_from_newer_other_os(self):
        old = release("v0.5.0-alpha.2", "2026-11-08T01:00:00Z",
                      [("Linux-x86_64.AppImage", "LINUX_PROVENANCE.json"),
                       ("macOS-universal.dmg", "MACOS_PROVENANCE.json")])
        new = release("v0.5.0-alpha.3", "2026-11-08T02:00:00Z")
        output = mod.catalog([old, new])["platforms"]
        self.assertEqual(output["windows"]["tag"], "v0.5.0-alpha.3")
        self.assertEqual(output["linux"]["tag"], "v0.5.0-alpha.2")
        self.assertEqual(output["macos"]["tag"], "v0.5.0-alpha.2")

    def test_reject_draft_forged_hash_moving_tag_unsafe_url_missing_provenance(self):
        a = release()
        a["draft"] = True
        self.assertIsNone(mod.catalog([a])["platforms"]["windows"])
        b = release()
        b["assets"][1]["digest"] = "sha256:" + "z" * 64
        self.assertIsNone(mod.catalog([b])["platforms"]["windows"])
        c = release()
        c["target_commitish"] = "main"
        self.assertIsNone(mod.catalog([c])["platforms"]["windows"])
        d = release()
        d["assets"][1]["browser_download_url"] = "https://example.com/not-an-arsonmix-binary"
        self.assertIsNone(mod.catalog([d])["platforms"]["windows"])
        e = release()
        e["assets"].pop()
        self.assertIsNone(mod.catalog([e])["platforms"]["windows"])
        f = release()
        f["assets"][1]["name"] = f"ArSonMix-{f['tag_name']}-Windows-x64-FAT-UNQUALIFIED.zip"
        self.assertIsNone(mod.catalog([f])["platforms"]["windows"])

    def test_missing_checksum_or_duplicate_asset_is_not_promoted(self):
        a = release()
        a["assets"].pop(0)
        self.assertIsNone(mod.catalog([a])["platforms"]["windows"])
        b = release()
        b["assets"].append(dict(b["assets"][1]))
        self.assertIsNone(mod.catalog([b])["platforms"]["windows"])

    def test_no_release_and_malformed_api_fail_closed(self):
        self.assertEqual(mod.catalog([])["platforms"],
                         {"windows": None, "linux": None, "macos": None})
        with self.assertRaises(ValueError):
            mod.catalog({"releases": []})


    def test_zip_first_and_backward_compatible_exe(self):
        tag = "v0.5.0-alpha.5"
        r = release(tag, suffixes=[
            ("Windows-x64-Setup.exe", "INSTALLER_PROVENANCE.json"),
            ("Windows-x64-Setup.zip", "INSTALLER_PROVENANCE.json"),
        ])
        chosen = mod.catalog([r])["platforms"]["windows"]
        self.assertEqual(chosen["name"], f"ArSonMix-{tag}-Windows-x64-Setup.zip")
        r["assets"] = [a for a in r["assets"]
                       if not a["name"].endswith("Windows-x64-Setup.zip")]
        fallback = mod.catalog([r])["platforms"]["windows"]
        self.assertTrue(fallback["name"].endswith("Windows-x64-Setup.exe"))

    def test_version_order_beats_late_publication_of_old_build(self):
        early = release("v0.5.0-alpha.5", "2026-11-08T02:00:00Z")
        late = release("v0.5.0-alpha.4", "2026-11-09T00:00:00Z")
        result = mod.catalog([late, early])["platforms"]["windows"]
        self.assertEqual(result["tag"], "v0.5.0-alpha.5")

    def test_invalid_zip_cannot_displace_verified_exe(self):
        tag = "v0.5.0-alpha.5"
        r = release(tag, suffixes=[
            ("Windows-x64-Setup.exe", "INSTALLER_PROVENANCE.json"),
            ("Windows-x64-Setup.zip", "INSTALLER_PROVENANCE.json"),
        ])
        for a in r["assets"]:
            if a["name"].endswith("Windows-x64-Setup.zip"):
                a["digest"] = "sha256:invalid"
        chosen = mod.catalog([r])["platforms"]["windows"]
        self.assertTrue(chosen["name"].endswith("Windows-x64-Setup.exe"))


if __name__ == "__main__":
    unittest.main()
