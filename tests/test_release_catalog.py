"""Strict offline release-catalog fixtures; never query GitHub in tests."""
import importlib.util
import tempfile
import unittest
from unittest.mock import patch
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
    def test_beta_supersedes_alpha_without_relabeling_other_platforms(self):
        alpha = release('v0.5.0-alpha.33', suffixes=[('Windows-x64-Setup.exe', 'INSTALLER_PROVENANCE.json'), ('Linux-x86_64.tar.gz', 'LINUX_PROVENANCE.json')])
        beta = release('v0.5.0-beta.1')
        selected = mod.catalog([beta, alpha])
        self.assertEqual(selected['platforms']['windows']['tag'], 'v0.5.0-beta.1')
        self.assertEqual(selected['platforms']['linux']['tag'], 'v0.5.0-alpha.33')
        html = mod.hydrate_landing((ROOT / 'site/index.html').read_text('utf-8'), selected)
        self.assertIn('v0.5.0-beta.1', html)
        self.assertIn('Public Beta', html)
        self.assertIn('Alpha preview', html)

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


    def test_installer_zip_first_with_verified_exe_fallback(self):
        tag = "v0.5.0-alpha.5"
        r = release(tag)
        r["assets"].append(asset(tag, f"ArSonMix-{tag}-Windows-x64-Setup.zip"))
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
        r = release(tag)
        r["assets"].append(asset(tag, f"ArSonMix-{tag}-Windows-x64-Setup.zip"))
        for a in r["assets"]:
            if a["name"].endswith("Windows-x64-Setup.zip"):
                a["digest"] = "sha256:invalid"
        chosen = mod.catalog([r])["platforms"]["windows"]
        self.assertTrue(chosen["name"].endswith("Windows-x64-Setup.exe"))


    def test_build_time_landing_cards_use_exact_verified_catalog(self):
        html = (ROOT / "site/index.html").read_text("utf-8")
        older = release("v0.5.0-alpha.5", suffixes=[
            ("Linux-x86_64.tar.gz", "LINUX_PROVENANCE.json"),
            ("macOS-universal.dmg", "MACOS_PROVENANCE.json")])
        newer = release("v0.5.0-alpha.26", suffixes=[
            ("Windows-x64-Setup.zip", "INSTALLER_PROVENANCE.json")])
        selected = mod.catalog([older, newer])
        rendered = mod.hydrate_landing(html, selected)
        self.assertIn("Windows v0.5.0-alpha.26", rendered)
        self.assertIn("Linux v0.5.0-alpha.5", rendered)
        self.assertIn("macOS v0.5.0-alpha.5", rendered)
        self.assertEqual(rendered.count("data-release-sha256="), 3)
        self.assertEqual(rendered.count('class="os-card is-available"'), 3)
        self.assertEqual(rendered.count("https://github.com/masarray/arsonmix/releases/download/"), 3)
        self.assertIn("data-link-label>Download installer ZIP", rendered)
        self.assertNotIn("NOT PUBLISHED", rendered)
        self.assertNotIn("Native Linux release is not yet published", rendered)
        self.assertEqual(html.count('data-platform="'), 3)
        self.assertNotIn("releases/download/", html, "Do not hardcode links in editable source")

    def test_build_time_landing_rejects_tampered_or_missing_platform_assets(self):
        html = (ROOT / "site/index.html").read_text("utf-8")
        approved = mod.catalog([release("v0.5.0-alpha.26")])
        rendered = mod.hydrate_landing(html, approved)
        self.assertEqual(rendered.count("data-release-sha256="), 1)
        self.assertEqual(rendered.count('data-release-link href="#download"'), 2)
        self.assertIn("CHECKING", rendered)
        approved["platforms"]["windows"]["url"] = "https://attacker.invalid/Setup.exe"
        with self.assertRaisesRegex(ValueError, "Unverified download"):
            mod.hydrate_landing(html, approved)
        with self.assertRaisesRegex(ValueError, "Incomplete platform catalog"):
            mod.hydrate_landing(html, {"schemaVersion": 1, "platforms": {"windows": None}})
        with self.assertRaisesRegex(ValueError, "exactly one source card"):
            mod.hydrate_landing(html.replace('data-platform="windows"', 'data-platform="obsolete"'),
                                    mod.catalog([]))



    def test_publisher_does_not_replace_last_good_site_with_empty_catalog(self):
        with tempfile.TemporaryDirectory() as folder:
            fixture = Path(folder) / "releases.json"
            output = Path(folder) / "site" / "release-catalog.json"
            fixture.write_text("[]", encoding="utf-8")
            with patch("sys.argv", ["build_release_catalog.py", "--fixture", str(fixture),
                                    "--output", str(output)]):
                with self.assertRaisesRegex(ValueError, "No verified published"):
                    mod.main()
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
