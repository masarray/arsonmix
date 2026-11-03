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
        assert await page.locator('.left-rail [data-mix-mode]').count()==4
        assert await page.locator('.left-rail [data-my-sounds]').count()==1
        assert await page.locator('.left-rail [data-plugin-manager]').count()==1
        assert await page.locator('.left-rail #audioSetupBtn').count()==1
        assert await page.locator('.left-rail #arSignalOpen').count()==1
        assert await page.locator('.workbench > .arMixerBankBar').count()==0
        assert await page.locator('.arRailModeSection > .arMixerBankBar').count()==1
        assert await page.locator('#mixer > .strip').count()==11
        assert await page.locator('#mixer').get_attribute('data-io-mode')=='CONSOLE'

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
            await page.evaluate("""()=>{window.__stableFader=document.querySelector('[data-fader="0"]')}""")
            for mode in ('FX','ROUTE','SENDS'):
                await page.locator(f'.left-rail [data-mix-mode="{mode}"]').click()
                assert await page.locator('#mixer .rack').count()==10
                assert await page.locator('#masterLeft .fader-zone').count()==1
                heights[mode]=await assert_linked(mode,width,height)
                if mode=='ROUTE':
                    bounds=await page.evaluate("""()=>[...document.querySelectorAll('#mixer .strip[data-strip]')].map(strip=>{
                      const bank=strip.querySelector('.arChannelBank').getBoundingClientRect();
                      const patch=strip.querySelector('.arBankPatch')?.getBoundingClientRect();
                      return patch && patch.bottom<=bank.bottom+1 && patch.top>=bank.top-1;
                    })""")
                    assert all(bounds),(mode,width,height,'ROUTE patch clipped')
                # MASTER remains visible on small laptop screens, including scroll.
                assert await page.evaluate("""() => {
                    const m=document.querySelector('#masterLeft').getBoundingClientRect();
                    const v=document.querySelector('#mixer').getBoundingClientRect();
                    return m.left>=v.left-2 && m.right<=v.right+2;
                }"""),(mode,width,height,'MASTER clipped')
                # Selecting CH10 from sidebar exposes it before MASTER.
                await page.locator('#channelList [data-choose="9"]').click()
                assert await page.evaluate("""() => {
                    const s=document.querySelector('[data-strip="9"]').getBoundingClientRect();
                    const m=document.querySelector('#masterLeft').getBoundingClientRect();
                    const v=document.querySelector('#mixer').getBoundingClientRect();
                    return s.left>=v.left-2 && s.right<=m.left+2;
                }"""),(mode,width,height,'CH10 obscured by MASTER')
                await page.locator('#channelList [data-choose="0"]').click()
                if width==1680:
                    await page.screenshot(path=f'mixer-shell-{mode.lower()}-1680.png')
                if width==1366 and mode=='SENDS':
                    await page.screenshot(path='mixer-shell-compact-1366.png')
            # No vertical shift while navigating FX / ROUTE / SENDS.
            assert max(heights.values())-min(heights.values())<1.1,(width,height,heights)
            assert await page.evaluate("""()=>document.querySelector('[data-fader="0"]')===window.__stableFader"""),(width,height,'fader remounted')
            await page.locator('.left-rail [data-mix-mode="FX"]').click()
            assert abs(await assert_linked('FX RESTORE',width,height)-heights['FX'])<1.1

        # Visual acceptance of the REAL Console (not the isolated #278 source):
        # at desktop widths CH01..CH10 and pinned MAIN share a viewport; no
        # wrapped identity, no invisible overlapping labels or repeated Add FX.
        await page.locator('.left-rail [data-mix-mode="CONSOLE"]').click()
        for width,height in [(1366,768),(1440,820),(1680,900),(1024,720)]:
            await page.set_viewport_size({"width":width,"height":height})
            await page.evaluate("document.getElementById('mixer').scrollLeft=0")
            verdict=await page.evaluate("""() => {
              const m=document.querySelector('#mixer').getBoundingClientRect();
              const main=document.querySelector('#masterLeft').getBoundingClientRect();
              const strips=[...document.querySelectorAll('#mixer > .strip[data-strip]')];
              const heads=strips.map(s=>{
                const el=s.querySelector('.ch-id'),nr=el.querySelector('.arChannelNumber'),
                  btn=el.querySelector('.arChannelTopIdentity'),
                  mode=el.querySelector('.arFormatBadge');
                const h=el.getBoundingClientRect(),n=nr.getBoundingClientRect(),
                  b=btn.getBoundingClientRect(),f=mode.getBoundingClientRect();
                return {h:h.height,inside:n.left>=h.left-1&&f.right<=h.right+1&&
                  b.left>=n.right-2&&b.right<=f.left+2,
                  single:n.bottom<=h.bottom+1&&b.bottom<=h.bottom+1&&f.bottom<=h.bottom+1,
                  ellipsis:getComputedStyle(btn.querySelector('.arChannelTopName')).textOverflow};
              });
              return {width:window.innerWidth,mainVisible:main.left>=m.left-2&&main.right<=m.right+2,
                lastVisible:strips[9].getBoundingClientRect().right<=main.left+2,
                headProblems:heads.filter(x=>!x.inside||!x.single||x.h<27||x.ellipsis!=='ellipsis'),
                visibleSources:strips.filter(s=>{const r=s.getBoundingClientRect();
                  return r.left>=m.left-2&&r.right<=main.left+2}).length,
                repeatedAdd:document.querySelectorAll('#mixer .strip[data-strip="0"] .rack .insert-slot:has(.insert.empty)')
                  .length,
                displayedAdd:[...document.querySelectorAll('#mixer .strip[data-strip="0"] .rack .insert-slot:has(.insert.empty)')]
                  .filter(el=>getComputedStyle(el).display!=='none').length};
            }""")
            assert verdict['mainVisible'],('MAIN obscured',verdict)
            assert not verdict['headProblems'],('identity clip/overlap/wrap',verdict)
            assert verdict['displayedAdd']<=1,('repeated ADD FX',verdict)
            if width>=1366:
                assert verdict['lastVisible'],('CH10 clipped by MAIN',verdict)
                assert verdict['visibleSources']==10,('not ten visible strips',verdict)
            if width in (1366,1440,1680):
                await page.screenshot(path=('mixer-shell-compact-1366.png' if width==1366 else
                    'mixer-design-review-1440.png' if width==1440 else 'mixer-g1-g2-1680.png'))
        await page.set_viewport_size({"width":1024,"height":720})
        await page.locator('.left-rail [data-mix-mode="FX"]').click()

        # Parent view changes still work; click rail mode from PATCH to return MIX.
        await page.locator('[data-view="SIGNAL FLOW"]').click()
        assert await page.locator('#mixer').evaluate("(e)=>e.classList.contains('hidden')")
        await page.locator('.left-rail [data-mix-mode="ROUTE"]').click()
        assert not await page.locator('#mixer').evaluate("(e)=>e.classList.contains('hidden')")
        assert await page.locator('#mixer').get_attribute('data-io-mode')=='ROUTE'

        # Full repaint must recreate only strip internals, not left-rail controls.
        await page.evaluate('renderMixer()')
        assert await page.locator('.left-rail [data-mix-mode]').count()==4
        assert await page.locator('#mixer > .strip').count()==11
        await assert_linked('RENDER',1024,720)
        assert not errors,errors
        await browser.close()
        print("PASS: 11 shared fader tracks, 4 viewport sizes, 3 banks, rail navigation and repaint")

if __name__=="__main__":
    # Execute the v8 native-producer fixture replay through real Chromium
    # within this existing GUI smoke job; no second workflow/CI matrix.
    from operator_console_v6_smoke import main as native_console_smoke
    async def run_gui_suite():
        await main()
        await native_console_smoke()
    asyncio.run(run_gui_suite())
