"""V13: real Chromium baseline against EXACT original owner CSS.

Remove declarations only where an identical selector/context/priority has a
later winning declaration. Reversible text ledger proves source integrity.
Temporary original stylesheets are co-located with current assets, preserving
relative URL resolution and CSS import semantics. Always clean temporary files.
"""
import asyncio
import json
import tempfile
from pathlib import Path
from playwright.async_api import async_playwright
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import HTML, check, PROPS

ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/"tests/fixtures/css-v13-owner-removals.json"
# Independently fingerprint the approved current owner tails (GUI M1 + CSS V17).
# The historic V13 prefix, declaration-removal ledger and baseline parity
# remain exact; changing the fader/meter ruler deliberately re-approves ONLY
# the owned suffix fingerprints, not the historical owner source.
V18_OWNER_TAILS={
    "mixer-pan-controls.css":(294,"01ccf01a"),
    "mixer-fader-assembly.css":(5239,"bc5d21f1"),
    "mixer-meter-display.css":(4159,"94c0c481"),
    "mixer-plugin-browser.css":(4285,"1ce2ede9"), # V17 exact appended owner suffix
    "mixer-fx-rack.css":(310,"bc79a5b1"), # M3 empty FX placeholder
}
def tail_fingerprint(text):
    h=2166136261
    for char in text:
        h=((h^ord(char))*16777619)&0xffffffff
    return f"{h:08x}"
SAMPLES=[
    ".console-body", ".command-bar", ".mixer-title",
    ".console-body .section-rail", "#tracks .strip", "#master .strip",
    "#tracks .strip .fx-section", "#tracks .strip .fxslot-row",
    "#tracks .strip .fx-pick", "#tracks .strip .pan-section",
    "#tracks .strip .center-pan", "#tracks .strip .fader-section",
    "#tracks .strip .fader-box", "#tracks .strip .fader-box::before",
    "#tracks .strip .fader-travel", "#tracks .strip .fader-travel .cap",
    "#tracks .strip .meter-box", "#tracks .strip .meter-box::before",
    "#tracks .strip .meter-box .vu-bars", ".plugin-browser", ".plugin-head",
    ".plugin-search-row", ".plugin-filters", ".plugin-item",
    "#tracks .strip .mx-hw-row",
    '#tracks .strip button[data-hw-kind="mute"]',
    '#tracks .strip button[data-hw-kind="solo"]',
    '#tracks .strip button[data-hw-kind="cue"]',
    ".console-body.wire-routing-v15 .strip .wire-node-out",
    ".console-body.wire-routing-v15 .strip .wire-node-in",
]
SNAP="""([sels,props]) => sels.map(selector=>{
 const pseudo=selector.endsWith('::before')?'::before':selector.endsWith('::after')?'::after':null;
 const e=document.querySelector(pseudo?selector.slice(0,-pseudo.length):selector);
 if(!e)return [selector,null];
 const s=getComputedStyle(e,pseudo),r=e.getBoundingClientRect();
 return [selector,{
   rect:pseudo?null:[r.x,r.y,r.width,r.height].map(v=>Math.round(v*10)/10),
   styles:Object.fromEntries([...props,'content','transform','transition',
      'border-left-color','background-size','background-position','accent-color',
      'z-index'].map(p=>[p,s.getPropertyValue(p)])),
   pressed:e.getAttribute('aria-pressed'),
   cls:e.className?.baseVal??e.className,
   value:'value' in e?e.value:null,
   hidden:e.hidden
 }];
})"""
def restore_baseline():
    manifest=json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert manifest["milestone"]=="V13"
    html=HTML.read_text(encoding="utf-8")
    temporary=[]
    saved=0
    removed_imp=0
    try:
        for name,record in manifest["files"].items():
            assert "/" not in name and name.endswith(".css")
            path=HTML.parent/"assets"/name
            from css_v18_restore import reconstruct as restore_previous_chrome,manifest as v18_manifest
            current=(restore_previous_chrome()[1] if name=="mixer-chrome-material.css"
                     else path.read_text(encoding="utf-8"))
            original=current[:record["afterSize"]]
            tail=current[record["afterSize"]:]
            expected_tail=V18_OWNER_TAILS.get(name,(0,"811c9dc5"))
            assert (len(tail),tail_fingerprint(tail))==expected_tail,(name,"unapproved CSS owner tail")
            assert len(original)==record["afterSize"],name
            restored=original
            for entry in sorted(record["insertions"],key=lambda e:e["offset"]):
                off=entry["offset"]
                assert 0<=off<=len(restored),(name,off)
                restored=restored[:off]+entry["text"]+restored[off:]
            assert len(restored)==record["beforeSize"],name
            assert original.count("!important")==record["afterImportant"],name
            assert restored.count("!important")==record["beforeImportant"],name
            saved+=len(restored)-len(original)
            removed_imp+=restored.count("!important")-original.count("!important")
            tf=tempfile.NamedTemporaryFile(mode="w",suffix=".css",prefix=".v13-original-",
                       dir=path.parent,encoding="utf-8",delete=False)
            with tf:
                # The current owner suffix must exist on BOTH compared pages. Only the
                # original V13 removal history differs in these reconstructions.
                # Keep V18 moved material before historical V13 Chrome source.
                prefix=(v18_manifest()["prefix"] if name=="mixer-chrome-material.css" else "")
                from gui_m3_compat import suffix
                tf.write(prefix+restored+tail+(suffix("chrome")[1] if name=="mixer-chrome-material.css" else ""))
            temporary.append(Path(tf.name))
            search=f"assets/{name}?v="
            assert html.count(search)==1,name
            html=html.replace(search,f"assets/{Path(tf.name).name}?v=")
        assert saved==5287,saved
        assert removed_imp==111,removed_imp
        assert len(manifest["files"])==5
        htmlfile=tempfile.NamedTemporaryFile(mode="w",suffix=".html",
                prefix=".v13-original-console-",dir=HTML.parent,
                encoding="utf-8",delete=False)
        with htmlfile:
            htmlfile.write(html)
        temporary.append(Path(htmlfile.name))
        return Path(htmlfile.name),temporary
    except Exception:
        for path in temporary:path.unlink(missing_ok=True)
        raise

async def snapshot(page):
    return await page.evaluate(SNAP,[SAMPLES,PROPS])

async def interactive_compare(browser,baseline,width,height):
    a=await browser.new_page(viewport={"width":width,"height":height})
    b=await browser.new_page(viewport={"width":width,"height":height})
    errors=[]
    for page in (a,b):
        page.on("pageerror",lambda e:errors.append(str(e)))
    try:
        await a.goto(baseline.as_uri())
        await b.goto(HTML.as_uri())
        for page in (a,b):
            await page.wait_for_function(
                "() => document.querySelectorAll('#tracks .strip').length>=5 && !!document.querySelector('#pluginsButton')")
        async def assert_equal(label):
            old,new=await asyncio.gather(snapshot(a),snapshot(b))
            assert old==new,(width,label,next(
                ((x,y) for x,y in zip(old,new) if x!=y),None))
            assert sum(x[1] is not None for x in new)>=23,(width,label)
        await assert_equal("initial")
        if width==1680:
            await b.screenshot(path="pages-v13-owner-console-1680.png")
        for action,selector in (
            ("click",'#tracks .strip[data-id="1"] button[data-hw-kind="mute"]'),
            ("click",'#tracks .strip[data-id="1"] button[data-hw-kind="solo"]'),
            ("click",'#tracks .strip[data-id="1"] button[data-hw-kind="cue"]'),
            ("hover",'#tracks .strip[data-id="1"] .fader-box'),
            ("click","#pluginsButton"),
            ("click","#pluginMinimize"),
            ("click","#pluginMinimize"),
            ("click","#pluginClose"),
        ):
            for page in (a,b):
                loc=page.locator(selector)
                assert await loc.count()>0,(selector,"expected GUI component absent")
                if action=="click":await loc.click()
                else:await loc.hover()
            await assert_equal(f"{action}:{selector}")
        for page in (a,b):
            el=page.locator('#tracks .strip .center-pan').first
            await el.evaluate("""el=>{
                 el.value='70'; el.dispatchEvent(new Event('input',{bubbles:true}));
                 el.dispatchEvent(new Event('change',{bubbles:true}));
            }""")
        await assert_equal("pan-change")
        assert not errors,(width,errors)
    finally:
        await a.close()
        await b.close()

async def main():
    source_gate() # V12 main cascade fingerprint MUST remain exactly intact.
    baseline,cleanup=restore_baseline()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await check(browser,baseline,width,height)
                await interactive_compare(browser,baseline,width,height)
            await browser.close()
    finally:
        for f in cleanup:f.unlink(missing_ok=True)
    print("PASS V13: 115 redundant declarations, 19 dead rules, 111 real !important removed;"
          " exact component owner restore, 3 viewport x 8 fold plus physical control/plugin/pan parity")

if __name__=="__main__":
    asyncio.run(main())
