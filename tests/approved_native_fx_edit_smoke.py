"""Compiled native pointer ghost -> MOVE/REMOVE intent -> ACK/readback.
Real PCM/order/lease lifetime are covered separately by native CTest.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS
from approved_native_wire_gain_smoke import snapshot,emit,ack,SOURCE,RECEIVER
ROOT=Path(__file__).resolve().parents[1]

def main():
    spec=importlib.util.spec_from_file_location('bundle',ROOT/'scripts/build_approved_native_gui.py')
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory() as directory,sync_playwright() as p:
        out=Path(directory);bundle.stage(out);browser=p.chromium.launch()
        for width,height in [(1366,768),(1680,900),(1920,1080)]:
            page=browser.new_page(viewport={'width':width,'height':height});errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.add_init_script(NATIVE_EVENTS + """window.__nativeIntents=[];window.__actions=[];
              window.__JUCE__={backend:{emitEvent(name,payload){
                if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
                if(name==='arMixAction')window.__actions.push(payload);},
                ...window.__nativeListenerMethods}};""")
            page.goto((out/'index.html').as_uri());page.wait_for_function('()=>window.__ARMIX_NATIVE_M2__?.enabled')
            state=snapshot();state['graph']['capabilities']['supportedOps']+=['LOAD_INSERT','MOVE_INSERT','REMOVE_INSERT']
            state['graph']['channels'][0]['inserts']=[
                {'id':'actual-a','catalogId':'a','name':'ArSonEQ','bypassed':False},
                {'id':'actual-b','catalogId':'b','name':'ArSonKuPik','bypassed':True}]
            state["globalFxBypassed"]=False
            state["secondaryMainReady"]=True
            for channel in state["graph"]["channels"]:
                channel["fxRackBypassed"]=False
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,'arMixConsoleSnapshot',state)
            def row(channel,slot):return page.locator(f'#tracks .strip[data-id="{channel}"] .fxslot-row[data-slot="{slot}"]')
            def move(source,slot,destination,target_slot,after=False):
                surface=row(source,slot).locator('.fx-pick').bounding_box();target=row(destination,target_slot).bounding_box()
                assert surface and target
                page.mouse.move(surface['x']+surface['width']/2,surface['y']+surface['height']/2);page.mouse.down()
                x=target['x']+target['width']/2;y=target['y']+target['height']*(.8 if after else .2)
                page.mouse.move(x,y,steps=10);page.wait_for_timeout(60)
                ghost=page.locator('.fx-drag-ghost');assert ghost.count()==1
                bounds=ghost.bounding_box();assert bounds and abs(bounds['x']-x)<220
                page.mouse.move(x+18,y+2,steps=3);page.wait_for_timeout(30)
                moved=ghost.bounding_box();assert moved and abs(moved['x']-bounds['x']-18)<2
                assert page.locator('.fx-drop-marker:visible').count()==1
                page.mouse.move(x,y);page.mouse.up();assert ghost.count()==0
                return page.evaluate('()=>window.__nativeIntents.at(-1)')
            def accept(request):
                n=str(int(state['graph']['revision'])+1)
                state['graph'].update(revision=n,rosterRevision=n)
                state['mixerSession'].update(routeRevision=n,rosterRevision=n,channels=copy.deepcopy(state['graph']['channels']))
                ack(page,state,request);emit(page,'arMixConsoleSnapshot',state)
                page.wait_for_function('()=>!window.__ARMIX_NATIVE_M2__.state().pending')
            # A failed insert used to trigger a strip rebuild and another
            # subscribe each time the same snapshot arrived from native.
            emit(page,"arMixCatalog",{"items":[],"builtIns":[
                {"id":"armix.builtin.auto-ducker.v1","name":"Auto Ducker","category":"Dynamics"}
            ]})
            row(1,2).locator(".fx-pick").click()
            page.get_by_role("button",name="Auto Ducker",exact=False).click()
            insertion=page.evaluate("()=>window.__nativeIntents.at(-1)")
            assert insertion["operation"]=="LOAD_INSERT" and insertion["payload"]["slot"]==2
            old_row=row(1,0).element_handle()
            emit(page,"arMixConsoleAck",{"requestId":insertion["requestId"],"status":"REJECTED",
                "errorCode":"AUTO_DUCKER_HOST_UNAVAILABLE","deviceEpoch":state["device"]["epoch"],
                "graphRevision":state["graph"]["revision"],"rosterRevision":state["graph"]["rosterRevision"]})
            for _ in range(3):
                emit(page,"arMixConsoleSnapshot",state)
            assert old_row.evaluate("(row)=>row.isConnected"),"rejection remounted the FX rack"
            assert "could not be added" in page.locator("#armixNativeToast").inner_text()
            assert page.locator("#fastClose").is_visible()
            page.locator("#fastClose").click()
            page.locator("#armixNativeToast").evaluate("(el)=>el.hidden=true")
            assert row(1,2).locator(".fx-pick.empty").is_visible()

            def click_power(slot):
                row(1,slot).locator('[data-action="slotPower"]').click()
                action=page.evaluate("()=>window.__actions.at(-1)")
                assert action["action"]=="routingNativeFxInsertBypass"
                return action

            def send_power_ack(action):
                emit(page,"arMixNativeFxPowerAck",{"requestId":action["requestId"],
                    "channelId":action["channelId"],"ok":True,"bypassed":action["value"]})

            def publish_power(slot,value):
                state["graph"]["channels"][0]["inserts"][slot]["bypassed"]=value
                state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
                emit(page,"arMixConsoleSnapshot",state)

            # Native title stripping must not fight stable FX rack repaint.
            page.evaluate("""()=>{window.__rackPowerMutations=[];
              const rack=document.querySelector('#tracks .strip[data-id="1"] .fx-section');
              window.__rackObserver=new MutationObserver(rs=>window.__rackPowerMutations.push(...rs.map(r=>r.attributeName)));
              window.__rackObserver.observe(rack,{subtree:true,attributes:true,
                attributeFilter:['title','aria-pressed','aria-label','aria-description','data-fx-chain-enabled','data-global-fx-enabled','data-fx-slot-enabled','data-fx-effective','class']});}""")
            for _ in range(3):
                emit(page,"arMixConsoleSnapshot",state)
            assert page.evaluate("()=>window.__rackPowerMutations")==[]
            assert row(1,0).locator('[data-action="slotPower"]').get_attribute("title") is None
            # A different slot remains editable before the first slot is
            # confirmed. Deliver first slot snapshot before its ACK.
            power=click_power(0)
            other=click_power(1)
            assert power["insertId"]=="actual-a" and power["value"] is True
            assert other["insertId"]=="actual-b" and other["value"] is False
            publish_power(0,True)
            send_power_ack(power)
            emit(page,"arMixConsoleSnapshot",state)
            send_power_ack(other)
            publish_power(1,False)
            retry=click_power(0)
            assert retry["requestId"]!=power["requestId"] and retry["value"] is False
            send_power_ack(retry)
            publish_power(0,False)
            publish_power(1,True)
            assert row(1,0).get_attribute("data-fx-slot-enabled")=="true"
            assert row(1,1).get_attribute("data-fx-slot-enabled")=="false"
            page.evaluate("()=>window.__nativeIntents.length=0")

            # Escape cancels presentation without committing an audio edit.
            assert page.locator('.fx-grip').count()==0,'retired handle must never appear'
            surface=row(1,0).locator('.fx-pick').bounding_box();assert surface
            page.mouse.move(surface['x']+5,surface['y']+5);page.mouse.down();page.mouse.move(450,50,steps=10)
            page.wait_for_timeout(40);assert page.locator('.fx-drag-ghost').count()==1
            page.keyboard.press('Escape');page.mouse.up()
            assert page.locator('.fx-drag-ghost').count()==0
            assert page.evaluate('()=>window.__nativeIntents.length')==0
            # POWER/DELETE remain dedicated click targets, not drag origins.
            for action in ("slotPower","removeFx"):
                rect=row(1,0).locator(f'[data-action="{action}"]').bounding_box()
                assert rect
                page.mouse.move(rect['x']+rect['width']/2,rect['y']+rect['height']/2)
                page.mouse.down();page.mouse.move(450,50,steps=6)
                assert page.locator('.fx-drag-ghost').count()==0,action
                page.mouse.up()
                assert page.evaluate('()=>window.__nativeIntents.length')==0,action
            # A short click on the name still opens the real native editor.
            row(1,0).locator('.fx-pick').click()
            first_editor=page.evaluate("()=>window.__actions.at(-1)")
            assert first_editor['action']=="routingFxEditor" and first_editor['requestId']
            assert page.evaluate('()=>window.__nativeIntents.length')==0
            # Drag lower within the same rack: exact plugin follows pointer.
            request=move(1,0,1,1,True)
            assert request['operation']=='MOVE_INSERT' and request['payload']['toSlot']==1
            assert row(1,0).get_attribute('data-insert-id')=='actual-a','optimistic reorder'
            state['graph']['channels'][0]['inserts'].reverse();accept(request)
            assert row(1,0).get_attribute('data-insert-id')=='actual-b'
            # Deliberate MOVE retires the pending slot lease; even a late
            # rejected ACK cannot show a toast for the already-closed editor.
            emit(page,'arMixRoutingFxEditorAck',{'ok':False,'requestId':first_editor['requestId'],
                'instanceId':first_editor['instanceId'],'error':'obsolete editor request'})
            assert page.locator('#armixNativeToast:visible').count()==0
            # Cross-channel MOVE, not a new plugin load or name-only copy.
            request=move(1,1,2,0)
            assert request['operation']=='MOVE_INSERT' and request['payload']['toChannelId']==RECEIVER
            assert request['payload']['instanceId']=='actual-a'
            state['graph']['channels'][1]['inserts'].append(state['graph']['channels'][0]['inserts'].pop(1));accept(request)
            assert row(2,0).get_attribute('data-insert-id')=='actual-a'
            row(2,0).locator('[data-action="openPlugin"]').click()
            editor=page.evaluate('()=>window.__actions.at(-1)')
            assert editor['action']=='routingFxEditor' and editor['channelId']==RECEIVER and editor['instanceId']=='actual-a'
            emit(page,'arMixRoutingFxEditorAck',{'ok':True,'requestId':editor['requestId'],'instanceId':'actual-a'})
            row(1,0).locator('[data-action="removeFx"]').click()
            request=page.evaluate('()=>window.__nativeIntents.at(-1)')
            assert request['operation']=='REMOVE_INSERT' and request['payload']['instanceId']=='actual-b'
            assert row(1,0).get_attribute('data-insert-id')=='actual-b','optimistic delete'
            state['graph']['channels'][0]['inserts'].clear();accept(request)
            assert page.locator('#tracks .strip[data-id="1"] .fxslot-row:not(.is-empty)').count()==0
            assert row(2,0).get_attribute('data-insert-id')=='actual-a'
            assert page.locator('.wire-main-fragment').count()==0
            assert any(e['to']==state['graph']['mainChannelId'] for e in state['graph']['edges'])
            # ImmerSIB native editor must dispatch the *accepted* ChannelId
            # token, even when a dearVR/VST3 editor is opened in the same
            # live rack. This is a browser transport test; the JUCE HWND's
            # separate lifetime is compiled and checked by Windows FAT.
            state['graph']['channels'][1]['inserts']=[
                {'id':'concert-accepted','catalogId':'armix.builtin.imersip.v1',
                 'name':'ImmerSIB','bypassed':False},
                {'id':'dearvr-accepted','catalogId':'vst3.dearvr.pro2',
                 'name':'dearVR PRO 2','bypassed':False}]
            state['mixerSession']['channels']=copy.deepcopy(state['graph']['channels'])
            emit(page,'arMixConsoleSnapshot',state)
            row(2,0).locator('[data-action="openPlugin"]').click()
            opened=page.evaluate('()=>window.__actions.at(-1)')
            assert opened['action']=='routingFxEditor' and opened['channelId']==RECEIVER,opened
            assert opened['instanceId']=='concert-accepted',opened
            assert str(opened['deviceEpoch'])==str(state['device']['epoch']),opened
            assert str(opened['revision'])==str(state['graph']['revision']),opened
            emit(page,'arMixRoutingFxEditorAck',{'ok':True,'requestId':opened['requestId'],'instanceId':'concert-accepted'})
            row(2,1).locator('[data-action="openPlugin"]').click()
            vst=page.evaluate('()=>window.__actions.at(-1)')
            assert vst['action']=='routingFxEditor' and vst['instanceId']=='dearvr-accepted',vst
            emit(page,'arMixRoutingFxEditorAck',{'ok':True,'requestId':vst['requestId'],'instanceId':'dearvr-accepted'})
            row(2,0).locator('[data-action="openPlugin"]').click()
            reopened=page.evaluate('()=>window.__actions.at(-1)')
            assert reopened['action']=='routingFxEditor' and reopened['instanceId']=='concert-accepted',reopened
            assert reopened['channelId']==RECEIVER,reopened
            emit(page,'arMixRoutingFxEditorAck',{'ok':True,'requestId':reopened['requestId'],'instanceId':'concert-accepted'})
            assert page.locator('#armixNativeToast:visible').count()==0,(
                'Successful native editor open must not raise an FX settings toast')
            # A genuine correlated failure is still visible. Do not silence
            # all editor errors merely to make the success case pass.
            row(2,1).locator('[data-action="openPlugin"]').click()
            failed=page.evaluate('()=>window.__actions.at(-1)')
            emit(page,'arMixRoutingFxEditorAck',{'ok':False,'requestId':failed['requestId'],
                'instanceId':failed['instanceId'],'error':'fixture native host rejected'})
            assert page.locator('#armixNativeToast:visible').count()==1
            assert 'FX editor unavailable' in page.locator('#armixNativeToast').inner_text()
            assert not errors,errors
            page.screenshot(path=f'native-fx-edit-{width}.png');page.close()
        browser.close()
    print('PASS native handle-free FX pointer ghost, reorder, cross-channel move, delete, editor token and quiet MAIN; 3 sizes')

if __name__=='__main__':main()
