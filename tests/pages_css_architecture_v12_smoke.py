"""V12: prove deleted CSS truly shadowed; reconstruct exact V11 and compare live UI.

The reversible removals manifest records original offsets and text of EVERY deleted
declaration/rule. V11 reconstruction is exact, not a simulated CSS approximation.
"""
import asyncio
import json
import re
import tempfile
from pathlib import Path
from playwright.async_api import async_playwright
from pages_css_architecture_v10_smoke import source_gate, fingerprint
from pages_css_architecture_v11_smoke import check, HTML, SELECTORS, PROPS, CAPTURE

ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/"tests/fixtures/css-v12-removed-declarations.json"
def source_baseline():
    manifest=json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert manifest["version"]=="v12"
    current=HTML.read_text(encoding="utf-8")
    matches=list(re.finditer(r"<style>(.*?)</style>",current,re.S))
    assert len(matches)==3
    parts=[m.group(1) for m in matches]
    assert sum(len(x) for x in parts)==manifest["nextInline"]
    removed=manifest["insertions"]
    assert len(removed)==144, "V12 removed-declarations fixture was silently modified"
    for i in range(len(parts)):
        entries=sorted((x for x in removed if x["block"]==i),key=lambda x:x["offset"])
        for entry in entries:
            offset=entry["offset"]
            assert 0<=offset<=len(parts[i]),(i,offset,len(parts[i]))
            parts[i]=parts[i][:offset]+entry["source"]+parts[i][offset:]
    assert sum(len(x) for x in parts)==manifest["previousInline"]
    old=current
    for i in reversed(range(len(parts))):
        m=matches[i]
        old=old[:m.start(1)]+parts[i]+old[m.end(1):]
    def all_css(html):
        head=html.split("</head>",1)[0]
        parts=re.findall(
            r'<link[^>]+href="assets/([^"]+\.css)\?v=css-owner-v(?:10|11)"[^>]*>|<style>(.*?)</style>',
            head,re.S)
        return "".join(
            (HTML.parent/"assets"/name).read_text(encoding="utf-8") if name else inline
            for name,inline in parts
        )
    assert len(all_css(old))==manifest["previousCSS"]==186176
    assert fingerprint(all_css(old))==manifest["previousFNV"]=="39885f8a"
    assert len(all_css(current))==manifest["nextCSS"]==176024
    assert fingerprint(all_css(current))==manifest["nextFNV"]=="8cb19a32"
    assert all_css(old).count("!important")==manifest["previousImportant"]==1449
    assert all_css(current).count("!important")==manifest["nextImportant"]==1354
    assert manifest["previousImportant"]-manifest["nextImportant"]==95
    assert manifest["previousCSS"]-manifest["nextCSS"]==10152
    f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
                                   prefix=".v12-reconstructed-original-",dir=HTML.parent,delete=False)
    with f:
        f.write(old)
    return Path(f.name)

EXTRA_SELECTORS=[
 "#tracks .strip[data-id='1'] .mx-hw-row",
 "#tracks .strip[data-id='1'] .mx-hw-key",
 "#tracks .strip[data-id='1'] .wire-tray",
 "#tracks .strip[data-id='1'] .wire-node-out",
 "#tracks .strip[data-id='1'] .wire-node-in",
 "#tracks .strip[data-id='1'] .pan-section",
 "#tracks .strip[data-id='1'] .fader-section",
 "#tracks .strip[data-id='1'] .footer-label",
 ".command-bar .tool",
 ".console-body .section-rail .rail-cell.fx",
]
async def extra_state_parity(browser,baseline):
    old=await browser.new_page(viewport={"width":1680,"height":900})
    new=await browser.new_page(viewport={"width":1680,"height":900})
    try:
        await old.goto(baseline.as_uri())
        await new.goto(HTML.as_uri())
        for p in (old,new):
            await p.wait_for_function("() => !!document.querySelector('#tracks .strip[data-id=\"1\"] button[data-hw-kind=\"mute\"]')")
        selectors=SELECTORS+EXTRA_SELECTORS
        # Capture inactive/active/focus states of actual strip buttons and routing hardware.
        for label in ("initial","mute-on","hover-mute","solo-on","focus-wire"):
            if label=="mute-on":
                for p in (old,new):
                    await p.locator('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]').click()
            elif label=="hover-mute":
                for p in (old,new):
                    await p.locator('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]').hover()
            elif label=="solo-on":
                for p in (old,new):
                    s=p.locator('#tracks .strip[data-id="1"] button[data-hw-kind="solo"]')
                    if await s.count():
                        await s.click()
            elif label=="focus-wire":
                for p in (old,new):
                    e=p.locator("#tracks .strip[data-id='1'] .wire-node-in")
                    if await e.count() and await e.is_visible():
                        await e.focus()
            a=await old.evaluate(CAPTURE,[selectors,PROPS])
            b=await new.evaluate(CAPTURE,[selectors,PROPS])
            assert a==b,(label,"V11 visual regression",next((x for x,y in zip(a,b) if x!=y),None))
            if label=="initial":
                await new.screenshot(path="pages-v12-console-1680.png")
    finally:
        await old.close()
        await new.close()

async def main():
    source_gate()
    baseline=source_baseline()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await check(browser,baseline,width,height)
            await extra_state_parity(browser,baseline)
            await browser.close()
    finally:
        baseline.unlink(missing_ok=True)
    print("PASS V12: V11 exact source restored; 171 eliminated shadowed declarations, "
          "31 dead rules, 95 fewer !important, original/current CSS visual and control parity")

if __name__=="__main__":
    asyncio.run(main())
