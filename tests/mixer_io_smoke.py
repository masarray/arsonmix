"""G1/G2 exact-source Chromium regression: stable FX, native routed input draft and I/O readback."""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

async def main():
    root=Path(__file__).resolve().parents[1]
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        page=await browser.new_page(viewport={"width":1366,"height":768})
        errors=[]
        page.on("pageerror",lambda e:errors.append(str(e)))
        await page.add_init_script("""(() => {
          const listeners={};
          window.__armixTestActions=[];
          window.__JUCE__={backend:{
            addEventListener(name,fn){(listeners[name]??=[]).push(fn)},
            emitEvent(name,data){window.__armixTestActions.push({name,data})}
          }};
          window.__armixTestEmit=(name,datum)=>(listeners[name]||[]).forEach(fn=>fn(datum));
        })();""")
        await page.goto((root/"web/index.html").as_uri())
        assert await page.locator("#mixer .rack").count()==10
        assert await page.locator("#mixer .arChannelBank").count()==10
        assert await page.locator("[data-mix-mode]").count()==3
        status=await page.evaluate("""() => {
          const ch=Array.from({length:10},()=>({mode:'NONE',left:-1,right:-1,gain:.25,pan:0,main:true,mute:false,solo:false,fxId:''}));
          const auxes=Array.from({length:4},()=>({enabled:false,gain:1,main:true,mute:false,fxId:''}));
          const dryRoutingLab={active:true,ready:true,epoch:4,revision:3,
            ports:[{port:0,label:'Microphone 1'},{port:1,label:'Microphone 2'}],
            outputPorts:[0,1],acceptedRoute:{deviceEpoch:4,channels:ch,auxes,sends:[]}};
          const status={connected:true,driver:'ASIO · TEST HARDWARE',rate:48000,buffer:128,
            monitor:false,masterMute:false,chainCount:0,dryRoutingLab};
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""")
        assert not errors,errors
        # Changing the bank may hide, but may not delete, the original six-slot FX rows.
        sizes=await page.evaluate("""() => {
          const keys=['#mixer [data-strip="0"] .fader-zone','#mixer [data-strip="0"] .channel-actions'];
          return keys.map(q=>document.querySelector(q).getBoundingClientRect().top);
        }""")
        for mode in ("ROUTE","SENDS","FX","ROUTE","FX"):
            await page.locator(f'[data-mix-mode="{mode}"]').click()
            assert await page.locator("#mixer .rack").count()==10
            assert await page.locator("#mixer [data-strip='0'] .rack .insert-slot").count()==6
        after=await page.evaluate("""() => {
          const keys=['#mixer [data-strip="0"] .fader-zone','#mixer [data-strip="0"] .channel-actions'];
          return keys.map(q=>document.querySelector(q).getBoundingClientRect().top);
        }""")
        assert all(abs(a-b)<1 for a,b in zip(sizes,after)),(sizes,after)
        await page.locator('[data-mix-mode="ROUTE"]').click()
        palette=await page.evaluate("""() => ['FX','ROUTE'].map(x=>getComputedStyle(
          document.querySelector('[data-mix-mode="'+x+'"]')).backgroundColor)""")
        assert palette[0]!=palette[1],palette
        await page.locator('[data-strip="0"] select.arBankSource').select_option("MONO:0")
        assert "DRAFT" in await page.locator('[data-strip="0"] .arBankStage').inner_text()
        await page.locator('.arMixerBankBar [data-mix-apply]').click()
        actions=await page.evaluate("""() => window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply')""")
        assert len(actions)==1,actions
        payload=actions[0]['data']['value']
        assert payload['deviceEpoch']==4 and payload['revision']==3,payload
        assert payload['channels'][0]['mode']=='MONO' and payload['channels'][0]['left']==0,payload
        await page.evaluate("""status => {
          status.dryRoutingLab.revision=4;
          status.dryRoutingLab.acceptedRoute.channels[0]={...status.dryRoutingLab.acceptedRoute.channels[0],mode:'MONO',left:0};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);
        }""",status)
        assert "LIVE" in await page.locator('[data-strip="0"] .arBankStage').inner_text()
        await page.locator('#audioSetupBtn').click()
        assert await page.locator('#arIOOverlay').is_visible()
        assert await page.locator('#arIOBody select.arIOSource').count()==10
        await page.locator('[data-io-tab="outputs"]').click()
        outputs=await page.locator('#arIOBody').inner_text()
        assert "HW OUT 1" in outputs and "HW OUT 2" in outputs,outputs
        assert "AUX1" in outputs and "not physical" in outputs,outputs
        await page.locator('[data-io-tab="device"]').click()
        await page.evaluate("""() => window.__armixTestEmit('arMixAudioDeviceInventory',{
          types:[{name:'ASIO',active:true,inputs:['Mic 1','Mic 2'],outputs:['Out 1','Out 2']}]
        })""")
        assert "ASIO" in await page.locator('#arIOBody').inner_text()
        await page.locator('[data-io-tab="inputs"]').click()
        for width in (1366,1440,1680):
            await page.set_viewport_size({"width":width,"height":768})
            await page.screenshot(path=f'mixer-g1-g2-{width}.png')
            assert await page.locator('#arIOOverlay .arIOWindow').is_visible()
        await page.locator('[data-io-close]').click()
        assert not await page.locator('#arIOOverlay').is_visible()
        assert not errors,errors
        await browser.close()
        print("PASS: G1/G2 FX retention, stable faders, muted native input draft/ACK, real I/O, 3 viewport screenshots")

if __name__=="__main__":
    asyncio.run(main())
