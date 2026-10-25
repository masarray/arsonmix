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
        # All 12 PAN knobs have been replaced by horizontal slider tracks.
        assert await page.locator('[data-pan]').count()==12
        assert await page.locator('[data-knob="pan"]').count()==0
        assert ''.join((await page.locator('.brand').inner_text()).split())=='ArSonMix'
        for idx in range(12):
            p=page.locator(f'[data-pan="{idx}"]')
            assert await p.get_attribute('aria-orientation')=='horizontal'
            assert await p.locator('.pan-thumb').count()==1
        for selector in ['.nav button.selected','.left-rail .seg button.selected','.right-rail .seg button.selected','[data-output="Main L/R"]']:
            bg=await page.locator(selector).first.evaluate('(x)=>getComputedStyle(x).backgroundImage')
            assert 'data:image/png;base64,' in bg, (selector,bg[:110])
        # Visual-architecture regression: four intentionally different hardware
        # families, FX slots recessed rather than taking on button-cap artwork.
        family=await page.evaluate("""() => {
          const bg=s=>getComputedStyle(document.querySelector(s)).backgroundImage;
          const rect=s=>{const r=document.querySelector(s).getBoundingClientRect();return {x:r.x,y:r.y,right:r.right,bottom:r.bottom,width:r.width,height:r.height}};
          const rail=rect('.right-rail'), left=rect('.left-rail'), mode=rect('#modeToggle'), nav=rect('.nav');
          const overflow=[...document.querySelectorAll('.strip .channel-actions button,.strip .utility-row button')].some(el=>{
            const b=el.getBoundingClientRect(),p=el.closest('.strip').getBoundingClientRect();
            return b.left < p.left-1 || b.right > p.right+1;
          });
          return {nav:bg('.nav button.selected'),small:bg('[data-mute="0"]'),
            long:bg('.rail-presets button'),large:bg('#talkBtn'),
            fx:bg('.strip .rack .insert'),chrome:bg('#settingsBtn'),
            label:bg('.master-fader-label'),navBounds:nav,mode,rail,left,overflow,
            stripButtons:[...document.querySelectorAll('.strip .channel-actions button')].map(x=>x.getBoundingClientRect().height),
            talkHeight:rect('#talkBtn').height,
            tabHeights:[...document.querySelectorAll('.master-tabs button')].map(x=>x.getBoundingClientRect().height)};
        }""")
        assert len({family[k] for k in ('nav','small','long','large')})==4, 'Button families collapsed to one global skin'
        assert 'data:image/png;base64' not in family['fx'], 'Recessed FX slots must not look like huge buttons'
        assert 'data:image/png;base64' not in family['chrome'], 'Tool icons need their own chrome'
        assert 'data:image/png;base64' not in family['label'], 'Main Master label is a metal plaque, not a button'
        assert not family['overflow'], 'Some channel switches escape their strip'
        assert all(abs(n-29)<=1 for n in family['stripButtons']),family['stripButtons']
        assert family['talkHeight']==48
        assert all(n==30 for n in family['tabHeights']),family['tabHeights']
        assert family['navBounds']['right'] <= family['mode']['x']+1, 'Navigation overlaps Easy View'
        await page.screenshot(path=str(project/'mixer-design-review.png'),full_page=True)
        # Medium desktop check; channel strips may scroll *inside* workbench.
        await page.set_viewport_size({"width":1440,"height":820})
        await page.screenshot(path=str(project/'mixer-design-review-1440.png'),full_page=True)
        assert await page.locator('.strip').count()==12
        await page.set_viewport_size({"width":1680,"height":920})
        pan=page.locator('[data-pan="0"]')
        await pan.focus()
        await pan.press('Home')
        assert await pan.get_attribute('aria-valuenow')=='0'
        await pan.press('ArrowRight')
        assert await pan.get_attribute('aria-valuenow')=='1'
        await pan.press('Shift+ArrowLeft')
        assert await pan.get_attribute('aria-valuenow')=='-9'
        await pan.press('End')
        assert await pan.get_attribute('aria-valuenow')=='100'
        await pan.press('Home')
        bbox=await pan.bounding_box()
        await page.mouse.click(bbox['x']+bbox['width']*.76,bbox['y']+20)
        assert int(await pan.get_attribute('aria-valuenow'))>0
        await pan.press('Home')
        thumb=await pan.locator('.pan-thumb').bounding_box()
        x=thumb['x']+thumb['width']/2
        y=thumb['y']+thumb['height']/2
        await page.mouse.move(x,y)
        await page.mouse.down()
        assert await pan.get_attribute('aria-valuenow')=='0','PAN jumped on grab'
        await page.mouse.move(x+14,y,steps=3)
        assert int(await pan.get_attribute('aria-valuenow'))>0
        await page.mouse.up()
        await pan.dblclick()
        assert await pan.get_attribute('aria-valuenow')=='0'
        assert not await page.locator('#editor').is_visible(),'PAN double click opened editor'

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
