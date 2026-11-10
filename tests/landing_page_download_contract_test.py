"""Landing product/download contract: no fictitious installers or secondary GUI."""
from pathlib import Path
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
            self.assertIn("prefers-reduced-motion", css)
            self.assertNotIn("https://github.com", html)  # Never navigate to GitHub UI.
            self.assertIn("release-catalog.json", js)
            self.assertIn("https://api.github.com/repos/masarray/arsonmix/releases?per_page=40", js)
            self.assertIn("Download installer ZIP", js)
            self.assertIn('id="hero-download" href="#download"', html)
            self.assertIn("Choose a verified platform download", html)
            self.assertNotIn("heroDownload.href =", js)
            self.assertNotIn("isWindowsVisitor", js)
            self.assertIn("Free to download", html)
            self.assertIn("Earlier Linux/macOS builds", html)
            self.assertIn("latest verified Windows alpha preview", html)
            self.assertFalse(bool(re.search(r"Windows alpha\\.\\d+", html)),
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
