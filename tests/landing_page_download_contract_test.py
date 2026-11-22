"""Landing product/download contract: no fictitious installers or secondary GUI."""
from pathlib import Path
import json
import re
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
DIST = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "dist-pages"

class LandingReleaseContract(unittest.TestCase):
    def test_source_and_built_assets(self):
        for base in (ROOT / "site", DIST):
            html = (base / "index.html").read_text("utf-8")
            js = (base / "downloads.js").read_text("utf-8")
            css = (base / "landing.css").read_text("utf-8")
            self.assertIn('<link rel="icon" href="./favicon.ico?v=brand-20261107"', html)
            self.assertIn("apple-touch-icon.png", html)
            self.assertIn("site.webmanifest", html)
            self.assertIn('src="./arsonmix-128.png"', html)
            self.assertIn('src="./brand-512.png"', html)
            self.assertIn("hero-brand-lockup", css)
            self.assertIn('href="./preview/"', html)
            self.assertIn('id="download"', html)
            self.assertIn("media/console-overview.jpg", html)
            self.assertIn("media/console-routing.jpg", html)
            self.assertIn("media/console-faders.jpg", html)
            self.assertIn('id="effects"', html)
            self.assertIn('id="getting-started"', html)
            self.assertIn('id="feedback"', html)
            self.assertIn("Actual approved ArSonMix console screenshot", html)
            self.assertIn("browser preview does not process audio", html)
            self.assertIn("issues/new?template=01_bug_report.yml", html)
            self.assertIn("issues/new?template=02_feature_request.yml", html)
            self.assertIn("prefers-reduced-motion", css)
            if base == ROOT / "site":
                # Public feedback is a legitimate GitHub destination; binary
                # downloads, by contrast, MUST come from verified release data.
                self.assertNotIn("github.com/masarray/arsonmix/releases/download/", html)
            else:
                # The deployed HTML has direct asset URLs from the exact same
                # validated catalog, for offline/JS-blocked progressive download.
                data = json.loads((DIST / "release-catalog.json").read_text("utf-8"))
                available = [v for v in data["platforms"].values() if v is not None]
                self.assertEqual(
                    html.count("https://github.com/masarray/arsonmix/releases/download/"),
                    len(available), "No extra or missing server-rendered download links")
                for entry in available:
                    self.assertIn('href="' + entry["url"] + '"', html)
                    self.assertIn('data-release-tag="' + entry["tag"] + '"', html)
                    self.assertIn('data-release-sha256="' + entry["sha256"] + '"', html)
                self.assertNotIn("https://github.com/masarray/arsonmix/releases/tag/", html)
                self.assertEqual(html.count('class="os-card is-available"'),
                                 len(available))
            self.assertIn('href="./LICENSE.txt"', html)
            self.assertIn("release-catalog.json", js)
            self.assertIn("https://api.github.com/repos/masarray/arsonmix/releases?per_page=40", js)
            self.assertIn("Download installer ZIP", js)
            self.assertIn('id="hero-download" href="#download"', html)
            self.assertIn("Choose a verified platform download", html)
            self.assertNotIn("heroDownload.href =", js)
            self.assertNotIn("isWindowsVisitor", js)
            self.assertIn("Free to download", html)
            for feature in ("Vocal Delay", "8D-SonKu", "ImmerSIB", "Concert Front Row XL", "P9.9", "Guitar FX", "Harmonice", "WIRE"):
                self.assertIn(feature, html)
            self.assertIn("latest verified Windows alpha preview", html)
            self.assertIn("updates automatically after an approved public release", html)
            self.assertIn("latest verified downloads.", html.lower())
            self.assertIn("Earlier Linux/macOS builds", html)
            self.assertIn("latest verified Windows alpha preview", html)
            self.assertFalse(bool(re.search(r"Windows alpha\.\d+", html)),
                             "Marketing copy must not hardcode a current alpha version")
            self.assertIn("Extract ZIP, run Setup", js)
            self.assertNotIn("ZIP does not bypass security warnings", html)
            self.assertIn("isPreferred", js)
            self.assertIn("SHA256SUMS.txt", js)
            self.assertIn("INSTALLER_PROVENANCE.json", js)
            self.assertIn("github.com", js)  # Only fixed, validated direct binary URLs.
            self.assertIn("unqualified", js.lower())
            self.assertNotIn("browser_download_url", html) # No hardcoded unverified links.
            self.assertEqual(html.count('data-platform="'), 3)
            self.assertEqual(html.count('data-release-link'), 3)
            self.assertFalse(bool(re.search(r'<iframe\b', html, re.I)),
                             "Landing should not embed a second, independently rendered GUI")
            for name in ("favicon.ico", "favicon-16x16.png", "favicon-32x32.png",
                         "apple-touch-icon.png", "site.webmanifest", "arsonmix-48.png", "arsonmix-128.png", "brand-512.png",
                         "landing.css", "downloads.js"):
                self.assertTrue((base / name).is_file(), (str(base), name))
            for name in ("console-overview.jpg", "console-routing.jpg", "console-faders.jpg"):
                path = base / "media" / name
                self.assertTrue(path.is_file(), str(path))
                self.assertGreater(path.stat().st_size, 12000)
                self.assertEqual(path.read_bytes()[:3], b"\xff\xd8\xff", str(path))

    def test_public_agpl_notice_is_exact_repo_license(self):
        # Legal documentation is delivered on the official Pages domain
        # rather than linking visitors to a GitHub source-code UI.
        copied = DIST / "LICENSE.txt"
        self.assertTrue(copied.is_file(), str(copied))
        self.assertEqual(copied.read_bytes(), (ROOT / "LICENSE").read_bytes())

    def test_readme_uses_evergreen_public_download_and_real_screenshots(self):
        readme = (ROOT / "README.md").read_text("utf-8")
        self.assertIn("https://masarray.github.io/arsonmix/#download", readme)
        self.assertIn("https://github.com/masarray/arsonmix/releases", readme)
        self.assertIn("site/media/console-overview.jpg", readme)
        self.assertIn("site/media/console-routing.jpg", readme)
        self.assertIn("site/media/console-faders.jpg", readme)
        self.assertIn("ImmerSIB", readme)
        self.assertIn("8D-SonKu P9.9", readme)
        self.assertIn("GitHub account", readme)
        self.assertIn("visual only", readme)
        self.assertNotIn("Latest published Windows alpha as of", readme)
        self.assertNotRegex(readme, r"releases/download/v\d+\.\d+\.\d+-alpha\.\d+/",
                            "README must not pin a stale binary when the verified landing is dynamic")

    def test_release_notes_are_safe_landing_only_changes(self):
        # Changelogs must not silently suppress the Pages update when a
        # landing/readme synchronization also adds the corresponding notes.
        workflow = (ROOT / ".github" / "workflows" / "pages-publish.yml").read_text("utf-8")
        self.assertIn("README.md|docs/releases/*.md|", workflow)
        self.assertIn("|site/index.html|", workflow)
        self.assertIn("docs/CLOUDFLARE_PAGES_BUILD.md", workflow)
        self.assertIn("NOT LANDING ONLY:", workflow)
        # A skipped PR workflow_run must not cancel a valid main push deploy.
        self.assertIn("arsonmix-pages-ignored-pr-check", workflow)
        self.assertIn("github.event.workflow_run.event != 'push'", workflow)
        self.assertIn("arsonmix-production-pages", workflow)
        self.assertIn("cancel-in-progress: ${{ github.event_name != 'workflow_run' }}", workflow)

    def test_no_unverified_binaries(self):
        js = (ROOT / "site/downloads.js").read_text("utf-8")
        self.assertIn("validUrl", js)
        self.assertIn("data.size < 2097152", js)
        self.assertIn("aria-disabled", js)
        html = (ROOT / "site/index.html").read_text("utf-8")
        self.assertNotIn("FAT-UNQUALIFIED", html)
        for key in ("windows", "linux", "macos"):
            self.assertIn('data-platform="' + key + '"', html)

if __name__ == "__main__":
    # Unittest interprets argv; separate path parameter above if supplied.
    unittest.main(argv=[sys.argv[0]])
