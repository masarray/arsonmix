"""Cloudflare Pages builder: fast, deterministic offline contracts."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from build_cloudflare_pages import DEFAULT_URL, public_url, response_headers, write_catalog


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

    def test_catalog_network_unavailable_fails_safe(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "release-catalog.json"
            def offline():
                raise urllib.error.URLError("rate limit")
            actual = write_catalog(path, fetch=offline)
            self.assertEqual(actual["platforms"],
                             {"windows": None, "linux": None, "macos": None})
            self.assertEqual(json.loads(path.read_text("utf-8")), actual)
            with self.assertRaises(ValueError):
                write_catalog(path, fetch=lambda: {"invalid": "not a list"})

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
