"""V17 exact Plugin Manager and Fast FX Picker Chromium parity."""
import asyncio,re
from playwright.async_api import async_playwright
from css_v17_restore import manifest,reconstruct,mount_original,HTML,OWNER
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare

SELECTORS=[".plugin-browser",".plugin-head",".plugin-target",".plugin-close",
 ".plugin-results",".plugin-item",".plugin-flag",".plugin-footer",".plugin-disclaimer",
 ".plugin-scan-settings",".scan-paths",".fast-picker",".fast-head","#fastSearch",
 ".fast-results",".fast-effect",".fast-foot",".more-tools-panel",".command-bar"]
SNAP="""sels=>sels.map(s=>{
 const el=document.querySelector(s);if(!el)return [s,null];
 const st=getComputedStyle(el),r=el.getBoundingClientRect();
 return [s,{rect:[r.x,r.y,r.width,r.height].map(v=>Math.round(v*10)/10),
   styles:Object.fromEntries(["display","visibility","position","color","background-color",
   "background-image","box-shadow","border-top-color","border-left-color","border-radius",
   "width","height","min-height","max-height","padding","gap","font-size","font-weight",
   "opacity","cursor","z-index","overflow","pointer-events"].map(p=>[p,st.getPropertyValue(p)])),
   hidden:el.hidden,cls:el.className}];
})"""
async def plugin_compare(browser,baseline,width,height):
    old=await browser.new_page(viewport={"width":width,"height":height})
    now=await browser.new_page(viewport={"width":width,"height":height})
    errors=[]
    for page in (old,now):page.on("pageerror",lambda e:errors.append(str(e)))
    try:
        for page,url in ((old,baseline.as_uri()),(now,HTML.as_uri())):
            await page.goto(url)
            await page.wait_for_function("() => document.querySelectorAll('#tracks .strip').length>=5")
        async def same(label):
            a,b=await asyncio.gather(old.evaluate(SNAP,SELECTORS),now.evaluate(SNAP,SELECTORS))
            assert a==b,(width,height,label,next(((x,y) for x,y in zip(a,b) if x!=y),None))
        await same("initial")
        for selector in ("#pluginsButton","#pluginMinimize","#pluginMinimize","#pluginClose"):
            for page in (old,now):await page.locator(selector).click()
            await same(selector)
        for page in (old,now):
            btn=page.locator('#tracks .strip .fx-pick').first
            assert await btn.count()>0
            await btn.click()
            assert await page.locator("#fastPicker").evaluate("e=>!e.hidden")
        await same("fast-picker-open")
        for page in (old,now):await page.locator("#fastClose").click()
        await same("fast-picker-closed")
        for page in (old,now):await page.locator("#moreTools summary").click()
        await same("toolbar-more-open")
        assert not errors,(width,errors)
        if width==1680:await now.screenshot(path="pages-v17-plugin-owner-1680.png")
    finally:
        await old.close();await now.close()

async def main():
    source_gate();reconstruct()
    info=manifest()
    assert len(OWNER.read_text(encoding="utf-8"))==info["newOwnerLength"]
    inline="".join(re.findall(r"<style>(.*?)</style>",HTML.read_text(encoding="utf-8"),re.S))
    for fragment in (".plugin-target{",".fast-picker{",".plugin-scan-settings{"):
        assert fragment not in inline,fragment
    baseline,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),(1440,580)):
                await check(browser,baseline,width,height)
                await plugin_compare(browser,baseline,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await interactive_compare(browser,baseline,width,height)
            await browser.close()
    finally:
        for path in temporary:path.unlink(missing_ok=True)
    print(f"PASS V17: {info['movedCharacters']} CSS chars moved; real Chromium and interactions")

if __name__=="__main__":
    asyncio.run(main())
