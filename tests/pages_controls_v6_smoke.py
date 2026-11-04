"""Approved ONE /preview/ hardware key regression; no rejected CSS-only/SVG variant."""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
PAGE=ROOT/"site/preview/index.html"
CSS=ROOT/"site/preview/assets/mixer-control-keys.css"
JS=ROOT/"site/preview/assets/mixer-control-keys.js"
SPRITE="mute-solo-pfl-56x37.png"

def source_gate():
    html=PAGE.read_text(encoding="utf-8")
    css=CSS.read_text(encoding="utf-8")
    assert "switch-section mx-hw-row" in html
    assert "assets/mixer-control-keys.css" in html
    assert "assets/mixer-control-keys.js" in html
    assert "background-size:200% 300%" in css
    assert "aria-pressed" in css
    assert SPRITE in css
    assert JS.is_file()
    assert (PAGE.parent/"assets"/SPRITE).is_file()
    assert not (ROOT/"site/preview-v2").exists()
    assert not (ROOT/"site/preview-buttons").exists()

async def main():
    source_gate()
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        for width in (1366,1680,1920):
            page=await browser.new_page(viewport={"width":width,"height":900})
            errors=[]
            page.on("pageerror",lambda exc:errors.append(str(exc)))
            await page.goto(PAGE.as_uri())
            keys=page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button.mx-hw-key')
            await keys.first.wait_for()
            assert await keys.count()==3
            labels=await keys.evaluate_all("els=>els.map(x=>x.dataset.hwKind)")
            assert labels==["mute","solo","cue"],labels
            assert await page.locator("#wireLayer").count()==1
            assert await page.locator(".mx-mute-face,svg.mx-hw-face").count()==0
            before=await keys.evaluate_all("els=>els.map(x=>x.getAttribute('aria-pressed'))")
            faces=await keys.evaluate_all(
                "els=>els.map(x=>getComputedStyle(x,'::before').backgroundImage)")
            assert all(SPRITE in face for face in faces),(width,faces)
            for i in range(3):
                await keys.nth(i).click()
            after=await keys.evaluate_all("els=>els.map(x=>x.getAttribute('aria-pressed'))")
            assert all(x!=y for x,y in zip(before,after)),(width,before,after)
            assert not errors,(width,errors)
            await page.close()
        await browser.close()
    print("PASS: one /preview/ PNG MUTE/SOLO/PFL owner, 3 widths, WIRE and latch states")

if __name__=="__main__":
    asyncio.run(main())
