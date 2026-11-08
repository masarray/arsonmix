"""Actual approved /preview/ bundle: Compressor native FX insertion, accepted
instance, SVG curve, parameter ACK/readback and desktop/mobile screenshots.
JUCE backend is deliberately emulated here; native PCM is covered by CTest.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE

ROOT=Path(__file__).resolve().parents[1]
ID="armix.builtin.channel-compressor.v1"
INSTANCE="compressor-native-1"
DEFAULTS={"thresholdDb":-18.0,"ratio":3.0,"attackMs":10.0,"releaseMs":120.0,
          "kneeDb":6.0,"makeupDb":0.0,"mix":1.0}

def last_action(page,kind):
    actions=page.evaluate("kind=>window.__actions.filter(x=>x.action===kind)",kind)
    assert actions,kind+" action absent"
    return actions[-1]

def emit_snapshot(page,state,values,applied=None,gr=0.0):
    action=last_action(page,"nativeCompSnapshot")
    packet={"requestId":action["requestId"],"channelId":SOURCE,
            "instanceId":INSTANCE,"slot":0,"status":"ACCEPTED",
            "deviceEpoch":state["device"]["epoch"],
            "graphRevision":state["graph"]["revision"],"schemaVersion":1,
            "sampleRate":48000,"addedLatencySamples":0,
            "gainReductionDb":gr,"settings":copy.deepcopy(values)}
    if applied:packet["appliedRequestId"]=applied
    emit(page,"arMixBuiltinCompSnapshot",packet)

def main():
    spec=importlib.util.spec_from_file_location("bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-comp-") as directory,sync_playwright() as pw:
        out=Path(directory);bundle.stage(out)
        browser=pw.chromium.launch(headless=True)
        for width,height in [(1366,768),(390,844)]:
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
                  window.__nativeListeners.set(name,callback);}}};""")
            page.goto((out/"index.html").as_uri())
            page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeCompressor")
            state=snapshot()
            state["graph"]["capabilities"]["supportedOps"]+=["LOAD_INSERT","MOVE_INSERT","REMOVE_INSERT"]
            emit(page,"arMixConsoleSnapshot",state)
            strip=page.locator('#tracks .strip[data-id="1"]')
            if width<=500:page.locator("#bankSelect").select_option("0")
            strip.locator('.fx-pick.empty').first.click()
            picker=page.locator("#fastResults [data-fast-native-comp]")
            picker.wait_for(state="visible")
            assert picker.count()==1, "exactly one built-in Compressor in FX picker"
            picker.click()
            intents=page.evaluate("""()=>window.__nativeIntents.filter(x=>
                x.operation==='LOAD_INSERT')""")
            assert len(intents)==1,intents
            assert intents[0]["payload"]["catalogId"]==ID
            assert page.locator('#mxNativeComp').is_hidden(),"editor opened before DSP insert"
            assert strip.locator('.fxslot-row[data-insert-id="compressor-native-1"]').count()==0
            state["graph"].update(revision="3",rosterRevision="3")
            state["graph"]["channels"][0]["inserts"].append({
                "id":INSTANCE,"catalogId":ID,"name":"Compressor","bypassed":False})
            state["mixerSession"].update(routeRevision="3",rosterRevision="3",
                channels=copy.deepcopy(state["graph"]["channels"]))
            ack(page,state,intents[0])
            emit(page,"arMixConsoleSnapshot",state)
            row=strip.locator('.fxslot-row[data-insert-id="compressor-native-1"]')
            row.wait_for(state="visible")
            page.keyboard.press("Escape")
            row.locator('[data-action="openPlugin"]').click()
            editor=page.locator("#mxNativeComp")
            editor.wait_for(state="visible")
            emit_snapshot(page,state,DEFAULTS)
            page.wait_for_function("""()=>document.querySelector('.mx-comp-status')?.textContent
                ?.includes('NATIVE DSP ACCEPTED')""")
            assert page.locator(".mx-comp-plot svg").count()==1
            assert page.locator(".mx-comp-curve").count()==1
            assert page.locator(".mx-comp-control").count()==7
            assert page.locator(".mx-comp-presets button").count()==5
            assert page.locator(".mx-comp-grvalue").inner_text()=="0.0 dB"
            rect=editor.bounding_box()
            assert rect and rect["width"]<=width and rect["height"]<=height,(width,height,rect)
            page.screenshot(path=f"arsonmix-native-compressor-{width}.png",full_page=True)
            if width==1366:
                # CONTROL request is not a DSP ACK: no accepted status until
                # audio completion + matching revision-bound follow-up readback.
                ratio=page.locator('.mx-comp-control').nth(1).locator('input')
                ratio.press("ArrowRight")
                edit=last_action(page,"nativeCompSet")
                assert abs(edit["settings"]["ratio"]-3.1)<.0001
                assert edit["instanceId"]==INSTANCE and edit["channelId"]==SOURCE
                assert not page.locator(".mx-comp-status").inner_text().startswith("NATIVE DSP ACCEPTED")
                emit(page,"arMixBuiltinCompQueue",{"requestId":edit["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinCompAck",{"requestId":edit["requestId"],"status":"APPLIED"})
                accepted={**DEFAULTS,"ratio":3.1}
                emit_snapshot(page,state,accepted,applied=edit["requestId"])
                page.wait_for_function("""()=>document.querySelector('.mx-comp-status')?.textContent
                    ?.includes('NATIVE DSP ACCEPTED')""")
                assert page.locator('.mx-comp-control').nth(1).locator("output").inner_text()=="3.1:1"
                # The MIX=0 curve must be geometrically unity, even if
                # the wet compressor ratio and makeup are not neutral.
                mix=page.locator('.mx-comp-control').nth(6).locator('input')
                mix.press("Home")
                before=page.locator(".mx-comp-curve").get_attribute("d")
                assert before
                after=last_action(page,"nativeCompSet")
                assert after["settings"]["mix"]==0
                emit(page,"arMixBuiltinCompQueue",{"requestId":after["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinCompAck",{"requestId":after["requestId"],"status":"APPLIED"})
                emit_snapshot(page,state,{**accepted,"mix":0.0},applied=after["requestId"])
                assert page.locator('.mx-comp-control').nth(6).locator("output").inner_text()=="0%"
            assert not errors,(width,errors)
            page.close()
        browser.close()
    print("PASS Compressor native bundle: ordered insert, no optimistic DSP state,"
          " seven controls, transfer curve, callback ACK/readback, 2 viewports."
          " UI test double, NOT ASIO hardware evidence.")

if __name__=="__main__":
    main()
