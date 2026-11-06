"""Compiled native pointer ghost -> MOVE/REMOVE intent -> ACK/readback.
Real PCM/order/lease lifetime are covered separately by native CTest.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
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
            page.add_init_script("""window.__nativeListeners=new Map();window.__nativeIntents=[];window.__actions=[];
              window.__JUCE__={backend:{emitEvent(name,payload){
                if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
                if(name==='arMixAction')window.__actions.push(payload);},
                addEventListener(name,callback){window.__nativeListeners.set(name,callback);}}};""")
            page.goto((out/'index.html').as_uri());page.wait_for_function('()=>window.__ARMIX_NATIVE_M2__?.enabled')
            state=snapshot();state['graph']['capabilities']['supportedOps']+=['MOVE_INSERT','REMOVE_INSERT']
            state['graph']['channels'][0]['inserts']=[
                {'id':'actual-a','catalogId':'a','name':'ArSonEQ','bypassed':False},
                {'id':'actual-b','catalogId':'b','name':'ArSonKuPik','bypassed':True}]
            emit(page,'arMixConsoleSnapshot',state)
            def row(channel,slot):return page.locator(f'#tracks .strip[data-id="{channel}"] .fxslot-row[data-slot="{slot}"]')
            def move(source,slot,destination,target_slot,after=False):
                grip=row(source,slot).locator('.fx-grip').bounding_box();target=row(destination,target_slot).bounding_box()
                assert grip and target
                page.mouse.move(grip['x']+grip['width']/2,grip['y']+grip['height']/2);page.mouse.down()
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
            # Escape cancels presentation without committing an audio edit.
            grip=row(1,0).locator('.fx-grip').bounding_box();assert grip
            page.mouse.move(grip['x']+5,grip['y']+5);page.mouse.down();page.mouse.move(450,50,steps=10)
            page.wait_for_timeout(40);assert page.locator('.fx-drag-ghost').count()==1
            page.keyboard.press('Escape');page.mouse.up()
            assert page.locator('.fx-drag-ghost').count()==0
            assert page.evaluate('()=>window.__nativeIntents.length')==0
            # Drag lower within the same rack: exact plugin follows pointer.
            request=move(1,0,1,1,True)
            assert request['operation']=='MOVE_INSERT' and request['payload']['toSlot']==1
            assert row(1,0).get_attribute('data-insert-id')=='actual-a','optimistic reorder'
            state['graph']['channels'][0]['inserts'].reverse();accept(request)
            assert row(1,0).get_attribute('data-insert-id')=='actual-b'
            # Cross-channel MOVE, not a new plugin load or name-only copy.
            request=move(1,1,2,0)
            assert request['operation']=='MOVE_INSERT' and request['payload']['toChannelId']==RECEIVER
            assert request['payload']['instanceId']=='actual-a'
            state['graph']['channels'][1]['inserts'].append(state['graph']['channels'][0]['inserts'].pop(1));accept(request)
            assert row(2,0).get_attribute('data-insert-id')=='actual-a'
            row(2,0).locator('[data-action="openPlugin"]').click()
            editor=page.evaluate('()=>window.__actions.at(-1)')
            assert editor['action']=='routingFxEditor' and editor['channelId']==RECEIVER and editor['instanceId']=='actual-a'
            row(1,0).locator('[data-action="removeFx"]').click()
            request=page.evaluate('()=>window.__nativeIntents.at(-1)')
            assert request['operation']=='REMOVE_INSERT' and request['payload']['instanceId']=='actual-b'
            assert row(1,0).get_attribute('data-insert-id')=='actual-b','optimistic delete'
            state['graph']['channels'][0]['inserts'].clear();accept(request)
            assert page.locator('#tracks .strip[data-id="1"] .fxslot-row:not(.is-empty)').count()==0
            assert row(2,0).get_attribute('data-insert-id')=='actual-a'
            assert page.locator('.wire-main-fragment').count()==0
            assert any(e['to']==state['graph']['mainChannelId'] for e in state['graph']['edges'])
            assert not errors,errors
            page.screenshot(path=f'native-fx-edit-{width}.png');page.close()
        browser.close()
    print('PASS native FX pointer ghost, reorder, cross-channel move, delete, editor token and quiet MAIN; 3 sizes')

if __name__=='__main__':main()
