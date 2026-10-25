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
        # Pixel-perfect state mapping uses original silver/red/green images, not CSS flats.
        async def image_for(selector):
            return await page.locator(selector).first.evaluate(
                '(b) => getComputedStyle(b).backgroundImage')
        assert "data:image/png;base64," in await image_for('[data-mute="0"]')
        assert await image_for('[data-mute="0"]') == await image_for('[data-solo="0"]')
        await page.locator('[data-mute="0"]').click()
        assert await page.locator('[data-mute="0"]').get_attribute('aria-pressed') == 'true'
        assert await image_for('[data-mute="0"]') != await image_for('[data-solo="0"]')
        await page.locator('[data-solo="0"]').click()
        assert await page.locator('[data-solo="0"]').get_attribute('aria-pressed') == 'true'
        assert await image_for('[data-solo="0"]') != await image_for('[data-mute="0"]')
        await page.locator('[data-mute="0"]').click()
        await page.locator('[data-solo="0"]').click()
        await page.locator('#masterMute').click()
        assert await page.locator('#masterMute').get_attribute('aria-pressed') == 'true'
        await page.locator('#masterMute').click()
        await page.locator('#dim').click()
        assert await page.locator('#dim').get_attribute('aria-pressed') == 'true'
        await page.locator('#dim').click()
        assert await page.locator('#dim').get_attribute('aria-pressed') == 'false'

        # The source image has a real metallic body and a long cast shadow.
        # The body must travel inside the track, centered on image y=35.
        geometry=await page.evaluate("""async () => {
            const slots=[...document.querySelectorAll('[data-fader]')];
            const heads=slots.map(f => {
                const c=f.querySelector('.cap'),b=c.getBoundingClientRect(),r=f.getBoundingClientRect();
                const s=getComputedStyle(c);
                return {width:b.width,height:b.height,bodyInside:b.top>=r.top-1 && b.top+66<=r.bottom+1,
                        top:b.top,trackTop:r.top,radius:s.borderRadius,
                        bg:s.backgroundImage,transform:s.transform};
            });
            const uri=heads[0].bg.match(/data:image\\/png;base64,[a-zA-Z0-9+/=]+/)?.[0];
            if(!uri) throw new Error('Image data URI not found');
            const image=new Image();image.src=uri;await image.decode();
            return {heads,imgW:image.naturalWidth,imgH:image.naturalHeight};
        }""")
        assert len(geometry['heads'])==13
        assert (geometry['imgW'],geometry['imgH'])==(31,90), geometry
        for g in geometry['heads']:
            assert (g['width'],g['height'])==(31,90),g
            assert g['bodyInside'],g
            assert g['radius']=='0px',g
            assert 'matrix(1, 0, 0, 1, -15.5, -35)' in g['transform'],g
        for fader in ('0','master'):
            ctrl=page.locator(f'[data-fader="{fader}"]')
            await ctrl.focus()
            await ctrl.press('Home')
            assert float(await ctrl.get_attribute('aria-valuenow'))==-60
            await ctrl.press('End')
            assert float(await ctrl.get_attribute('aria-valuenow'))==10
        # Repaint at 0 dB, then dragging the head starts without a value jump.
        channel=page.locator('[data-fader="0"]')
        await channel.focus()
        for _ in range(20):
            await channel.press('ArrowDown')
        original=float(await channel.get_attribute('aria-valuenow'))
        cap=channel.locator('.cap')
        rect=await cap.bounding_box()
        cx=rect['x']+rect['width']/2
        cy=rect['y']+35
        await page.mouse.move(cx,cy)
        await page.mouse.down()
        assert float(await channel.get_attribute('aria-valuenow'))==original
        await page.mouse.move(cx,cy+18,steps=3)
        assert float(await channel.get_attribute('aria-valuenow'))<original
        await page.mouse.up()
        await page.screenshot(path=str(project/'fader-ui-smoke.png'),full_page=True)
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
    print("PASS: asset PNG for 13 faders, detent alignment, no-jump drag, endpoints, FX chain, no JS errors")

if __name__=="__main__":asyncio.run(main())
