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
            page.add_init_script("""window.__nativeListeners=new Map();window.__nativeIntents=[];window.__nativeEvents=[];
              window.__JUCE__={backend:{
                emitEvent(name,payload){window.__nativeEvents.push({name,payload});if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);},
                addEventListener(name,callback){window.__nativeListeners.set(name,callback);}}};""")
            page.goto((out/'index.html').as_uri())
            page.wait_for_function('()=>window.__ARMIX_NATIVE_M2__?.enabled')
            return page
        state=snapshot()
        page=open_page()
        ready=page.evaluate("()=>window.__nativeEvents.find(e=>e.name==='arMixGuiReady')?.payload")
        assert ready and ready['viewportWidth']==1366 and ready['viewportHeight']==768
        # Never show demo-only controls as clickable on a physical host.
        for id in ('saveScene','recallScene','copy','paste','group','ungroup','preview','clearCue','reset','sofToggle','sofTarget'):
            element=page.locator('#'+id)
            assert element.is_hidden() and element.is_disabled(),id
        assert page.locator('[data-native-monitor]').count()==1
        assert page.locator('#pluginsButton').is_visible()
        assert page.locator('#wireNavigatorToggle').is_visible()
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_selector('#tracks .strip[data-id="1"]')
        # Single click on the top identity, not double click, is the
        # discoverable native edit path. It must not create PCM intents.
        page.locator('#tracks .strip[data-id="1"] .strip-identity-top').click()
        editor=page.locator('#channelIdentity')
        assert editor.is_visible(),"single-click native identity editor did not open"
        assert page.locator('#identityName').evaluate("el=>el===document.activeElement")
        page.locator('#identityName').fill("Discard this")
        page.locator('#identityCancel').click()
        assert editor.is_hidden(),"Cancel must dismiss without modifying the channel"
        assert "Discard this" not in page.locator('#tracks .strip[data-id="1"]').inner_text()
        # A normal click on the nameplate is also actionable.
        page.locator('#tracks .strip[data-id="1"] .footer-label .name').click()
        assert editor.is_visible(),"footer name click did not open editor"
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
        # Keyboard F2 edits the selected loaded channel; Enter on focused
        # footer name is accessible when using keyboard-only navigation.
        page.locator('#tracks .strip[data-id="2"] .strip-identity-top').click()
        assert page.locator('#channelIdentity').is_visible()
        page.locator('#identityCancel').click()
        page.keyboard.press('F2')
        assert page.locator('#channelIdentity').is_visible(),"F2 edit not available"
        page.locator('#identityName').fill('Studio Vox')
        page.locator('#identitySave').click()
        assert page.locator('#channelIdentity').is_visible(),\
            "duplicate visible channel names should not silently collide"
        page.locator('#identityClose').click()
        page.locator('#tracks .strip[data-id="2"] .footer-label .name').focus()
        page.keyboard.press('Enter')
        assert page.locator('#channelIdentity').is_visible(),"keyboard Enter did not open editor"
        page.keyboard.press('Escape')
        assert page.locator('#channelIdentity').is_hidden()
        assert page.evaluate("()=>window.__nativeIntents.length")==0
        assert not errors,errors
        page.close()
        context.close();browser.close()
    print("PASS native identity: click/header/footer, F2/Enter, Cancel/Escape, edited name/icon/color, readback/reload, duplicate reject, exact ChannelId, zero PCM intents")

if __name__=="__main__":main()
