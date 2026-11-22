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
            previous = '{"schemaVersion":1,"verified":"last-known-good"}\n'
            path.write_text(previous, encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "preserve"):
                write_catalog(path, fetch=offline)
            self.assertEqual(path.read_text("utf-8"), previous,
                             "API outage must not overwrite published catalog")
            with self.assertRaises(ValueError):
                write_catalog(path, fetch=lambda: {"invalid": "not a list"})
            with self.assertRaisesRegex(ValueError, "No verified"):
                write_catalog(path, fetch=lambda: [])
            self.assertEqual(path.read_text("utf-8"), previous)

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

    def test_release_publisher_cloudflare_sync_is_post_publication_and_safe(self):
        # The released Windows binary must exist before any external deploy
        # hook may request a static Cloudflare catalog refresh.
        workflow = (ROOT / ".github/workflows/alpha-preview-publish.yml").read_text("utf-8")
        published = workflow.index("Publish verified GitHub prerelease and its Windows binaries")
        github_refresh = workflow.index("Refresh official landing catalog after publishing real verified binaries")
        cf_refresh = workflow.index("Refresh Cloudflare Pages after verified public release")
        self.assertLess(published, github_refresh)
        self.assertLess(github_refresh, cf_refresh)
        self.assertIn("secrets.ARSONMIX_CLOUDFLARE_PAGES_DEPLOY_HOOK", workflow)
        self.assertIn('Host -ne "api.cloudflare.com"', workflow)
        self.assertIn("/client/v4/pages/webhooks/deploy_hooks/", workflow)
        self.assertIn("CLOUDFLARE_PAGES_REDEPLOY_ACCEPTED", workflow)
        self.assertIn("CLOUDFLARE_PAGES_DEPLOY_HOOK_NOT_CONFIGURED", workflow)
        self.assertIn("CLOUDFLARE_PAGES_DEPLOY_HOOK_FAILED", workflow)
        self.assertIn('if ([string]::IsNullOrWhiteSpace($env:CF_PAGES_DEPLOY_HOOK))', workflow)
        self.assertIn("gh workflow run pages-publish.yml", workflow)

    def test_caching_and_branch_index_policy(self):
        self.assertIn("Cache-Control: no-store", response_headers("main"))
        self.assertNotIn("X-Robots-Tag", response_headers("main"))
        self.assertIn("X-Robots-Tag: noindex", response_headers("feature"))


if __name__ == "__main__":
    unittest.main()
