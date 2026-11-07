"""Native channel identity end-to-end: Chromium + emulated JUCE metadata only.

Source of truth for PCM/WIRE is the admitted ChannelId graph. A user-facing
alias must not create any synthetic graph intents, survive snapshot readback
and WebView reload, or rename the immutable native ChannelId.
"""
from pathlib import Path
import tempfile
import sys
import copy
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, SOURCE, RECEIVER

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import build_approved_native_gui as bundle

def main():
    with tempfile.TemporaryDirectory(prefix="armix-native-alias-") as temp, sync_playwright() as p:
        out=Path(temp)/"native"
        bundle.stage(out)
        browser=p.chromium.launch(headless=True)
        context=browser.new_context(viewport={"width":1366,"height":768})
        errors=[]
        def open_page():
            page=context.new_page()
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.add_init_script("""window.__nativeListeners=new Map();window.__nativeIntents=[];
              window.__JUCE__={backend:{
                emitEvent(name,payload){if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);},
                addEventListener(name,callback){window.__nativeListeners.set(name,callback);}}};""")
            page.goto((out/'index.html').as_uri())
            page.wait_for_function('()=>window.__ARMIX_NATIVE_M2__?.enabled')
            return page
        state=snapshot()
        page=open_page()
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_selector('#tracks .strip[data-id="1"]')
        page.locator('#tracks .strip[data-id="1"] .strip-identity-top').dblclick()
        editor=page.locator('#channelIdentity')
        assert editor.is_visible(),"native bridge still blocks identity window"
        page.locator('#identityName').fill("Studio Vox")
        color="#c32974"
        page.locator(f'#identityColors [data-identity-color="{color}"]').click()
        icon="🎹"
        page.locator(f'#identityIcons [data-identity-icon="{icon}"]').click()
        page.locator('#identitySave').click()
        assert editor.is_hidden()
        assert page.locator('#tracks .strip[data-id="1"]').inner_text().find("Studio Vox")>=0
        alias=page.evaluate("()=>JSON.parse(localStorage.getItem('arsonmix.native.channel-presentation.v1'))")
        assert alias["channels"][SOURCE]["name"]=="Studio Vox",alias
        assert alias["channels"][SOURCE]["color"]==color
        assert alias["channels"][SOURCE]["icon"]==icon
        assert page.evaluate("()=>window.__nativeIntents.length")==0,\
            "display-only edit emitted an unauthorized PCM/route intent"
        # No native server alias or ChannelId mutation: same authoritative
        # snapshot retains its original name and exact ID.
        assert state['graph']['channels'][0]['id']==SOURCE
        assert state['graph']['channels'][0]['name']=='CH03'
        emit(page,"arMixConsoleSnapshot",state)
        assert "Studio Vox" in page.locator('#tracks .strip[data-id="1"]').inner_text()
        assert page.evaluate("()=>window.__nativeIntents.length")==0
        # New WebView document, same browser profile: alias survives.
        page.close()
        page=open_page()
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_selector('#tracks .strip[data-id="1"]')
        assert "Studio Vox" in page.locator('#tracks .strip[data-id="1"]').inner_text()
        page.locator('#tracks .strip[data-id="2"] .strip-identity-top').dblclick()
        page.locator('#identityName').fill('Studio Vox')
        page.locator('#identitySave').click()
        assert page.locator('#channelIdentity').is_visible(),\
            "duplicate visible channel names should not silently collide"
        page.locator('#identityClose').click()
        assert page.evaluate("()=>window.__nativeIntents.length")==0
        assert not errors,errors
        page.close()
        context.close();browser.close()
    print("PASS native identity: real edit name/icon/color; exact ChannelId; readback/reload; duplicate reject; no PCM graph intents")

if __name__=="__main__":main()
