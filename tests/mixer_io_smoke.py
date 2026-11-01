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
        # The REAL M4 host, not the future v6 demo, opens one console:
        # actual native selectors, six VST3 slots and four AUX controls
        # coexist in every input strip without FX/ROUTE/SENDS switching.
        assert await page.locator('#mixer').get_attribute('data-io-mode')=='CONSOLE'
        assert await page.locator('[data-mix-mode="CONSOLE"]').get_attribute('aria-pressed')=='true'
        assert await page.locator('#mixer .arBankSource:visible').count()==10
        assert await page.locator('#mixer .arBankOutput:visible').count()==10
        assert await page.locator('#mixer .rack .insert-slot').count()==60
        assert await page.locator('#mixer .arSendBank:visible').count()==10
        await page.screenshot(path='mixer-m4-unified-1366.png')
        # Main is visually pinned during navigation across the 10 real strips.
        # Do not accept a CSS "sticky" label if its pixels are offscreen.
        main_geom=await page.evaluate("""() => {
          const mixer=document.querySelector('#mixer');
          const master=document.querySelector('#masterLeft');
          const start=master.getBoundingClientRect();
          const frame=mixer.getBoundingClientRect();
          mixer.scrollLeft=99999;
          const end=master.getBoundingClientRect();
          mixer.scrollLeft=0;
          return {left:start.left,right:start.right,endLeft:end.left,endRight:end.right,
            frameRight:frame.right,frameLeft:frame.left};
        }""")
        assert main_geom['right']<=main_geom['frameRight']+2,main_geom
        assert main_geom['left']>=main_geom['frameLeft']-2,main_geom
        assert abs(main_geom['right']-main_geom['endRight'])<2,main_geom
        assert await page.locator('.arChannelSignalFlow [data-focus-step]').count()==4
        assert 'NO NATIVE ACK' in await page.locator('[data-focus-source]').inner_text()
        assert await page.locator("[data-mix-mode]").count()==4
        status=await page.evaluate("""() => {
          const ch=Array.from({length:10},()=>({mode:'NONE',left:-1,right:-1,gain:.25,pan:0,main:true,mute:false,solo:false,fxId:''}));
          const auxes=Array.from({length:4},()=>({enabled:false,gain:1,main:true,mute:false,fxId:''}));
          const dryRoutingLab={active:true,ready:true,epoch:4,revision:3,
            ports:[{port:0,label:'Microphone 1'},{port:1,label:'Microphone 2'}],
            outputPorts:[0,1],acceptedRoute:{deviceEpoch:4,channels:ch,auxes,sends:[]}};
          const status={connected:true,directRouteAuthorized:true,driver:'ASIO · TEST HARDWARE',rate:48000,buffer:128,
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
        assert "PENDING" in await page.locator('[data-strip="0"] .arBankStage').inner_text()
        assert await page.locator('[data-strip="0"] .arBankSource').is_disabled()
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
          status.dryRoutingLab.hasAcceptedRoute=true;
          status.dryRoutingLab.acceptedRoute.channels[0]={...status.dryRoutingLab.acceptedRoute.channels[0],mode:'MONO',left:0};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        # FAST3: the first accepted routing APPLY triggers guarded ARM.
        # Complete its actual native ACK before the next operator action.
        await page.evaluate("""status=>window.__armixTestEmit('arMixAudioArmAck',{
          ok:true,armed:true,deviceEpoch:status.dryRoutingLab.epoch
        })""",status)
        assert not (await page.evaluate("()=>window.__arMixFastAudioState().pending"))
        # R7: one MONO physical input can feed a user-selected STEREO
        # processing channel. Native ACCEPTED channelFormat (not channel
        # index/draft) alone determines single vs independent L/R meter DOM.
        status=await page.evaluate("""status=>{
          status.dryRoutingLab.acceptedRoute.channels[0].channelFormat='STEREO';
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert await page.locator('[data-strip="0"] .vu-bank.is-stereo [data-meter]').count()==2
        assert await page.locator('[data-strip="0"] [data-meter="0L"]').count()==1
        assert await page.locator('[data-strip="0"] [data-meter="0R"]').count()==1
        status=await page.evaluate("""status=>{
          status.dryRoutingLab.acceptedRoute.channels[0].channelFormat='MONO';
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert await page.locator('[data-strip="0"] .vu-bank.is-mono [data-meter]').count()==1
        assert await page.locator('[data-strip="0"] [data-meter="0"]').count()==1
        # P0: verify real capture signal, accepted route and safe output are
        # three separate pieces of evidence, not optimistic LIVE labels.
        silent=await page.evaluate("""()=>window.__arMixAudioPathView({
          connected:true,inputDevice:'CABLE Output (VB-Audio Virtual Cable)',
          callbackCount:10,callbackInputChannels:2,inputPeak:0,monitor:false,
          dryRoutingLab:{ready:true,hasAcceptedRoute:true}
        })""")
        assert silent['input']=='NO RAW CAPTURE PCM',silent
        assert silent['route']=='NATIVE ROUTE ACK' and silent['output']=='OUTPUT SAFE-MUTED',silent
        assert silent['mixer']=='MIXER · WAIT ARM',silent
        assert 'CABLE Input' in silent['message'],silent
        muted=await page.evaluate("""()=>window.__arMixAudioPathView({
          connected:true,inputDevice:'CABLE Output (VB-Audio Virtual Cable)',
          callbackCount:11,callbackInputChannels:2,inputPeak:.14,monitor:false,
          dryRoutingLab:{ready:true,hasAcceptedRoute:true,
            acceptedRoute:{channels:[{mode:'MONO'}]},channelPeaks:[0]}
        })""")
        assert muted['input']=='RAW CAPTURE PCM',muted
        assert muted['mixer']=='MIXER · WAIT ARM' and muted['output']=='OUTPUT SAFE-MUTED',muted
        assert 'direct monitor' in muted['message'].lower(),muted
        audible=await page.evaluate("""()=>window.__arMixAudioPathView({
          connected:true,inputDevice:'CABLE Output (VB-Audio Virtual Cable)',
          callbackCount:11,callbackInputChannels:2,inputPeak:.14,peak:.035,monitor:true,
          dryRoutingLab:{ready:true,hasAcceptedRoute:true,
            acceptedRoute:{channels:[{mode:'MONO'}]},channelPeaks:[.09]}
        })""")
        assert audible['input']=='RAW CAPTURE PCM' and audible['mixer']=='POST-FADER PCM',audible
        assert audible['output']=='MAIN PCM · ARMED',audible
        assert 'sound must stop' in audible['message'],audible
        await page.evaluate("()=>window.__arMixOpenAudioIO('device')")
        assert await page.locator('#arIOAudioPath').is_visible()
        assert await page.locator('#arIOAudioPath [data-io-signal-check]').is_visible()
        assert await page.locator('#arIOAudioPath [data-audio-path-mixer]').is_visible()
        # FAST2: one APPLY only. No second START AUDIO button is rendered.
        assert await page.locator('#arIOAudioPath [data-io-start-audio]').count()==0
        await page.locator('#arIOAudioPath [data-io-signal-check]').click()
        assert await page.locator('#arSignalOverlay').evaluate("(e)=>e.classList.contains('open')")
        await page.locator('#arSignalClose').click()
        assert "ROUTE ACK" in await page.locator('[data-strip="0"] .arBankStage').inner_text()
        assert 'Microphone 1' in await page.locator('[data-focus-source]').inner_text()
        assert 'MAIN' in await page.locator('[data-focus-out]').inner_text()
        # M2 Studio-style BUS: one existing empty native lane, no phantom
        # CH11; group re-routes direct MAIN, parallel FX BUS preserves dry.
        # Both remain DRAFT until the operator explicitly MUTES/APPLIES/ACKs.
        group=await page.evaluate("""()=>({
          result:window.__arMixStageBusFromSelected({source:0,kind:'group'}),
          view:window.__arMixRouteView()
        })""")
        assert group['result']['ok'],group
        bus=group['result']['target']
        assert group['view']['draftRoutes'][bus]['mode']=='BUS',group
        assert group['view']['draftRoutes'][bus]['gain']==1,group
        assert group['view']['draftRoutes'][0]['main'] is False,group
        assert any(x['from']==0 and x['to']==bus and x['gain']==1
                   for x in group['view']['draftSends']),group
        assert (await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply').length"""))==1
        assert (await page.evaluate("()=>window.__arMixRevertMixerDraft()"))['ok']
        await page.locator('#arRouteClose').click()
        fxbus=await page.evaluate("""()=>({
          result:window.__arMixStageBusFromSelected({source:0,kind:'fx'}),
          view:window.__arMixRouteView()
        })""")
        assert fxbus['result']['ok'] and fxbus['result']['target']==bus,fxbus
        assert fxbus['view']['draftRoutes'][0]['main'] is True,fxbus
        assert any(x['from']==0 and x['to']==bus and x['gain']==.25
                   for x in fxbus['view']['draftSends']),fxbus
        assert (await page.evaluate("()=>window.__arMixRevertMixerDraft()"))['ok']
        await page.locator('#arRouteClose').click()
        assert (await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply').length"""))==1
        await page.locator('[data-focus-step="fx"]').click()
        assert await page.locator('#mixer').get_attribute('data-io-mode')=='FX'
        await page.locator('[data-focus-step="source"]').click()
        assert await page.locator('.arSourceChooser').is_visible()
        assert await page.locator('.arSourceChoice').count()>=2
        assert await page.locator('.arSourceChoice[aria-pressed="true"]').count()==1
        await page.locator('[data-source-close]').click()
        assert not await page.locator('.arSourceChooser').is_visible()
        # The legacy strip selector is intentionally available in ROUTE
        # mode; selected-channel SOURCE no longer changes the global bank.
        await page.locator('[data-mix-mode="ROUTE"]').click()
        # R7: an unpatched CH uses ONE operator gesture. While OUTPUT is
        # ARMED it first requests an explicit guarded MUTE; it must NOT
        # publish a structural route or fake accepted/live readback yet.
        await page.evaluate("""status=>{
          status.monitor=true;
          window.__armixTestEmit('arMixStatus',status);
        }""",status)
        await page.locator('[data-strip="1"] select.arBankSource').select_option('MONO:1')
        assert await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply').length""")==1
        assert (await page.evaluate("()=>window.__arMixRouteView().draftRoutes[1].mode"))=='MONO'
        assert (await page.evaluate("()=>window.__arMixRouteView().acceptedRoute.channels[1].mode"))=='NONE'
        assert await page.evaluate("""()=>window.__armixTestActions.some(
          x=>x.name==='arMixAction'&&x.data.action==='monitor'&&x.data.value===false)""")
        # An explicit negative hardware mute acknowledgement must leave
        # accepted audio unchanged. Revert the uncommitted draft.
        rejected=await page.evaluate("""status=>{
          window.__armixTestEmit('arMixAudioArmAck',{
            ok:false,armed:true,deviceEpoch:status.dryRoutingLab.epoch});
          return window.__arMixRevertMixerDraft();
        }""",status)
        assert rejected['ok'],rejected
        assert (await page.evaluate("()=>window.__arMixRouteView().draftRoutes[1].mode"))=='NONE'
        # M1: an ALREADY PATCHED MONO channel may change physical input
        # while output is ARMED: one native source PARAM/ACK and no APPLY.
        await page.locator('[data-mix-mode="FX"]').click()
        # SOURCE follows the selected strip, not the previous editable row.
        await page.evaluate("()=>chooseChannel(0)")
        assert await page.locator('#mixer [data-strip="0"]').evaluate("(e)=>e.classList.contains('selected')")
        await page.locator('[data-focus-step="source"]').click()
        assert await page.locator('.arSourceChooser').is_visible()
        await page.locator('.arSourceChoice[data-source-choice="MONO:1"]').click()
        switching=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingSourceSwitch')""")
        assert len(switching)==1,(switching,await page.locator('[data-source-note]').inner_text())
        assert switching[0]['data']['requestId']==1 and switching[0]['data']['channel']==0
        assert switching[0]['data']['left']==1 and switching[0]['data']['deviceEpoch']==4
        assert (await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').length"""))==1
        assert 'Microphone 1' in await page.locator('[data-focus-source]').inner_text()
        # Stale request cannot impersonate the first accepted switch.
        await page.evaluate("""status=>window.__armixTestEmit('arMixSourceSwitchAck',{
          ok:true,requestId:321,channel:0,state:status.dryRoutingLab})""",status)
        assert 'Microphone 1' in await page.locator('[data-focus-source]').inner_text()
        status=await page.evaluate("""status=>{
          status.dryRoutingLab.acceptedRoute.channels[0].left=1;
          window.__armixTestEmit('arMixSourceSwitchAck',{
            ok:true,requestId:1,channel:0,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);return status;
        }""",status)
        assert 'Microphone 2' in await page.locator('[data-focus-source]').inner_text()
        # Restore original source to keep other mixer regression fixtures stable.
        await page.locator('.arSourceChoice[data-source-choice="MONO:0"]').click()
        status=await page.evaluate("""status=>{
          status.dryRoutingLab.acceptedRoute.channels[0].left=0;
          window.__armixTestEmit('arMixSourceSwitchAck',{
            ok:true,requestId:2,channel:0,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);return status;
        }""",status)
        assert 'Microphone 1' in await page.locator('[data-focus-source]').inner_text()
        assert (await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').length"""))==1
        await page.locator('[data-source-close]').click()
        await page.evaluate("""status=>{
          status.monitor=false;
          window.__armixTestEmit('arMixStatus',status);
        }""",status)
        await page.locator('#audioSetupBtn').click()
        assert await page.locator('#arIOOverlay').is_visible()
        assert await page.locator('[data-io-tab="device"]').get_attribute('aria-selected')=='true'
        assert await page.locator('.arIORealDeviceSetup').is_visible()
        # DEVICE1: actual driver dropdown, not a cosmetic inventory/native popup.
        await page.evaluate("""()=>window.__armixTestEmit('arMixAudioDeviceInventory',{
          currentType:'ASIO',
          active:{backend:'ASIO',captureDevice:'MAONO PS22',playbackDevice:'MAONO PS22',
            rate:48000,buffer:128,open:true,deviceGeneration:7,outputMuted:true,
            inputNames:['Mic 1','Virtual L','Virtual R','Mic 4'],
            outputNames:['Output 1','Output 2','Monitor 3','Monitor 4'],
            enabledInputs:[0,1,2,3],enabledOutputs:[0,1,2,3],
            rates:[44100,48000],buffers:[64,128,256]},
          types:[
            {name:'ASIO',active:true,separate:false,inputs:['MAONO PS22'],outputs:['MAONO PS22']},
            {name:'Windows Audio',active:false,separate:true,
              inputs:['Windows Microphone'],outputs:['Windows Speakers']},
            {name:'DirectSound',active:false,separate:true,
              inputs:['Legacy Mic'],outputs:['Legacy Speakers']}
          ]})""")
        # DEVICE2: opening custom panel reads cached hardware, never rescans vendors.
        native_actions=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction')""")
        assert any(x['data']['action']=='audioDeviceInventory' for x in native_actions)
        assert not any(x['data']['action']=='audioDeviceRefresh' for x in native_actions)
        await page.locator('[data-io-refresh-device]').click()
        refreshed=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='audioDeviceRefresh')""")
        assert len(refreshed)==1,refreshed
        # DEVICE3: ASIO/Windows two-mode toggle + real active port M/L/R matrix.
        assert await page.locator('.arIODeviceDriverButton').count()==2
        assert await page.locator('.arIODeviceDriverButton[aria-pressed="true"]').inner_text()=='ASIO'
        assert await page.locator('.arIODevicePatchMatrix').is_visible()
        assert not await page.locator('.arIODevicePatchMatrix').evaluate('(e)=>e.open')
        assert await page.locator('.arIODevicePins').is_visible()
        await page.locator('.arIODevicePatchMatrix > summary').click()
        assert await page.locator('.arIODevicePatchMatrix').evaluate('(e)=>e.open')
        assert await page.locator('.arIODevicePatchMatrix .arIOMatrixCell').count()>0
        # FAST3 source one-gesture has already completed an ARM ACK above.
        # This separate DEVICE APPLY must produce ONE new guarded arm, not
        # an absolute total of one for the entire operator test session.
        assert await page.locator('#arIOAudioPath [data-io-start-audio]').count()==0
        arm_before=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='monitor'&&x.data.value===true).length""")
        await page.locator('[data-io-device-apply]').click()
        arm=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='monitor'&&x.data.value===true)""")
        assert len(arm)==arm_before+1,arm
        assert arm[-1]['data']['expectedEpoch']==4,arm
        assert arm[-1]['data']['expectedRevision']==4,arm
        await page.evaluate("""()=>window.__armixTestEmit('arMixAudioArmAck',{
          ok:true,armed:true,deviceEpoch:4,error:''})""")
        await page.evaluate("""status=>{status.monitor=true;
          window.__armixTestEmit('arMixStatus',status)}""",status)
        assert await page.locator('[data-io-start-audio]').inner_text()=='MUTE OUTPUT'
        mute_before=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='monitor'&&x.data.value===false).length""")
        await page.locator('[data-io-start-audio]').click()
        muted=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='monitor'&&x.data.value===false)""")
        assert len(muted)==mute_before+1,muted
        await page.evaluate("""status=>{status.monitor=false;
          window.__armixTestEmit('arMixStatus',status)}""",status)
        assert await page.locator('.arIODeviceDriverButton').nth(1).is_enabled()
        await page.locator('.arIODeviceDriverButton').nth(1).click()
        assert 'APPLY DEVICE first' in await page.locator('.arIODeviceMatrixEmpty').inner_text()
        # Selecting another backend is only a draft; the real matrix must not
        # silently display pins belonging to a different device/clock.
        assert await page.locator('.arIODevicePatchMatrix .arIOMatrixCell').count()==0
        await page.locator('[data-io-device-revert]').click()
        assert await page.locator('.arIODevicePatchMatrix .arIOMatrixCell').count()>0
        await page.locator('.arIODeviceMatrixTab').last.click()
        assert await page.locator('.arIODevicePatchMatrix .arIOMatrixCell').count()>0
        await page.locator('.arIODeviceMatrixTab').first.click()
        await page.locator('.arIODeviceAdvanced summary').click()
        assert await page.locator('.arIODevicePin').count()==8
        assert await page.locator('.arIODeviceCapture').is_disabled()
        assert await page.locator('.arIODeviceRate').input_value()=='48000'
        # FAST2: APPLY remains enabled on an accepted device to ARM real MAIN.
        assert await page.locator('[data-io-device-apply]').is_enabled()
        # Staging sample rate may not reopen any hardware automatically.
        await page.locator('.arIODeviceBuffer').select_option('256')
        assert await page.locator('[data-io-device-apply]').is_enabled()
        assert not await page.evaluate("""()=>window.__armixTestActions.some(x=>
          x.name==='arMixAction'&&x.data.action==='audioDeviceApply')""")
        await page.locator('[data-io-device-apply]').click()
        actions=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='audioDeviceApply')""")
        assert len(actions)==1,actions
        assert actions[0]['data']['config']['bufferSize']==256,actions
        assert actions[0]['data']['expectedDeviceGeneration']==7,actions
        assert await page.locator('[data-io-device-apply]').is_disabled()
        # Stale ACK cannot falsely accept/relabel the driver's real state.
        await page.evaluate("""()=>window.__armixTestEmit('arMixAudioDeviceApplyAck',{
          requestId:'stale',ok:true,state:'ACCEPTED_MUTED'})""")
        assert await page.locator('[data-io-device-apply]').is_disabled()
        await page.evaluate("""action=>window.__armixTestEmit('arMixAudioDeviceApplyAck',{
          requestId:action.requestId,ok:true,state:'ACCEPTED_MUTED',
          active:{backend:'ASIO',captureDevice:'MAONO PS22',playbackDevice:'MAONO PS22',
            rate:48000,buffer:256,open:true,deviceGeneration:8,outputMuted:true,
            inputNames:['Mic 1','Virtual L','Virtual R','Mic 4'],
            outputNames:['Output 1','Output 2','Monitor 3','Monitor 4'],
            enabledInputs:[0,1,2,3],enabledOutputs:[0,1,2,3],
            rates:[44100,48000],buffers:[64,128,256]}})""",actions[0]['data'])
        assert await page.locator('.arIODeviceBuffer').input_value()=='256'
        assert await page.locator('.arIODevicePatchMatrix .arIOMatrixCell').count()==0
        assert 'Waiting for' in await page.locator('.arIODeviceMatrixEmpty').inner_text()
        assert 'ACCEPTED' in await page.locator('#arIOFeedback').inner_text()
        assert await page.locator('[data-io-device-apply]').is_disabled()
        # Separate capture/playback choice for WASAPI is a DRAFT only.
        await page.locator('.arIODeviceDriverButton').nth(1).click()
        assert await page.locator('.arIODeviceCapture').is_enabled()
        assert await page.locator('.arIODevicePlayback').input_value()=='Windows Speakers'
        assert await page.locator('.arIODeviceRate').input_value()=='0'
        await page.locator('[data-io-device-revert]').click()
        assert await page.locator('.arIODeviceDriverButton[aria-pressed="true"]').inner_text()=='ASIO'
        await page.locator('[data-io-tab="inputs"]').click()
        assert await page.locator('#arIOBody .arIOQuickRow').count()==10
        assert await page.locator('[data-io-quick-row]:visible').count()==1
        # Operator-first mapper is visible; crosspoint is explicitly advanced.
        assert await page.locator('#arIOQuickPatch, .arIOQuickPatch').count()==1
        assert await page.locator('[data-io-quick-row]').count()==10
        await page.locator('.arIOAdvancedMatrix summary').click()
        # IO-MATRIX: real input pins, no implied right channel, no second DSP graph.
        assert await page.locator('[data-io-row-mode]').count()==10
        assert await page.locator('[data-io-matrix-kind="inputs"]').count()==20
        await page.locator('[data-io-row-mode="1"]').select_option('STEREO')
        assert await page.locator('.arIOWindow [data-mix-apply]').is_disabled()
        assert (await page.evaluate('()=>window.__arMixRouteView().draftRoutes[1].mode'))=='NONE'
        await page.locator('[data-io-matrix-kind="inputs"][data-io-matrix-row="1"][data-io-matrix-pin="0"]').click()
        assert (await page.evaluate('()=>window.__arMixRouteView().draftRoutes[1].mode'))=='NONE'
        assert 'incomplete' in (await page.locator('#arIOFeedback').inner_text()).lower()
        await page.locator('[data-io-matrix-kind="inputs"][data-io-matrix-row="1"][data-io-matrix-pin="1"]').click()
        pair=await page.evaluate('()=>window.__arMixRouteView().draftRoutes[1]')
        assert pair['mode']=='STEREO' and pair['left']==0 and pair['right']==1,pair
        # Preserve the pre-test accepted route for later AUX negative tests.
        await page.evaluate('()=>window.__arMixRevertMixerDraft()')
        await page.locator('[data-io-tab="outputs"]').click()
        assert await page.locator('.arIOSimpleMain').is_visible()
        assert await page.locator('.arIOMainOutput').count()==2
        assert await page.locator('.arIOAdvancedMatrix').count()==1
        if not await page.locator('.arIOAdvancedMatrix').evaluate('(e)=>e.open'):
            await page.locator('.arIOAdvancedMatrix > summary').click()
        outputs=await page.locator('#arIOBody').inner_text()
        assert "HW OUT 1" in outputs and "HW OUT 2" in outputs,outputs
        assert await page.locator('[data-io-matrix-kind="outputs"]').count()==4
        # Swapping two occupied outputs is an atomic native draft, never L=R.
        await page.locator('[data-io-matrix-kind="outputs"][data-io-matrix-row="0"][data-io-matrix-pin="1"]').click()
        assert (await page.evaluate('()=>window.__arMixRouteView().draftMainOutputs'))==[1,0]
        duplicate=await page.evaluate('()=>window.__arMixStageMainOutputs(1,1)')
        assert duplicate['ok'] is False and 'different' in duplicate['error'],duplicate
        assert (await page.evaluate('()=>window.__arMixRouteView().draftMainOutputs'))==[1,0]
        await page.evaluate('()=>window.__arMixRevertMixerDraft()')
        assert "AUX1–4 are internal FX returns, not hardware ports" in outputs,outputs
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
        # G3 SENDS invokes FAST3 protected one-APPLY, so positive routing
        # ACK alone is NOT the whole transaction. The mock must complete
        # its subsequent native ARM ACK; otherwise unrelated actions remain
        # correctly rejected as 'transaction still pending'.
        status=await page.evaluate("""status=>{
          window.__armixTestEmit('arMixAudioArmAck',{
            ok:true,armed:true,deviceEpoch:status.dryRoutingLab.epoch});
          status.monitor=true;
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert not await page.evaluate("()=>window.__arMixFastAudioState().pending")
        # The AUX master is a separate native realtime parameter on the same
        # prepared graph, after AUX VST3 and before BOTH physical sinks.
        master=page.locator('[data-aux-master]')
        assert await master.is_enabled()
        await master.evaluate("(e)=>{e.value='-6';e.dispatchEvent(new Event('change',{bubbles:true}))}")
        param=await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingAuxBusGain')""")
        assert len(param)==1,param
        assert param[0]['data']['bus']==0 and param[0]['data']['revision']==5,param
        assert abs(param[0]['data']['value']-10**(-6/20))<.00001,param
        assert status['dryRoutingLab']['acceptedRoute']['auxes'][0]['gain']==1
        status=await page.evaluate("""status=>{
          const d=status.dryRoutingLab;
          d.acceptedRoute.auxes[0].gain=Math.pow(10,-6/20);
          window.__armixTestEmit('arMixAuxBusGainAck',{
            ok:true,bus:0,requestId:1,gain:d.acceptedRoute.auxes[0].gain,state:d});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert '-6.0' in await page.locator('[data-aux-master-read]').inner_text()
        assert await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply').length""")==2
        # Return AUX master to unity for the remaining legacy G3 tests.
        await master.evaluate("(e)=>{e.value='0';e.dispatchEvent(new Event('change',{bubbles:true}))}")
        status=await page.evaluate("""status=>{
          const d=status.dryRoutingLab;d.acceptedRoute.auxes[0].gain=1;
          window.__armixTestEmit('arMixAuxBusGainAck',{
            ok:true,bus:0,requestId:2,gain:1,state:d});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert await page.locator('#mixer [data-strip="0"] .rack .insert-slot').count()==6
        # MS5 operator journey: select one real AUX and flip visible channel
        # faders to POST sends without moving MAIN faders or remounting FX.
        await page.locator('#arSendEditor [data-send-close]').click()
        main_fader=await page.locator('[data-fader="0"]').get_attribute('aria-valuenow')
        await page.evaluate("()=>window.__sofMainFaderRef=document.querySelector('[data-fader=\"0\"]')")
        await page.locator('[data-sof-bus]').select_option('0')
        await page.locator('[data-sof-toggle]').click()
        assert await page.locator('#mixer').get_attribute('data-sof-active')=='true'
        assert await page.locator('[data-strip="0"] .arSofFader').is_visible()
        assert '-6.0' in await page.locator('[data-strip="0"] .db-display').evaluate(
            "(e)=>getComputedStyle(e,'::after').content")
        assert await page.locator('[data-fader="0"]').get_attribute('aria-hidden')=='true'
        await page.locator('[data-strip="0"] [data-sof-fader]').focus()
        await page.keyboard.press('End')
        # M1: fader move issues a LIVE parameter command, not a new
        # MUTE/APPLY topology request, and does not optimistically paint LIVE.
        live_actions=await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingAuxSendGain')""")
        assert len(live_actions)==1,live_actions
        # During the first native ACK flight, newer moves must only replace
        # one queued value. Returning to the in-flight target drops the queue.
        queued=await page.evaluate("""()=>({
          first:window.__arMixSetLiveAuxSendGain({from:0,bus:0,gain:.3}),
          second:window.__arMixSetLiveAuxSendGain({from:0,bus:0,gain:1}),
          count:window.__armixTestActions.filter(x=>
            x.name==='arMixAction'&&x.data.action==='routingAuxSendGain').length
        })""")
        assert queued['first']['queued'] and queued['second']['queued'] and queued['count']==1,queued
        gain_command=live_actions[-1]['data']
        assert gain_command['from']==0 and gain_command['bus']==0
        assert gain_command['deviceEpoch']==4 and gain_command['revision']==5
        assert abs(gain_command['value']-1)<1e-7
        before_ack=await page.evaluate("""()=>window.__arMixRouteView().draftSends.find(
          s=>s.from===0&&s.to===10&&s.tap==='POST').gain""")
        assert abs(before_ack-.5)<1e-7,before_ack
        status=await page.evaluate("""status=>{
          const d=status.dryRoutingLab;
          d.acceptedRoute.sends[0].gain=1;
          window.__armixTestEmit('arMixAuxSendGainAck',{
            ok:true,requestId:1,from:0,bus:0,gain:1,state:d});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        native_gain=await page.evaluate("""()=>window.__arMixRouteView().draftSends.find(
          s=>s.from===0&&s.to===10&&s.tap==='POST').gain""")
        assert abs(native_gain-1)<1e-7,native_gain
        # Restore .5 by another LIVE request and ACK, preserving the
        # remaining existing structural-draft regression suite.
        live_return=await page.evaluate("""()=>window.__arMixSetLiveAuxSendGain({
          from:0,bus:0,gain:.5})""")
        assert live_return['ok'],live_return
        status=await page.evaluate("""status=>{
          const d=status.dryRoutingLab;
          d.acceptedRoute.sends[0].gain=.5;
          window.__armixTestEmit('arMixAuxSendGainAck',{
            ok:true,requestId:2,from:0,bus:0,gain:.5,state:d});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert await page.locator('[data-fader="0"]').get_attribute('aria-valuenow')==main_fader
        assert await page.evaluate("()=>document.querySelector('[data-fader=\"0\"]')===window.__sofMainFaderRef")
        actions=await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply')""")
        assert len(actions)==2,actions  # live PARAM never triggered structural APPLY
        # EXIT or mode switch never commits a draft or alters MAIN.
        await page.locator('[data-sof-toggle]').click()
        assert await page.locator('#mixer').get_attribute('data-sof-active')=='false'
        assert await page.locator('[data-fader="0"]').get_attribute('aria-hidden') is None
        assert await page.evaluate('()=>window.__arMixRevertMixerDraft().ok')
        await page.locator('[data-strip="0"] [data-send-focus="0"]').click()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=='0.5'
        # M1 unified editor: existing POST LEVEL is LIVE while speakers
        # are armed. Topology remains protected by the muted APPLY gate.
        await page.evaluate("""status=>{
          status.monitor=true;window.__armixTestEmit("arMixStatus",status);
        }""",status)
        await page.locator('#arSendEditor [data-send-gain="POST"]').select_option("0.25")
        editor_actions=await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==="arMixAction"&&x.data.action==="routingAuxSendGain")""")
        assert len(editor_actions)==3,editor_actions
        assert abs(editor_actions[-1]['data']['value']-.25)<1e-7
        assert await page.locator('#arSendEditor [data-send-apply]').is_disabled()
        assert len(await page.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.name==="arMixAction"&&x.data.action==="routingDryApply")"""))==2
        status=await page.evaluate("""status=>{
          const d=status.dryRoutingLab;d.acceptedRoute.sends[0].gain=.25;
          window.__armixTestEmit("arMixAuxSendGainAck",{
            ok:true,requestId:3,from:0,bus:0,gain:.25,state:d});
          window.__armixTestEmit("arMixStatus",status);
          return status;
        }""",status)
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0.25"
        # Return gain using the same LIVE controller and native ACK.
        await page.locator('#arSendEditor [data-send-gain="POST"]').select_option("0.5")
        status=await page.evaluate("""status=>{
          const d=status.dryRoutingLab;d.acceptedRoute.sends[0].gain=.5;
          window.__armixTestEmit("arMixAuxSendGainAck",{
            ok:true,requestId:4,from:0,bus:0,gain:.5,state:d});
          window.__armixTestEmit("arMixStatus",status);
          return status;
        }""",status)
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0.5"
        # Keep the Python fixture synchronized with the JS status snapshot:
        # Playwright arguments are serialized, not references. Without this
        # assignment later ACK mocks accidentally re-ARM the previous output.
        status=await page.evaluate("""status=>{
          status.monitor=false;window.__armixTestEmit("arMixStatus",status);
          return status;
        }""",status)
        # Enable/disable is still structural, even for an existing POST edge.
        await page.locator('#arSendEditor [data-send-enable="POST"]').uncheck()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0"
        await page.locator('#arSendEditor [data-send-enable="POST"]').check()
        assert await page.locator('#arSendEditor [data-send-gain="POST"]').input_value()=="0.5"
        await page.locator('#arSendEditor [data-send-revert]').click()
        # Reject a structural PAN draft: native graph must stay unchanged.
        accepted_pan=await page.locator('#arSendEditor [data-send-pan="POST"]').input_value()
        await page.locator('#arSendEditor [data-send-pan="POST"]').select_option("0.5")
        await page.locator('#arSendEditor [data-send-apply]').click()
        await page.evaluate("""status=>{
          window.__armixTestEmit("arMixRoutingLabAck",
            {ok:false,error:"Stale graph revision",state:status.dryRoutingLab});
        }""",status)
        assert "REJECTED" in await page.locator('#arSendFeedback').inner_text()
        assert await page.locator('#arSendEditor [data-send-pan="POST"]').input_value()=="0.5"
        await page.locator('#arSendEditor [data-send-revert]').click()
        assert await page.locator('#arSendEditor [data-send-pan="POST"]').input_value()==accepted_pan
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
        # R7 format picker auto-submits the existing guarded one-APPLY
        # transaction. A second manual APPLY is correctly rejected while
        # that first native request is in flight, so do NOT submit twice.
        assert await page.evaluate("()=>window.__arMixFastAudioState().pending")
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
        assert "PENDING" in await page.locator('[data-strip="1"] .arBankStage').inner_text()
        status=await page.evaluate("""status=>{
          const payload=window.__armixTestActions.filter(x=>
            x.name==='arMixAction'&&x.data.action==='routingDryApply').at(-1).data.value;
          status.dryRoutingLab.revision=6;
          status.dryRoutingLab.acceptedRoute=payload;
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:status.dryRoutingLab});
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""",status)
        assert "ROUTE ACK" in await page.locator('[data-strip="1"] .arBankStage').inner_text()
        # Accepted source automatically begins the final guarded hardware
        # ARM. Reject ARM deliberately for this G5 structural/FX fixture:
        # the new source route remains accepted, output remains safely
        # MUTED, and the next edit is not blocked by an unanswered ACK.
        assert await page.evaluate("()=>window.__arMixFastAudioState().pending")
        await page.evaluate("""status=>window.__armixTestEmit('arMixAudioArmAck',{
          ok:false,armed:false,deviceEpoch:status.dryRoutingLab.epoch,
          error:'Test output intentionally kept muted for FX edits'
        })""",status)
        assert not await page.evaluate("()=>window.__arMixFastAudioState().pending")
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
        reverted=await page.evaluate("()=>window.__arMixRevertMixerDraft()")
        assert reverted['ok'],reverted
        assert await page.evaluate("""()=>window.__arMixFxLaneView(1).ids.join('|')""")=="eq-pro|rv-pro"
        assert await page.locator('[data-strip="1"] .rack .insert-slot').count()==6
        # G5C native instance tokens are authoritative: the row power must
        # emit a fenced control event, not set a fake local bypass lamp.
        status=await page.evaluate("""status=>{
          const lanes=Array.from({length:14},()=>[]);
          lanes[1]=[
            {instanceId:'g5c-101',catalogId:'eq-pro',bypassed:false},
            {instanceId:'g5c-102',catalogId:'rv-pro',bypassed:false}];
          status.dryRoutingLab.fxInstances=lanes;
          window.__armixTestEmit('arMixRoutingFxBypassAck',
            {ok:true,state:status.dryRoutingLab});
          return status;
        }""",status)
        assert await page.locator('[data-strip="1"] [data-routed-bypass]').count()==2
        assert await page.locator('[data-strip="1"] .rack .fx-power[data-routed-bypass]').count()==2
        assert await page.locator('[data-strip="1"] .rack .native-power').count()==0
        await page.locator('[data-strip="1"] [data-routed-bypass]').first.click()
        fx_cmd=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingFxBypass').at(-1).data""")
        assert fx_cmd['channel']==1 and fx_cmd['slot']==0,fx_cmd
        assert fx_cmd['instanceId']=='g5c-101' and fx_cmd['value'] is True,fx_cmd
        assert fx_cmd['deviceEpoch']==4 and fx_cmd['revision']==status['dryRoutingLab']['revision'],fx_cmd
        status=await page.evaluate("""status=>{
          status.dryRoutingLab.fxInstances[1][0].bypassed=true;
          window.__armixTestEmit('arMixRoutingFxBypassAck',
            {ok:true,state:status.dryRoutingLab});
          return status;
        }""",status)
        assert await page.locator('[data-strip="1"] [data-routed-bypass]').first.get_attribute('aria-pressed')=='false'
        assert await page.evaluate("""()=>window.__arMixFxLaneView(1).liveSlots[0].bypassed""") is True
        # G5D: FX RACK editor opens the exact committed vendor instance, not
        # a catalog preview or stale/position-only CH1 editor.
        await page.evaluate("()=>focusChannel(1)")
        await page.locator('[data-view="FX RACK"]').click()
        await page.wait_for_function("""()=>document.querySelector(
          '#rackWorkspace [data-routed-editor="1"][data-routed-slot="0"]')!==null""")
        editor_button=page.locator('#rackWorkspace [data-routed-editor="1"][data-routed-slot="0"]')
        assert await editor_button.is_enabled()
        before_editor=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingFxEditor').length""")
        await editor_button.click()
        editor=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingFxEditor').at(-1).data""")
        assert editor['channel']==1 and editor['slot']==0,editor
        assert editor['instanceId']=='g5c-101' and editor['deviceEpoch']==4,editor
        assert editor['revision']==status['dryRoutingLab']['revision'],editor
        await page.evaluate("""()=>window.__armixTestEmit('arMixRoutingFxEditorAck',
          {ok:true,instanceId:'g5c-101'})""")
        assert await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingFxEditor').length""")==before_editor+1
        # The synthetic driver must ACK/REJECT any pending FAST2 hardware
        # ARM before another structural route APPLY. Real native hosts send
        # this event; the smoke fixture does not unless we explicitly emit it.
        await page.evaluate("""()=>{
          if(window.__arMixFastAudioState?.().pending)
            window.__armixTestEmit('arMixAudioArmAck',{
              ok:false,armed:false,deviceEpoch:4,error:'synthetic ARM rejection'});
        }""")
        assert not await page.evaluate("()=>window.__arMixFastAudioState?.().pending")
        # MS3A: changing MAIN physical destination is one native APPLY,
        # not a UI preview and never silently unmutes audio.
        await page.evaluate("""status=>{
          status.dryRoutingLab.outputPorts=[0,1,4,5];
          status.dryRoutingLab.mainOutputs=[0,1];
          window.__armixTestEmit('arMixStatus',status);
        }""",status)
        await page.locator('[data-io-open]').click()
        await page.locator('[data-io-tab="outputs"]').click()
        assert await page.locator('.arIOSimpleMain').is_visible()
        assert await page.locator('.arIOMainOutput').count()==2
        # Crosspoint remains available, but is no longer the normal workflow.
        if not await page.locator('.arIOAdvancedMatrix').evaluate('(e)=>e.open'):
            await page.locator('.arIOAdvancedMatrix > summary').click()
        # Exercise actual sparse-pin OUTPUT crosspoints, not hidden fallback dropdowns.
        await page.locator('[data-io-matrix-kind="outputs"][data-io-matrix-row="0"][data-io-matrix-pin="4"]').click()
        await page.locator('[data-io-matrix-kind="outputs"][data-io-matrix-row="1"][data-io-matrix-pin="5"]').click()
        assert await page.evaluate("""()=>window.__arMixRouteView().draftMainOutputs.join(',')""")=='4,5'
        # MS3B: enable real independent MONITOR only on unused physical HW1/2.
        await page.locator('[data-io-monitor-toggle="on"]').click()
        assert await page.locator('[data-io-matrix-kind="outputs"]').count()==16
        assert await page.evaluate("""()=>window.__arMixRouteView().draftMonitorOutputs""")==[0,1]
        await page.locator('[data-io-matrix-kind="outputs"][data-io-matrix-row="2"][data-io-matrix-pin="4"]').click()
        assert await page.evaluate("""()=>window.__arMixRouteView().draftMonitorOutputs""")==[0,1]
        await page.locator('.arIOMonitorSends summary').click()
        await page.locator('.arIOMonitorSend[data-lane="0"]').select_option('0.5')
        assert await page.evaluate("""()=>window.__arMixRouteView().draftMonitorSends[0]""")==0.5
        assert await page.evaluate("""()=>window.__arMixRouteView().monitorOutputs""")==[]
        await page.locator('#arIOOverlay [data-mix-apply]').click()
        command=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').at(-1).data.value""")
        debug=await page.evaluate("""()=>({
          fast:window.__arMixFastAudioState?.(),
          route:window.__arMixRouteView?.(),
          feedback:document.querySelector('#arIOFeedback')?.textContent,
          events:window.__armixTestActions.filter(x=>x.name==='arMixAction').slice(-6)
        })""")
        assert command['mainOutputs']==[4,5],(command,debug)
        assert command['monitorOutputs']==[0,1],command
        assert len(command['monitorSends'])==14 and command['monitorSends'][0]==.5,command
        assert all(x==0 for x in command['monitorSends'][1:]),command

        # PATCH1: real-world operator acceptance using a fake native driver
        # inventory. Sparse MAONO virtual pins must never be invented by UI.
        await page.goto((root/"web/index.html").as_uri())
        status=await page.evaluate("""() => {
          const channels=Array.from({length:10},()=>({
            mode:'NONE',left:-1,right:-1,channelFormat:'MONO',
            gain:.25,pan:0,main:true,mute:false,solo:false,fxId:''}));
          const auxes=Array.from({length:4},()=>({
            enabled:false,gain:1,main:true,mute:false,fxId:''}));
          const dryRoutingLab={active:true,ready:true,epoch:21,revision:3,
            ports:[{port:0,label:'MAONO Mic In 1'},
              {port:1,label:'MAONO Mic In 2'},
              {port:4,label:'MAONO VC In L'},
              {port:7,label:'MAONO VC In R'}],
            outputPorts:[0,1],mainOutputs:[0,1],
            acceptedRoute:{deviceEpoch:21,channels,auxes,sends:[]}};
          const status={connected:true,driver:'ASIO · MAONO PS22 MOCK',
            rate:48000,buffer:128,monitor:false,chainCount:0,dryRoutingLab};
          window.__armixTestEmit('arMixStatus',status);
          return status;
        }""")
        await page.locator('#audioSetupBtn').click()
        await page.locator('[data-io-tab="inputs"]').click()
        assert await page.locator('.arIOQuickPatch').is_visible()
        assert await page.locator('[data-io-quick-row]').count()==10
        await page.locator('[data-io-jump]').select_option('2')
        assert await page.locator('[data-io-quick-row]:visible').count()==1
        assert await page.locator('[data-io-quick-row="2"] [data-io-quick-pin="left"] option').count()==5
        # R7: each USE INPUT commits the existing guarded native route
        # automatically, without a second STAGE/MUTE/APPLY button ritual.
        # Complete each ACK before selecting the next channel; overlapping
        # structural requests must remain rejected until the prior finishes.
        ch3=page.locator('[data-io-quick-row="2"]')
        await ch3.locator('[data-io-quick-mode]').select_option('MONO')
        await ch3.locator('[data-io-quick-pin="left"]').select_option('0')
        await ch3.locator('[data-io-quick-stage]').click()
        draft=await page.evaluate('()=>window.__arMixRouteView().draftRoutes[2]')
        assert draft['mode']=='MONO' and draft['left']==0,draft
        sent=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply')""")
        assert len(sent)==1,sent
        first=sent[0]['data']['value']
        assert first['deviceEpoch']==21 and first['revision']==3,first
        assert first['channels'][2]['mode']=='MONO' and first['channels'][2]['left']==0,first
        status=await page.evaluate("""status=>{
          const lab=status.dryRoutingLab;
          const payload=window.__armixTestActions.filter(x=>
            x.data?.action==='routingDryApply').at(-1).data.value;
          lab.revision=4;lab.hasAcceptedRoute=true;
          lab.acceptedRoute={deviceEpoch:21,channels:payload.channels,auxes:payload.auxes,
            sends:payload.sends};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:lab});
          window.__armixTestEmit('arMixStatus',status);return status;
        }""",status)
        assert 'ROUTE ACK' in await ch3.locator('.arIOQuickState').inner_text()
        await page.evaluate("""status=>window.__armixTestEmit('arMixAudioArmAck',{
          ok:false,armed:false,deviceEpoch:status.dryRoutingLab.epoch,
          error:'Output kept safely muted during multi-source setup'
        })""",status)
        assert not await page.evaluate("()=>window.__arMixFastAudioState().pending")
        # Sparse driver pins 5/8 -> CH6 stereo, independent of CH3's
        # accepted mono processing and with no fabricated adjacent R pin.
        ch6=page.locator('[data-io-quick-row="5"]')
        await page.locator('[data-io-jump]').select_option('5')
        assert await ch6.is_visible()
        assert await page.locator('[data-io-quick-row]:visible').count()==1
        await ch6.locator('[data-io-quick-mode]').select_option('STEREO')
        await ch6.locator('[data-io-quick-pin="left"]').select_option('4')
        await ch6.locator('[data-io-quick-pin="right"]').select_option('7')
        await ch6.locator('[data-io-quick-stage]').click()
        draft=await page.evaluate('()=>window.__arMixRouteView().draftRoutes[5]')
        assert draft['mode']=='STEREO' and draft['left']==4 and draft['right']==7,draft
        sent=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply')""")
        assert len(sent)==2,sent
        second=sent[-1]['data']['value']
        assert second['deviceEpoch']==21 and second['revision']==4,second
        assert second['channels'][2]['mode']=='MONO' and second['channels'][2]['left']==0,second
        assert second['channels'][5]['mode']=='STEREO' and second['channels'][5]['left']==4 and second['channels'][5]['right']==7,second
        status=await page.evaluate("""status=>{
          const lab=status.dryRoutingLab;
          const payload=window.__armixTestActions.filter(x=>
            x.data?.action==='routingDryApply').at(-1).data.value;
          lab.revision=5;lab.hasAcceptedRoute=true;
          lab.acceptedRoute={deviceEpoch:21,channels:payload.channels,auxes:payload.auxes,
            sends:payload.sends};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:lab});
          window.__armixTestEmit('arMixStatus',status);return status;
        }""",status)
        assert 'ROUTE ACK' in await ch6.locator('.arIOQuickState').inner_text()
        await page.evaluate("""status=>window.__armixTestEmit('arMixAudioArmAck',{
          ok:false,armed:false,deviceEpoch:status.dryRoutingLab.epoch,
          error:'Output intentionally stays muted in this fixture'
        })""",status)
        assert not await page.evaluate("()=>window.__arMixFastAudioState().pending")
        # M1: a user-defined sparse stereo pair is selectable in CHANNEL SOURCE
        # only when the real device identity and *all* active pins match.
        await page.evaluate("""()=>window.__armixTestEmit('arMixAudioDeviceInventory',{
          currentType:'ASIO',
          active:{backend:'ASIO',captureDevice:'MAONO PS22',playbackDevice:'MAONO PS22',
            open:true,deviceGeneration:51,enabledInputs:[0,1,4,7],
            enabledOutputs:[0,1],outputNames:['MAONO Main L','MAONO Main R'],
            inputNames:['MAONO Mic In 1','MAONO Mic In 2','Unused 3','Unused 4',
              'MAONO VC In L','Unused 6','Unused 7','MAONO VC In R']},
          types:[{name:'ASIO',active:true,separate:false,
            inputs:['MAONO PS22'],outputs:['MAONO PS22']}]})""")
        # DEVICE4: name one logical stereo INPUT over sparse verified hardware,
        # then rename and remove it. Metadata must not emit routingDryApply.
        await page.locator('[data-io-tab="device"]').click()
        if not await page.locator('.arIODevicePatchMatrix').evaluate('(e)=>e.open'):
            await page.locator('.arIODevicePatchMatrix > summary').click()
        assert await page.locator('.arIOSoftwareGrid').is_visible()
        count_before=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').length""")
        await page.locator('[data-io-software-add="STEREO"]').click()
        await page.locator('.arIOSoftwareNameEdit').fill('Studio USB Music')
        await page.locator('[data-io-software-pin="4"][data-io-software-index="-1"]').click()
        await page.locator('[data-io-software-action="save"]').click()
        assert 'INCOMPLETE' in await page.locator('#arIOFeedback').inner_text()
        await page.locator('[data-io-software-pin="7"][data-io-software-index="-1"]').click()
        await page.locator('[data-io-software-action="save"]').click()
        assert await page.locator('.arIOSoftwareName strong').get_by_text('Studio USB Music').count()==1
        assert await page.locator('.arIOSoftwareGrid [data-stage="saved"][data-assigned="true"]').count()==2
        await page.locator('[data-io-software-action="edit"][data-io-software-index="0"]').click()
        await page.locator('.arIOSoftwareNameEdit').fill('Studio PC Music')
        await page.locator('[data-io-software-action="save"]').click()
        assert await page.locator('.arIOSoftwareName strong').get_by_text('Studio PC Music').count()==1
        # Direct hardware crosspatch remains usable across stereo side changes.
        await page.locator('.arIODeviceDirectMatrix summary').click()
        assert await page.locator('.arIODeviceDirectMatrix').evaluate('(e)=>e.open')
        await page.locator('.arIODeviceDirectMatrix [data-io-matrix-side="R"][data-io-matrix-row="5"]').click()
        assert await page.locator('.arIODeviceDirectMatrix').evaluate('(e)=>e.open')
        await page.locator('[data-io-software-action="remove"][data-io-software-index="0"]').click()
        assert await page.locator('.arIOSoftwareName strong').get_by_text('Studio PC Music').count()==0
        count_after=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply').length""")
        assert count_before==count_after,(count_before,count_after)
        await page.locator('#arIOOverlay [data-io-close]').click()
        await page.locator('[data-focus-step="source"]').click()
        await page.locator('.arSourceAliasManager summary').click()
        await page.locator('[data-source-alias-name]').fill('MAONO Virtual Stereo')
        await page.locator('[data-source-alias-mode]').select_option('STEREO')
        await page.locator('[data-source-alias-left]').select_option('4')
        await page.locator('[data-source-alias-right]').select_option('7')
        assert await page.locator('[data-source-alias-save]').is_enabled()
        await page.locator('[data-source-alias-save]').click()
        named=page.locator('[data-source-choice="STEREO:4:7"]')
        assert await named.count()==1
        assert 'MAONO Virtual Stereo' in await named.inner_text()
        selected=await page.evaluate("""()=>Number(
          document.querySelector('.strip[data-strip].selected')?.dataset.strip||0)""")
        await named.click()
        native=await page.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.name==='arMixAction'&&x.data.action==='routingDryApply')""")
        # A third source choice from the selected channel auto-submits one
        # more guarded transaction; metadata edits did NOT submit anything.
        assert len(native)==count_after+1,native
        target=native[-1]['data']['value']['channels'][selected]
        assert target['mode']=='STEREO' and target['left']==4 and target['right']==7,target
        status=await page.evaluate("""status=>{
          const lab=status.dryRoutingLab;
          const cmd=window.__armixTestActions.filter(x=>
            x.data?.action==='routingDryApply').at(-1).data.value;
          lab.revision=cmd.revision+1;lab.hasAcceptedRoute=true;
          lab.acceptedRoute={deviceEpoch:lab.epoch,channels:cmd.channels,
            auxes:cmd.auxes,sends:cmd.sends};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:lab});
          window.__armixTestEmit('arMixStatus',status);return status;
        }""",status)
        assert not await page.evaluate("()=>window.__arMixRouteView().pending")
        await page.evaluate("""status=>window.__armixTestEmit('arMixAudioArmAck',{
          ok:false,armed:false,deviceEpoch:status.dryRoutingLab.epoch,
          error:'Output remains muted for device-rebind negative test'
        })""",status)
        assert not await page.evaluate("()=>window.__arMixFastAudioState().pending")
        assert 'MAONO Virtual Stereo' in await page.locator('[data-focus-source]').inner_text()
        # An altered pin mask must NOT inherit the saved alias or silently remap.
        await page.evaluate("""()=>window.__armixTestEmit('arMixAudioDeviceInventory',{
          currentType:'ASIO',
          active:{backend:'ASIO',captureDevice:'MAONO PS22',playbackDevice:'MAONO PS22',
            open:true,deviceGeneration:52,enabledInputs:[0,1,4],
            inputNames:['MAONO Mic In 1','MAONO Mic In 2','Unused 3','Unused 4','MAONO VC In L']},
          types:[{name:'ASIO',active:true,separate:false,
            inputs:['MAONO PS22'],outputs:['MAONO PS22']}]})""")
        # The old pin route may still appear as CUSTOM for inspection; the
        # device-bound named alias itself MUST disappear on mask mismatch.
        assert await page.locator('.arSourceAliasRow [data-source-choice="STEREO:4:7"]').count()==0
        assert await page.locator('[data-source-alias-save]').is_disabled()
        await page.screenshot(path="mixer-patch1-maono-ch3-ch6-1366.png")
        assert not errors,errors
        # M2B independent browser-native routing graph (same browser process):
        # create group, ACK, route a second CH into that same existing BUS.
        bp=await browser.new_page(viewport={"width":1366,"height":768})
        bus_errors=[]
        bp.on("pageerror",lambda e:bus_errors.append(str(e)))
        await bp.add_init_script("""(()=>{
          const listeners={};window.__armixTestActions=[];
          window.__JUCE__={backend:{
            addEventListener(name,fn){(listeners[name]??=[]).push(fn)},
            emitEvent(name,data){window.__armixTestActions.push({name,data})}
          }};
          window.__armixTestEmit=(name,data)=>(listeners[name]||[]).forEach(fn=>fn(data));
        })();""")
        await bp.goto((root/"web/index.html").as_uri())
        await bp.evaluate("""()=>{
          const channels=Array.from({length:10},(_,i)=>({
            mode:i<2?'MONO':'NONE',channelFormat:'MONO',left:i<2?i:-1,
            right:-1,gain:.25,pan:0,main:true,mute:false,solo:false,
            fxId:'',fxIds:[]}));
          const auxes=Array.from({length:4},()=>({
            enabled:false,gain:1,main:true,mute:false,fxId:'',fxIds:[]}));
          const d={active:true,ready:true,epoch:45,revision:3,
            ports:[{port:0,label:'Mic 1'},{port:1,label:'Mic 2'}],
            outputPorts:[0,1],channelControls:[],
            acceptedRoute:{deviceEpoch:45,channels,auxes,sends:[]}};
          window.__busDevice={connected:true,driver:'ASIO',
            monitor:false,chainCount:0,dryRoutingLab:d};
          window.__armixTestEmit('arMixStatus',window.__busDevice);
        }""")
        created=await bp.evaluate("""()=>({
          action:window.__arMixStageBusFromSelected({source:0,kind:'group'}),
          draft:window.__arMixRouteView()})""")
        assert created['action']['ok'],created
        bus=created['action']['target']
        assert created['draft']['draftRoutes'][bus]['mode']=='BUS',created
        assert created['draft']['draftRoutes'][bus]['gain']==1,created
        assert created['draft']['draftRoutes'][0]['main'] is False,created
        assert not await bp.evaluate("""()=>window.__armixTestActions.some(
          x=>x.data?.action==='routingDryApply')""")
        await bp.locator('#arRouteApply').click()
        assert await bp.evaluate("""()=>window.__armixTestActions.some(
          x=>x.data?.action==='routingDryApply')""")
        await bp.evaluate("""()=>{
          const view=window.__arMixRouteView();
          const d=window.__busDevice.dryRoutingLab;
          d.acceptedRoute={...d.acceptedRoute,
            channels:view.draftRoutes,auxes:view.draftAux,sends:view.draftSends};
          d.revision=4;
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:d});
          window.__armixTestEmit('arMixStatus',window.__busDevice);
        }""")
        await bp.locator('#arRouteClose').click()
        await bp.locator('[data-view="FX RACK"]').click()
        await bp.evaluate('()=>{currentChannel=1;renderRackWorkspace()}')
        assert await bp.locator('#rackWorkspace [data-bus-target] option').count()==1
        assert await bp.locator('#rackWorkspace [data-bus-target]').input_value()==str(bus)
        # Existing BUS accepts a second input; no new strip is allocated.
        await bp.locator('#rackWorkspace [data-bus-assign="group"]').click()
        send=await bp.evaluate('()=>window.__arMixRouteView()')
        assert send['draftRoutes'][1]['main'] is False,send
        assert send['draftRoutes'][bus]['mode']=='BUS',send
        assert len(send['draftSends'])==2,send
        assert any(e['from']==1 and e['to']==bus and e['gain']==1
                   for e in send['draftSends']),send
        assert len(await bp.evaluate("""()=>window.__armixTestActions.filter(
          x=>x.data?.action==='routingDryApply')"""))==1
        assert (await bp.evaluate('()=>window.__arMixRevertMixerDraft()'))['ok']
        await bp.locator('#arRouteClose').click()
        await bp.locator('[data-view="FX RACK"]').click()
        await bp.evaluate('()=>{currentChannel=1;renderRackWorkspace()}')
        await bp.locator('#rackWorkspace [data-bus-assign="fx"]').click()
        fxsend=await bp.evaluate('()=>window.__arMixRouteView()')
        assert fxsend['draftRoutes'][1]['main'] is True,fxsend
        assert any(e['from']==1 and e['to']==bus and e['gain']==.25
                   for e in fxsend['draftSends']),fxsend
        assert (await bp.evaluate('()=>window.__arMixRevertMixerDraft()'))['ok']
        self_cycle=await bp.evaluate("""target=>window.__arMixStageSendToBus({
          source:target,target,kind:'fx'})""",bus)
        assert not self_cycle['ok'] and 'Feedback' in self_cycle['error']
        duplicate=await bp.evaluate("""target=>window.__arMixStageSendToBus({
          source:0,target,kind:'fx'})""",bus)
        assert not duplicate['ok'] and 'already' in duplicate['error']
        assert not bus_errors,bus_errors
        await bp.close()
        # FAST1 first-boot path: no user patch yet. A single click stages
        # CH1/CH2 once, verifies revisioned ACK, THEN emits ARM. No extra engine.
        qp=await browser.new_page(viewport={"width":1366,"height":768})
        await qp.add_init_script("""(()=>{
          const listeners={};window.__armixTestActions=[];
          window.__JUCE__={backend:{
            addEventListener(k,f){(listeners[k]??=[]).push(f)},
            emitEvent(k,d){window.__armixTestActions.push({name:k,data:d})}
          }};
          window.__armixTestEmit=(k,d)=>(listeners[k]||[]).forEach(f=>f(d));
        })()""")
        await qp.goto((root/"web/index.html").as_uri())
        init=await qp.evaluate("""()=>{
          const channels=Array.from({length:10},()=>({mode:'NONE',left:-1,right:-1,
            channelFormat:'MONO',gain:.25,pan:0,main:true,mute:false,solo:false,fxId:''}));
          const auxes=Array.from({length:4},()=>({enabled:false,gain:1,main:true,mute:false,fxId:''}));
          const lab={active:true,ready:true,epoch:31,revision:2,hasAcceptedRoute:false,
            ports:[{port:0,label:'Mic A'},{port:1,label:'Mic B'}],
            outputPorts:[0,1],mainOutputs:[0,1],
            acceptedRoute:{deviceEpoch:31,channels,auxes,sends:[]}};
          const status={connected:true,directRouteAuthorized:true,driver:'ASIO',
            rate:48000,buffer:128,monitor:false,masterMute:false,chainCount:0,dryRoutingLab:lab};
          window.__quickBoot=status;window.__armixTestEmit('arMixStatus',status);return status;
        }""")
        await qp.locator('#audioSetupBtn').click()
        await qp.evaluate("""()=>window.__armixTestEmit('arMixAudioDeviceInventory',{
          currentType:'ASIO',active:{backend:'ASIO',captureDevice:'Mock Interface',playbackDevice:'Mock Interface',
            open:true,authorized:true,deviceGeneration:6,enabledInputs:[0,1],enabledOutputs:[0,1],
            inputNames:['Mic A','Mic B'],outputNames:['Main L','Main R']},
          types:[{name:'ASIO',active:true,separate:false,
            inputs:['Mock Interface'],outputs:['Mock Interface']}]
        })""")
        await qp.locator('[data-io-device-apply]').click()
        requests=await qp.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.data?.action==='routingDryApply')""")
        assert len(requests)==1,requests
        req=requests[0]['data']['value']
        assert req['channels'][0]['mode']=='MONO' and req['channels'][0]['left']==0,req
        assert req['channels'][1]['mode']=='MONO' and req['channels'][1]['left']==1,req
        assert await qp.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.data?.action==='monitor'&&x.data.value===true).length""")==0
        await qp.evaluate("""()=>{const d=window.__quickBoot.dryRoutingLab;
          const req=window.__armixTestActions.find(x=>x.data?.action==='routingDryApply').data.value;
          d.revision=3;d.hasAcceptedRoute=true;
          d.acceptedRoute={deviceEpoch:31,channels:req.channels,auxes:req.auxes,sends:req.sends};
          window.__armixTestEmit('arMixRoutingLabAck',{ok:true,state:d});
          window.__armixTestEmit('arMixStatus',window.__quickBoot);
        }""")
        armed=await qp.evaluate("""()=>window.__armixTestActions.filter(x=>
          x.data?.action==='monitor'&&x.data.value===true)""")
        assert len(armed)==1 and armed[0]['data']['expectedEpoch']==31,armed
        assert armed[0]['data']['expectedRevision']==3,armed
        await qp.close()

        # Field screenshot regression: one enabled VB-Cable/recording pin may
        # ONLY be selected as mono. Do not fabricate a second R endpoint.
        one=await browser.new_page(viewport={"width":1366,"height":768})
        await one.add_init_script("""(() => {
          const listeners={};
          window.__armixTestActions=[];
          window.__JUCE__={backend:{
            addEventListener(name,fn){(listeners[name]??=[]).push(fn)},
            emitEvent(name,data){window.__armixTestActions.push({name,data})}
          }};
          window.__armixTestEmit=(name,datum)=>(listeners[name]||[]).forEach(fn=>fn(datum));
        })();""")
        await one.goto((root/"web/index.html").as_uri())
        await one.evaluate("""() => {
          const channels=Array.from({length:10},()=>({
            mode:'NONE',left:-1,right:-1,gain:.25,pan:0,main:true,
            mute:false,solo:false,fxId:''}));
          const auxes=Array.from({length:4},()=>({
            enabled:false,gain:1,main:true,mute:false,fxId:''}));
          const lab={active:true,ready:true,epoch:51,revision:3,
            ports:[{port:0,label:'CABLE Output capture pin 1'}],
            outputPorts:[0,1],
            acceptedRoute:{deviceEpoch:51,channels,auxes,sends:[]}};
          window.__armixTestEmit('arMixStatus',{
            connected:true,directRouteAuthorized:true,driver:'Windows Audio',
            rate:48000,buffer:480,monitor:false,masterMute:false,
            chainCount:0,dryRoutingLab:lab});
          window.__armixTestEmit('arMixAudioDeviceInventory',{
            currentType:'Windows Audio',
            active:{backend:'Windows Audio',
              captureDevice:'CABLE Output (VB-Audio Virtual Cable)',
              playbackDevice:'Realtek HD Audio 2nd output',open:true,
              authorized:true,deviceGeneration:51,enabledInputs:[0],
              enabledOutputs:[0,1],inputNames:['CABLE recording'],
              outputNames:['Speakers L','Speakers R']},
            types:[{name:'Windows Audio',active:true,separate:true,
              inputs:['CABLE Output (VB-Audio Virtual Cable)'],
              outputs:['Realtek HD Audio 2nd output']}]});
        }""")
        await one.locator('#audioSetupBtn').click()
        assert await one.locator('.arIODevicePin').count()==3
        assert 'MONO ONLY' in await one.locator('.arIOPinHeading').inner_text()
        assert not await one.locator('.arIODevicePatchMatrix').evaluate('(e)=>e.open')
        await one.locator('[data-io-tab="inputs"]').click()
        assert await one.locator('[data-io-quick-row]:visible').count()==1
        ch1=one.locator('[data-io-quick-row="0"]')
        assert await ch1.locator('[data-io-quick-mode] option[value="STEREO"]').is_disabled()
        await ch1.locator('[data-io-quick-mode]').select_option('MONO')
        await ch1.locator('[data-io-quick-pin="left"]').select_option('0')
        await ch1.locator('[data-io-quick-stage]').click()
        sent=await one.evaluate("""() => window.__armixTestActions.filter(
          x=>x.name==='arMixAction'&&x.data.action==='routingDryApply')""")
        assert len(sent)==1,sent
        assert sent[0]['data']['value']['channels'][0]['mode']=='MONO',sent
        assert sent[0]['data']['value']['channels'][0]['left']==0,sent
        assert sent[0]['data']['value']['channels'][0]['right']==-1,sent
        # A pending routing command does not claim input PCM, MAIN sound, or
        # output ARM. These require native acceptance and capture telemetry.
        assert not await one.evaluate("""() => window.__armixTestActions.some(
          x=>x.data?.action==='monitor'&&x.data.value===true)""")
        await one.screenshot(path='audio-setup-one-pin-1366.png')
        await one.close()
        await browser.close()
        print("PASS: G1/G2 + G3 real PRE/POST draft, AUX return, 0-gain bypass, native ACK/reject/revert, stable rack and 3 viewports")

if __name__=="__main__":
    asyncio.run(main())
