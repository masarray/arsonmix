"""Stage C: visual + computed CSS parity between original and deployed owner sheets.

Compare the real *compiled* /preview/ to exactly the SAME assembled GUI/PNG
assets with unoptimized original authoring CSS restored. Never use legacy
web/app.js, preview-v2 routes, a demo-renderer substitute, or another layout.
"""
import asyncio
import hashlib
from pathlib import Path
import re
import shutil
import tempfile

from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
COMPILED=ROOT/"dist-pages/preview"


STYLE_EVIDENCE=r"""() => {
 const props=['display','position','width','height','min-width','max-width',
  'padding','margin','gap','grid-template-columns','flex','transform',
  'color','background','background-color','background-image',
  'border','border-radius','box-shadow','text-shadow','filter',
  'clip-path','opacity','visibility','font','line-height','z-index',
  '--lit','--peak-extent','--ch'];
 const included='.app,.app *,#wireWorkspace,#wireWorkspace *,#moreTools,#moreTools *';
 const nodes=[...document.querySelectorAll(included)];
 return nodes.map((node,i)=>{
   const style=getComputedStyle(node);
   const name=typeof node.className==='string'?node.className:'';
   const values=props.map(p=>style.getPropertyValue(p));
   const pseudo=node.matches('.strip,.meter-fill,.meter-lane,.mx-hw-key,.fader-travel,.wire-node-out,.strip-identity-top');
   const before=pseudo?getComputedStyle(node,'::before'):null;
   const after=pseudo?getComputedStyle(node,'::after'):null;
   const pseudoValue=s=>s?[s.content,s.backgroundImage,s.border,s.boxShadow,
                          s.transform,s.filter,s.opacity]:null;
   return [i,node.tagName,name,values,pseudoValue(before),pseudoValue(after)];
 });
}"""


async def render(browser, path, viewport, state):
    page=await browser.new_page(viewport={"width":viewport[0],"height":viewport[1]},
                                reduced_motion="reduce")
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    try:
        await page.goto(path.resolve().as_uri())
        await page.wait_for_selector("#tracks .strip[data-id='1']")
        await page.evaluate("document.fonts.ready")
        await page.wait_for_timeout(100)
        if state=="hover-identity":
            await page.locator("#tracks .strip[data-id='1'] .strip-identity-top").hover()
            await page.wait_for_timeout(130)
        elif state=="hover-hardware":
            await page.locator("#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']").hover()
            await page.wait_for_timeout(130)
        assert not errors,errors
        styles=await page.evaluate(STYLE_EVIDENCE)
        screenshot=await page.screenshot(animations="disabled",full_page=False)
        return styles,hashlib.sha256(screenshot).hexdigest()
    finally:
        await page.close()


async def main():
    assert (COMPILED/"build.json").is_file(),"build_pages.py must run first"
    with tempfile.TemporaryDirectory(prefix="armix-stage-c-original-") as tmp:
        baseline=Path(tmp)/"preview"
        shutil.copytree(COMPILED,baseline)
        cssnames=[p.name for p in (COMPILED/"assets").glob("*.css")]
        for name in cssnames:
            source=(ROOT/("site/preview-buttons/assets"
                           if name=="mixer-control-keys.css"
                           else "site/preview-v2/assets")/name)
            if source.is_file():
                shutil.copyfile(source,baseline/"assets"/name)
        async with async_playwright() as playwright:
            browser=await playwright.chromium.launch(headless=True)
            try:
                for viewport,state in [
                    ((1366,768),"default"),
                    ((1366,768),"hover-identity"),
                    ((1366,768),"hover-hardware"),
                    ((1600,900),"default"),
                ]:
                    original_style,original_image=await render(
                        browser,baseline/"index.html",viewport,state)
                    optimized_style,optimized_image=await render(
                        browser,COMPILED/"index.html",viewport,state)
                    assert len(original_style)==len(optimized_style),(
                        "DOM changed",viewport,state)
                    assert original_style==optimized_style,(
                        "computed CSS cascade changed",viewport,state,
                        next(((a,b) for a,b in zip(original_style,optimized_style)
                              if a!=b),None))
                    assert original_image==optimized_image,(
                        "approved GUI pixel screenshot changed",viewport,state,
                        original_image,optimized_image)
                    print("PASS Stage C computed/screenshot parity:",
                          viewport,state,len(original_style),"elements")
            finally:
                await browser.close()
    print("PASS: same actual /preview/ PNG GUI, compiled CSS declarations are invisible "
          "across 1366x768, 1600x900 and pointer-hover states")


if __name__=="__main__":
    asyncio.run(main())
