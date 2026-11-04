"""V27 source-exact CSS winner cleanup + scoped global material parity.

The current approved #363 Pages GUI is the ONLY visual baseline.
Zero layout/color/shading/interactive/functional changes are permitted.
"""
import asyncio
from playwright.async_api import async_playwright
from css_v27_restore import HTML,ASSETS,meta,manifest,reconstruct,mount_original
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

def source_gate():
    d=manifest()
    original,owners=reconstruct()
    changed=HTML.read_text(encoding="utf-8")
    assert changed!=original
    chrome=(ASSETS/"mixer-chrome-material.css").read_text(encoding="utf-8")
    scoped=d["owners"]["mixer-chrome-material.css"]["globalScope"]
    assert scoped["old"] not in chrome and chrome.count(scoped["now"])==1
    assert scoped["oldSpecificity"]==scoped["newSpecificity"]
    assert ".app" in scoped["now"] and ":where(" in scoped["now"]
    for name in d["ownerOrder"][1:]:
        record=d["owners"][name]
        css=(ASSETS/name).read_text(encoding="utf-8")
        assert len(record["edits"])==record["metrics"]["declarations"]
        assert all(edit["winnerText"] in owners[name] and
                   edit["winnerOffset"]>edit["offset"] and
                   edit["priority"]==edit["winnerValue"].lower().endswith("!important")
                   for edit in record["edits"])
        assert css.count("!important")==record["current"]["important"]
        assert len(css)<len(owners[name])
    assert sum(len(d["owners"][n]["edits"]) for n in d["ownerOrder"])==37
    assert d["totals"]["cssCharsDeleted"]>300
    assert not any(".mx-hw-key" in edit["selector"] for name in d["ownerOrder"]
                   for edit in d["owners"][name]["edits"])

async def full_dom_filter_parity(browser,original):
    props=["filter","backdrop-filter","-webkit-backdrop-filter"]
    capture="""props=>{
      const all=[...document.querySelectorAll('.app,.app *')];
      return all.map((el,i)=>[i,el.tagName,el.id,el.className?.baseVal??el.className,
        ...['', '::before','::after'].map(pseudo=>{
          const s=getComputedStyle(el,pseudo);
          return props.map(name=>s.getPropertyValue(name))
        })]);
    }"""
    # Compare every original GUI DOM node and its generated ::before/::after,
    # not a small cherry-picked set of elements.
    for width,height in ((1366,768),(1680,900),(1920,1080)):
        a=await browser.new_page(viewport={"width":width,"height":height})
        b=await browser.new_page(viewport={"width":width,"height":height})
        errors=[]
        for p in (a,b):p.on("pageerror",lambda error:errors.append(str(error)))
        try:
            await a.goto(original.as_uri())
            await b.goto(HTML.as_uri())
            for p in (a,b):await p.wait_for_selector("#tracks .strip[data-id='1'] .mx-hw-row")
            for density in ("comfort","standard","compact"):
                for p in (a,b):await p.locator("#densitySelect").select_option(density)
                old=await a.evaluate(capture,props)
                current=await b.evaluate(capture,props)
                assert len(current)>=100,(width,density,"insufficient real DOM coverage")
                assert old==current,(width,density,next(
                    ((x,y) for x,y in zip(old,current) if x!=y),None))
            # A foreign node outside .app must no longer inherit an app-only
            # policy; this is precisely the global selector leakage fixed.
            inject="""() => {
              const el=document.createElement('div');
              el.id='v27-external-integration-probe';
              el.style.filter='blur(2px)';
              document.body.appendChild(el);
              return getComputedStyle(el).filter;
            }"""
            assert await a.evaluate(inject)=="none","Old global rule no longer matches: baseline invalid"
            assert await b.evaluate(inject)=="blur(2px)","Out-of-app CSS filter leakage persists"
            assert not errors,(width,errors)
        finally:
            await a.close()
            await b.close()

async def main():
    source_gate()
    original,tmp=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),
                                 (1366,700),(1440,580)):
                await check(browser,original,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,original,width,height)
                await material_compare(browser,original,width,height)
                await interactive_compare(browser,original,width,height)
                await root_parity(browser,original,width,height)
            await full_dom_filter_parity(browser,original)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip[data-id='1'] .mx-hw-row")
            await page.screenshot(path="pages-v27-scoped-chrome-1680.png")
            await page.close()
            await browser.close()
    finally:
        for f in tmp:f.unlink(missing_ok=True)
    print("PASS V27: 37 exact same-owner overridden declarations pruned. "
          "Scoped Chrome filter keeps original specificity and every GUI "
          "filter/backdrop/pseudo state, 9 density-viewports, 5x8 folded "
          "layout, 3x material/pseudo/interaction/token parity. "
          "Outside-app filter leak fixed without touching the audio engine.")

if __name__=="__main__":
    asyncio.run(main())
