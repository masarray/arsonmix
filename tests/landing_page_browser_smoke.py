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


def browser_check(page):
    assert page.title() == "ArSonMix — Professional Live Mixing Studio"
    assert page.locator("h1").count() == 1
    assert page.locator(".os-card").count() == 3
    assert page.locator(".app-shot img").count() == 1
    assert page.locator(".gallery img").count() == 3
    assert page.locator("a[href='./preview/']").count() >= 2
    imgs = page.locator(".gallery img,.app-shot img,.brand img,.hero-brand-lockup img")
    assert page.locator(".hero-brand-lockup img").count() == 1
    assert page.locator(".hero-brand-lockup img").evaluate("img=>img.naturalWidth >= 512")
    assert all(imgs.evaluate_all(
        "nodes=>nodes.map(n=>n.complete && n.naturalWidth>0)")), "Screenshot/brand broken"
    overflow = page.evaluate("document.documentElement.scrollWidth - window.innerWidth")
    assert overflow <= 2, "Horizontal overflow (px): " + str(overflow)
    assert page.locator('a[href*="github.com/masarray/arsonmix/releases"]:not([data-release-link])').count() == 0


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
                page.goto(site)
                page.wait_for_function(
                    "document.getElementById('release-summary').textContent.includes('Downloads are being prepared')")
                browser_check(page)
                assert page.locator(".os-card.is-available").count() == 0
                assert page.locator("a[data-release-link][href]").count() == 0
                assert not errors, errors
                page.screenshot(path="landing-v1-" + label + ".png",
                                full_page=True, animations="disabled")
                page.close()

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
    print("PASS landing: HTTP same-origin, desktop/mobile, OS availability, zero GitHub UI")


if __name__ == "__main__":
    main()
