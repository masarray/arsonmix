"""V23 true dead-declaration removal: 2 owners, 25 exact cascade losers.

Historical V10-V22 SHA/FNV assertions remain intact. Real Chromium compares
original V22 owner HTML/CSS to V23 at five responsive views and eight folds.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright
from css_v23_restore import manifest,reconstruct,mount_original,original_owner,HTML,ASSETS,fingerprint
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

def owner_gate():
    source_gate()
    data=manifest()
    old_html,owners=reconstruct()
    assert "css-chrome-owner-v22-m3-header" in old_html
    assert "v47-css-v16" in old_html
    assert "css-chrome-owner-v23-m3-header" in HTML.read_text(encoding="utf-8")
    assert "v23-wire-cascade" in HTML.read_text(encoding="utf-8")
    assert data["totals"]["removedDeclarations"]==25
    assert data["totals"]["removedImportant"]==20
    assert data["totals"]["removedWholeRules"]==6
    total=0
    for name in data["ownerOrder"]:
        old=owners[name]
        now=(ASSETS/name).read_text(encoding="utf-8")
        info=data["owners"][name]
        assert (len(old),fingerprint(old))==(info["old"]["length"],info["old"]["fnv"])
        assert (len(now),fingerprint(now))==(info["current"]["length"],info["current"]["fnv"])
        assert old.count("!important")-now.count("!important")==info["metrics"]["important"]
        assert len(info["evidence"])==info["metrics"]["declarations"]
        assert len(info["edits"])==info["metrics"]["patches"]
        assert all(e["winningSourceOffset"]>e["losingSourceOffset"] and
                   e["selector"] and e["property"] and
                   e["important"] in (True,False) for e in info["evidence"])
        assert all(e["kind"] in ("declaration","rule") for e in info["edits"])
        total+=len(old)-len(now)
    assert total==data["totals"]["removedCssChars"]

async def main():
    owner_gate()
    baseline,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for w,h in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,w,h)
            for w,h in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,baseline,w,h)
                await material_compare(browser,baseline,w,h)
                await interactive_compare(browser,baseline,w,h)
                await root_parity(browser,baseline,w,h)
            page=await browser.new_page(viewport={"width":1680,"height":900})
            await page.goto(HTML.as_uri())
            await page.wait_for_selector("#tracks .strip")
            await page.screenshot(path="pages-v23-owners-pruned-1680.png")
            await page.close()
            await browser.close()
    finally:
        for file in temporary:file.unlink(missing_ok=True)
    print("PASS V23: 25 provably dead same-selector owner declarations, "
          "20 !important and 6 fully shadowed rules removed. "
          "Exact V22 files recover, 5x8 responsive folds and "
          "3x material/pseudo/interactive/state/token sweeps.")

if __name__=="__main__":
    asyncio.run(main())
