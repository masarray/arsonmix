"""V26: Retired hardware socket CSS removal with exact V25 visual lock."""
import asyncio,re
from playwright.async_api import async_playwright
from css_v26_restore import HTML,KEY,manifest,reconstruct,mount_original,inline_css,meta
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

def source_gate():
    d=manifest()
    old_html,old_key=reconstruct()
    current=HTML.read_text(encoding="utf-8")
    key=KEY.read_text(encoding="utf-8")
    assert d["totals"]["removedChars"]>2000
    assert d["totals"]["deadRules"]==11 and d["totals"]["removedImportant"]==51
    assert inline_css(old_html).count("!important")-inline_css(current).count("!important")==37
    assert old_key.count("!important")-key.count("!important")==14
    assert current.count("/* V30 PHYSICAL BUTTON CORRECTION")==0
    assert key.count("/* V30 PHYSICAL BUTTON CORRECTION")==1
    assert ".mx-hw-key" not in "".join(
        e["text"] for r in d["sources"].values() for e in r["edits"]
    )
    assert all(e["kind"]=="dead legacy wrapper" and
               ".hw-switch-socket" in e["selector"]
               for e in d["sources"]["mixer-control-keys.css"]["edits"])
    assert all(e["kind"]=="dead legacy wrapper" and
               ".hw-switch-socket" in e["selector"]
               or e["kind"]=="empty" and e["selector"]==".console-body .strip .gain-scale-travel"
               for e in d["sources"]["inline"]["edits"])
    assert ":is(" in key and ".hw-switch-socket>.switch" in key,(
        "Legacy :is() specificity stays deliberately unchanged"
    )
    assert "switch-section mx-hw-row" in current
    assert '<span class="hw-switch-socket">' not in current

async def no_legacy_dom(browser):
    for width,height in ((1366,768),(1680,900),(1920,1080)):
        page=await browser.new_page(viewport={"width":width,"height":height})
        errors=[]
        page.on("pageerror",lambda e:errors.append(str(e)))
        try:
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip[data-id='1'] .mx-hw-row")
            for density in ("comfort","standard","compact"):
                await page.locator("#densitySelect").select_option(density)
                assert await page.locator(".hw-switch-socket").count()==0
                keys=page.locator("#tracks .strip[data-id='1'] .mx-hw-row>button.mx-hw-key")
                assert await keys.count()==3
                for i in (0,1,2):
                    await keys.nth(i).click()
                    assert await page.locator(".hw-switch-socket").count()==0
                for i in (0,1,2):
                    await keys.nth(i).click()
                assert await page.locator(".hw-switch-socket").count()==0
            assert not errors,(width,errors)
        finally:
            await page.close()

async def main():
    source_gate()
    old,tmp=mount_original()
    try:
        async with async_playwright() as playwright:
            browser=await playwright.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),
                                 (1366,700),(1440,580)):
                await check(browser,old,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,old,width,height)
                await material_compare(browser,old,width,height)
                await interactive_compare(browser,old,width,height)
                await root_parity(browser,old,width,height)
            await no_legacy_dom(browser)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip[data-id='1'] .mx-hw-row")
            await page.screenshot(path="pages-v26-dead-socket-pruned-1680.png")
            await page.close()
            await browser.close()
    finally:
        for p in tmp:p.unlink(missing_ok=True)
    print("PASS V26: 11 truly dead socket rules + 1 empty rule removed, "
          "51 important declarations deleted. Exact V25 source recovered, "
          "5x8 viewport/folding and 3x computed pseudo, material, token, "
          "MUTE/SOLO/PFL interaction parity, no obsolete wrapper in 9 density states.")

if __name__=="__main__":
    asyncio.run(main())
