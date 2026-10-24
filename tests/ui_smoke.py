"""UI smoke test (optional): pip install playwright && playwright install chromium"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

async def main():
    project=Path(__file__).resolve().parents[1]
    async with async_playwright() as tool:
        browser=await tool.chromium.launch(headless=True)
        page=await browser.new_page(viewport={"width":1680,"height":920})
        errors=[]
        page.on("pageerror", lambda e: errors.append(str(e)))
        await page.set_content((project/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").read_text())
        assert await page.locator(".strip").count()==12
        # Cap uses equal upper/lower mirrored grip halves on all channels and master.
        geometry=await page.evaluate("""() => [...document.querySelectorAll('[data-fader]')].map(f => {
            const c=f.querySelector('.cap'),b=c.getBoundingClientRect(),r=f.getBoundingClientRect();
            const x=getComputedStyle(c,'::before'),y=getComputedStyle(c,'::after');
            return {width:b.width,height:b.height,inside:b.top>=r.top && b.bottom<=r.bottom,
                    upper:x.height,lower:y.height,upperBG:x.backgroundImage,lowerBG:y.backgroundImage,
                    mirror:y.transform,radius:getComputedStyle(c).borderRadius}
        })""")
        assert len(geometry)==13
        for g in geometry:
            assert (g['width'],g['height'])==(29,42),g
            assert g['inside'],g
            assert g['upper']==g['lower'] and g['upperBG']==g['lowerBG'],g
            assert 'matrix(1, 0, 0, -1' in g['mirror'],g
            assert g['radius']=='5px',g
        for fader in ('0','master'):
            control=page.locator(f'[data-fader="{fader}"]')
            await control.focus()
            await control.press('Home')
            assert float(await control.get_attribute('aria-valuenow'))==-60
            await control.press('End')
            assert float(await control.get_attribute('aria-valuenow'))==10
        await page.locator('[data-view="FX RACK"]').click()
        await page.locator('[data-addplugin]').click()
        await page.locator("#pluginSearch").fill("de-esser")
        await page.locator('[data-pluginname="De-Esser"]').click()
        assert await page.locator(".chain-node").count() == 5
        await page.locator('[data-chainedit="0"]').click()
        await page.locator('[data-fxparam="mix"]').fill("72")
        assert await page.evaluate('fxParams["0:0"].mix')==72
        await page.locator('[data-chainmove="1"]').first.click()
        assert await page.evaluate('fxParams["0:1"].mix')==72
        await page.locator('[data-view="SIGNAL FLOW"]').click()
        assert await page.locator('.flow-node').count()==4
        await page.locator('[data-view="SETUP"]').click()
        assert await page.locator('.setup-section').count()==4
        await page.locator('[data-view="MIX"]').click()
        await page.locator('#modeToggle').click()
        assert await page.locator('.app.easy').count()==0
        assert not errors, errors
        await browser.close()
    print("PASS: mirrored channel/master caps, keyboard endpoints, FX flow and no JS errors")

if __name__=="__main__":asyncio.run(main())
