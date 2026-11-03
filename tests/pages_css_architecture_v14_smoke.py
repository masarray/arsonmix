"""V14 canonical material-token ownership and full V13/V14 browser parity.

The original V13 source is reconstructed exactly from a reversible removal
ledger. Baseline renders against a temporary co-located V13 chrome stylesheet.
Checks the computed value of ALL 45 root variables as well as interactive
MUTE/SOLO/PFL, PAN, FX, fader, WIRE and 8 section fold combinations.
"""
import asyncio
import json
import re
import tempfile
from pathlib import Path
from playwright.async_api import async_playwright
from pages_css_architecture_v10_smoke import source_gate, fingerprint
from pages_css_architecture_v11_smoke import HTML, check
from pages_css_architecture_v13_smoke import interactive_compare
from css_v15_restore import restore_previous_owners,before_v15_html,original_assets_as_baseline

ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/"tests/fixtures/css-v14-material-token-ownership.json"
CHROME=HTML.parent/"assets/mixer-chrome-material.css"

def combined_css(document, chrome_override=None, owner_override=None):
    head=document.split("</head>",1)[0]
    css_order=re.findall(
        r'<link[^>]+href="assets/([^"]+\.css)\?v=css-owner-v(?:10|11|14|15)"[^>]*>|<style>(.*?)</style>',
        head,re.S
    )
    def lookup(name,inline):
        if not name:return inline
        if name=="mixer-chrome-material.css" and chrome_override is not None:
            return chrome_override
        if owner_override and name in owner_override:return owner_override[name]
        if name=="mixer-chrome-material.css":
            # Historical V10–V17 byte comparisons must not read the V18 relocated prefix.
            from css_v18_restore import reconstruct as pre_v18_source
            return pre_v18_source()[1]
        return (HTML.parent/"assets"/name).read_text(encoding="utf-8")
    return "".join(lookup(name,inline) for name,inline in css_order)

def reconstruct_previous():
    ledger=json.loads(MANIFEST.read_text(encoding="utf-8"))
    assert ledger["milestone"]=="V14"
    originals=restore_previous_owners()
    current=before_v15_html()
    current_chrome=originals["mixer-chrome-material.css"]
    c=ledger["chrome"]
    assert len(current_chrome)==c["newSize"]
    assert current_chrome.count(c["after"])==1
    original_chrome=current_chrome.replace(c["after"],c["before"],1)
    assert len(original_chrome)==c["originalSize"]
    matches=list(re.finditer(r"<style>(.*?)</style>",current,re.S))
    assert len(matches)==3
    parts=[m.group(1) for m in matches]
    for i in (1,2):
        pieces=sorted((e for e in ledger["removedInlineRoots"] if e["block"]==i),
                      key=lambda e:e["offset"])
        for entry in pieces:
            at=entry["offset"]
            assert 0<=at<=len(parts[i]),(i,at)
            parts[i]=parts[i][:at]+entry["source"]+parts[i][at:]
    old=current
    for i in reversed(range(3)):
        match=matches[i]
        old=old[:match.start(1)]+parts[i]+old[match.end(1):]
    new_link='href="assets/mixer-chrome-material.css?v=css-owner-v14"'
    old_link='href="assets/mixer-chrome-material.css?v=css-owner-v11"'
    assert old.count(new_link)==1
    old=old.replace(new_link,old_link,1)
    assert sum(map(len,parts))==ledger["beforeInline"]==126354
    assert len(ledger["removedInlineRoots"])==8
    before=combined_css(old,original_chrome,owner_override=originals)
    after=combined_css(current,owner_override=originals)
    assert len(before)==ledger["beforeCSS"]==176024
    assert fingerprint(before)==ledger["beforeFNV"]=="8cb19a32"
    assert len(after)==ledger["afterCSS"]==175850
    assert fingerprint(after)==ledger["afterFNV"]=="188064b0"
    assert before.count("!important")==after.count("!important")==1354
    assert len(ledger["uniqueTokens"])==45
    assert ledger["tokenBefore"]==56 and ledger["tokenAfter"]==45
    for match in matches:
        # No :root groups may re-own these 45 global material tokens inline.
        assert not re.search(r':root\s*\{',match.group(1))
    assert all(token in current_chrome for token in ledger["uniqueTokens"])
    return old,original_chrome

def open_v13_baseline():
    previous,original_chrome=reconstruct_previous()
    original_owners=restore_previous_owners()
    # V13 predates V14 Chrome token consolidation and V15 owner removals.
    original_owners["mixer-chrome-material.css"]=original_chrome
    return original_assets_as_baseline(previous,original_owners)

async def root_parity(browser,original,width,height):
    old=await browser.new_page(viewport={"width":width,"height":height})
    new=await browser.new_page(viewport={"width":width,"height":height})
    try:
        await old.goto(original.as_uri())
        await new.goto(HTML.as_uri())
        tokens=json.loads(MANIFEST.read_text(encoding="utf-8"))["uniqueTokens"]
        reader="""tokens=>Object.fromEntries(tokens.map(k=>[
          k,getComputedStyle(document.documentElement).getPropertyValue(k).trim()
        ]))"""
        prior=await old.evaluate(reader,tokens)
        after=await new.evaluate(reader,tokens)
        assert prior==after,(width,"root material token drift",next(
            ((k,prior[k],after[k]) for k in tokens if prior[k]!=after[k]),None))
        assert all(after.values()),(width,"missing root material token")
        # One definitive declaration of each token in the material owner root.
        css=CHROME.read_text(encoding="utf-8")
        root=re.search(r'(?s):root\s*\{(.*?)\}',css)
        assert root
        for key in tokens:
            count=len(re.findall(r'(?m)^\s*'+re.escape(key)+r'\s*:',root.group(1)))
            assert count==1,(key,"not single material root owner",count)
    finally:
        await old.close()
        await new.close()

async def main():
    source_gate()
    baseline,temporary=open_v13_baseline()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await root_parity(browser,baseline,width,height)
                await check(browser,baseline,width,height)
                await interactive_compare(browser,baseline,width,height)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector('#tracks .strip')
            await page.screenshot(path="pages-v14-tokens-console-1680.png")
            await page.close()
            await browser.close()
    finally:
        for path in temporary:path.unlink(missing_ok=True)
    print("PASS V14: 56 global declarations consolidated to 45 single-owner material tokens;"
          " exact V13 reconstruction, 3 resolutions x 8 folds, all 45 root values,"
          " hardware MUTE/SOLO/PFL, fader, PAN, FX Browser and WIRE parity")

if __name__=="__main__":
    asyncio.run(main())
