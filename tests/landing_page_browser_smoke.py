"""Landing screenshot/layout and truth-in-download browser checks."""
import json
from pathlib import Path
from playwright.sync_api import sync_playwright
ROOT = Path(__file__).resolve().parents[1]
LANDING = ROOT / "dist-pages/index.html"
DOWNLOAD_PATTERN = "**/release-catalog.json*"
OFFICIAL = "https://github.com/masarray/arsonmix/releases/download/v0.9.0/"

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
    assert all(imgs.evaluate_all("nodes=>nodes.map(n=>n.complete && n.naturalWidth>0)")), "Screenshot/brand broken"
    overflow = page.evaluate("document.documentElement.scrollWidth - window.innerWidth")
    assert overflow <= 2, "Horizontal overflow (px): " + str(overflow)

def main():
    assert LANDING.is_file(), "Run build_pages.py before landing visual smoke"
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        for width, height, label in ((1440,900,"desktop"),(390,844,"mobile")):
            page = browser.new_page(viewport={"width":width,"height":height},reduced_motion="reduce")
            errors = []
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.route(DOWNLOAD_PATTERN,lambda route:route.fulfill(
                status=200,content_type="application/json",body=json.dumps({"schemaVersion":1,"platforms":{"windows":None,"linux":None,"macos":None}})))
            page.goto(LANDING.resolve().as_uri())
            page.wait_for_function("document.getElementById('release-summary').textContent.includes('No supported desktop')")
            browser_check(page)
            assert page.locator(".os-card.is-available").count() == 0, "Fake download"
            assert page.locator("a[data-release-link][href]").count() == 0, "Missing binary must not lead anywhere"
            assert not errors,errors
            page.screenshot(path="landing-v1-"+label+".png",full_page=True,animations="disabled")
            page.close()
        def entry(platform_suffix):
            name="ArSonMix-v0.9.0-"+platform_suffix
            return {"tag":"v0.9.0","name":name,"url":OFFICIAL+name,
                    "size":65000000,"sha256":"a"*64,"prerelease":True,
                    "publishedAt":"2026-11-08T00:00:00Z"}
        fake={"schemaVersion":1,"platforms":{
            "windows":entry("Windows-x64-Setup.exe"),
            "linux":entry("Linux-x86_64.AppImage"),
            "macos":entry("macOS-universal.dmg")}}
        page=browser.new_page(viewport={"width":1366,"height":768})
        page.route(DOWNLOAD_PATTERN,lambda route:route.fulfill(
            status=200,content_type="application/json",body=json.dumps(fake)))
        page.goto(LANDING.resolve().as_uri())
        page.wait_for_function("document.querySelectorAll('.os-card.is-available').length === 3")
        browser_check(page)
        windows=page.locator("[data-platform=windows] [data-release-link]")
        assert windows.get_attribute("href")==OFFICIAL+"ArSonMix-v0.9.0-Windows-x64-Setup.exe"
        assert "UNQUALIFIED" not in windows.get_attribute("href")
        for os in ("linux","macos"):
            assert page.locator("[data-platform="+os+"] [data-release-link]").get_attribute("href").startswith(OFFICIAL)
        assert page.locator("#release-summary").inner_text().startswith("Latest verified downloads")
        page.close()
        browser.close()
    print("PASS landing: responsive screenshots, release-gated OS downloads")

if __name__ == "__main__":
    main()
