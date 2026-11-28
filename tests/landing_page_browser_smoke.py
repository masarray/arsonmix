"""Real HTTP browser check: same-origin downloads.json cannot be fetched from file://."""
import json
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
from threading import Thread
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
LANDING = ROOT / "dist-pages/index.html"
DOWNLOAD_PATTERN = "**/release-catalog.json*"
LIVE_PATTERN = re.compile(r"^https://api[.]github[.]com/repos/masarray/arsonmix/releases[?]per_page=40$")
OFFICIAL = "https://github.com/masarray/arsonmix/releases/download/v0.9.0/"


class QuietSiteHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT / "dist-pages"), **kwargs)

    def log_message(self, *args):
        pass


def check_release_navigation(page):
    # The collapsed compatibility details intentionally expose official notes,
    # checksums, licensing and rollback. This is not a platform download CTA.
    notes = page.locator(
        '.download-details a[href="https://github.com/masarray/arsonmix/releases"]'
    )
    assert notes.count() == 1
    # Chromium returns an empty innerText for descendants of closed <details>.
    # Assert exact DOM text, then exercise the real disclosure (also without JS).
    expected_label = "official release notes, portable packages and checksums"
    assert notes.text_content() == expected_label
    disclosure = page.locator("details.download-details")
    assert disclosure.count() == 1
    was_open = disclosure.get_attribute("open") is not None
    if not was_open:
        assert not notes.is_visible()
        disclosure.locator("summary").click()
    assert notes.is_visible()
    assert notes.inner_text() == expected_label
    if not was_open:
        disclosure.locator("summary").click()
        assert not notes.is_visible()
    assert notes.get_attribute("data-release-link") is None
    # No other GitHub release navigation or unverified direct asset may escape
    # the catalog-owned controls, even when downloads are unavailable.
    outside_catalog = page.locator(
        'a[href*="github.com/masarray/arsonmix/releases"]:not([data-release-link])'
    )
    assert outside_catalog.count() == 1
    assert page.locator(
        'a[data-release-link]:not(.os-card a[data-release-link])'
    ).count() == 0


def check_reflow(page):
    # Report actual overflowing DOM owners, not just a pixel count.
    geometry = page.evaluate("""() => {
        const width = document.documentElement.clientWidth;
        const offenders = [...document.querySelectorAll('body *')]
            .filter(el => el.getBoundingClientRect().width > 0)
            .map(el => ({element: el.tagName + '.' + (typeof el.className === 'string' ? el.className : ''),
                         right: el.getBoundingClientRect().right,
                         left: el.getBoundingClientRect().left}))
            .filter(el => el.right > width + 2 || el.left < -2).slice(0, 20);
        return {width, overflow: document.documentElement.scrollWidth - width, offenders};
    }""")
    assert geometry["overflow"] <= 2, f"Horizontal overflow: {geometry}"
    assert page.locator(".footer-inner>div").evaluate("el => el.scrollWidth <= el.clientWidth + 2")
    assert page.locator("#system-requirements").evaluate("el => el.scrollWidth <= el.clientWidth + 2")


def browser_check(page):
    assert page.title() == "ArSonMix — Free Open Source Digital Mixer for Windows"
    assert page.locator("h1").count() == 1
    assert page.locator(".os-card").count() == 3
    requirements = page.locator("#system-requirements")
    assert requirements.is_visible()
    assert requirements.locator("tbody tr").count() == 9
    assert "4 GB" in requirements.inner_text() and "8 GB" in requirements.inner_text()
    assert requirements.locator("thead th").all_text_contents() == [
        "Component", "Minimum (estimated)", "Recommended"]
    assert "HDD or SSD" in requirements.inner_text()
    assert "Hardware estimates have not yet been validated" in requirements.inner_text()
    assert "no ssd required" not in page.locator("body").inner_text().lower()
    assert "Compact installer" in page.locator(".availability-note").inner_text()
    assert page.locator('.hero a[href="#system-requirements"]').is_visible()
    assert requirements.evaluate("el => el.scrollWidth <= el.clientWidth + 2")
    assert requirements.evaluate("el => el.getBoundingClientRect().bottom <= document.querySelector('.os-grid').getBoundingClientRect().top")

    assert page.locator(".app-shot img").count() == 1
    assert page.locator(".gallery img").count() == 3
    hero_image = page.locator(".app-shot img")
    assert hero_image.get_attribute("srcset").count(" 960w") == 1
    assert hero_image.get_attribute("srcset").count(" 1440w") == 1
    assert page.locator(".gallery-large img").get_attribute("srcset")
    assert page.locator(".gallery-pair img").first.get_attribute("srcset")
    hero_image.evaluate("img => img.decode()")
    # With width-descriptor srcset, naturalWidth is density-corrected CSS px,
    # not the physical JPEG width; assert actual decode + approved currentSrc.
    assert hero_image.evaluate("img => img.complete && img.naturalWidth > 300")
    assert hero_image.evaluate(
        "img => /\\/media\\/console-overview(?:-(?:960|1440))?\\.jpg$/.test("
        "new URL(img.currentSrc).pathname)")
    # Public conversion path: authentic rack image, six described FX, first
    # sound guide, and one-click feedback, on desktop AND phone.
    assert page.locator("#effects .fx-list article").count() == 8
    assert page.locator("#effects .fx-list article a[href^='./effects/']").count() == 8
    assert page.locator("#effects .fx-visual img").count() == 1
    # The effect image is intentionally lazy-loaded; bring it into the
    # viewport before checking decoded bytes rather than racing Chromium.
    assert (ROOT / "dist-pages/media/console-routing.jpg").is_file(), (
        "Stage actual marketing assets before starting Chromium"
    )
    rack = page.locator("#effects .fx-visual img")
    rack.scroll_into_view_if_needed()
    page.wait_for_function(
        "() => {const img=document.querySelector('#effects .fx-visual img'); return img && img.complete && img.naturalWidth>0;}",
        timeout=15000,
    )
    gallery = page.locator("#fx-gallery .fx-gallery-card")
    assert gallery.count() == 6
    assert page.locator("#fx-gallery img").count() == 6
    native_hero = page.locator("#fx-native-gallery .fx-gallery-card")
    assert native_hero.count() == 2
    assert page.locator("#fx-native-scenes").count() == 0
    # Only ONE ImmerSIB screenshot is referenced by homepage DOM. Five venue
    # scene images are moved to the dedicated crawlable detail page.
    assert page.locator('img[src*="fx-editor-immersib-"]').count() == 1
    for editor in page.locator("#fx-native-gallery img").all():
        editor.scroll_into_view_if_needed()
        editor.evaluate("img => img.decode()")
        assert editor.evaluate("img => img.complete && img.naturalWidth >= 937")
    assert "DSP OFFLINE" in page.locator("#effects").inner_text()
    for editor in page.locator("#fx-gallery img").all():
        editor.scroll_into_view_if_needed()
        editor.evaluate("img => img.decode()")
        assert editor.evaluate("img => img.complete && img.naturalWidth > 300")
    assert page.locator("#getting-started .step-grid article").count() == 3
    assert page.locator("#feedback").count() == 1
    assert page.locator('a[href="#effects"]').count() >= 2
    assert page.locator('a[href="#getting-started"]').count() >= 1
    assert page.locator('a[href="#feedback"]').count() >= 1
    assert page.locator(
        'a[href="https://github.com/masarray/arsonmix/issues/new?template=01_bug_report.yml"]'
    ).count() == 1
    assert page.locator(
        'a[href="https://github.com/masarray/arsonmix/issues/new?template=02_feature_request.yml"]'
    ).count() == 1

    assert page.locator("a[href='./preview/']").count() >= 2
    imgs = page.locator(".gallery img,.app-shot img,.brand img,.hero-brand-lockup img")
    assert page.locator(".hero-brand-lockup img").count() == 1
    assert page.locator(".hero-brand-lockup img").evaluate("img=>img.naturalWidth >= 512")
    # Offscreen gallery images also use loading=lazy. Verify the genuine
    # assets after visiting their viewport, without disabling lazy loading.
    for idx in range(imgs.count()):
        img = imgs.nth(idx)
        img.scroll_into_view_if_needed()
        img.evaluate("img => img.decode()")
    assert all(imgs.evaluate_all(
        "nodes=>nodes.map(n=>n.complete && n.naturalWidth>0)")), "Screenshot/brand broken"
    check_reflow(page)
    check_release_navigation(page)


def main():
    assert LANDING.is_file(), "Run build_pages.py before landing visual smoke"
    server = ThreadingHTTPServer(("127.0.0.1", 0), QuietSiteHandler)
    worker = Thread(target=server.serve_forever, daemon=True)
    worker.start()
    site = f"http://127.0.0.1:{server.server_port}/"
    try:
        with sync_playwright() as pw:
            browser = pw.chromium.launch(headless=True)
            for width, height, label in ((1440, 900, "desktop"), (390, 844, "mobile")):
                page = browser.new_page(
                    viewport={"width": width, "height": height}, reduced_motion="reduce")
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                unavailable = {"schemaVersion": 1,
                               "platforms": {"windows": None, "linux": None, "macos": None}}
                page.route(DOWNLOAD_PATTERN, lambda route: route.fulfill(
                    status=200, content_type="application/json", body=json.dumps(unavailable)))
                page.route(LIVE_PATTERN, lambda route: route.fulfill(
                    status=200, content_type="application/json", body="[]"))
                page.goto(site, wait_until="networkidle")
                browser_check(page)
                if label == "desktop":
                    for reflow_width in (320, 360, 768, 960, 1024, 1051, 1180, 1280):
                        page.set_viewport_size({"width": reflow_width, "height": 900})
                        check_reflow(page)
                    page.set_viewport_size({"width": width, "height": height})
                for slug, scene_count in (("8d-sonku", 0), ("immersib", 6), ("guitar-fx", 0)):
                    detail = browser.new_page(
                        viewport={"width": width, "height": height},
                        reduced_motion="reduce")
                    response = detail.goto(site + "effects/" + slug + "/", wait_until="networkidle")
                    assert response.status == 200, (slug, response.status)
                    assert detail.locator("h1").count() == 1
                    assert detail.locator("link[rel=canonical]").count() == 1
                    assert detail.locator('script[type="application/ld+json"]').count() == 1
                    assert detail.locator(".detail-scene-grid img").count() == scene_count
                    image = detail.locator(".detail-preview img")
                    image.evaluate("img => img.decode()")
                    assert image.evaluate("img => img.naturalWidth > 300")
                    for scene in detail.locator(".detail-scene-grid img").all():
                        scene.scroll_into_view_if_needed()
                        scene.evaluate("img => img.decode()")
                        assert scene.evaluate("img => img.naturalWidth == 937")
                    assert detail.evaluate(
                        "document.documentElement.scrollWidth - innerWidth") <= 2, slug
                    detail.close()
                # Both mocked API sources are unavailable; the build-time
                # verified HTML must retain its approved direct asset links.
                seeded = page.locator(".os-card[data-release-tag]").count()
                assert page.locator(".os-card.is-available").count() == seeded
                assert page.locator("a[data-release-link][href]").count() == seeded
                if seeded:
                    assert page.locator("#release-summary").inner_text().startswith("Ready to download")
                else:
                    assert "Downloads are being prepared" in page.locator("#release-summary").inner_text()
                assert not errors, errors
                page.screenshot(path="landing-v1-" + label + ".png",
                                full_page=True, animations="disabled")
                page.close()

            # Progressive enhancement: release links are real verified HTML,
            # not contingent on JavaScript or GitHub's live API availability.
            nojs = browser.new_page(viewport={"width": 1100, "height": 650},
                                    java_script_enabled=False)
            nojs.goto(site)
            check_release_navigation(nojs)
            verified = nojs.locator(".os-card[data-release-tag]").count()
            assert verified == nojs.locator(".os-card.is-available").count()
            # Unhydrated source HTML intentionally has inert #download
            # placeholders. Only exact GitHub asset links count as downloads.
            direct = nojs.locator(
                'a[data-release-link][href^="https://github.com/masarray/arsonmix/releases/download/"]'
            )
            assert verified == direct.count()
            for link in nojs.locator("a[data-release-link]").all():
                if link.get_attribute("href") == "#download":
                    assert link.get_attribute("aria-disabled") == "true"
            assert nojs.locator(".os-state").filter(has_text="NOT PUBLISHED").count() == 0
            nojs.close()

            def entry(suffix):
                name = "ArSonMix-v0.9.0-" + suffix
                return {"tag": "v0.9.0", "name": name, "url": OFFICIAL + name,
                        "size": 65000000, "sha256": "a" * 64, "prerelease": True,
                        "publishedAt": "2026-11-08T00:00:00Z"}

            valid = {"schemaVersion": 1, "platforms": {
                "windows": entry("Windows-x64-Setup.exe"),
                "linux": entry("Linux-x86_64.AppImage"),
                "macos": entry("macOS-universal.dmg")}}
            page = browser.new_page(viewport={"width": 1366, "height": 768})
            page.route(DOWNLOAD_PATTERN, lambda route: route.fulfill(
                status=200, content_type="application/json", body=json.dumps(valid)))
            page.route(LIVE_PATTERN, lambda route: route.fulfill(
                status=200, content_type="application/json", body="[]"))
            page.goto(site)
            page.wait_for_function(
                "document.querySelectorAll('.os-card.is-available').length === 3")
            browser_check(page)
            windows = page.locator("[data-platform=windows] [data-release-link]")
            assert windows.get_attribute("href") == OFFICIAL + "ArSonMix-v0.9.0-Windows-x64-Setup.exe"
            assert windows.get_attribute("target") is None, "Downloads must not open GitHub tabs"
            for os in ("linux", "macos"):
                assert page.locator("[data-platform=" + os + "] [data-release-link]").get_attribute(
                    "href").startswith(OFFICIAL)
            assert page.locator("#release-summary").inner_text().startswith(
                "Ready to download")
            page.close()
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)
    print("PASS landing: HTTP same-origin, desktop/mobile, OS availability, catalog-only download CTAs and official release information")


if __name__ == "__main__":
    main()
