#!/usr/bin/env python3
"""One-time real screenshots of the approved public /preview/ GUI for the landing page.

These are browser captures of the current assembled source (including approved
PNG buttons), not generated fake UI or the obsolete secondary console.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "dist-pages/preview/index.html"
OUT = ROOT / "site/media"

def main():
    if not PAGE.is_file():
        raise SystemExit("Run scripts/build_pages.py first")
    OUT.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        page = browser.new_page(
            viewport={"width": 1680, "height": 900},
            device_scale_factor=1,
            reduced_motion="reduce",
        )
        errors = []
        page.on("pageerror", lambda err: errors.append(str(err)))
        page.goto(PAGE.resolve().as_uri())
        page.locator('#tracks .strip[data-id="1"] .mx-hw-row').wait_for(timeout=25000)
        page.evaluate("document.fonts.ready")
        page.wait_for_timeout(280)
        assert page.locator("#wireLayer").count() == 1, "Not the approved WIRE console"
        assert page.locator("#tracks .strip").count() >= 9, "Incomplete mixer"
        page.screenshot(path=str(OUT / "console-overview.jpg"),
                        type="jpeg", quality=87, animations="disabled")
        # Evidence of real WIRE interaction, accepted in the reference GUI.
        body_class = page.locator(".console-body").get_attribute("class") or ""
        if "collapsed-send" in body_class:
            page.locator('.section-rail [data-section="send"]').click()
        page.locator('#tracks .strip[data-id="5"] .wire-node-out').click()
        page.locator('#tracks .strip[data-id="3"] .wire-node-in').click()
        src = page.locator('#tracks .strip[data-id="5"] .wire-node-out')
        assert src.get_attribute("data-wire-connected") == "true", "WIRE route missing"
        page.wait_for_timeout(180)
        page.screenshot(path=str(OUT / "console-routing.jpg"), type="jpeg",
                        quality=87, animations="disabled",
                        clip={"x": 280, "y": 175, "width": 1060, "height": 355})
        # Crop the physical buttons, dual VU meters and silver faders exactly.
        page.screenshot(path=str(OUT / "console-faders.jpg"), type="jpeg",
                        quality=88, animations="disabled",
                        clip={"x": 280, "y": 495, "width": 1060, "height": 355})
        if errors:
            raise AssertionError("UI JavaScript errors during capture: " + "; ".join(errors[:4]))
        for file in OUT.glob("console-*.jpg"):
            assert file.stat().st_size > 12000, "Screenshot file suspiciously small: " + file.name
        page.close()
        browser.close()
    print("ARSONMIX_LANDING_SCREENSHOTS_CAPTURED", *[x.name for x in OUT.glob("*.jpg")])

if __name__ == "__main__":
    main()
