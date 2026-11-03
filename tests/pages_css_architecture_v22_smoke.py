"""V22: exact historical V21 source and browser state comparison.

Migrate 11,389 chars of physically shared key material into the existing
Chrome owner. Delete only a declaration-free pseudo-element rule; no new
override, visual recipe, stylesheets, JS or engine modifications.
"""
import asyncio
from playwright.async_api import async_playwright
from css_v22_restore import manifest,reconstruct,mount_original,HTML,CHROME,fingerprint
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

async def source_audit():
    source_gate()
    old_html,old_chrome=reconstruct()
    data=manifest()
    live=HTML.read_text(encoding="utf-8")
    chrome=CHROME.read_text(encoding="utf-8")
    assert data["old"]["inlineLength"]==34529
    assert data["current"]["inlineLength"]==23140
    assert data["old"]["inlineImportant"]-data["current"]["inlineImportant"]==52
    assert len(chrome)-len(old_chrome)==11389-416
    assert old_chrome+data["sourceInlineSlice"].replace(data["deadEmptyRule"],"",1)==chrome
    assert data["metrics"]["removedDeclarations"]==0
    assert data["metrics"]["removedImportant"]==0
    assert "/* ARSONMIX GLOBAL HARDWARE KEY V1" in old_html
    assert "/* ARSONMIX GLOBAL HARDWARE KEY V1" not in live
    assert "/* ARSONMIX GLOBAL HARDWARE KEY V1" in chrome
    assert old_html.count(data["oldLink"])==1 and live.count(data["newLink"])==1
    # No dynamic pseudo behavior exists in the removed literally empty rule.
    assert data["deadEmptyRule"].strip().endswith("::before{\n}")
    assert len(data["deadEmptyRule"])==416
    assert data["current"]["chromeFNV"]==fingerprint(chrome)

async def main():
    await source_audit()
    baseline,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,baseline,width,height)
                await material_compare(browser,baseline,width,height)
                await interactive_compare(browser,baseline,width,height)
                await root_parity(browser,baseline,width,height)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip")
            await page.screenshot(path="pages-v22-legacy-keys-1680.png")
            await page.close()
            await browser.close()
    finally:
        for path in temporary:path.unlink(missing_ok=True)
    print("PASS V22: 11,389 shared key material chars owned by Chrome, "
          "one empty pseudo rule deleted (416 chars, 0 declarations/important), "
          "exact V21 source restoration and 5x8 responsive + 3 material/state/control sweeps")

if __name__=="__main__":
    asyncio.run(main())
