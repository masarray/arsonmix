"""V18 Chrome owner refactor: exact V17 source plus real Chromium control parity."""
import asyncio
from playwright.async_api import async_playwright
from css_v18_restore import manifest,reconstruct,mount_original,HTML,CHROME
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity

SAMPLES=(
 ".window-title",".command-bar",".command-bar button.tool",
 ".console-body","#tracks .strip","#master .strip",
 "#tracks .strip .switch-section","#tracks .strip .fader-section",
 "#tracks .strip .fader-travel .cap","#tracks .strip .meter-box",
 "#tracks .strip .pan-section",".console-body .gain-scale-travel",
 ".console-body .section-rail",".console-body .route-section",
 ".plugin-browser",".plugin-head",".more-tools-panel",
)
SNAP="""sels=>sels.map(sel=>{
 const el=document.querySelector(sel);if(!el)return [sel,null];
 const st=getComputedStyle(el),r=el.getBoundingClientRect();
 return [sel,{rect:[r.x,r.y,r.width,r.height].map(v=>Math.round(v*10)/10),
   styles:Object.fromEntries(["display","color","background-color","background-image",
    "box-shadow","text-shadow","border-top-color","border-left-color","border-radius",
    "font-size","font-weight","font-family","height","width","max-height","min-height",
    "padding","gap","opacity","position","outline-color","transform","transition",
    "pointer-events"].map(k=>[k,st.getPropertyValue(k)])),
   pressed:el.getAttribute("aria-pressed")}];
})"""

async def material_compare(browser,baseline,width,height):
    old=await browser.new_page(viewport={"width":width,"height":height})
    now=await browser.new_page(viewport={"width":width,"height":height})
    errors=[]
    for page in (old,now):page.on("pageerror",lambda e:errors.append(str(e)))
    try:
        for page,url in ((old,baseline.as_uri()),(now,HTML.as_uri())):
            await page.goto(url)
            await page.wait_for_function("() => document.querySelectorAll('#tracks .strip').length>=5")
        async def same(label):
            a,b=await asyncio.gather(old.evaluate(SNAP,SAMPLES),now.evaluate(SNAP,SAMPLES))
            assert a==b,(width,height,label,next(((x,y) for x,y in zip(a,b) if x!=y),None))
        await same("resting")
        for selector in ('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]',
                         '#tracks .strip[data-id="1"] button[data-hw-kind="solo"]',
                         '#tracks .strip[data-id="1"] button[data-hw-kind="cue"]',
                         '#pluginsButton','#pluginMinimize','#pluginMinimize','#pluginClose'):
            for page in (old,now):await page.locator(selector).click()
            await same(selector)
        for page in (old,now):
            pan=page.locator('#tracks .strip .center-pan').first
            await pan.evaluate("""e=>{e.value="70";e.dispatchEvent(new Event("input",{bubbles:true}));
                                 e.dispatchEvent(new Event("change",{bubbles:true}));}""")
        await same("PAN adjustment")
        assert not errors,(width,height,errors)
        if width==1680:await now.screenshot(path="pages-v18-chrome-owner-1680.png")
    finally:
        await old.close();await now.close()

async def main():
    source_gate()
    previous,owner=reconstruct()
    data=manifest()
    assert data["inlineBefore"]-data["inlineNow"]==17244
    assert "url(" not in data["originalStyle"],"URL base must not change"
    assert CHROME.read_text(encoding="utf-8")==data["prefix"]+owner
    original_rule=data["originalStyle"].split(".console-body .switch.btn-180{",1)[1].split("}",1)[0]
    new_rule=data["prefix"].split(".console-body .switch.btn-180{",1)[1].split("}",1)[0]
    for declaration in data["removedDeclarations"]:
        assert declaration in original_rule and declaration not in new_rule
    baseline,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await material_compare(browser,baseline,width,height)
                await interactive_compare(browser,baseline,width,height)
                await root_parity(browser,baseline,width,height)
            await browser.close()
    finally:
        for path in temporary:path.unlink(missing_ok=True)
    print("PASS V18: 17,244 inline CSS characters moved to Chrome owner;"
          " 4 shadowed important declarations eliminated;"
          " Chromium 5x8 folds + 3 comprehensive interactive/token checks")

if __name__=="__main__":
    asyncio.run(main())
