"""V16 actual browser parity against byte-perfect V15 WIRE source.

No fake CSS. 241 unchanged top-level WIRE component rules moved into their
existing dedicated CSS owner; compare old V15 and new V16 materials/layouts
at 5 screen sizes and genuine interactive hardware / WIRE states.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright
from css_v16_restore import manifest,reconstruct,mount_original,HTML,WIRE
from pages_css_architecture_v10_smoke import source_gate,fingerprint
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import combined_css
from pages_css_architecture_v15_smoke import variables

async def main():
    source_gate()
    doc=manifest()
    prior,old_wire=reconstruct()
    current=HTML.read_text(encoding="utf-8")
    old_css=combined_css(prior)
    new_css=combined_css(current)
    assert len(old_css)==doc["sourceCSS"]==173298
    assert fingerprint(old_css)==doc["sourceFNV"]=="ad118593"
    assert len(new_css)==doc["newCSS"]==130403
    assert fingerprint(new_css)==doc["newFNV"]=="4ed3cee1"
    assert len(old_wire)==doc["beforeWireSize"]==9600
    assert len(WIRE.read_text(encoding="utf-8"))==doc["afterWireSize"]
    assert (old_css.count("!important"),new_css.count("!important"))==(1317,952)
    assert doc["movedRules"]==241 and doc["movedChars"]==42895
    assert len(doc["insertions"])==241
    baseline,temp=mount_original(prior,old_wire)
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,width,height)
                await variables(browser,baseline,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await interactive_compare(browser,baseline,width,height)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip")
            await page.screenshot(path="pages-v16-wire-owner-1680.png")
            await page.close()
            await browser.close()
    finally:
        for path in temp:path.unlink(missing_ok=True)
    print("PASS V16: 241 WIRE rules relocated (42,895 chars) with exact V15 source"
          " recreation; 5 resolutions x 8 folds + 3 comprehensive control sweeps")

if __name__=="__main__":
    asyncio.run(main())
