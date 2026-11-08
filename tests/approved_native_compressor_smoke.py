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

def emit_snapshot(page,state,values,applied=None,gr=0.0,
                  input_db=-72.0,output_db=-72.0,serial=0):
    action=last_action(page,"nativeCompSnapshot")
    packet={"requestId":action["requestId"],"channelId":SOURCE,
            "instanceId":INSTANCE,"slot":0,"status":"ACCEPTED",
            "deviceEpoch":state["device"]["epoch"],
            "graphRevision":state["graph"]["revision"],"schemaVersion":1,
            "sampleRate":48000,"addedLatencySamples":0,
            "gainReductionDb":gr,
            "inputPeakDbfs":input_db,"outputPeakDbfs":output_db,
            "meterBlockSerial":serial,"settings":copy.deepcopy(values)}
    if applied:packet["appliedRequestId"]=applied
    emit(page,"arMixBuiltinCompSnapshot",packet)

def apply_native_edit_chain(page,state,first,limit=8):
    """Complete ordered realtime DSP edits, including one coalesced final drag.

    A pointer can emit an initial control request and a delayed final setting.
    The native stub must acknowledge BOTH (and read back each matching ID);
    asserting after the first ACK would mistake valid DSP coalescing for a hang.
    """
    command=first
    seen=set()
    for _ in range(limit):
        request=command["requestId"]
        assert request not in seen,("duplicate DSP control request",request)
        seen.add(request)
        emit(page,"arMixBuiltinCompQueue",{"requestId":request,"status":"QUEUED"})
        emit(page,"arMixBuiltinCompAck",{"requestId":request,"status":"APPLIED"})
        emit_snapshot(page,state,command["settings"],applied=request)
        latest=last_action(page,"nativeCompSet")
        if latest["requestId"]!=request:
            command=latest
            continue
        page.wait_for_function("""()=>document.querySelector('.mx-comp-status')
            ?.textContent?.includes('NATIVE DSP ACCEPTED')""",timeout=3500)
        return command
    raise AssertionError("native DSP edit coalescer did not drain within bounded ACKs")

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
                  const prior=window.__nativeListeners.get(name);
                  window.__nativeListeners.set(name,prior
                    ?payload=>{prior(payload);callback(payload);}
                    :callback);}}};""")
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
            state["graph"]["channels"][0]["fxRackBypassed"]=False
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
            # A pending native snapshot must survive multiple meter-poll ticks.
            # Previously each 160ms tick changed readId, starving slower DSP
            # readbacks and causing ON/Bypass UI to remain unconfirmed.
            first_read=last_action(page,"nativeCompSnapshot")["requestId"]
            page.wait_for_timeout(360)
            assert last_action(page,"nativeCompSnapshot")["requestId"]==first_read,\
                "meter poll overwrote outstanding native readback"
            emit_snapshot(page,state,DEFAULTS)
            status_at_first_readback=page.locator(".mx-comp-status").inner_text()
            if "NATIVE DSP ACCEPTED" not in status_at_first_readback:
                print("COMP-FIRST-SNAPSHOT-DEBUG",{
                    "status":status_at_first_readback,
                    "actions":page.evaluate("""()=>window.__actions.filter(x=>
                        x.action==='nativeCompSnapshot').slice(-4)"""),
                    "current":page.evaluate("""()=>{const c=window.__ARMIX_NATIVE_M2__?.state()?.current;
                        return {admitted:c?.admitted,epoch:c?.device?.epoch,
                        revision:c?.graph?.revision,first:c?.graph?.channels?.[0]};}"""),
                    "instance":INSTANCE})
                # The WebView may have requested a fresher timer readback while
                # the test replied. Respond to that exact latest request.
                emit_snapshot(page,state,DEFAULTS)
            page.wait_for_function("""()=>document.querySelector('.mx-comp-status')?.textContent
                ?.includes('NATIVE DSP ACCEPTED')""",timeout=3000)
            assert page.locator(".mx-comp-plot svg").count()==1
            assert page.locator(".mx-comp-curve").count()==1
            assert page.locator(".mx-comp-brand").inner_text()=="Compressor"
            assert page.locator(".mx-comp-plot-brand").count()==0
            assert "ARMIX   COMPRESSOR" not in page.locator(".mx-comp-plot").inner_text()
            assert page.locator(".mx-comp-display").bounding_box()["height"]>=260
            assert page.locator(".mx-comp-display .mx-comp-meter-bank").count()==1
            assert page.locator(".mx-comp-meter-lane").count()==3
            assert page.locator(".mx-comp-meter-caption").all_inner_texts()==["IN","OUT","GR"]
            assert page.locator(".mx-comp-detector-value").inner_text()=="INTERNAL · STEREO LINK"
            assert page.locator(".mx-comp-display").evaluate("""el=>{
                const svg=el.querySelector('svg').getBoundingClientRect();
                const meters=el.querySelector('.mx-comp-meter-bank').getBoundingClientRect();
                const frame=el.getBoundingClientRect();
                return svg.x<meters.x&&meters.right<=frame.right+1;
            }"""),"meters must be inside black LCD to the RIGHT of graph"
            assert page.locator(".mx-comp-control[data-key]").count()==7
            # One shared SVG per dial, not seven bitmap frames or animated canvases.
            assert (out/"assets"/"compressor-knob-metal.svg").is_file()
            dials=page.locator(".mx-comp-dial")
            assert dials.count()==7
            appearance=dials.evaluate_all("""nodes=>nodes.map(el=>{
                const ring=getComputedStyle(el);
                const face=getComputedStyle(el,'::before');
                const needle=getComputedStyle(el,'::after');
                return {face:face.backgroundImage,ring:ring.backgroundImage,
                    transform:needle.transform,angle:el.style.getPropertyValue('--dial-angle'),
                    width:el.getBoundingClientRect().width,
                    filter:ring.filter,animation:ring.animationName};
            })""")
            assert all("compressor-knob-metal.svg" in a["face"] for a in appearance),appearance
            assert all("conic-gradient" in a["ring"] for a in appearance),appearance
            assert all(a["angle"].endswith("deg") for a in appearance),appearance
            assert all(a["width"]>=30 and a["filter"]=="none" and
                a["animation"]=="none" for a in appearance),appearance
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
            if width>650:
                left=page.locator(".mx-comp-left").bounding_box()
                graph=page.locator(".mx-comp-visual").bounding_box()
                right=page.locator(".mx-comp-right").bounding_box()
                assert left and graph and right and left["x"]<graph["x"]<right["x"]
                size=editor.bounding_box()
                if width>850: assert 780<=size["width"]<=805,size
                else: assert 700<=size["width"]<=720,size
                assert 350<=size["height"]<=415,size

            # All floating-window close controls share one computed 24px
            # chrome, independent of their historical SVG/text glyph markup.
            for selector in ("#identityClose","#pluginClose","#fastClose",
                             "#wireConfigClose","#wireWsClose","#routeChoiceClose",
                             ".mx-peq-close",".mx-comp-close"):
                visual=page.locator(selector).evaluate("""el=>{
                  const s=getComputedStyle(el),icon=getComputedStyle(el,'::after');
                  return {width:s.width,height:s.height,rounded:s.borderRadius,
                    glyph:s.fontSize,icon:icon.width,iconContent:icon.content};
                }""")
                assert visual["width"]=="24px" and visual["height"]=="24px", (selector,visual)
                assert visual["glyph"]=="0px" and visual["icon"]=="12px",(selector,visual)
            audio_close=page.evaluate("""()=>{
              const b=document.createElement('button');
              b.className='armix-floating-close';b.setAttribute('aria-label','Close Audio I/O');
              document.body.appendChild(b);const s=getComputedStyle(b);
              const out={width:s.width,height:s.height,icon:getComputedStyle(b,'::after').width};
              b.remove();return out;
            }""")
            assert audio_close=={"width":"24px","height":"24px","icon":"12px"},audio_close
            power=page.locator(".mx-comp-power")
            assert power.get_attribute("data-fx-state")=="on"
            assert power.get_attribute("aria-pressed")=="true"
            assert not power.is_disabled()
            assert "rgb(118, 240, 185)" in power.evaluate("(el)=>getComputedStyle(el).color")
            # Scalar bypass readbacks must not rebuild the structural FX rack.
            state["graph"]["channels"][0]["inserts"][0]["bypassed"]=True
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            page.wait_for_function("""()=>document.querySelector('.mx-comp-power')
                ?.dataset.fxState==='off'""")
            assert power.get_attribute("aria-pressed")=="false"
            page.wait_for_function("""()=>getComputedStyle(
                document.querySelector('.mx-comp-power')).color==='rgb(233, 155, 159)'""")
            state["graph"]["channels"][0]["inserts"][0]["bypassed"]=False
            state["graph"]["channels"][0]["fxRackBypassed"]=True
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            page.wait_for_function("""()=>document.querySelector('.mx-comp-power')
                ?.dataset.fxState==='rack-off'""")
            assert power.is_disabled()
            page.wait_for_function("""()=>getComputedStyle(
                document.querySelector('.mx-comp-power')).color==='rgb(240, 196, 131)'""")
            state["graph"]["channels"][0]["fxRackBypassed"]=False
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            page.wait_for_function("""()=>document.querySelector('.mx-comp-power')
                ?.dataset.fxState==='on'""")
            assert not power.is_disabled()
            page.wait_for_function("""()=>getComputedStyle(
                document.querySelector('.mx-comp-power')).color==='rgb(118, 240, 185)'""")
            assert page.locator(".mx-comp-grvalue").inner_text()=="0.0 dB"
            assert page.locator(".mx-comp-meter-gr").get_attribute("aria-valuenow")=="0.0"
            assert page.locator(".mx-comp-invalue").inner_text()=="−∞"
            assert page.locator(".mx-comp-outvalue").inner_text()=="−∞"
            # Actual PCM meter snapshots: measured pre/post dBFS and GR travel
            # through the accepted instance/revision gated native readback.
            # Every message must respond to its own distinct in-flight ID.
            first_live_request=last_action(page,"nativeCompSnapshot")["requestId"]
            page.wait_for_function("""id=>window.__actions.filter(x=>
                x.action==='nativeCompSnapshot').slice(-1)[0]?.requestId!==id""",
                arg=first_live_request,timeout=3000)
            emit_snapshot(page,state,DEFAULTS,gr=6.5,input_db=-10.0,output_db=-16.5,serial=12)
            page.wait_for_function("""()=>{
                return document.querySelector('.mx-comp-invalue')?.textContent==='-10.0' &&
                    document.querySelector('.mx-comp-outvalue')?.textContent==='-16.5' &&
                    document.querySelector('.mx-comp-grvalue')?.textContent==='6.5 dB';
            }""")
            assert page.locator(".mx-comp-meter-bank").get_attribute("data-live")=="true"
            levels=page.locator(".mx-comp-meter-fill").evaluate_all("""els=>
                els.map(el=>parseFloat(el.style.height))""")
            assert levels[0]>levels[1]>0 and levels[2]>0,levels
            # The 110ms meter animation must settle before reading rendered
            # geometry. getComputedStyle(bottom) is a USED pixel offset even
            # when the CSS declaration is 'auto' for a top-anchored bar.
            page.wait_for_function("""()=>{
                const bar=document.querySelector('.mx-comp-meter-gr');
                if(!bar)return false;
                const rail=bar.parentElement.getBoundingClientRect();
                return rail.height>20&&
                    bar.getBoundingClientRect().height/rail.height>=.19;
            }""",timeout=2500)
            geometry=page.locator(".mx-comp-meter-fill").evaluate_all("""els=>
                els.map(el=>{
                    const bar=el.getBoundingClientRect();
                    const rail=el.parentElement.getBoundingClientRect();
                    const css=getComputedStyle(el);
                    return {top:bar.top,railTop:rail.top,railHeight:rail.height,
                        bottom:css.bottom,cssTop:css.top,height:bar.height};
                })""")
            # 6.5 dB GR occupies 21.7% of rail, anchored at TOP zero.
            # IN/OUT remain bottom-anchored as conventional audio levels.
            assert geometry[0]["bottom"]==geometry[1]["bottom"]=="0px",geometry
            assert geometry[2]["cssTop"]=="2px",geometry
            assert geometry[2]["bottom"]!="0px",geometry
            assert 1<=geometry[2]["top"]-geometry[2]["railTop"]<=5,geometry
            assert .19<=geometry[2]["height"]/geometry[2]["railHeight"]<=.24,geometry
            assert page.locator(".mx-comp-meter-gr").get_attribute("aria-valuenow")=="6.5"
            readout_boxes=page.locator(".mx-comp-meter-lane output").evaluate_all("""els=>
                els.map(el=>{const r=el.getBoundingClientRect();
                    return {left:r.left,right:r.right,width:r.width};})""")
            assert all(readout_boxes[i]["right"]+0.5<=readout_boxes[i+1]["left"]
                for i in range(2)),("overlapping LCD meter numbers",readout_boxes,width)
            assert page.locator(".mx-comp-preset-select").is_visible()
            assert page.locator(".mx-comp-status").evaluate("""el=>{\n                const s=getComputedStyle(el);return s.clipPath.includes("inset")\n                && el.getBoundingClientRect().width<=1;}"""),"technical status polluted panel"
            rect=editor.bounding_box()
            assert rect and rect["width"]<=width and rect["height"]<=height,(width,height,rect)
            assert rect["x"]>=0 and rect["x"]+rect["width"]<=width+1,rect
            assert editor.evaluate("(el)=>el.scrollWidth<=el.clientWidth+1"),"editor horizontal clipping"
            page.screenshot(path=f"arsonmix-native-compressor-{width}.png",full_page=True)
            editor.screenshot(path=f"arsonmix-native-compressor-editor-{width}.png")
            # If native callback serial stalls, frozen PCM levels must decay
            # to silence rather than misleading the singer during reconnect.
            page.wait_for_function("""()=>document.querySelector('.mx-comp-meter-bank')
                ?.dataset.live==='false'""",timeout=2000)
            assert page.locator(".mx-comp-invalue").inner_text()=="−∞"
            assert page.locator(".mx-comp-outvalue").inner_text()=="−∞"
            assert page.locator(".mx-comp-grvalue").inner_text()=="0.0 dB"
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
                final_gesture=apply_native_edit_chain(page,state,gesture)
                assert final_gesture["settings"]["thresholdDb"]!=DEFAULTS["thresholdDb"]
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
                final_scrub=apply_native_edit_chain(page,state,scrub)
                assert final_scrub["settings"]["ratio"]>=scrub["settings"]["ratio"]
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

            if width==1366:
                # Press Close (not hover): global button must visibly turn red
                # while pointer is held and close the dialog on release.
                closer=page.locator(".mx-comp-close")
                box=closer.bounding_box();assert box
                x,y=box["x"]+box["width"]/2,box["y"]+box["height"]/2
                page.mouse.move(x,y);page.mouse.down()
                red=closer.evaluate("(el)=>getComputedStyle(el).backgroundImage")
                assert "rgb(204, 71, 83)" in red,red
                page.screenshot(path="arsonmix-close-pressed-red.png")
                page.mouse.up()
                assert editor.is_hidden(), "native Compressor Close did not dismiss"
            assert not errors,(width,errors)
            page.close()
        browser.close()
    print("PASS full-size Compressor GUI: 3 native PCM meter columns inside"
          " LCD, stale serial decay, larger EQ-scale panel, preset header,"
          " DSP ACK/readback, 3 viewports. Browser uses UI test double.")

if __name__=="__main__":
    main()
