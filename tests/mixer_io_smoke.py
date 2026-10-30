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
        assert "UNPATCHED" in await page.locator('[data-strip="1"] .arBankOutput').inner_text()
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
        # Carry forward the new authoritative native status. A status mock
        # based on the pre-ACK snapshot would incorrectly erase a live send.
        status=await page.evaluate("""status => {
          status.dryRoutingLab.revision=4;
          status.dryRoutingLab.acceptedRoute.channels[0]={...status.dryRoutingLab.acceptedRoute.channels[0],mode:'MONO',left:0};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);
          return status;
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
        await page.set_viewport_size({"width":1680,"height":920})
        for bank in ("FX","ROUTE","SENDS"):
            await page.locator(f'[data-mix-mode="{bank}"]').click()
            assert await page.locator("#mixer .rack").count()==10
            assert await page.locator("#mixer .fader-zone").count()==11
            await page.screenshot(path=f'mixer-g1-bank-{bank.lower()}-1680.png')
        # G3 Studio One style: selected CH→AUX PRE/POST editor lives in
        # SENDS mode; no routing patchbay opening, no fader/FX DOM teardown.
        await page.locator('[data-mix-mode="SENDS"]').click()
        assert await page.locator('[data-strip="0"] .arSendChip').count()==4
        await page.locator('[data-strip="0"] [data-send-focus="0"]').click()
        assert await page.locator('#arSendEditor').is_visible()
        await page.locator('#arSendEditor [data-send-return="enabled"]').check()
        await page.locator('#arSendEditor [data-send-create="POST"]').click()
        assert await page.locator('#arSendEditor [data-send-tap="POST"]').count()==1
        await page.locator('#arSendEditor [data-send-gain="POST"]').select_option("0.5")
        await page.locator('#arSendEditor [data-send-pan="POST"]').select_option("0.5")
        await page.locator('#arSendEditor [data-send-apply]').click()
        actions=await page.evaluate("""() => window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply')""")
        assert len(actions)==2,actions
        g3=actions[-1]['data']['value']
        assert g3['deviceEpoch']==4 and g3['revision']==4,g3
        assert g3['auxes'][0]['enabled'] is True,g3
        assert len(g3['sends'])==1,g3
        assert (g3['sends'][0]['from']==0 and g3['sends'][0]['to']==10 and
          g3['sends'][0]['tap']=='POST' and g3['sends'][0]['gain']==0.5 and
          g3['sends'][0]['pan']==0.5),g3
        status=await page.evaluate("""status => {
          const native=status.dryRoutingLab;
          native.revision=5;
          native.acceptedRoute.auxes[0].enabled=true;
          native.acceptedRoute.sends=[{from:0,to:10,tap:"POST",gain:.5,pan:.5}];
          window.__armixTestEmit("arMixRoutingLabAck",{ok:true,state:native});
          window.__armixTestEmit("arMixStatus",status);
          return status;
        }""",status)
        assert "NATIVE ACK" in await page.locator('#arSendFeedback').inner_text()
        assert await page.locator('#mixer [data-strip="0"] .rack .insert-slot').count()==6
        # With speakers armed, SEND edit remains a draft and APPLY is gated.
        await page.evaluate("""status=>{
          status.monitor=true;
          window.__armixTestEmit("arMixStatus",status);
        }""",status)
        await page.locator('#arSendEditor [data-send-gain="POST"]').select_option("0.25")
        assert await page.locator('#arSendEditor [data-send-apply]').is_disabled()
        assert len(await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==="arMixAction"&&x.data.action==="routingDryApply")"""))==2
        # REVERT restores authoritative ACK and all other faders/FX unchanged.
        await page.locator('#arSendEditor [data-send-revert]').click()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0.5"
        await page.evaluate("""status=>{
          status.monitor=false;window.__armixTestEmit("arMixStatus",status);
        }""",status)
        # Disabled send = actual native zero gain; re-enable restores previous
        # level without creating a duplicate edge.
        await page.locator('#arSendEditor [data-send-enable="POST"]').uncheck()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0"
        await page.locator('#arSendEditor [data-send-enable="POST"]').check()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0.5"
        await page.locator('#arSendEditor [data-send-revert]').click()
        # Negative ACK keeps the old native route while UI shows rejection.
        await page.locator('#arSendEditor [data-send-gain="POST"]').select_option("0.25")
        await page.locator('#arSendEditor [data-send-apply]').click()
        await page.evaluate("""status=>{
          window.__armixTestEmit("arMixRoutingLabAck",
            {ok:false,error:"Stale graph revision",state:status.dryRoutingLab});
        }""",status)
        assert "REJECTED" in await page.locator('#arSendFeedback').inner_text()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0.25"
        await page.locator('#arSendEditor [data-send-revert]').click()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0.5"
        # Unpatched CH2 may not create a send even with AUX1 already enabled.
        result=await page.evaluate("""()=>window.__arMixStageAuxSendEdit({
          from:1,bus:0,tap:"PRE",action:"create"})""")
        assert result['ok'] is False and "Patch CH 2" in result['error'],result
        for width in (1366,1440,1680):
            await page.set_viewport_size({"width":width,"height":768})
            await page.screenshot(path=f'mixer-g3-sends-{width}.png')
            assert await page.locator('#arSendEditor').is_visible()
        await page.locator('#arSendEditor [data-send-close]').click()
        # MS1: true channel processing width is independent of physical SOURCE.
        # The draft must travel through the SAME versioned native APPLY path.
        await page.locator('[data-mix-mode="ROUTE"]').click()
        await page.locator('[data-strip="0"] .arBankChannelFormat').select_option('STEREO')
        assert await page.locator('[data-strip="0"] .arBankChannelFormat').input_value()=='STEREO'
        assert 'DRAFT' in await page.locator('[data-strip="0"] .arBankStage').inner_text()
        draft=await page.evaluate('()=>window.__arMixRouteView().draftRoutes[0]')
        assert draft['mode']=='MONO' and draft['channelFormat']=='STEREO',draft
        result=await page.evaluate('()=>window.__arMixApplyMixerDraft()')
        assert result['ok'],result
        typed=await page.evaluate("""() => window.__armixTestActions.filter(
          a=>a.name==='arMixAction'&&a.data.action==='routingDryApply').at(-1).data.value""")
        assert typed['schemaVersion']==2 and typed['deviceEpoch']==4 and typed['revision']==5,typed
        row=typed['channels'][0]
        assert (row['sourceKind']=='PHYSICAL' and row['sourceFormat']=='MONO' and
                row['channelFormat']=='STEREO' and row['left']==0),row
        # Native negative ACK does not overwrite the accepted processing format.
        await page.evaluate("""status => {
          window.__armixTestEmit('arMixRoutingLabAck',
              {ok:false,error:'Stale graph revision',state:status.dryRoutingLab});
        }""",status)
        assert 'DRAFT' in await page.locator('[data-strip="0"] .arBankStage').inner_text()
        result=await page.evaluate('()=>window.__arMixRevertMixerDraft()')
        assert result['ok'],result
        assert await page.locator('[data-strip="0"] .arBankChannelFormat').input_value()=='MONO'
        # PATCH has a second control for CHANNEL width distinct from SOURCE mode.
        await page.evaluate('()=>window.__arMixOpenChannelPatch(0)')
        fmt=page.locator('[data-patch-channel-format="0"]')
        assert await fmt.input_value()=='MONO'
        await fmt.select_option('STEREO')
        stateDraft=await page.evaluate('()=>window.__arMixRouteView().draftRoutes[0]')
        assert stateDraft['mode']=='MONO' and stateDraft['channelFormat']=='STEREO'
        await page.locator('#arRouteClose').click()
        # L2 all eleven faders remain aligned even after repeated mode changes.
        for bank in ('SENDS','FX','ROUTE','FX','SENDS','ROUTE'):
            await page.locator(f'[data-mix-mode="{bank}"]').click()
            tops=await page.locator('#mixer .fader-zone').evaluate_all(
                '(els)=>els.map(x=>x.getBoundingClientRect().top)')
            assert max(tops)-min(tops)<1.1,(bank,tops)
        # MS1 may leave a channel-format draft; ACKed state is authoritative.
        await page.evaluate("()=>window.__arMixRevertMixerDraft()")
        # Assign a REAL separate physical port first; the second FX lane
        # cannot publish an unpatched/missing CH2.
        await page.locator('[data-mix-mode="ROUTE"]').click()
        await page.locator('[data-strip="1"] select.arBankSource').select_option("MONO:1")
        await page.locator('.arMixerBankBar [data-mix-apply]').click()
        status=await page.evaluate("""status=>{
          const payload=window.__armixTestActions.filter(x=>
            x.name==='arMixAction'&&x.data.action==='routingDryApply').at(-1).data.value;
          status.dryRoutingLab.revision=6;
          status.dryRoutingLab.acceptedRoute=payload;
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert "LIVE" in await page.locator('[data-strip="1"] .arBankStage').inner_text()
        g5before=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').length""")
        # G5B: two actual, indexed native VST3s on CH2 use ONE route
        # transaction (no JS AudioContext); reorder/remove are DRAFT until ACK.
        await page.evaluate("""() => window.__armixTestEmit('arMixCatalog',{
          items:[{id:'eq-pro',name:'EQ Pro',vendor:'Test',category:'EQ',path:'test.vst3'},
                 {id:'rv-pro',name:'Reverb Pro',vendor:'Test',category:'Reverb',path:'rv.vst3'}]
        })""")
        await page.locator('[data-mix-mode="FX"]').click()
        stage=await page.evaluate("""()=>{
          return [window.__arMixStageRackFx(1,'eq-pro',0,'set'),
                  window.__arMixStageRackFx(1,'rv-pro',1,'set')];
        }""")
        assert all(x['ok'] for x in stage),stage
        await page.wait_for_function("""()=>window.__arMixFxLaneView(1).ids.length===2""")
        assert await page.locator('[data-strip="1"] .rack .insert-slot').count()==6
        assert await page.locator('[data-strip="1"] .rack .ar-routed-loaded').count()==2
        # No automatic output unmute or implicit native APPLY.
        g5after=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').length""")
        assert g5after==g5before,(g5before,g5after)
        queued=await page.evaluate("""()=>window.__arMixApplyRackFx()""")
        assert queued['ok'],queued
        g5=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').at(-1).data.value""")
        assert g5['channels'][1]['fxIds']==['eq-pro','rv-pro'],g5
        assert g5['channels'][1]['fxId']=='eq-pro',g5
        # Rejected APPLY must keep the previous native live graph unchanged.
        await page.evaluate("""status=>{
          window.__armixTestEmit('arMixRoutingLabAck',
            {ok:false,error:'FX plugin rejected',state:status.dryRoutingLab});
        }""",status)
        assert await page.evaluate("""()=>window.__arMixFxLaneView(1).live.length""")==0
        # Re-submit after rejection, native ACK makes both inserts live.
        await page.evaluate("""()=>window.__arMixApplyRackFx()""")
        status=await page.evaluate("""status=>{
          const payload=window.__armixTestActions.filter(x=>
            x.name==='arMixAction'&&x.data.action==='routingDryApply').at(-1).data.value;
          status.dryRoutingLab.revision++;
          status.dryRoutingLab.acceptedRoute={...payload};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert await page.evaluate("""()=>window.__arMixFxLaneView(1).live.join('|')""")=="eq-pro|rv-pro"
        await page.evaluate("""()=>window.__arMixStageRackFx(1,'',1,'up')""")
        assert await page.evaluate("""()=>window.__arMixFxLaneView(1).ids.join('|')""")=="rv-pro|eq-pro"
        assert await page.evaluate("""()=>window.__arMixFxLaneView(1).live.join('|')""")=="eq-pro|rv-pro"
        # Stage is reversible; routing device must not be reopened.
        await page.locator('#arPatchRevert').click()
        assert await page.evaluate("""()=>window.__arMixFxLaneView(1).ids.join('|')""")=="eq-pro|rv-pro"
        assert await page.locator('[data-strip="1"] .rack .insert-slot').count()==6
        assert not errors,errors
        await browser.close()
        print("PASS: G1/G2 + G3 real PRE/POST draft, AUX return, 0-gain bypass, native ACK/reject/revert, stable rack and 3 viewports")

if __name__=="__main__":
    asyncio.run(main())
