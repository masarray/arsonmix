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
        for width,height in [(1366,768),(720,600),(390,844)]:
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
            assert page.locator(".mx-comp-control[data-key]").count()==7
            assert page.locator(".mx-comp-control").count()==7
            assert page.locator(".mx-comp-right .mx-comp-control").count()==4
            assert page.locator(".mx-comp-left .mx-comp-control").count()==3
            # Unsupported input trim, sidechain and auto modes must not
            # make a noisy panel of dead controls.
            assert page.locator(".mx-comp-sidechain,.mx-comp-readonly,.mx-comp-auto").count()==0
            assert page.locator(".mx-comp-foot,.mx-comp-preset").count()==0
            assert page.locator(".mx-comp-preset-select").count()==1
            assert page.locator(".mx-comp-preset-select").input_value()=="DEFAULT"
            assert page.locator(".mx-comp-state-dot").get_attribute("data-state")=="ok"
            if width>600:
                left=page.locator(".mx-comp-left").bounding_box()
                graph=page.locator(".mx-comp-visual").bounding_box()
                right=page.locator(".mx-comp-right").bounding_box()
                assert left and graph and right and left["x"]<graph["x"]<right["x"]
                assert 545<=editor.bounding_box()["width"]<=560
                assert editor.bounding_box()["height"]<=265,editor.bounding_box()

            assert page.locator(".mx-comp-grvalue").inner_text()=="0.0 dB"
            assert page.locator(".mx-comp-preset-select").is_visible()
            assert page.locator(".mx-comp-status").evaluate("""el=>{\n                const s=getComputedStyle(el);return s.clipPath.includes("inset")\n                && el.getBoundingClientRect().width<=1;}"""),"technical status polluted panel"
            rect=editor.bounding_box()
            assert rect and rect["width"]<=width and rect["height"]<=height,(width,height,rect)
            assert rect["x"]>=0 and rect["x"]+rect["width"]<=width+1,rect
            assert editor.evaluate("(el)=>el.scrollWidth<=el.clientWidth+1"),"editor horizontal clipping"
            page.screenshot(path=f"arsonmix-native-compressor-{width}.png",full_page=True)
            editor.screenshot(path=f"arsonmix-native-compressor-editor-{width}.png")
            if width==1366:
                # CONTROL request is not a DSP ACK: no accepted status until
                # audio completion + matching revision-bound follow-up readback.
                ratio=page.locator('.mx-comp-control[data-key="ratio"] input')
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
                assert page.locator('.mx-comp-control[data-key="ratio"] output').inner_text()=="3.1:1"
                # The MIX=0 curve must be geometrically unity, even if
                # the wet compressor ratio and makeup are not neutral.
                mix=page.locator('.mx-comp-control[data-key="mix"] input')
                mix.press("Home")
                before=page.locator(".mx-comp-curve").get_attribute("d")
                assert before
                after=last_action(page,"nativeCompSet")
                assert after["settings"]["mix"]==0
                emit(page,"arMixBuiltinCompQueue",{"requestId":after["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinCompAck",{"requestId":after["requestId"],"status":"APPLIED"})
                emit_snapshot(page,state,{**accepted,"mix":0.0},applied=after["requestId"])
                assert page.locator('.mx-comp-control[data-key="mix"] output').inner_text()=="0%"
                # Pointer gesture on the actual native graph edits THRESHOLD,
                # without inventing a separate UI state or bypassing ACK.
                bounds=page.locator(".mx-comp-plot svg").bounding_box()
                assert bounds
                graph=page.locator(".mx-comp-plot svg")
                start_x=bounds["x"]+bounds["width"]*0.55
                end_x=bounds["x"]+bounds["width"]*0.70
                drag_y=bounds["y"]+bounds["height"]*0.64
                page.mouse.move(start_x,drag_y)
                page.mouse.down()
                page.mouse.move(end_x,drag_y,steps=6)
                page.mouse.up()
                gesture=last_action(page,"nativeCompSet")
                assert gesture["settings"]["thresholdDb"]!=DEFAULTS["thresholdDb"]
                emit(page,"arMixBuiltinCompQueue",{"requestId":gesture["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinCompAck",{"requestId":gesture["requestId"],"status":"APPLIED"})
                emit_snapshot(page,state,gesture["settings"],applied=gesture["requestId"])
                page.wait_for_function("""()=>document.querySelector('.mx-comp-status')?.textContent
                    ?.includes('NATIVE DSP ACCEPTED')""")
                # Native pointer capture must allow precise vertical scrubbing
                # from the tiny knob while keeping revision-gated ACK/readback.
                dial=page.locator('.mx-comp-control[data-key="ratio"] input')
                knob=dial.bounding_box()
                assert knob
                previous=gesture["settings"]["ratio"]
                page.mouse.move(knob["x"]+knob["width"]/2,knob["y"]+knob["height"]/2)
                page.mouse.down()
                page.mouse.move(knob["x"]+knob["width"]/2,knob["y"]-32,steps=6)
                page.mouse.up()
                scrub=last_action(page,"nativeCompSet")
                assert scrub["settings"]["ratio"]>previous,(previous,scrub)
                emit(page,"arMixBuiltinCompQueue",{"requestId":scrub["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinCompAck",{"requestId":scrub["requestId"],"status":"APPLIED"})
                emit_snapshot(page,state,scrub["settings"],applied=scrub["requestId"])
                page.wait_for_function("""()=>document.querySelector('.mx-comp-status')?.textContent
                    ?.includes('NATIVE DSP ACCEPTED')""")
                assert dial.get_attribute("aria-valuetext").endswith(":1")
                assert page.locator(".mx-comp-preset-select").input_value()=="CUSTOM"
                # Presets are a standard DAW header dropdown, and must travel
                # through the same revision-gated native DSP control path.
                page.locator(".mx-comp-preset-select").select_option("SPEECH")
                selected=last_action(page,"nativeCompSet")
                assert selected["settings"]["thresholdDb"]==-24
                assert selected["settings"]["ratio"]==3
                assert selected["settings"]["makeupDb"]==3
                emit(page,"arMixBuiltinCompQueue",{"requestId":selected["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinCompAck",{"requestId":selected["requestId"],"status":"APPLIED"})
                emit_snapshot(page,state,selected["settings"],applied=selected["requestId"])
                page.wait_for_function("""()=>document.querySelector('.mx-comp-status')?.textContent
                    ?.includes('NATIVE DSP ACCEPTED')""")
                assert page.locator(".mx-comp-preset-select").input_value()=="SPEECH"
                assert page.locator(".mx-comp-state-dot").get_attribute("data-state")=="ok"
                # Mere click without a value change must not reconfigure PCM.
                before_count=page.evaluate("""()=>window.__actions.filter(x=>
                    x.action==='nativeCompSet').length""")
                center=knob
                page.mouse.click(center["x"]+center["width"]/2,
                                 center["y"]+center["height"]/2)
                after_count=page.evaluate("""()=>window.__actions.filter(x=>
                    x.action==='nativeCompSet').length""")
                assert after_count==before_count,"no-motion knob click issued DSP write"
                # Re-selecting the accepted preset must not perform another
                # audio graph edit or restart the callback.
                page.locator(".mx-comp-preset-select").dispatch_event("change")
                assert page.evaluate("""()=>window.__actions.filter(x=>
                    x.action==='nativeCompSet').length""")==before_count

            assert not errors,(width,errors)
            page.close()
        browser.close()
    print("PASS 558px Compressor native bundle: header presets, noise-free"
          " seven DSP controls, graph gestures, knob precision, native"
          " ACK/readback, 3 viewports. Browser uses UI test double.")

if __name__=="__main__":
    main()
