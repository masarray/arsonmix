"""V21: V9–V14 physical chassis geometry is now owned by one Chrome sheet.

Strict V20 source reconstruction plus actual browser-fold, hardware states,
token/interaction and computed material parity. No new sheet or repaint recipe.
"""
import asyncio
from playwright.async_api import async_playwright
from css_v21_restore import manifest,reconstruct,mount_original,HTML,CHROME
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare
from pages_css_architecture_v20_smoke import pseudo_state_compare

async def source_audit():
    source_gate()
    prior,owner=reconstruct()
    data=manifest()
    prefix=data["sourceInlinePrefix"]
    assert len(prefix)==13907
    assert "url(" not in prefix and "@import" not in prefix
    assert data["current"]["inlineLength"]==34529
    assert data["old"]["inlineLength"]==48436
    assert data["old"]["inlineImportant"]-data["current"]["inlineImportant"]==130
    assert data["current"]["chromeLength"]-data["old"]["chromeLength"]==13907
    assert data["metrics"]["deletedCssChars"]==0
    assert data["metrics"]["addedRules"]==0
    assert data["metrics"]["changedJsFiles"]==0
    assert "/* V9: CONSOLE MECHANICAL LAYOUT" in CHROME.read_text(encoding="utf-8")
    assert "/* V15 original physical patch connectors:" in HTML.read_text(encoding="utf-8")
    assert "/* V9: CONSOLE MECHANICAL LAYOUT" not in HTML.read_text(encoding="utf-8")
    from css_v22_restore import reconstruct as restore_v21_source
    v21_html,v21_chrome=restore_v21_source()
    assert "/* ARSONMIX GLOBAL HARDWARE KEY V1" in v21_html
    assert data["newLink"] in v21_html
    assert data["oldLink"] in prior
    assert v21_chrome==owner+prefix
    assert "/* ARSONMIX GLOBAL HARDWARE KEY V1" in CHROME.read_text(encoding="utf-8")

async def main():
    await source_audit()
    baseline,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            # Stage-laptop and studio widths, full eight fold combinations.
            for w,h in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,w,h)
            # Cross-owner material, physical controls, 45 tokens, pseudo states.
            for w,h in ((1366,768),(1680,900),(1920,1080)):
                await pseudo_state_compare(browser,baseline,w,h)
                await material_compare(browser,baseline,w,h)
                await interactive_compare(browser,baseline,w,h)
                await root_parity(browser,baseline,w,h)
            await browser.close()
    finally:
        for path in temporary:
            path.unlink(missing_ok=True)
    print("PASS V21: V9–V14 chassis geometry exact-cascade owner transfer;"
          " 13,907 chars removed from inline, zero CSS declarations deleted,"
          " no ENGINE/JS changes; 5x8 fold masks and 3 material/pseudo/interaction sweeps")

if __name__=="__main__":
    asyncio.run(main())
