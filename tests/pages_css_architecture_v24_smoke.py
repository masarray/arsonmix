"""V24: strict FADER/METER/FX owner-pruning gate with previous V23 browser parity.

The only change is deletion of precisely same-specificity, at-rule, priority,
and property earlier declarations in the same owner's cascading source.
"""
import asyncio,re
from playwright.async_api import async_playwright
from css_v24_restore import manifest,reconstruct,mount_original,HTML,ASSETS,fingerprint
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

def no_exact_owner_duplicates(source):
    """Fail closed on repeat property/priority/full selector inside any @context.

    A lexical rule scanner handles comments, strings and parentheses before
    reading declarations. It intentionally does not merge selector lists or
    invent specificity equivalence; those require richer cascade evidence.
    """
    stack=[{"kind":"root","ctx":""}]
    begin=0
    comment=False
    quote=None
    parenthesis=0
    bracket=0
    seen=set()
    duplicates=[]
    i=0
    while i<len(source):
        ch=source[i]
        next_ch=source[i+1] if i+1<len(source) else ""
        if comment:
            if ch=="*" and next_ch=="/":
                comment=False
                i+=2
            else:
                i+=1
            continue
        if quote:
            if ch=="\\":
                i+=2
            elif ch==quote:
                quote=None
                i+=1
            else:
                i+=1
            continue
        if ch=="/" and next_ch=="*":
            comment=True
            i+=2
            continue
        if ch in ("'",'"'):
            quote=ch
            i+=1
            continue
        if ch=="(":
            parenthesis+=1
        elif ch==")":
            parenthesis-=1
        elif ch=="[":
            bracket+=1
        elif ch=="]":
            bracket-=1
        if not parenthesis and not bracket and ch=="{":
            head=re.sub(r"/\*[\s\S]*?\*/","",source[begin:i]).strip()
            selector=re.sub(r"\s+"," ",head)
            kind="at" if selector.startswith("@") else "rule"
            ctx=stack[-1]["ctx"]+(" > "+selector if kind=="at" else "")
            stack.append({"kind":kind,"selector":selector,"ctx":ctx,"body":i+1})
            begin=i+1
        elif not parenthesis and not bracket and ch=="}":
            assert len(stack)>1,"CSS unbalanced closing brace"
            node=stack.pop()
            if node["kind"]=="rule":
                body=source[node["body"]:i]
                for match in re.finditer(
                    r"([-\w]+)\s*:\s*([^;{}]+)\s*(?:;|$)",body
                ):
                    prop=match.group(1)
                    value=match.group(2).strip()
                    priority=value.lower().endswith("!important")
                    key=(node["selector"],node["ctx"],prop,priority)
                    if key in seen:
                        duplicates.append(key)
                    seen.add(key)
            begin=i+1
        i+=1
    assert len(stack)==1 and not quote and not comment,"CSS source syntax unfinished"
    assert not duplicates,("V24 duplicate exact owner declarations",duplicates)
    return len(seen)

def source_audit():
    source_gate()
    data=manifest()
    old_html,owners=reconstruct()
    assert "v24-pruned" not in old_html
    live=HTML.read_text(encoding="utf-8")
    assert "v24-pruned" in live
    totals=dict(declarations=0,important=0,removedChars=0,wholeRules=0)
    for name in data["ownerOrder"]:
        old=owners[name]
        modern=(ASSETS/name).read_text(encoding="utf-8")
        record=data["owners"][name]
        assert (len(old),fingerprint(old))==(
            record["old"]["length"],record["old"]["fnv"])
        assert (len(modern),fingerprint(modern))==(
            record["current"]["length"],record["current"]["fnv"])
        assert no_exact_owner_duplicates(modern)>0,(name,"empty CSS owner")
        assert modern.count("!important")==record["current"]["important"]
        assert old.count("!important")-modern.count("!important")==record["metrics"]["important"]
        assert len(record["evidence"])==record["metrics"]["declarations"]
        assert all(e["selector"] and e["property"] and
                   e["losingOffset"]<e["winningOffset"] and
                   e["important"] in (True,False) and
                   e["mediaContext"] is not None for e in record["evidence"])
        assert all(e["kind"] in ("rule","declaration") for e in record["edits"])
        for key,attr in [("declarations","declarations"),
                         ("important","important"),
                         ("removedChars","removedChars"),
                         ("wholeRules","wholeRules")]:
            totals[key]+=record["metrics"][attr]
    assert totals["declarations"]==data["totals"]["removedDeclarations"]==26
    assert totals["important"]==data["totals"]["removedImportant"]==25
    assert totals["removedChars"]==data["totals"]["removedCssChars"]
    assert totals["wholeRules"]==data["totals"]["removedWholeRules"]
    assert "assets/mixer-fader-assembly.css?v=css-owner-v9-ruler-m3-left-70-v24-pruned" in live
    assert "assets/mixer-meter-display.css?v=css-owner-v9-ruler-m3-left-70-v24-pruned" in live
    assert "assets/mixer-fx-rack.css?v=fx-owner-v8-m3-inserts-v24-pruned" in live

async def main():
    source_audit()
    before,temporary=mount_original()
    try:
        async with async_playwright() as playwright:
            browser=await playwright.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080),
                                 (1366,700),(1440,580)):
                await check(browser,before,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,before,width,height)
                await material_compare(browser,before,width,height)
                await interactive_compare(browser,before,width,height)
                await root_parity(browser,before,width,height)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip")
            await page.screenshot(path="pages-v24-fader-meter-fx-1680.png")
            await page.close()
            await browser.close()
    finally:
        for file in temporary:file.unlink(missing_ok=True)
    print("PASS V24: 26 actual deleted owner declarations; 25 !important; "
          "V23 originals restore byte-exactly; 5x8 folds; "
          "3x material, pseudo-state, control interaction and responsive parity")

if __name__=="__main__":
    asyncio.run(main())
