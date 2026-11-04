"""CSS V28 current V59 source-order parity after eliminating inline CSS.

Only the owner of declarations changes. All browser-computed styles, modern
selected-only WIRE and physical MUTE/SOLO/PFL/material must remain unchanged.
"""
import asyncio,re
from playwright.async_api import async_playwright
from css_v28_restore import HTML,OWNER,meta,manifest,reconstruct,mount_original
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

def source_gate():
    record=manifest()
    original,old_chrome=reconstruct()
    new_html=HTML.read_text(encoding="utf-8")
    new_chrome=OWNER.read_text(encoding="utf-8")
    moved=record["movedCss"]
    assert new_chrome==old_chrome+moved
    assert meta(new_chrome)["important"]==meta(old_chrome)["important"]+118
    assert len(moved)==13043
    assert original.count("<style>")==3 and new_html.count("<style>")==0
    assert original.count("/* V28 FADER HEIGHT")==1
    assert new_html.count("/* V28 FADER HEIGHT")==0
    assert new_chrome.count("/* V28 FADER HEIGHT")==1
    assert original.count("/* V15 original physical patch connectors")==1
    assert new_chrome.count("/* V15 original physical patch connectors")==1
    assert "v59-selected-wire-quiet" in new_html
    assert "v8" in original.lower()
    # The script and HTML after </head> must not be modified. CSS move is
    # restricted to the single link cache key + three style node deletions.
    assert original[original.index("<body>"):] == new_html[new_html.index("<body>"):]
    assert record["removedDeclarations"]==record["addedDeclarations"]==0

async def computed_full_console_parity(browser,original):
    props=[
        "display","position","visibility","width","height","min-width","max-width",
        "min-height","max-height","margin-top","margin-right","margin-bottom",
        "margin-left","padding-top","padding-right","padding-bottom","padding-left",
        "top","right","bottom","left","z-index","flex","flex-basis","flex-grow",
        "grid-template-columns","grid-template-rows","gap","overflow",
        "color","background-color","background-image","border-top-color",
        "border-top-width","border-bottom-color","border-bottom-width",
        "border-radius","box-shadow","text-shadow","transform","filter",
        "backdrop-filter","opacity","font-size","line-height",
        "--v13-fader-h","--send-h","--fx-h"
    ]
    sample="""props => [...document.querySelectorAll('.app,.app *')].map((el,index)=>{
       const sty=getComputedStyle(el);
       const cname=typeof el.className==='string'?el.className:
             (el.className?.baseVal||'');
       const vals=props.map(p=>sty.getPropertyValue(p));
       const pseudo=(
         el.matches('.strip,.section-rail .rail-cell,.mx-hw-key,.fader-travel,.meter-lane,.wire-node-out,.wire-node-in,.route-choice,.command-bar,.switch-section,.routing-wire-overlay')
         ? ['::before','::after'].map(p=> {
             const s=getComputedStyle(el,p);
             return [s.content,s.display,s.backgroundImage,s.backgroundColor,s.boxShadow,s.opacity,s.filter,s.transform];
           }):null);
       return [index,el.tagName,el.id,cname,vals,pseudo];
    })"""
    for width,height in ((1366,768),(1680,900),(1920,1080)):
        before=await browser.new_page(viewport={"width":width,"height":height})
        after=await browser.new_page(viewport={"width":width,"height":height})
        errors=[]
        for page in (before,after):
            page.on("pageerror",lambda e:errors.append(str(e)))
        try:
            await before.goto(original.as_uri())
            await after.goto(HTML.as_uri())
            for page in (before,after):
                await page.wait_for_selector("#tracks .strip[data-id='1'] .mx-hw-row")
            for density in ("comfort","standard","compact"):
                for page in (before,after):
                    await page.locator("#densitySelect").select_option(density)
                lhs=await before.evaluate(sample,props)
                rhs=await after.evaluate(sample,props)
                assert len(rhs)>100,(width,height,density,"DOM coverage too small")
                assert lhs==rhs,(width,height,density,len(lhs),len(rhs),
                     next(((a,b) for a,b in zip(lhs,rhs) if a!=b),None))
            assert not errors,(width,height,errors)
        finally:
            await before.close()
            await after.close()

async def main():
    source_gate()
    old,tmp=mount_original()
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
            await computed_full_console_parity(browser,old)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip[data-id='1'] .mx-hw-row")
            await page.screenshot(path="pages-v28-physical-console-owner-v59-1680.png")
            await page.close()
            await browser.close()
    finally:
        for f in tmp:f.unlink(missing_ok=True)
    print("PASS CSS V28: 13,043 original late inline CSS chars migrated to "
          "existing Chrome physical owner, zero CSS declaration/cascade/JS/engine "
          "changes. Exact V59 restoration, 5 viewport x 8 folds, 3 viewport "
          "material/interaction/pseudo/token and 9 density-viewports full-DOM "
          "48-property styles with hardware/rail/WIRE pseudo-elements.")

if __name__=="__main__":
    asyncio.run(main())
