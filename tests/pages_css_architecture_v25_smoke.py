"""V25 V51 visual LOCK: physically identical CSS declaration stream after migration.

No visual redesign. The approved GUI V51 is compared to an exact reconstructed
same-SHA V51 HTML/CSS baseline in real Chromium, without mock markup/ENGINE.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright
from css_v25_restore import (
    HTML,OWNER,fnv,manifest,reconstruct,mount_original,inline_css,meta
)
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

def source_gate():
    data=manifest()
    old_html,old_key=reconstruct()
    live=HTML.read_text(encoding="utf-8")
    key=OWNER.read_text(encoding="utf-8")
    new_inline,_=inline_css(live)
    previous_inline,_=inline_css(old_html)
    assert len(previous_inline)==23140 and len(new_inline)==14394
    assert len(data["movedCss"])==8746
    assert key==data["movedCss"]+old_key
    assert previous_inline+old_key==new_inline+key
    assert old_html.count(data["priorLink"])==1
    assert live.count(data["currentLink"])==1
    assert old_html.count("/* V30 PHYSICAL BUTTON CORRECTION")==1
    assert live.count("/* V30 PHYSICAL BUTTON CORRECTION")==0
    assert key.count("/* V30 PHYSICAL BUTTON CORRECTION")==1
    assert key.count("/* ArSonMix mixer hardware keys v6")==1
    assert old_key.count("!important")==12
    assert key.count("!important")==109
    assert meta(previous_inline+old_key)==data["combined"]
    assert not any(x in data["movedCss"].lower()
                   for x in ("url(","@import","@charset","filter:blur"))
    assert ".mx-hw-key" not in data["movedCss"]
    assert "document.styleSheets" not in live
    assert data["metrics"]["deletedCssDeclarations"]==0
    assert data["metrics"]["addedImportant"]==0

async def v51_rect_and_material(browser,original):
    """Stable CSS/geometry of the current approved V51 meter, key and WIRE."""
    selectors=[
        ".command-bar", ".command-bar button.tool",
        ".console-body .strip .switch-section",
        "#tracks .strip[data-id='1'] .mx-hw-row",
        "#tracks .strip[data-id='1'] .mx-hw-key",
        "#tracks .strip[data-id='1'] .mx-mute-face",
        "#tracks .strip[data-id='1'] .fader-travel .cap",
        "#tracks .strip[data-id='1'] .meter-box",
        "#tracks .strip[data-id='1'] .meter-lane",
        "#tracks .strip[data-id='1'] .meter-fill",
        "#tracks .strip[data-id='1'] .meter-peak-hold",
        ".wire-workspace", "#wireConfigWindow",
        "#wireNavigatorToggle", "#wireMatrixToggle"
    ]
    props=["display","position","background-color","background-image",
        "background-size","box-shadow","text-shadow","border-radius",
        "color","opacity","transform","filter","font-size","font-weight",
        "padding-top","padding-left","height","width","min-height","max-width",
        "outline-color","outline-width"]
    capture="""([selectors,props])=>selectors.map(sel=>{
        const el=document.querySelector(sel);
        if(!el)return [sel,null];
        const s=getComputedStyle(el),r=el.getBoundingClientRect();
        return [sel,{rect:[r.x,r.y,r.width,r.height].map(v=>Math.round(v*10)/10),
            css:Object.fromEntries(props.map(p=>[p,s.getPropertyValue(p)])),
            pressed:el.getAttribute('aria-pressed'),
            expanded:el.getAttribute('aria-expanded'),
            disabled:el.disabled||false}];
    })"""
    for width,height in ((1366,768),(1680,900),(1920,1080)):
        a=await browser.new_page(viewport={"width":width,"height":height})
        b=await browser.new_page(viewport={"width":width,"height":height})
        errors=[]
        for p in (a,b):p.on("pageerror",lambda error:errors.append(str(error)))
        try:
            await a.goto(original.as_uri())
            await b.goto(HTML.as_uri())
            for p in (a,b):
                await p.wait_for_selector("#tracks .strip[data-id='1'] .meter-peak-hold")
            for density in ("comfort","standard","compact"):
                for p in (a,b):
                    await p.locator("#densitySelect").select_option(density)
                old=await a.evaluate(capture,[selectors,props])
                new=await b.evaluate(capture,[selectors,props])
                assert old==new,(width,density,next(
                    ((x,y) for x,y in zip(old,new) if x!=y),None))
                assert sum(y is not None for _,y in new)>=12
            assert not errors,(width,errors)
        finally:
            await a.close()
            await b.close()

async def main():
    source_gate()
    old,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),
                                 (1366,700),(1440,580)):
                await check(browser,old,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,old,width,height)
                await material_compare(browser,old,width,height)
                await interactive_compare(browser,old,width,height)
                await root_parity(browser,old,width,height)
            await v51_rect_and_material(browser,old)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip[data-id='1'] .meter-peak-hold")
            await page.screenshot(path="pages-v25-control-keys-v51-1680.png")
            await page.close()
            await browser.close()
    finally:
        for f in temporary:f.unlink(missing_ok=True)
    print("PASS V25: V51 button material ownership migrated without changing "
          "a single CSS declaration or cascade order. Five viewports x eight "
          "fold states; three viewports materials/pseudo/interactive/token; "
          "nine current V51 density+meter/peak-hold/fader/WIRE snapshots.")

if __name__=="__main__":
    asyncio.run(main())
