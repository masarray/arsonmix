"""V23 true dead-declaration removal: 2 owners, 25 exact cascade losers.

Historical V10-V22 SHA/FNV assertions remain intact. Real Chromium compares
original V22 owner HTML/CSS to V23 at five responsive views and eight folds.
"""
import asyncio
from pathlib import Path
from contextlib import ExitStack
from unittest.mock import patch
import tempfile
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
    from gui_v49_restore import historical_html
    assert "v23-wire-cascade" in historical_html()
    assert data["totals"]["removedDeclarations"]==25
    assert data["totals"]["removedImportant"]==20
    assert data["totals"]["removedWholeRules"]==6
    total=0
    for name in data["ownerOrder"]:
        old=owners[name]
        from gui_v49_restore import historical_wire_css
        now=(historical_wire_css() if name=="mixer-wire-dots.css" else
             (ASSETS/name).read_text(encoding="utf-8"))
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
    # Test the exact V23 styling milestone against its V22 parent, not the
    # later V49 interaction changes. V49 has its own full pointer/gesture gate.
    from gui_v49_restore import historical_html,historical_wire_css
    wire=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
        prefix=".v23-accepted-wire-",dir=ASSETS,delete=False)
    with wire:wire.write(historical_wire_css())
    temporary.append(Path(wire.name))
    source=historical_html()
    old_link="assets/mixer-wire-dots.css?v=v23-wire-cascade"
    assert source.count(old_link)==1
    source=source.replace(old_link,"assets/"+Path(wire.name).name+"?v=historical-v23",1)
    pagefile=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
        prefix=".v23-accepted-console-",dir=HTML.parent,delete=False)
    with pagefile:pagefile.write(source)
    accepted=Path(pagefile.name)
    temporary.append(accepted)
    try:
        import pages_css_architecture_v11_smoke as v11
        import pages_css_architecture_v13_smoke as v13
        import pages_css_architecture_v14_smoke as v14
        import pages_css_architecture_v18_smoke as v18
        import pages_css_architecture_v20_smoke as v20
        with ExitStack() as overrides:
            # Keep old/new browser references on one historical version;
            # never alter source ledgers or weaken any computed-style checks.
            for module in (v11,v13,v14,v18,v20):
                overrides.enter_context(patch.object(module,"HTML",accepted))
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
                await page.goto(accepted.as_uri())
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
