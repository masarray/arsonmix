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
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE, RECEIVER

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
            if 'collapsed-send' in (page.locator('.console-body').get_attribute('class') or ''):
                page.locator('.section-rail [data-section="send"]').click()
            # Release before bootstrap SOURCE's later native readback arrives.
            drag(page,1,2)
            assert not page.evaluate('()=>window.__nativeIntents.length')
            state=snapshot()
            state['graph']['edges'].pop(1)  # genuinely no existing channel send
            state['graph']['capabilities']['supportedOps'] += ['LOAD_INSERT']
            state['legacyM4']={'active':True,'routeAccepted':True,'routingRevision':'77',
                'nativeBusStructural':False,'fxInstances':[[] for _ in range(14)]}
            emit(page,'arMixConsoleSnapshot',state)
            page.wait_for_function('()=>window.__nativeIntents.length===1')
            route=page.evaluate('()=>window.__nativeIntents[0]')
            assert route['operation']=='APPLY_WIRE_TRANSACTION'
            assert route['payload']['changes'][0]['from']==SOURCE
            assert route['payload']['changes'][0]['to']==RECEIVER
            assert route['payload']['changes'][0]['op']=='CONNECT'
            state['graph'].update(revision='3',rosterRevision='3')
            state['graph']['edges'].append({'edgeId':'9223372036854775810','from':SOURCE,'to':RECEIVER,
                'primaryOutput':False,'enabled':True,'gainLinear':1,'tap':'POST_FADER','signalKind':'AUDIO'})
            state['mixerSession'].update(routeRevision='3',rosterRevision='3',channels=copy.deepcopy(state['graph']['channels']))
            ack(page,state,route)
            emit(page,'arMixConsoleSnapshot',state)
            page.wait_for_function('()=>!window.__ARMIX_NATIVE_M2__.state().pending')
            # First FX on original CH: native authority even when the legacy
            # structural marker is false and its independent revision is 77.
            emit(page,'arMixCatalog',{'items':[{'id':'test-vst-64','name':'Latency VST','vendor':'Fixture',
                'category':'EQ','favorite':True}],'scanning':False})
            page.locator('#tracks .strip[data-id="1"] .fx-section [data-action="openPlugin"]').first.click()
            page.locator('#fastPicker [data-fast-id="test-vst-64"]').click()
            page.wait_for_function('()=>window.__nativeIntents.length===2')
            fx=page.evaluate('()=>window.__nativeIntents.at(-1)')
            assert fx['operation']=='LOAD_INSERT' and fx['baseGraphRevision']=='3'
            assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='routingFxInsert')")
            state['graph'].update(revision='4',rosterRevision='4')
            state['graph']['channels'][0]['inserts']=[{'id':'g5c-actual','catalogId':'test-vst-64',
                'name':'Latency VST','bypassed':False,'latencySamples':64}]
            state['mixerSession'].update(routeRevision='4',rosterRevision='4',channels=copy.deepcopy(state['graph']['channels']))
            ack(page,state,fx)
            emit(page,'arMixConsoleSnapshot',state)
            page.wait_for_function("()=>document.getElementById('fastPicker').hidden")
            emit(page,'arMixCatalog',{'items':[{'id':'test-vst-64','name':'Latency VST','vendor':'Fixture',
                'category':'EQ','favorite':True}],'scanning':False})
            assert page.locator('#tracks .strip[data-id="1"] .fx-section [data-action="openPlugin"]').first.inner_text()=='Latency VST', 'catalog refresh overwrote the accepted native rack'

            page.locator('#tracks .strip[data-id="1"] .fx-section [data-action="openPlugin"]').first.click()
            editor=page.evaluate("()=>window.__events.filter(e=>e.payload?.action==='routingFxEditor').at(-1)?.payload")
            assert editor and editor['channelId']==SOURCE and editor['instanceId']=='g5c-actual'
            assert editor['revision']=='4' and 'channel' not in editor
            assert not errors, errors
            page.screenshot(path=f'native-cold-start-{width}.png')
            page.close()
        browser.close()
    print('PASS G0 first drag without OUT priming; original-channel native FX insert/editor; 3 sizes (UI isolation)')


if __name__=='__main__':
    main()
