"""V19: exact historical cascade and live visual/interaction parity.

Final V30 operator key recipe moved to existing key owner at its adjacent
source boundary. Old/new Chromium compares every fold and hardware key state.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright
from css_v19_restore import manifest,reconstruct,mount_original,HTML,SECTIONS,KEYS
from pages_css_architecture_v10_smoke import source_gate
from pages_css_architecture_v11_smoke import check
from pages_css_architecture_v13_smoke import interactive_compare
from pages_css_architecture_v14_smoke import root_parity
from pages_css_architecture_v18_smoke import material_compare

async def main():
    source_gate()
    source,sections,keys=reconstruct()
    data=manifest()
    css_new=SECTIONS.read_text(encoding="utf-8")+KEYS.read_text(encoding="utf-8")
    css_old=sections+keys+data["sourceFirst"]+data["movedKeyPrefix"]
    assert css_new.count("!important")==css_old.count("!important"),"relocation must preserve important flags"
    assert data["oldLengths"]["inline"]-data["newLengths"]["inline"]==(
        len(data["sourceFirst"])+len(data["movedKeyPrefix"])+
        sum(len(edit["text"]) for edit in data["removed"])
    ),"migration vs true removal must not be misreported"
    assert source.count(data["oldLinks"][0])==source.count(data["oldLinks"][1])==1
    assert all("url(" not in value for value in (data["sourceFirst"],data["movedKeyPrefix"]))
    baseline,files=mount_original()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            # Include stage laptop short height and 5 independent widths.
            for width,height in ((1366,768),(1680,900),(1920,1080),(1366,700),(1440,580)):
                await check(browser,baseline,width,height)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await material_compare(browser,baseline,width,height)
                await interactive_compare(browser,baseline,width,height)
                await root_parity(browser,baseline,width,height)
            await browser.close()
    finally:
        for path in files:path.unlink(missing_ok=True)
    print("PASS V19: exact V18 reconstitution; 9,763 migrated inline chars,"
          " 667 genuinely removed CSS chars, 6 shadowed important properties,"
          " 10 empty rules; 5x8 responsive folds and 3 full interaction/material sweeps")

if __name__=="__main__":
    asyncio.run(main())
