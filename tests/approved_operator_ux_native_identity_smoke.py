"""Actual compiled native Pages UI: channel identity requires disk-backed host ACK.

No optimistic JS rename, no localStorage source of authority, no synthetic
PCM graph operation. C++ NativeChannelAppearanceStore CTest independently
proves durable disk/lease validation and guards corruption/restart.
"""
from pathlib import Path
import tempfile
import sys
import copy
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, SOURCE, RECEIVER

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
import build_approved_native_gui as bundle

def main():
    with tempfile.TemporaryDirectory(prefix="armix-native-identity-") as temp, sync_playwright() as p:
        out=Path(temp)/"native"
        bundle.stage(out)
        browser=p.chromium.launch(headless=True)
        context=browser.new_context(viewport={"width":1366,"height":768})
        errors=[]
        def open_page():
            page=context.new_page()
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.add_init_script("""window.__nativeListeners=new Map();window.__nativeIntents=[];window.__nativeEvents=[];
              window.__JUCE__={backend:{
                emitEvent(name,payload){window.__nativeEvents.push({name,payload});
                  if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);},
                addEventListener(name,callback){window.__nativeListeners.set(name,callback);}}};""")
            page.goto((out/"index.html").as_uri())
            page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
            return page
        state=snapshot()
        appearance={"schema":"arsonmix.native.channel-appearance.v1",
                    "revision":"0","readOnly":False,"entries":[]}
        state["channelAppearance"]=appearance
        page=open_page()
        ready=page.evaluate("()=>window.__nativeEvents.find(e=>e.name==='arMixGuiReady')?.payload")
        assert ready and ready["viewportWidth"]==1366 and ready["viewportHeight"]==768
        # No unimplemented native functionality advertised as a working action.
        for id in ("saveScene","recallScene","copy","paste","group","ungroup",
                   "preview","clearCue","reset","sofToggle","sofTarget"):
            el=page.locator("#"+id)
            assert el.is_hidden() and el.is_disabled(),id
        assert page.locator("#pluginsButton").is_visible()
        emit(page,"arMixConsoleSnapshot",state)
        strip=page.locator('#tracks .strip[data-id="1"]')
        strip.wait_for(state="visible")
        # Single-click on visible NAME must open the actual native editor.
        strip.locator('.footer-label [data-action="rename"]').click()
        editor=page.locator("#channelIdentity")
        assert editor.is_visible(),"single click name did not open editor"
        page.locator("#identityName").fill("Studio Vox")
        color="#c32974"
        page.locator(f'#identityColors [data-identity-color="{color}"]').click()
        icon="🎹"
        page.locator(f'#identityIcons [data-identity-icon="{icon}"]').click()
        page.locator("#identitySave").click()
        assert editor.is_visible(),"native identity edited before ACK"
        assert "Studio Vox" not in strip.inner_text(),"optimistic rename without native evidence"
        req=page.evaluate("""()=>window.__nativeEvents.filter(e=>
           e.name==='arMixChannelIdentitySet').at(-1)?.payload""")
        assert req and req["channelId"]==SOURCE,req
        assert req["appearanceRevision"]=="0" and req["name"]=="Studio Vox"
        assert req["color"]==color and req["icon"]==icon
        assert page.locator("#identitySave").is_disabled()
        assert page.evaluate("()=>window.__nativeIntents.length")==0

        # Bad/stale native ACK: editor remains open and value stays untouched.
        emit(page,"arMixChannelIdentityAck",
             {"requestId":req["requestId"],"ok":False,
              "error":"STALE_CHANNEL_APPEARANCE_REVISION"})
        assert editor.is_visible()
        assert page.locator("#identitySave").is_enabled()
        assert "Studio Vox" not in strip.inner_text()
        # Retry after a fresh accepted snapshot.
        emit(page,"arMixConsoleSnapshot",state)
        page.locator("#identitySave").click()
        req=page.evaluate("""()=>window.__nativeEvents.filter(e=>
           e.name==='arMixChannelIdentitySet').at(-1)?.payload""")
        emit(page,"arMixChannelIdentityAck",
             {"requestId":req["requestId"],"ok":True,"error":""})
        # ACK alone MUST NOT rename or close the editor.
        assert editor.is_visible()
        assert "Studio Vox" not in strip.inner_text()
        appearance["revision"]="1"
        appearance["entries"]=[{"id":SOURCE,"name":"Studio Vox","color":color,"icon":icon}]
        emit(page,"arMixConsoleSnapshot",copy.deepcopy(state))
        page.wait_for_function("""()=>document.querySelector('#channelIdentity').hidden &&
          document.querySelector('#tracks .strip[data-id="1"]').textContent.includes('Studio Vox')""",timeout=5000)
        assert page.evaluate("()=>window.__nativeIntents.length")==0
        assert page.evaluate("()=>localStorage.getItem('arsonmix.native.channel-presentation.v1')") is None

        # A second channel cannot take an existing visible name; rejected
        # locally AND independently by the native store's CTest.
        other=page.locator('#tracks .strip[data-id="2"]')
        other.locator('.footer-label [data-action="rename"]').click()
        page.locator("#identityName").fill("Studio Vox")
        before=page.evaluate("()=>window.__nativeEvents.filter(e=>e.name==='arMixChannelIdentitySet').length")
        page.locator("#identitySave").click()
        after=page.evaluate("()=>window.__nativeEvents.filter(e=>e.name==='arMixChannelIdentitySet').length")
        assert before==after and editor.is_visible()
        page.locator("#identityCancel").click()
        assert editor.is_hidden()
        # Restore the WebView from the same durable *native* snapshot, with
        # no shared JS localStorage or reconstructed PCM graph.
        page.close()
        page=open_page()
        emit(page,"arMixConsoleSnapshot",copy.deepcopy(state))
        page.wait_for_selector('#tracks .strip[data-id="1"]')
        assert "Studio Vox" in page.locator('#tracks .strip[data-id="1"]').inner_text()
        assert page.evaluate("()=>window.__nativeIntents.length")==0
        assert not errors,errors
        page.close();context.close();browser.close()
    print("PASS native identity click/ACK/readback/reject/restart with zero PCM graph intents")

if __name__=="__main__":main()
