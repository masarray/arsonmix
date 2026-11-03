"""V15 original V14 vs current Chromium material/layout/interaction parity."""
import asyncio
from playwright.async_api import async_playwright
from pages_css_architecture_v10_smoke import source_gate,fingerprint
from pages_css_architecture_v11_smoke import HTML,check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import combined_css
from css_v15_restore import (
    manifest,restore_previous_owners,before_v15_html,original_assets_as_baseline
)
VARS=["--route-h","--send-h","--pan-h","--switch-h","--rail-w",
      "--identity-h","--fx-h","--eq-h","--trim-h","--am-metal-base"]

async def variables(browser,baseline,width,height):
    a=await browser.new_page(viewport={"width":width,"height":height})
    b=await browser.new_page(viewport={"width":width,"height":height})
    try:
        await a.goto(baseline.as_uri())
        await b.goto(HTML.as_uri())
        for page in (a,b):
            await page.wait_for_function("() => document.querySelectorAll('#tracks .strip').length>=5")
        snapshot="""keys=>["html",".console-body","#tracks .strip"].map(sel=>{
          const el=document.querySelector(sel);
          return [sel,Object.fromEntries(keys.map(k=>[k,
            getComputedStyle(el).getPropertyValue(k)]))]})"""
        for folded in ([],["route"],["fx"],["send"],["route","fx","send"]):
            for page in (a,b):
                await page.evaluate("""keys=>{
                  const el=document.querySelector(".console-body");
                  ["route","fx","send"].forEach(k=>el.classList.toggle("collapsed-"+k,keys.includes(k)))
                }""",folded)
            old,new=await asyncio.gather(a.evaluate(snapshot,VARS),b.evaluate(snapshot,VARS))
            assert old==new,(width,height,folded,"layout variable drift",old,new)
    finally:
        await a.close()
        await b.close()

async def main():
    source_gate()
    oldhtml=before_v15_html()
    oldowners=restore_previous_owners()
    oldcss=combined_css(oldhtml,owner_override=oldowners)
    newcss=combined_css(HTML.read_text(encoding="utf-8"))
    info=manifest()["totals"]
    assert len(oldcss)==info["beforeCss"]==175850
    assert fingerprint(oldcss)==info["beforeFNV"]=="188064b0"
    assert len(newcss)==info["afterCss"]==173298
    assert fingerprint(newcss)==info["afterFNV"]=="ad118593"
    assert oldcss.count("!important")==1354
    assert newcss.count("!important")==1317
    baseline,temporary=original_assets_as_baseline(oldhtml,oldowners)
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
            await page.screenshot(path="pages-v15-clean-cascade-1680.png")
            await page.close()
            await browser.close()
    finally:
        for file in temporary:file.unlink(missing_ok=True)
    print("PASS V15: exact V14 source reconstructed, 95 declarations, 15 whole rules,"
          " 37 important removed; 5 viewports x 8 fold states, hardware and WIRE parity")

if __name__=="__main__":
    asyncio.run(main())
