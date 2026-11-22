"""Cloudflare Pages builder: fast, deterministic offline contracts."""
from pathlib import Path
import copy
import json
import sys
import tempfile
import unittest
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_cloudflare_pages import (
    DEFAULT_URL, GITHUB_PAGES_CATALOG, public_url, response_headers,
    validate_official_pages_catalog, write_catalog,
)


class CloudflarePagesBuildTest(unittest.TestCase):
    def test_public_url_never_uses_random_branch_preview(self):
        self.assertEqual(public_url({}), DEFAULT_URL)
        self.assertEqual(public_url({"CF_PAGES_URL": "https://12345.arsonmix.pages.dev/",
                                     "CF_PAGES_BRANCH": "pr-123"}), DEFAULT_URL)
        self.assertEqual(public_url({"ARSONMIX_PUBLIC_URL": "https://mix.example.org",
                                     "CF_PAGES_URL": "https://pr1.arsonmix.pages.dev/"}),
                         "https://mix.example.org/")
        with self.assertRaises(ValueError):
            public_url({"ARSONMIX_PUBLIC_URL": "http://example.org/"})

    @staticmethod
    def trusted_snapshot():
        tag = "v0.5.0-alpha.32"
        name = f"ArSonMix-{tag}-Windows-x64-Setup.zip"
        return {"schemaVersion": 1, "platforms": {
            "windows": {
                "tag": tag, "name": name,
                "url": f"https://github.com/masarray/arsonmix/releases/download/{tag}/{name}",
                "size": 26335480, "sha256": "a" * 64,
                "prerelease": True, "publishedAt": "2026-11-22T10:05:32Z",
            },
            "linux": None, "macos": None,
        }}

    def test_rate_limited_release_api_recovers_from_official_pages(self):
        self.assertEqual(
            GITHUB_PAGES_CATALOG,
            "https://masarray.github.io/arsonmix/release-catalog.json")
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "release-catalog.json"
            calls = []
            def rate_limited():
                calls.append("api")
                raise urllib.error.HTTPError(
                    "https://api.github.com/repos/masarray/arsonmix/releases",
                    403, "rate limit", {}, None)
            wanted = self.trusted_snapshot()
            got = write_catalog(
                path, fetch=rate_limited,
                snapshot_fetch=lambda: copy.deepcopy(wanted),
                pause=lambda seconds: calls.append(seconds))
            self.assertEqual(calls, ["api", 1, "api", 2, "api"])
            self.assertEqual(got, wanted)
            self.assertEqual(json.loads(path.read_text("utf-8")), wanted)

    def test_both_network_sources_fail_without_overwriting_catalog(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "release-catalog.json"
            previous = '{"schemaVersion":1,"verified":"last-known-good"}\n'
            path.write_text(previous, encoding="utf-8")
            def offline():
                raise urllib.error.URLError("offline")
            with self.assertRaisesRegex(RuntimeError, "preserve"):
                write_catalog(path, fetch=offline, snapshot_fetch=offline,
                              pause=lambda _: None)
            self.assertEqual(path.read_text("utf-8"), previous)

    def test_reachable_malformed_or_empty_api_fails_without_fallback(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "release-catalog.json"
            def no_fallback():
                raise AssertionError("fallback must not mask invalid live API")
            with self.assertRaises(ValueError):
                write_catalog(path, fetch=lambda: {"invalid": "not a list"},
                              snapshot_fetch=no_fallback)
            with self.assertRaisesRegex(ValueError, "No verified"):
                write_catalog(path, fetch=lambda: [],
                              snapshot_fetch=no_fallback)
            self.assertFalse(path.exists())

    def test_fallback_rejects_tampered_or_unsigned_assets(self):
        clean = self.trusted_snapshot()
        self.assertEqual(validate_official_pages_catalog(clean), clean)
        corruptions = [
            lambda s: s.update(schemaVersion=2),
            lambda s: s["platforms"].pop("macos"),
            lambda s: s["platforms"].update(windows=None),
            lambda s: s["platforms"]["windows"].update(size=1),
            lambda s: s["platforms"]["windows"].update(size=True),
            lambda s: s["platforms"]["windows"].update(sha256="wrong"),
            lambda s: s["platforms"]["windows"].update(
                url="https://attacker.invalid/untrusted.zip"),
            lambda s: s["platforms"]["windows"].update(
                name="ArSonMix-v0.5.0-alpha.32-Windows-x64-FAT-UNQUALIFIED.zip"),
            lambda s: s["platforms"]["windows"].update(publishedAt="2026-11-22"),
            lambda s: s["platforms"]["windows"].update(prerelease="false"),
            lambda s: s["platforms"]["windows"].update(extraField="bad"),
        ]
        for mutate in corruptions:
            copy_ = copy.deepcopy(clean)
            mutate(copy_)
            with self.subTest(corrupted=copy_):
                with self.assertRaises(ValueError):
                    validate_official_pages_catalog(copy_)
            with tempfile.TemporaryDirectory() as root:
                path = Path(root) / "catalog.json"
                with self.assertRaisesRegex(RuntimeError, "preserve"):
                    write_catalog(
                        path, fetch=lambda: (_ for _ in ()).throw(
                            urllib.error.URLError("network")),
                        snapshot_fetch=lambda: copy_, pause=lambda _: None)
                self.assertFalse(path.exists())

    def test_signed_release_cannot_be_replaced_with_fake(self):
        tag = "v0.5.0-alpha.5"
        def asset(name, size):
            return {"name": name, "state": "uploaded", "size": size,
                    "digest": "sha256:" + "a" * 64,
                    "browser_download_url":
                        f"https://github.com/masarray/arsonmix/releases/download/{tag}/{name}"}
        published = {"tag_name": tag, "draft": False, "prerelease": True,
                     "published_at": "2026-11-08T00:00:00Z",
                     "target_commitish": "f" * 40, "assets": [
                        asset("SHA256SUMS.txt", 1024),
                        asset("INSTALLER_PROVENANCE.json", 4096),
                        asset(f"ArSonMix-{tag}-Windows-x64-Setup.zip", 5000000)]}
        with tempfile.TemporaryDirectory() as root:
            output = write_catalog(Path(root) / "catalog.json",
                                   fetch=lambda: [published])
            self.assertEqual(output["platforms"]["windows"]["tag"], tag)
            self.assertIsNone(output["platforms"]["linux"])
            self.assertIsNone(output["platforms"]["macos"])

    def test_caching_and_branch_index_policy(self):
        self.assertIn("Cache-Control: no-store", response_headers("main"))
        self.assertNotIn("X-Robots-Tag", response_headers("main"))
        self.assertIn("X-Robots-Tag: noindex", response_headers("feature"))


if __name__ == "__main__":
    unittest.main()
