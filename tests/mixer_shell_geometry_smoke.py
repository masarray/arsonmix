"""L2 geometry gate: MASTER and every input share one shrink/grow fader grid."""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

async def main():
    root=Path(__file__).resolve().parents[1]
    async with async_playwright() as tool:
        browser=await tool.chromium.launch(headless=True)
        page=await browser.new_page(viewport={"width":1680,"height":920})
        errors=[]
        page.on("pageerror",lambda e:errors.append(str(e)))
        await page.goto((root/"web/index.html").as_uri())
        await page.locator('.left-rail [data-mix-mode="FX"]').wait_for()
        assert await page.locator('.toolbar-actions .nav [data-view]').count()==3
        assert await page.locator('.toolbar-actions [data-native-action="audioSetup"]').count()==0
        assert await page.locator('.left-rail [data-mix-mode]').count()==3
        assert await page.locator('.left-rail [data-my-sounds]').count()==1
        assert await page.locator('.left-rail [data-plugin-manager]').count()==1
        assert await page.locator('.left-rail #audioSetupBtn').count()==1
        assert await page.locator('.left-rail #arSignalOpen').count()==1
        assert await page.locator('.workbench > .arMixerBankBar').count()==0
        assert await page.locator('.arRailModeSection > .arMixerBankBar').count()==1
        assert await page.locator('#mixer > .strip').count()==11

        async def geometry():
            return await page.evaluate("""() => {
              const rows=['.ch-id','.rack','.strip-controls','.channel-actions',
                '.db-display','.fader-zone','.strip-name'];
              const rect=e=>{const r=e.getBoundingClientRect();return {top:r.top,bottom:r.bottom,height:r.height}};
              const strips=[...document.querySelectorAll('#mixer > .strip')];
              return strips.map((s,i)=>({
                id:s.id||String(i),
                header:rect(s.querySelector('.ch-id')),
                upper:rect(s.querySelector(i===10?'.master-fx-rack':(getComputedStyle(s.querySelector('.rack')).display==='none'?'.arChannelBank':'.rack'))),
                controls:rect(s.querySelector('.strip-controls')),
                actions:rect(s.querySelector('.channel-actions')),
                value:rect(s.querySelector('.db-display')),
                fader:rect(s.querySelector('.fader-zone')),
                scale:rect(s.querySelector('.gain-scale-travel')),
                meter:rect(s.querySelector('.vu-scale-travel')),
                name:rect(s.querySelector('.strip-name'))
              }));
            }""")

        async def assert_linked(mode,width,height):
            data=await geometry()
            assert len(data)==11,data
            for field in ('header','upper','controls','actions','value','fader','scale','meter','name'):
                for dim in ('top','bottom','height'):
                    values=[item[field][dim] for item in data]
                    assert max(values)-min(values)<1.1,(mode,width,height,field,dim,values)
            assert data[0]['fader']['height']>=95,(mode,width,height,data[0]['fader'])
            assert data[10]['fader']['height']==data[0]['fader']['height'],(mode,width,height)
            return data[0]['fader']['height']

        for width,height in [(1680,920),(1440,768),(1366,768),(1024,720)]:
            await page.set_viewport_size({"width":width,"height":height})
            heights={}
            for mode in ('FX','ROUTE','SENDS'):
                await page.locator(f'.left-rail [data-mix-mode="{mode}"]').click()
                assert await page.locator('#mixer .rack').count()==10
                assert await page.locator('#masterLeft .fader-zone').count()==1
                heights[mode]=await assert_linked(mode,width,height)
                if width==1680:
                    await page.screenshot(path=f'mixer-shell-{mode.lower()}-1680.png')
                if width==1366 and mode=='SENDS':
                    await page.screenshot(path='mixer-shell-compact-1366.png')
            assert heights['SENDS']<heights['FX'],(width,height,heights)
            assert abs(heights['ROUTE']-heights['FX'])<1.1,(width,height,heights)
            await page.locator('.left-rail [data-mix-mode="FX"]').click()
            assert abs(await assert_linked('FX RESTORE',width,height)-heights['FX'])<1.1

        # Parent view changes still work; click rail mode from PATCH to return MIX.
        await page.locator('[data-view="SIGNAL FLOW"]').click()
        assert await page.locator('#mixer').evaluate("(e)=>e.classList.contains('hidden')")
        await page.locator('.left-rail [data-mix-mode="ROUTE"]').click()
        assert not await page.locator('#mixer').evaluate("(e)=>e.classList.contains('hidden')")
        assert await page.locator('#mixer').get_attribute('data-io-mode')=='ROUTE'

        # Full repaint must recreate only strip internals, not left-rail controls.
        await page.evaluate('renderMixer()')
        assert await page.locator('.left-rail [data-mix-mode]').count()==3
        assert await page.locator('#mixer > .strip').count()==11
        await assert_linked('RENDER',1024,720)
        assert not errors,errors
        await browser.close()
        print("PASS: 11 shared fader tracks, 4 viewport sizes, 3 banks, rail navigation and repaint")

if __name__=="__main__":
    asyncio.run(main())
