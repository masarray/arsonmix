"""P0 mobile /preview/ regression: toolbar/nav must not render stray > glyphs.

Tests the actual official assembled Pages DOM and PNG-key skin in real Chromium
with mobile viewport + touch, not a second HTML, temporary CSS patch or mock UI.
"""
import asyncio
import re
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
          const more = document.querySelector("#moreTools > summary");
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
            channelBanks:document.querySelectorAll("#bankSelect option").length,
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
        # At phone widths the approved mixer shows one channel PER BANK
        # instead of a horizontally overflowing ten-channel strip. Bank
        # selector and Next remain the official navigation authority.
        assert info["mixerClient"] > 75 and info["channelBanks"] > 1, info
        assert not re.match(r"^(?:>\s*){3,}", info["visualText"]), (
            "orphaned CSS-link closing > rendered above title", info["visualText"][:90])
        assert info["literalMarker"] is False and not info["pseudoMarkers"], (
            "stray mobile >>>>>>> chevrons/conflict markers", info)
        assert len(info["stylesheets"]) == 1, ("extra CSS layers", info)
        assert "arsonmix-approved-runtime.css?sha=" in info["stylesheets"][0], info

        # Approved original PNG hardware MUTE is unchanged and clickable.
        mute = page.locator("#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']")
        await mute.click()
        assert await mute.get_attribute("aria-pressed") == "true"
        await page.screenshot(path=f"pages-mobile-viewport-{width}.png")
        # Bank navigation is genuine, not a fake replacement mobile mixer.
        assert await page.locator(".strip[data-id='1']").count() == 1
        await page.locator("#bankSelect").select_option("1")
        await page.wait_for_selector("#tracks .strip[data-id='2']")
        assert await page.locator(".strip[data-id='1']").count() == 0
        await page.locator("#bankSelect").select_option("0")
        await page.wait_for_selector("#tracks .strip[data-id='1']")

        # Toolbar is genuinely swipeable; More remains reachable while scrolling.
        await page.locator(".command-bar").evaluate("(bar)=>{bar.scrollLeft=bar.scrollWidth;}")
        scroll = await page.locator(".command-bar").evaluate("(bar)=>bar.scrollLeft")
        assert scroll > 90, ("toolbar overflow locked against touch scrolling", scroll)
        menu = page.locator("#moreTools > summary")
        anchor = await menu.bounding_box()
        assert anchor and 0 <= anchor["x"] and anchor["x"] + anchor["width"] <= width
        assert 0 <= anchor["y"] and anchor["y"] + anchor["height"] <= 40, anchor
        # The fixed disclosure must actually receive touch after the bar scrolls.
        await menu.tap()
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
        category = panel.locator('[data-menu-category="channel"]')
        await category.tap()
        child = panel.locator("#armix-menu-channel")
        await child.wait_for(state="visible")
        assert await category.get_attribute("aria-expanded") == "true"
        assert await panel.locator(".armix-menu-flyout:visible").count() == 1
        assert await child.locator("#addTrack").is_visible()
        child_bounds = await child.bounding_box()
        assert child_bounds and child_bounds["x"] >= position["right"] - 2
        assert child_bounds["x"] + child_bounds["width"] <= width + 1
        assert child_bounds["y"] >= 0 and child_bounds["y"] + child_bounds["height"] <= height + 1
        # Verify hit testing, not merely visibility behind a scroll clip.
        assert await child.locator("#addTrack").evaluate("""el => {
          const r=el.getBoundingClientRect();
          const hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);
          return hit===el||el.contains(hit);
        }""")
        await page.screenshot(path=f"pages-mobile-toolbar-more-{width}.png")
        await menu.tap()
        await panel.wait_for(state="hidden")

        # Player is now reached through its V6 workflow tab, not a hidden
        # Mixer command. No second runtime CSS, mixer or browser audio.
        await page.locator("#ribbon-tab-player").tap()
        assert await page.locator("#ribbon-tab-player").get_attribute("aria-selected") == "true"
        await page.locator("#playerToggle").tap()
        player = page.locator("#arsonPlayer")
        await player.wait_for(state="visible")
        bounds = await player.bounding_box()
        assert bounds is not None
        assert bounds["x"] >= 0 and bounds["x"] + bounds["width"] <= width + 2
        assert bounds["y"] >= 0 and bounds["y"] + bounds["height"] <= height + 2
        assert await page.locator('link[rel="stylesheet"]').count() == 1
        # Without JUCE the public preview is a non-audio demo; the
        # contextual output chooser MUST NOT impersonate a device selector.
        assert await page.locator("#playerChooseOutput").is_hidden()
        assert await page.locator("#playerChooseOutput").count() == 1
        # M2 controls are compact and must never create WebAudio or a
        # competing callback on public /preview/ without native JUCE.
        assert await page.locator("#playerKeyValue").inner_text() == "0 st"
        assert await page.locator("#playerTempoValue").inner_text() == "100%"
        for control in ("#playerKeyMinus","#playerKeyPlus",
                        "#playerTempoMinus","#playerTempoPlus",
                        "#playerPitchReset"):
            button=page.locator(control)
            bounds=await button.bounding_box()
            assert bounds and bounds["x"]>=0 and bounds["x"]+bounds["width"]<=width+2
        await page.locator("#playerKeyPlus").click()
        assert "desktop app" in await page.locator("#playerFeedback").inner_text()
        assert await page.locator("#playerKeyValue").inner_text() == "0 st"
        folder_button=page.locator("#playerAddFolder")
        assert await folder_button.is_visible()
        folder_bounds=await folder_button.bounding_box()
        assert folder_bounds and folder_bounds["x"]>=0
        assert folder_bounds["x"]+folder_bounds["width"]<=width+2
        assert await page.locator("#playerRelink").is_hidden()
        await folder_button.click()
        assert "desktop app" in await page.locator("#playerFeedback").inner_text()
        await page.locator("#playerAdd").click()
        feedback = await page.locator("#playerFeedback").inner_text()
        assert "desktop app" in feedback, ("public preview must not fake playback", feedback)
        await page.locator("#playerClose").click()
        assert await player.is_hidden()
        assert not errors, errors
        print(f"PASS {width}x{height} mobile: one CSS, no orphan > marker, "
              f"swipe toolbar + real channel banks, fixed cascading menu, PNG MUTE")
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
