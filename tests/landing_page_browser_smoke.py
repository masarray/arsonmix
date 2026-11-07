"""Landing screenshot/layout and truth-in-download browser checks."""
import json
from pathlib import Path
from playwright.sync_api import sync_playwright
ROOT = Path(__file__).resolve().parents[1]
LANDING = ROOT / "dist-pages/index.html"
DOWNLOAD_PATTERN = "**/repos/masarray/arsonmix/releases?per_page=15"
OFFICIAL = "https://github.com/masarray/arsonmix/releases/download/v0.9.0/"

def browser_check(page):
    assert page.title() == "ArSonMix — Professional Live Mixing Studio"
    assert page.locator("h1").count() == 1
    assert page.locator(".os-card").count() == 3
    assert page.locator(".app-shot img").count() == 1
    assert page.locator(".gallery img").count() == 3
    assert page.locator("a[href='./preview/']").count() >= 2
    imgs = page.locator(".gallery img,.app-shot img,.brand img")
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
                status=200,content_type="application/json",headers={"Access-Control-Allow-Origin":"*"},body="[]"))
            page.goto(LANDING.resolve().as_uri())
            page.wait_for_function("document.getElementById('release-summary').textContent.includes('No public desktop')")
            browser_check(page)
            assert page.locator(".os-card.is-available").count() == 0, "Fake download"
            assert not errors,errors
            page.screenshot(path="landing-v1-"+label+".png",full_page=True,animations="disabled")
            page.close()
        fake=[{"draft":False,"prerelease":False,"tag_name":"v0.9.0","assets":[
            {"name":"ArSonMix-Windows-x64-FAT-UNQUALIFIED.zip","size":123456,
             "browser_download_url":OFFICIAL+"ArSonMix-Windows-x64-FAT-UNQUALIFIED.zip"},
            {"name":"ArSonMix-Windows-x64.exe","size":65000000,
             "browser_download_url":OFFICIAL+"ArSonMix-Windows-x64.exe"},
            {"name":"ArSonMix-Windows-x64.zip","size":68000000,
             "browser_download_url":OFFICIAL+"ArSonMix-Windows-x64.zip"},
            {"name":"ArSonMix-Linux-x64.AppImage","size":60000000,
             "browser_download_url":OFFICIAL+"ArSonMix-Linux-x64.AppImage"},
            {"name":"ArSonMix-macOS-arm64.dmg","size":61000000,
             "browser_download_url":OFFICIAL+"ArSonMix-macOS-arm64.dmg"},
        ]}]
        page=browser.new_page(viewport={"width":1366,"height":768})
        page.route(DOWNLOAD_PATTERN,lambda route:route.fulfill(
            status=200,content_type="application/json",headers={"Access-Control-Allow-Origin":"*"},body=json.dumps(fake)))
        page.goto(LANDING.resolve().as_uri())
        page.wait_for_function("document.querySelectorAll('.os-card.is-available').length === 3")
        browser_check(page)
        windows=page.locator("[data-platform=windows] [data-release-link]")
        assert windows.get_attribute("href")==OFFICIAL+"ArSonMix-Windows-x64.zip"
        assert "UNQUALIFIED" not in windows.get_attribute("href")
        for os in ("linux","macos"):
            assert page.locator("[data-platform="+os+"] [data-release-link]").get_attribute("href").startswith(OFFICIAL)
        assert page.locator("#release-summary").inner_text().startswith("Verified against")
        page.close()
        browser.close()
    print("PASS landing: responsive screenshots, release-gated OS downloads")

if __name__ == "__main__":
    main()
