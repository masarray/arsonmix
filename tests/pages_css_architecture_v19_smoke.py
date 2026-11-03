"""V19: exact V18 Chrome/toolbar CSS source and real GUI visual parity."""
import asyncio
from playwright.async_api import async_playwright
from css_v19_restore import manifest,reconstruct,mount_original,HTML,SECTIONS,CHROME
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare

async def main():
    source_gate()
    source,sections,chrome=reconstruct()
    data=manifest()
    assembled_new=SECTIONS.read_text(encoding="utf-8")+CHROME.read_text(encoding="utf-8")
    assembled_old=sections+chrome+data["sourceFirst"]+data["movedChromeSuffix"]
    assert assembled_new.count("!important")==assembled_old.count("!important"),"CSS relocation must preserve priority"
    assert data["oldLengths"]["inline"]-data["newLengths"]["inline"]==(
        len(data["sourceFirst"])+len(data["movedChromeSuffix"])+
        sum(len(entry["text"]) for entry in data["removed"])
    ),"moved vs deleted metric must remain honest"
    assert data["removedImportant"]==6 and data["removedEmptyRules"]==10
    assert all("url(" not in x for x in (data["sourceFirst"],data["movedChromeSuffix"]))
    baseline,temporary=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for w,h in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,w,h)
            for w,h in ((1366,768),(1680,900),(1920,1080)):
                await material_compare(browser,baseline,w,h)
                await interactive_compare(browser,baseline,w,h)
                await root_parity(browser,baseline,w,h)
            await browser.close()
    finally:
        for path in temporary:path.unlink(missing_ok=True)
    print("PASS V19: 10,847 CSS chars migrated to existing Chrome/toolbar owners;"
          " 667 CSS chars truly removed (6 shadowed important, 10 empty rules);"
          " 5x8 folds and 3 interactive/material/control state sweeps")

if __name__=="__main__":
    asyncio.run(main())
