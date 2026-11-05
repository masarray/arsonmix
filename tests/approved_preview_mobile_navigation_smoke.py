"""P0 mobile /preview/ regression: toolbar/nav must not render stray > glyphs.

Tests the actual official assembled Pages DOM and PNG-key skin in real Chromium
with mobile viewport + touch, not a second HTML, temporary CSS patch or mock UI.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "dist-pages/preview/index.html"


async def verify_phone(browser, width: int, height: int):
    context = await browser.new_context(
        viewport={"width": width, "height": height},
        is_mobile=True,
        has_touch=True,
        device_scale_factor=2,
    )
    page = await context.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    try:
        await page.goto(PAGE.as_uri())
        await page.wait_for_selector("#tracks .strip[data-id='1']")
        await page.evaluate("document.fonts.ready")
        info = await page.evaluate("""() => {
          const bar = document.querySelector(".command-bar");
          const more = document.querySelector("#moreTools summary");
          const root = document.documentElement;
          const mixer = document.querySelector(".tracks-viewport");
          const points = [...document.querySelectorAll(
            ".window-title *, .command-bar *, .section-rail *")];
          const glyphs = [];
          for (const el of points) {
            for (const pseudo of ["::before", "::after"]) {
              const content = getComputedStyle(el, pseudo).content;
              if (content && /(?:>{3,}|[›❯▸]{7,})/.test(content))
                glyphs.push({element:el.tagName, className:el.className,
                             pseudo, content});
            }
          }
          const a = bar.getBoundingClientRect();
          const m = more.getBoundingClientRect();
          return {
            viewport: innerWidth, pageWidth:root.scrollWidth,
            appWidth: document.querySelector(".app").getBoundingClientRect().width,
            toolbarWidth: a.width, toolbarLeft:a.left,
            toolbarClient: bar.clientWidth, toolbarScroll: bar.scrollWidth,
            toolbarOverflow: getComputedStyle(bar).overflowX,
            moreLeft: m.left, moreRight:m.right,
            moreText: more.textContent.trim(),
            mixerClient:mixer.clientWidth,mixerScroll:mixer.scrollWidth,
            visualText:document.body.innerText,
            literalMarker:document.body.innerText.includes(">>>>>>>"),
            pseudoMarkers:glyphs,
            stylesheets:[...document.querySelectorAll('link[rel="stylesheet"]')]
                          .map(l=>l.href)
          };
        }""")
        assert info["viewport"] == width, (width, info)
        assert info["appWidth"] <= width + 1, ("desktop min-width leaks on phone", info)
        assert info["toolbarWidth"] <= width + 1, ("toolbar is outside viewport", info)
        assert info["pageWidth"] <= width + 2, ("unscrollable full-page overflow", info)
        assert info["toolbarOverflow"] == "auto", ("toolbar cannot swipe", info)
        assert info["toolbarScroll"] > info["toolbarClient"] + 90, info
        assert 0 <= info["moreLeft"] < info["moreRight"] <= width, info
        assert info["mixerClient"] > 75 and info["mixerScroll"] > info["mixerClient"], info
        assert info["literalMarker"] is False and not info["pseudoMarkers"], (
            "stray mobile >>>>>>> chevrons/conflict markers", info)
        assert len(info["stylesheets"]) == 1, ("extra CSS layers", info)
        assert "arsonmix-approved-runtime.css?sha=" in info["stylesheets"][0], info

        # Approved original PNG hardware MUTE is unchanged and clickable.
        mute = page.locator("#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']")
        await mute.click()
        assert await mute.get_attribute("aria-pressed") == "true"
        await page.screenshot(path=f"pages-mobile-viewport-{width}.png")

        # Toolbar is genuinely swipeable; More remains reachable while scrolling.
        await page.locator(".command-bar").evaluate("(bar)=>{bar.scrollLeft=bar.scrollWidth;}")
        scroll = await page.locator(".command-bar").evaluate("(bar)=>bar.scrollLeft")
        assert scroll > 90, ("toolbar overflow locked against touch scrolling", scroll)
        await page.locator("#moreTools summary").click()
        panel = page.locator("#moreTools .more-tools-panel")
        await panel.wait_for(state="visible")
        position = await panel.evaluate("""el => {
          const rect=el.getBoundingClientRect();
          return {position:getComputedStyle(el).position,
                  left:rect.left,right:rect.right,top:rect.top,
                  bottom:rect.bottom,width:rect.width,height:rect.height};
        }""")
        assert position["position"] == "fixed", position
        assert 0 <= position["left"] <= position["right"] <= width, position
        assert position["top"] >= 40 and position["bottom"] <= height + 1, position
        assert await panel.locator("#addTrack").is_visible()
        await page.screenshot(path=f"pages-mobile-toolbar-more-{width}.png")
        await page.locator("#moreTools summary").click()
        assert not errors, errors
        print(f"PASS {width}x{height} mobile: one CSS, no > marker, "
              f"scrollable toolbar and channels, fixed More popup, PNG MUTE")
    finally:
        await context.close()


async def verify_desktop(browser):
    page = await browser.new_page(viewport={"width": 1366, "height": 768})
    try:
        await page.goto(PAGE.as_uri())
        await page.wait_for_selector("#tracks .strip[data-id='1']")
        result = await page.evaluate("""() => ({
            rootMin:getComputedStyle(document.body).minWidth,
            toolbarOverflow:getComputedStyle(document.querySelector('.command-bar')).overflowX,
            morePosition:getComputedStyle(document.querySelector('.more-tools-panel')).position,
            sheets:document.querySelectorAll('link[rel="stylesheet"]').length
        })""")
        assert result["toolbarOverflow"] == "visible", result
        assert result["morePosition"] == "absolute", result
        assert result["sheets"] == 1, result
        print("PASS desktop unaffected by mobile-specific toolbar navigation",result)
    finally:
        await page.close()


async def main():
    assert PAGE.is_file(), "Build the official /preview/ first"
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        try:
            for size in [(360, 780), (390, 844), (430, 932)]:
                await verify_phone(browser, *size)
            await verify_desktop(browser)
        finally:
            await browser.close()


if __name__ == "__main__":
    asyncio.run(main())
