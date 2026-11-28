"""Landing product/download contract: no fictitious installers or secondary GUI."""
from pathlib import Path
import json
import re
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
DIST = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "dist-pages"

class LandingReleaseContract(unittest.TestCase):
    def test_windows_requirements_match_readme(self):
        readme = (ROOT / "README.md").read_text("utf-8")
        md = readme.split("## System requirements (Windows)", 1)[1].split("\n## ", 1)[0]
        expected = [tuple(cell.strip() for cell in line.strip("|").split("|"))
                    for line in md.splitlines() if line.startswith("| ") and not line.startswith("| ---")]
        self.assertEqual(len(expected), 11)
        for base in (ROOT / "site", DIST):
            html = (base / "index.html").read_text("utf-8")
            self.assertEqual(html.count('id="system-requirements"'), 1)
            self.assertGreaterEqual(html.count('href="#system-requirements"'), 2)
            block = html.split('id="system-requirements"', 1)[1].split("</section>", 1)[0]
            actual = [tuple(re.findall(r"<t[hd][^>]*>([^<]*)</t[hd]>", row))
                      for row in re.findall(r"<tr>(.*?)</tr>", block, re.S)]
            self.assertEqual(actual, expected)
            self.assertLess(html.index('id="system-requirements"'), html.index('class="os-grid"'))
            for phrase in ("minimum hardware has not yet been qualified", "not a guarantee of dropout-free audio"):
                self.assertIn(phrase, block)
                self.assertIn(phrase, md)

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
            self.assertIn('id="fx-gallery"', html)
            for editor in ("8d-sonku","vocal-delay","harmonice","channel-eq","compressor","graphic-eq31"):
                self.assertIn("media/fx-editor-" + editor + ".jpg", html)
            native_screens = (
                "fx-editor-immersib-front-row-power.jpg",
                "fx-editor-immersib-symphonic-palace.jpg",
                "fx-editor-immersib-velvet-cinema.jpg",
                "fx-editor-immersib-arena-thunder.jpg",
                "fx-editor-immersib-royal-concert-hall.jpg",
                "fx-editor-immersib-concert-front-row-xl.jpg",
                "fx-editor-guitar-rig-clean-studio.jpg",
            )
            self.assertIn('id="fx-native-gallery"', html)
            self.assertNotIn('id="fx-native-scenes"', html)
            self.assertIn("DSP OFFLINE", html)
            self.assertEqual(html.count("fx-editor-immersib-concert-front-row-xl.jpg"), 1,
                             "Homepage must load one and only one ImmerSIB screenshot")
            for filename in native_screens:
                if filename == "fx-editor-immersib-concert-front-row-xl.jpg" or "guitar" in filename:
                    self.assertIn("media/" + filename, html)
                else:
                    self.assertNotIn("media/" + filename, html,
                                     "Full ImmerSIB gallery belongs on detail page, not homepage")
            fx_slugs = ("8d-sonku", "vocal-delay", "harmonice", "channel-eq",
                        "compressor", "graphic-eq31", "immersib", "guitar-fx")
            for slug in fx_slugs:
                self.assertIn('href="./effects/' + slug + '/"', html)
            self.assertEqual(html.count('class="fx-kind"'), len(fx_slugs))
            self.assertIn('id="getting-started"', html)
            self.assertIn('id="feedback"', html)
            self.assertIn("ArSonMix WIRE routing and input gain close-up", html)
            self.assertIn("Browse a dedicated explanation for each built-in audio effect", html)
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
            # One official informational release index is allowed in compatibility
            # details, but it must not masquerade as a catalog-owned download.
            self.assertEqual(html.count('href="https://github.com/masarray/arsonmix/releases"'), 1)
            self.assertIn('href="https://github.com/masarray/arsonmix/releases">official release notes, portable packages and checksums</a>', html)
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
                         "landing.css", "downloads.js", "fx-detail.css"):
                self.assertTrue((base / name).is_file(), (str(base), name))
            for name in ("console-overview.jpg", "console-routing.jpg", "console-faders.jpg",
                         "fx-editor-8d-sonku.jpg", "fx-editor-vocal-delay.jpg",
                         "fx-editor-harmonice.jpg", "fx-editor-channel-eq.jpg",
                         "fx-editor-compressor.jpg", "fx-editor-graphic-eq31.jpg",
                         *native_screens):
                path = base / "media" / name
                self.assertTrue(path.is_file(), str(path))
                self.assertGreater(path.stat().st_size, 12000)
                self.assertEqual(path.read_bytes()[:3], b"\xff\xd8\xff", str(path))
            console_manifest = json.loads((base / "media/console-capture-provenance.json").read_text("utf-8"))
            self.assertEqual(console_manifest["captureMode"], "owner-provided-windows-console")
            self.assertEqual(len(console_manifest["images"]), 2)
            responsive = {
                "console-overview-960.jpg": (960, 516),
                "console-overview-1440.jpg": (1440, 774),
                "console-routing-640.jpg": (640, 208),
            }
            variants = {v["file"]: v for v in console_manifest["responsiveVariants"]}
            self.assertEqual(set(variants), set(responsive))
            for filename, dims in responsive.items():
                variant = variants[filename]
                self.assertEqual((variant["width"], variant["height"]), dims)
                self.assertIn(variant["source"], {"console-overview.jpg", "console-routing.jpg"})
                data = (base / "media" / filename).read_bytes()
                self.assertEqual(data[:3], b"\xff\xd8\xff")
                self.assertEqual(len(data), variant["sizeBytes"])
                self.assertEqual(__import__("hashlib").sha256(data).hexdigest(), variant["sha256"])
            self.assertIn("console-overview-960.jpg 960w", html)
            self.assertIn("console-overview-1440.jpg 1440w", html)
            self.assertIn("console-routing-640.jpg 640w", html)

            for entry in console_manifest["images"]:
                self.assertTrue(entry["ownerProvidedNativeWindowsCapture"])
                self.assertFalse(entry["audioHardwareVerifiedByScreenshot"])
                self.assertRegex(entry["originalPngSha256"], r"^[0-9a-f]{64}$")
                media = base / "media" / entry["file"]
                self.assertEqual(media.stat().st_size, entry["sizeBytes"])
                self.assertEqual(__import__("hashlib").sha256(media.read_bytes()).hexdigest(),
                                 entry["sha256"])
            manifest = json.loads((base / "media/fx-gallery-provenance.json").read_text("utf-8"))
            self.assertEqual(manifest["schemaVersion"], 1)
            self.assertEqual(len(manifest["images"]), 6)
            for record in manifest["images"]:
                self.assertRegex(record["sha256"], r"^[0-9a-f]{64}$")
                self.assertFalse(record["actualWindowsScreenshot"])
                self.assertFalse(record["audioOrHardwareVerifiedByImage"])
                self.assertEqual((base / "media" / record["file"]).stat().st_size,
                                 record["sizeBytes"])
            native_manifest = json.loads((base / "media/native-fx-capture-provenance.json").read_text("utf-8"))
            self.assertEqual(native_manifest["schemaVersion"], 1)
            self.assertEqual(native_manifest["captureMode"],
                             "owner-provided-windows-native-juce-screenshots")
            self.assertIsNone(native_manifest["verifiedReleaseTag"])
            self.assertIsNone(native_manifest["verifiedWindowsBinarySha"])
            self.assertEqual(len(native_manifest["images"]), 7)
            self.assertEqual({entry["file"] for entry in native_manifest["images"]},
                             set(native_screens))
            for entry in native_manifest["images"]:
                self.assertTrue(entry["actualWindowsScreenshot"])
                self.assertFalse(entry["releasedInstallerVersionVerified"])
                self.assertFalse(entry["audioOrHardwareVerifiedByImage"])
                self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$")
                self.assertRegex(entry["originalSha256"], r"^[0-9a-f]{64}$")
                screenshot = base / "media" / entry["file"]
                self.assertEqual(screenshot.stat().st_size, entry["sizeBytes"])
                self.assertEqual(
                    __import__("hashlib").sha256(screenshot.read_bytes()).hexdigest(),
                    entry["sha256"], "Native screenshot changed since approval")
                if "guitar" in entry["file"]:
                    self.assertIn("DSP OFFLINE", entry["capturedUiState"])

    def test_indexable_fx_guides_are_unique_and_light(self):
        slugs = ("8d-sonku", "vocal-delay", "harmonice", "channel-eq",
                 "compressor", "graphic-eq31", "immersib", "guitar-fx")
        for slug in slugs:
            html_path = DIST / "effects" / slug / "index.html"
            self.assertTrue(html_path.is_file(), str(html_path))
            page = html_path.read_text("utf-8")
            self.assertIn("<h1>", page)
            self.assertIn('rel="canonical"', page)
            self.assertIn('application/ld+json', page)
            self.assertIn('href="../../#download"', page)
            self.assertIn('href="../../preview/"', page)
            self.assertEqual(page.count('<meta name="description"'), 1)
            self.assertEqual(page.count("<h1>"), 1)
            self.assertNotIn("releases/download/", page)
            self.assertNotIn("<iframe", page)
            count = page.count('class="detail-scene-grid"')
            if slug == "immersib":
                self.assertEqual(count, 1)
                self.assertEqual(page.count('class="detail-scene-grid"'), 1)
                for scene in ("front-row-power", "symphonic-palace",
                              "velvet-cinema", "arena-thunder", "royal-concert-hall",
                              "concert-front-row-xl"):
                    self.assertIn("fx-editor-immersib-" + scene + ".jpg", page)
            else:
                self.assertEqual(count, 0)
        self.assertEqual(len(list((DIST / "effects").glob("*/index.html"))), len(slugs))
        self.assertIn("effects/8d-sonku/", (DIST / "sitemap.xml").read_text("utf-8"))
        self.assertIn("effects/immersib/", (DIST / "sitemap.xml").read_text("utf-8"))

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
        self.assertIn("site/media/fx-editor-harmonice.jpg", readme)
        self.assertIn("site/media/fx-editor-8d-sonku.jpg", readme)
        self.assertIn("site/media/fx-editor-vocal-delay.jpg", readme)
        self.assertIn("site/media/fx-editor-immersib-concert-front-row-xl.jpg", readme)
        self.assertIn("site/media/fx-editor-guitar-rig-clean-studio.jpg", readme)
        self.assertIn("DSP OFFLINE", readme)
        self.assertIn("8D-SonKu P9.9", readme)
        for fx in ("8d-sonku", "vocal-delay", "harmonice", "channel-eq",
                   "compressor", "graphic-eq31", "immersib", "guitar-fx"):
            self.assertIn("https://masarray.github.io/arsonmix/effects/" + fx + "/", readme)
        self.assertNotIn("Harmonice + Vocal Delay", readme)
        self.assertNotIn("Guitar FX, EQ + Compressor", readme)
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
        # Full Chromium must test the staged public site, not just the
        # partial /preview/ assembler output (which omits landing media/CSS).
        ui = (ROOT / ".github" / "workflows" / "ui-smoke.yml").read_text("utf-8")
        self.assertIn("python scripts/build_landing_assets.py dist-pages", ui)
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
