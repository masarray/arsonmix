"""V20 browser-verified elimination of 53 exact same-selector losers.

No optimistic selector-subset removal. Exact V19 source is restored by the
reversible fixture; real Chromium compares material, pseudo states, geometry,
3 control interactions and all prior independent CSS/hardware invariants.
"""
import asyncio
from playwright.async_api import async_playwright
from css_v20_restore import manifest,reconstruct,mount_original,HTML
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare

PROPERTIES=(
 "background","background-color","background-image","box-shadow",
 "text-shadow","border","border-color","border-radius","border-width",
 "color","font-size","font-weight","transform","transition",
 "filter","opacity","outline","outline-color","left","top","right",
 "bottom","height","width","display","position","pointer-events",
 "--am-hw-key-face","--am-hw-key-bright","--am-hw-key-ink",
)
SAMPLES=(
 ".command-bar button.tool",
 ".command-bar summary.tool",
 ".console-body .strip .switch-section",
 ".console-body .strip .mx-hw-row>button.mx-hw-key",
 "#tracks .strip .fader-section",
 "#tracks .strip .fx-section",
 "#tracks .strip .send-section",
 "#tracks .strip.selected::after",
 ".plugin-browser",
)
SNAP="""({selectors,properties})=>selectors.map(selector=>{
 let pseudo=selector.endsWith("::after")?"::after":null;
 let el=document.querySelector(pseudo?selector.slice(0,-7):selector);
 if(!el)return [selector,null];
 let s=getComputedStyle(el,pseudo),r=el.getBoundingClientRect();
 return [selector,{geometry:pseudo?null:[r.x,r.y,r.width,r.height].map(v=>+v.toFixed(1)),
 css:Object.fromEntries(properties.map(p=>[p,s.getPropertyValue(p)])),
 pressed:el.getAttribute("aria-pressed"),disabled:el.disabled??false}];
})"""

async def pseudo_state_compare(browser,original,width,height):
    previous=await browser.new_page(viewport={"width":width,"height":height})
    current=await browser.new_page(viewport={"width":width,"height":height})
    try:
        for page,url in ((previous,original.as_uri()),(current,HTML.as_uri())):
            await page.goto(url)
            await page.wait_for_function(
                "() => document.querySelectorAll('#tracks .strip').length>=5"
            )
        async def assert_same(stage):
            payload={"selectors":SAMPLES,"properties":PROPERTIES}
            old,new=await asyncio.gather(
                previous.evaluate(SNAP,payload),current.evaluate(SNAP,payload))
            assert old==new,(width,height,stage,next(
                ((a,b) for a,b in zip(old,new) if a!=b),None))
        await assert_same("rest")
        for page in (previous,current):
            await page.locator(".command-bar button.tool").first.hover()
        await assert_same("hover")
        for page in (previous,current):
            await page.locator(".command-bar button.tool").first.focus()
        await assert_same("focus")
        for page in (previous,current):
            # Disable only after clearing focus on both independently rendered
            # pages; Chromium may otherwise retain :focus-visible for one event
            # turn on an element disabled while focused (test-order artifact).
            await page.locator(".command-bar button.tool").first.evaluate(
                "e=>{e.blur();e.disabled=true}")
            await page.wait_for_function(
                "() => !document.querySelector('.command-bar button.tool').matches(':focus-visible')"
            )
        await assert_same("disabled")
        for page in (previous,current):
            await page.locator(".command-bar button.tool").first.evaluate(
                "e=>{e.disabled=false;e.setAttribute('aria-pressed','true')}")
        await assert_same("latched")
        for page in (previous,current):
            await page.locator(".command-bar button.tool").first.evaluate(
                "e=>e.removeAttribute('aria-pressed')")
        await assert_same("restored")
        if width==1680:
            await current.screenshot(path="pages-v20-shadow-clean-1680.png")
    finally:
        await previous.close()
        await current.close()

async def main():
    source_gate()
    previous_html=reconstruct()
    data=manifest()
    assert data["original"]["cssImportant"]-data["result"]["cssImportant"]==51
    assert data["original"]["cssLength"]-data["result"]["cssLength"]==3861
    assert len([e for e in data["edits"] if e["kind"]=="rule"])==7
    assert len([e for e in data["edits"] if e["kind"]=="declaration"])==28
    assert all(x["important"] in (True,False) for x in data["evidence"])
    baseline,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for w,h in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,w,h)
            for w,h in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,baseline,w,h)
                await material_compare(browser,baseline,w,h)
                await interactive_compare(browser,baseline,w,h)
                await root_parity(browser,baseline,w,h)
            await browser.close()
    finally:
        for path in temporary:path.unlink(missing_ok=True)
    print("PASS V20: 53 real CSS cascade losers removed (51 !important),"
          " 7 dead selector rules, -3861 CSS chars; V19 exact reconstruction,"
          " 5x8 fold masks and 3 visual/pseudo/interaction/token sweeps")

if __name__=="__main__":
    asyncio.run(main())
