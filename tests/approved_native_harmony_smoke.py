"""Real assembled /preview/ Harmony editor: native readback, Music/Manual,
delayed ACK and responsive geometry. Emulated JUCE control transport only:
actual PCM/music guide evidence is covered by Windows native CTest.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, SOURCE, RECEIVER

ROOT=Path(__file__).resolve().parents[1]
ID="armix.builtin.arson-harmony.v1"
INSTANCE="harmony-native-1"
DEFAULT={
    "key":0,"scale":0,"voices":3,"correction":False,
    "guideMode":0,"guideChannelId":"0",
    "color":0.5,"space":0.6,"wet":0.45,"gainDb":0.0,
}

def last_action(page,kind):
    actions=page.evaluate(
        "kind=>window.__actions.filter(x=>x.action===kind)",kind)
    assert actions,"native "+kind+" request missing"
    return actions[-1]

def readback(page,state,values,key,scale,detected=(-1,-1),applied=None,
             request=None):
    action=request or last_action(page,"nativeHarmonySnapshot")
    packet={
        "requestId":action["requestId"],
        "channelId":SOURCE,"instanceId":INSTANCE,"status":"ACCEPTED",
        "deviceEpoch":state["device"]["epoch"],
        "graphRevision":state["graph"]["revision"],
        "schemaVersion":2,"sampleRate":48000,"latencySamples":1536,
        "settings":copy.deepcopy(values),"observedNote":-1,
        "guideDetectedKey":detected[0],"guideDetectedScale":detected[1],
        "guideConfidence":0.75 if detected[0]>=0 else 0.0,
        "effectiveKey":key,"effectiveScale":scale,
    }
    if applied:packet["appliedRequestId"]=applied
    emit(page,"arMixBuiltinHarmonySnapshot",packet)

def apply_edit(page,state,values,key,scale,detected=(-1,-1)):
    edit=last_action(page,"nativeHarmonySetSettings")
    assert edit["instanceId"]==INSTANCE and edit["channelId"]==SOURCE
    emit(page,"arMixBuiltinHarmonyQueue",
         {"requestId":edit["requestId"],"status":"QUEUED"})
    emit(page,"arMixBuiltinHarmonyAck",
         {"requestId":edit["requestId"],"status":"APPLIED"})
    readback(page,state,values,key,scale,detected,applied=edit["requestId"])
    return edit

def main():
    spec=importlib.util.spec_from_file_location(
        "native_bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-harmony-") as directory:
        out=Path(directory);bundle.stage(out)
        with sync_playwright() as playwright:
            browser=playwright.chromium.launch(headless=True)
            for width,height in ((1366,768),(720,600)):
                page=browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror",lambda error:errors.append(str(error)))
                page.add_init_script("""window.__nativeListeners=new Map();
                  window.__nativeIntents=[];window.__actions=[];
                  window.__JUCE__={backend:{
                    emitEvent(name,payload){
                      if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
                      if(name==='arMixAction')window.__actions.push(payload);},
                    addEventListener(name,callback){
                      const prior=window.__nativeListeners.get(name);
                      window.__nativeListeners.set(name,prior
                        ?payload=>{prior(payload);callback(payload);}
                        :callback);
                    }
                  }};""")
                page.goto((out/"index.html").as_uri())
                page.wait_for_function(
                    "()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeHarmony")
                state=snapshot()
                state["graph"]["capabilities"]["supportedOps"] += [
                    "LOAD_INSERT","MOVE_INSERT","REMOVE_INSERT"]
                state["graph"]["channels"][0]["inserts"].append({
                    "id":INSTANCE,"catalogId":ID,"name":"Harmony","bypassed":False})
                state["mixerSession"]["channels"]=copy.deepcopy(
                    state["graph"]["channels"])
                emit(page,"arMixConsoleSnapshot",state)
                strip=page.locator('#tracks .strip[data-id="1"]')
                row=strip.locator(
                    '.fxslot-row[data-insert-id="harmony-native-1"]')
                row.wait_for(state="visible",timeout=10000)
                row.locator('[data-action="openPlugin"]').click()
                editor=page.locator("#mxNativeHarmony")
                editor.wait_for(state="visible")
                # Native LCD must not fabricate manual/detected effective key
                # until the exact instance-scoped DSP snapshot arrives.
                assert page.locator("#mxHKeyReadout").inner_text()=="—"
                readback(page,state,DEFAULT,0,0)
                assert page.locator("#mxHKeyReadout").inner_text()=="C"
                assert page.locator("#mxHGuideStatus").inner_text()=="MANUAL"
                assert page.locator("#mxHSourcePicker").is_hidden()
                page.locator('[data-guide-mode="1"]').click()
                edit=last_action(page,"nativeHarmonySetSettings")
                assert edit["settings"]["guideMode"]==1
                assert edit["settings"]["guideChannelId"]==RECEIVER
                # UI is optimistic about toggle, but MUST NOT fabricate
                # an applied music key from a queued control message.
                assert page.locator("#mxHKeyReadout").inner_text()=="C"
                selected={**DEFAULT,"guideMode":1,"guideChannelId":RECEIVER}
                apply_edit(page,state,selected,0,0)
                assert page.locator("#mxHSourcePicker").is_visible()
                assert page.locator("#mxHGuideStatus").inner_text()=="LISTEN"
                # Music-mode polling uses the authoritative console tick.
                # Ten rapid ticks must never cause ten pending readbacks.
                before=len(page.evaluate(
                    "()=>window.__actions.filter(x=>x.action==='nativeHarmonySnapshot')"))
                page.wait_for_timeout(850)
                for _ in range(10):emit(page,"arMixConsoleSnapshot",state)
                page.wait_for_timeout(60)
                after=len(page.evaluate(
                    "()=>window.__actions.filter(x=>x.action==='nativeHarmonySnapshot')"))
                assert after==before+1,(before,after)
                old_poll=last_action(page,"nativeHarmonySnapshot")
                readback(page,state,selected,9,1,detected=(9,1),
                         request=old_poll)
                assert page.locator("#mxHKeyReadout").inner_text()=="Am"
                assert page.locator("#mxHGuideStatus").inner_text()=="AUTO"
                assert page.locator('[data-guide-mode="1"]').get_attribute(
                    "aria-pressed")=="true"
                # Manual note click exits Music. Effective LCD only follows
                # the corresponding APPLIED + verified native readback.
                page.locator('[data-key="7"]').click()
                manual=last_action(page,"nativeHarmonySetSettings")
                assert manual["settings"]["key"]==7
                assert manual["settings"]["guideMode"]==0
                assert manual["settings"]["guideChannelId"]=="0"
                assert page.locator("#mxHKeyReadout").inner_text()=="Am"
                locked={**DEFAULT,"key":7}
                apply_edit(page,state,locked,7,0)
                assert page.locator("#mxHKeyReadout").inner_text()=="G"
                assert page.locator("#mxHGuideStatus").inner_text()=="MANUAL"
                assert page.locator("#mxHSourcePicker").is_hidden()
                # A slow prior Music readback must never roll LCD back to Am.
                readback(page,state,selected,9,1,detected=(9,1),
                         request=old_poll)
                assert page.locator("#mxHKeyReadout").inner_text()=="G"
                assert not errors,errors
                rect=editor.bounding_box()
                assert rect and rect["width"]<=width+1,(width,rect)
                assert rect["x"]>=0 and rect["x"]+rect["width"]<=width+1,rect
                assert editor.evaluate(
                    "(el)=>el.scrollWidth<=el.clientWidth+1"),"horizontal clipping"
                editor.screenshot(path=f"arsonmix-native-harmony-editor-{width}.png")
                page.close()
            browser.close()
    print("PASS Harmony native editor: stable LCD, Music/Manual ACK, throttled readback, two viewports")

if __name__=="__main__":
    main()
