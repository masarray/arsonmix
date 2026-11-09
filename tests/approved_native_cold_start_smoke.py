"""Real compiled DOM from G0: first drag without OUT priming, first native
FX picker insert on original CH with structural=false, then vendor editor.
Backend isolation is emulated; actual latency/sample evidence is CTest.
"""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE, RECEIVER, MAIN

ROOT = Path(__file__).resolve().parents[1]


def drag(page, source, destination):
    a = page.locator(f'#tracks .strip[data-id="{source}"] .wire-node-out').bounding_box()
    b = page.locator(f'#tracks .strip[data-id="{destination}"] .wire-node-in').bounding_box()
    assert a and b
    page.mouse.move(a['x'] + a['width']/2, a['y'] + a['height']/2)
    page.mouse.down()
    page.mouse.move(b['x'] + b['width']/2, b['y'] + b['height']/2, steps=12)
    page.mouse.up()


def main():
    spec = importlib.util.spec_from_file_location('bundle', ROOT/'scripts/build_approved_native_gui.py')
    bundle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory() as directory, sync_playwright() as p:
        out = Path(directory)
        bundle.stage(out)
        browser = p.chromium.launch()
        for width, height in [(1366,768),(1680,900),(1920,1080)]:
            page = browser.new_page(viewport={'width':width,'height':height})
            errors=[]
            page.on('pageerror', lambda e:errors.append(str(e)))
            page.add_init_script("""window.__nativeListeners=new Map();window.__events=[];window.__nativeIntents=[];
              window.__JUCE__={backend:{emitEvent(name,payload){window.__events.push({name,payload});
                if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);},
                addEventListener(name,callback){window.__nativeListeners.set(name,callback);}}};""")
            page.goto((out/'index.html').as_uri())
            page.wait_for_function('()=>window.__ARMIX_NATIVE_M2__?.enabled')
            g0=json.loads((ROOT/'integration/console-v6/fixtures.json').read_text())['g0']
            g0['mixerSession']={'authority':'NONE','deviceEpoch':g0['device']['epoch'],'channels':[]}
            g0['legacyM4']={'active':True,'routeAccepted':False,'routingRevision':'1'}
            emit(page,'arMixConsoleSnapshot',g0)
            add_track=page.locator('.command-bar #addTrack')
            assert add_track.is_enabled()
            add_track.click()
            create='()=>window.__events.filter(e=>e.payload?.action==="routingNativeTrackCreate")'
            bootstrap=page.evaluate(create)[-1]['payload']
            assert bootstrap['bootstrapRevision']=='1'
            assert bootstrap['deviceEpoch']==g0['device']['epoch']
            assert add_track.is_disabled()
            emit(page,'arMixNativeBusAck',{'requestId':bootstrap['requestId'],
                'ok':False,'error':'Test stale starter graph'})
            assert add_track.is_enabled()
            # Native-only Studio controls must use real JUCE Show I/O; the
            # public Pages scene model must never acknowledge a persisted Show.
            show_panel = "#moreTools .more-tools-panel"
            show_id = "11111111-2222-4333-8444-555555555555"
            corrupt_id = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
            def show_click(text):
                page.evaluate("""([selector,text])=>{
                  const buttons=[...document.querySelectorAll(selector+' button')];
                  const button=buttons.find(b=>b.textContent===text);
                  if(!button)throw Error('Missing native Show button: '+text);
                  button.click();
                }""",[show_panel,text])
            def show_select(value):
                return page.evaluate("""([selector,value])=>{
                  const node=document.querySelector(selector+' select[aria-label="Saved session"]');
                  if(!node)throw Error('Native Show selector absent');
                  node.value=value;node.dispatchEvent(new Event('change'));
                  const buttons=[...document.querySelectorAll(selector+' button')];
                  const available=name=>buttons.find(b=>b.textContent===name)?.disabled===false;
                  return {recall:available('Recall Show'),recover:available('Recover Last Good')};
                }""",[show_panel,value])
            toolbar=page.locator(".command-bar")
            for caption,icon in [("Save","mixer-save.svg"),("Open","mixer-open.svg")]:
                button=toolbar.get_by_role("button",name=caption,exact=True)
                assert button.locator("img").get_attribute("src")=="assets/"+icon
                assert button.locator("img").evaluate("el=>el.complete && el.naturalWidth>0")
                button.click()
                assert page.evaluate("()=>window.__events.at(-1).payload.action")=="nativeMixerFile"+caption
            for name in ["Save Mixer Session","Restore Saved Mixer",
                         "Rebind Current Device…","Recover Previous Mixer"]:
                assert page.get_by_role("button",name=name,exact=True).count()==0
            emit(page,"arMixNativeSessionAck",{"operation":"OPEN","ok":False,"error":"Invalid mixer JSON"})
            assert page.get_by_text("Invalid mixer JSON",exact=True).count()>=1
            assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='monitor')")
            show_click("Browse Shows")
            assert page.evaluate("()=>window.__events.at(-1).payload.action")=="routingDryShowList"
            emit(page,"arMixNativeShowListAck",{"ok":True,"items":[
                {"id":show_id,"name":"Valid Performance","hasPrevious":True,"needsRecovery":False},
                {"id":corrupt_id,"name":"Damaged HEAD","hasPrevious":True,"needsRecovery":True}]})
            assert show_select(show_id)=={"recall":True,"recover":True}
            assert show_select(corrupt_id)=={"recall":False,"recover":True}
            show_click("Recover Last Good")
            recovery=page.evaluate("()=>window.__events.at(-1).payload")
            assert recovery=={"action":"routingDryShowRecover","value":corrupt_id}
            assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='monitor')")
            emit(page,"arMixNativeShowRecoverAck",{"ok":True,"showId":corrupt_id})
            assert page.evaluate("()=>window.__events.at(-1).payload.action")=="routingDryShowList"
            emit(page,"arMixNativeShowListAck",{"ok":True,"items":[
                {"id":corrupt_id,"name":"Recovered Performance","hasPrevious":True,"needsRecovery":False}]})
            assert show_select(corrupt_id)["recall"] is True
            show_click("Recall Show")
            recalled=page.evaluate("()=>window.__events.at(-1).payload")
            assert recalled=={"action":"routingDryShowRecall","value":corrupt_id}
            emit(page,"arMixNativeShowRecallAck",{"ok":False,"error":"DEVICE_REBIND_REQUIRED"})
            assert "Unable to open session · Mixer settings unchanged" in page.locator(show_panel).text_content()
            assert "DEVICE_REBIND_REQUIRED" not in page.locator(show_panel).text_content()
            show_click("Save New Version")
            assert page.evaluate("()=>window.__events.at(-1).payload")=={
                "action":"routingDryShowSave","value":corrupt_id}
            emit(page,"arMixNativeShowSaveAck",{"ok":True,"showId":corrupt_id})
            assert page.evaluate("()=>window.__events.at(-1).payload.action")=="routingDryShowList"
            emit(page,"arMixNativeShowListAck",{"ok":True,"items":[
                {"id":corrupt_id,"name":"Recovered Performance","hasPrevious":True,"needsRecovery":False}]})
            show_click("Save Show")
            assert page.evaluate("()=>window.__events.at(-1).payload.action")=="routingDryShowSave"
            emit(page,"arMixNativeShowSaveAck",{"ok":True,"showId":show_id})
            assert page.evaluate("()=>window.__events.at(-1).payload.action")=="routingDryShowList"
            emit(page,"arMixNativeShowListAck",{"ok":True,"items":[]})
            assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='monitor')")
            if 'collapsed-send' in (page.locator('.console-body').get_attribute('class') or ''):
                page.locator('.section-rail [data-section="send"]').click()
            # Release before bootstrap SOURCE's later native readback arrives.
            drag(page,1,2)
            assert not page.evaluate('()=>window.__nativeIntents.length')
            state=snapshot()
            state['graph']['edges'].pop(1)  # genuinely no existing channel send
            state['graph']['capabilities']['supportedOps'] += ['LOAD_INSERT']
            for ident, role, name in [('101','GROUP','Vocal Group'),
                                      ('102','FX_RETURN','Reverb Return')]:
                state['graph']['channels'].insert(-1, {
                    'id':ident,'role':role,'name':name,'processingFormat':'STEREO',
                    'controls':{'faderDb':-5,'pan':0,'mute':False,'solo':False},
                    'inserts':[],'sends':[]})
                state['graph']['destinations'].append({'id':ident,'role':role,'name':name})
            state['legacyM4']={'active':True,'routeAccepted':True,'routingRevision':'77',
                'nativeBusStructural':False,'fxInstances':[[] for _ in range(14)]}
            emit(page,'arMixConsoleSnapshot',state)
            page.wait_for_function('()=>window.__nativeIntents.length===1')
            route=page.evaluate('()=>window.__nativeIntents[0]')
            assert route['operation']=='APPLY_WIRE_TRANSACTION'
            assert route['payload']['changes'][0]['from']==SOURCE
            assert route['payload']['changes'][0]['to']==RECEIVER
            assert route['payload']['changes'][0]['op']=='CONNECT'
            # Source admission can advance after the GUI's last readback.
            # Reject once as stale, independently refresh, then rebase only
            # the same explicit connection with a new request ID.
            emit(page,'arMixConsoleAck',{'requestId':route['requestId'],'status':'REJECTED',
                'errorCode':'STALE_NATIVE_CONTROL_REVISION','deviceEpoch':state['device']['epoch'],
                'graphRevision':'3','rosterRevision':'3'})
            state['graph'].update(revision='3',rosterRevision='3')
            emit(page,'arMixConsoleSnapshot',state)
            page.wait_for_function('()=>window.__nativeIntents.length===2')
            retry=page.evaluate('()=>window.__nativeIntents.at(-1)')
            assert retry['baseGraphRevision']=='3' and retry['requestId']!=route['requestId']
            assert retry['payload']==route['payload']
            route=retry
            state['graph'].update(revision='4',rosterRevision='4')
            state['graph']['edges'].append({'edgeId':'9223372036854775810','from':SOURCE,'to':RECEIVER,
                'primaryOutput':False,'enabled':True,'gainLinear':1,'tap':'POST_FADER','signalKind':'AUDIO'})
            state['mixerSession'].update(routeRevision='4',rosterRevision='4',channels=copy.deepcopy(state['graph']['channels']))
            ack(page,state,route)
            emit(page,'arMixConsoleSnapshot',state)
            page.wait_for_function('()=>!window.__ARMIX_NATIVE_M2__.state().pending')
            # First FX on original CH: native authority even when the legacy
            # structural marker is false and its independent revision is 77.
            emit(page,'arMixCatalog',{'items':[{'id':'test-vst-64','name':'Latency VST','vendor':'Fixture',
                'category':'EQ','favorite':True}],'scanning':False})
            page.locator('#tracks .strip[data-id="1"] .fx-section [data-action="openPlugin"]').first.click()
            page.locator('#fastPicker [data-fast-id="test-vst-64"]').click()
            page.wait_for_function('()=>window.__nativeIntents.length===3')
            fx=page.evaluate('()=>window.__nativeIntents.at(-1)')
            assert fx['operation']=='LOAD_INSERT' and fx['baseGraphRevision']=='4'
            assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='routingFxInsert')")
            state['graph'].update(revision='5',rosterRevision='5')
            state['graph']['channels'][0]['inserts']=[{'id':'g5c-actual','catalogId':'test-vst-64',
                'name':'Latency VST','bypassed':False,'latencySamples':64}]
            state['mixerSession'].update(routeRevision='5',rosterRevision='5',channels=copy.deepcopy(state['graph']['channels']))
            ack(page,state,fx)
            emit(page,'arMixConsoleSnapshot',state)
            page.wait_for_function("()=>document.getElementById('fastPicker').hidden")
            emit(page,'arMixCatalog',{'items':[{'id':'test-vst-64','name':'Latency VST','vendor':'Fixture',
                'category':'EQ','favorite':True}],'scanning':False})
            assert page.locator('#tracks .strip[data-id="1"] .fx-section [data-action="openPlugin"]').first.inner_text()=='Latency VST', 'catalog refresh overwrote the accepted native rack'

            page.locator('#tracks .strip[data-id="1"] .fx-section [data-action="openPlugin"]').first.click()
            editor=page.evaluate("()=>window.__events.filter(e=>e.payload?.action==='routingFxEditor').at(-1)?.payload")
            assert editor and editor['channelId']==SOURCE and editor['instanceId']=='g5c-actual'
            assert editor['revision']=='5' and 'channel' not in editor
            # A new device epoch publishes the same document without a
            # callback, port assignment, meter evidence or write capability.
            retained=copy.deepcopy(state)
            retained['device'].update(epoch='92',outputArmed=False,sampleRate=0,
                bufferFrames=0,inputEndpoints=[],outputEndpoints=[],
                enabledInputs=[],enabledOutputs=[])
            retained['graph']['bindingState']='NEEDS_REMAP'
            retained['graph']['capabilities'].update(callbackOwnsChannelIds=False,supportedOps=[])
            retained['graph']['channels'][0]['source']={
                'bindingState':'NEEDS_REMAP','sourceFormat':'MONO'}
            retained['telemetry']['channelMeterRmsPeak']={}
            retained['legacyM4']={'active':False,'routeAccepted':False,'routingRevision':'0'}
            retained['mixerSession'].update(deviceEpoch='92',bindingState='NEEDS_REMAP',
                channels=copy.deepcopy(retained['graph']['channels']))
            emit(page,'arMixConsoleSnapshot',retained)
            page.wait_for_function('()=>window.__ARMIX_NATIVE_M2__.state().current?.retained')
            assert page.locator('.status-bar .warning').inner_text().startswith('RECONNECT AUDIO OUTPUT')
            assert page.locator('[data-native-monitor]').is_disabled()
            assert add_track.is_disabled()
            assert page.locator('#tracks .strip').count()==4
            assert 'Output disconnected · Open Audio I/O' in page.locator('#transport').inner_text()
            page.locator('#tracks .strip[data-id="1"] .wire-node-out').click()
            emit(page,'arMixConsoleSnapshot',retained)
            assert page.locator('#tracks .strip[data-id="2"] .wire-gain').is_disabled()
            strip=page.locator('#tracks .strip[data-id="1"]')
            assert strip.locator('select[data-field="input"]').input_value()=='Audio connection lost'
            assert strip.locator('.fx-section [data-action="openPlugin"]').first.inner_text()=='Latency VST'
            before=page.evaluate('()=>window.__events.filter(e=>e.payload?.action==="routingFxEditor").length')
            strip.locator('.fx-section [data-action="openPlugin"]').first.click()
            assert page.evaluate('()=>window.__events.filter(e=>e.payload?.action==="routingFxEditor").length')==before
            assert page.evaluate('()=>window.__nativeIntents.length')==3
            # Late readback from the disconnected epoch cannot re-admit audio.
            emit(page,'arMixConsoleSnapshot',state)
            assert page.evaluate('()=>window.__ARMIX_NATIVE_M2__.state().current.retained')
            # One explicit Apply accepts the verified replacement binding.
            page.locator('[data-native-io]').click()
            backend='Windows Audio'
            active={'backend':backend,'captureDevice':'New Capture','playbackDevice':'New Playback',
                'open':True,'authorized':True,'deviceGeneration':12,'rate':48000,'buffer':256,
                'inputNames':['Input 1'],'outputNames':['Output 1','Output 2'],
                'enabledInputs':[0],'enabledOutputs':[0,1],'rates':[48000],'buffers':[256]}
            emit(page,'arMixAudioDeviceInventory',{'active':active,'types':[
                {'name':backend,'inputs':['New Capture'],'outputs':['New Playback'],'separate':True}]})
            remap=page.locator('[data-audio-remap]')
            assert remap.is_hidden()
            page.locator('[data-audio-apply]').click()
            read_apply='()=>window.__events.filter(e=>e.payload?.action==="audioDeviceApply").at(-1).payload'
            opened=page.evaluate(read_apply)
            assert 'confirmRetainedPinMapping' not in opened['config']
            emit(page,'arMixAudioDeviceApplyAck',{'requestId':'old','state':'NEEDS_REMAP',
                'active':active,'canReusePins':True})
            assert remap.is_hidden()
            emit(page,'arMixAudioDeviceApplyAck',{'requestId':opened['requestId'],'ok':True,
                'state':'ACCEPTED_LIVE','active':active,'mixerBound':True,'outputMuted':False})
            assert page.locator('.armix-audio-shade').is_hidden()
            rebound=copy.deepcopy(state)
            rebound['device'].update(epoch='93',outputArmed=True)
            rebound['graph']['revision']='6'
            rebound['graph']['capabilities']['supportedOps'].append('CREATE_CHANNEL')
            rebound['mixerSession'].update(deviceEpoch='93',routeRevision='6')
            monitor_events=page.evaluate('()=>window.__events.filter(e=>e.payload?.action==="monitor").length')
            emit(page,'arMixConsoleSnapshot',rebound)
            page.wait_for_function('()=>window.__ARMIX_NATIVE_M2__.state().current?.admitted')
            assert page.locator('[data-native-monitor]').is_enabled()
            assert page.locator('[data-native-monitor]').get_attribute('data-armed')=='true'
            assert page.locator('#tracks .strip').count()==4
            assert strip.locator('.fx-section [data-action="openPlugin"]').first.inner_text()=='Latency VST'
            assert strip.locator('select[data-field="input"]').is_enabled()
            assert strip.locator('select[data-field="output"]').is_enabled()
            strip.locator('.wire-node-out').click()
            assert page.locator('#tracks .strip[data-id="2"] .wire-gain').is_enabled()
            strip.locator('.fx-section .fx-pick.empty').click()
            assert page.locator('#fastPicker [data-fast-id="test-vst-64"]').is_visible()
            page.locator('#fastClose').click()
            # Apply again while MAIN is live. The native transaction owns
            # temporary mute and restoration without an extra browser command.
            page.locator('[data-native-io]').click()
            emit(page,'arMixAudioDeviceInventory',{'active':active,'types':[
                {'name':backend,'inputs':['New Capture'],'outputs':['New Playback'],'separate':True}]})
            page.locator('[data-audio-apply]').click()
            repeated=page.evaluate(read_apply)
            assert repeated['requestId']!=opened['requestId']
            emit(page,'arMixAudioDeviceApplyAck',{'requestId':repeated['requestId'],'ok':True,
                'state':'ACCEPTED_LIVE','active':active,'mixerBound':True,'outputMuted':False})
            assert page.locator('.armix-audio-shade').is_hidden()
            assert page.evaluate('()=>window.__events.filter(e=>e.payload?.action==="monitor").length')==monitor_events
            add_track.click()
            request=page.evaluate(create)[-1]['payload']
            assert (request['deviceEpoch'],request['graphRevision'],request['rosterRevision'])==(
                '93','6',rebound['graph']['rosterRevision'])
            assert add_track.is_disabled()
            created_id='9007199254740997'
            emit(page,'arMixNativeBusAck',{'requestId':request['requestId'],
                'ok':True,'createdId':created_id,'graphRevision':'7'})
            assert add_track.is_disabled()
            assert page.locator('#tracks .strip').count()==4
            rebound['graph'].update(revision='7',rosterRevision='7')
            new_track=copy.deepcopy(rebound['graph']['channels'][1])
            new_track.update(id=created_id,name='Track 11',inserts=[],sends=[])
            new_track.pop('source',None)
            rebound['graph']['channels'].insert(-1,new_track)
            rebound['graph']['edges'].append(dict(edgeId='9223372036854775818',
                tap='POST_FADER',enabled=True,signalKind='AUDIO',
                **{'from':created_id,'to':MAIN,'primaryOutput':True,'gainLinear':1}))
            rebound['mixerSession'].update(routeRevision='7',rosterRevision='7',
                channels=copy.deepcopy(rebound['graph']['channels']))
            emit(page,'arMixConsoleSnapshot',rebound)
            assert add_track.is_enabled()
            selected=page.locator('#tracks .strip.selected')
            assert 'Track 11' in selected.inner_text()
            assert selected.locator('select[data-field="input"]').is_enabled()
            assert page.locator('[data-native-monitor]').get_attribute('data-armed')=='true'
            assert not errors, errors
            page.screenshot(path=f'native-cold-start-{width}.png')
            page.close()
        browser.close()
    print('PASS G0 first drag without OUT priming; original-channel FX/editor; retained GROUP/FX/WIRE and disarm; 3 sizes (UI isolation)')


if __name__=='__main__':
    main()
