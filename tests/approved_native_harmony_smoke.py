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
            for width,height in ((1366,768),(720,600),(390,844)):
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
                state["graph"]["channels"][0]["fxRackBypassed"]=False
                state["graph"]["channels"][0]["inserts"].append({
                    "id":INSTANCE,"catalogId":ID,"name":"Harmony","bypassed":False})
                state["mixerSession"]["channels"]=copy.deepcopy(
                    state["graph"]["channels"])
                emit(page,"arMixConsoleSnapshot",state)
                if width<=500:page.locator("#bankSelect").select_option("0")
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
                close=page.locator("#mxHClose")
                power=page.locator("#mxHPower")
                # This is the EXACT same shared CSS, SVG-less X and header
                # Power material used by both native EQ and Compressor.
                closeSkin=close.evaluate("""el=>{
                  const s=getComputedStyle(el),x=getComputedStyle(el,'::after');
                  return {w:s.width,h:s.height,icon:x.width,glyph:s.fontSize};
                }""")
                assert closeSkin=={"w":"24px","h":"24px","icon":"12px","glyph":"0px"},closeSkin
                assert power.evaluate("(el)=>getComputedStyle(el).width")=="26px"
                assert power.get_attribute("data-fx-state")=="on"
                assert power.get_attribute("aria-pressed")=="true"
                assert not power.is_disabled()
                assert power.evaluate("(el)=>getComputedStyle(el).color")=="rgb(118, 240, 185)"
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
                # G is a literal circle, subtly larger than unselected notes.
                # Check computed visual CSS, not a class-name-only assertion.
                note_g=page.locator('.mx-harm-note[data-key="7"]')
                note_c=page.locator('.mx-harm-note[data-key="0"]')
                assert "active" in (note_g.get_attribute("class") or "")
                # Reduced note rail radius leaves an actual margin inside the
                # wheel, including enlarged selected buttons on mobile.
                wheel_box=page.locator("#mxHWheel").bounding_box()
                note_box=note_g.bounding_box()
                assert wheel_box and note_box
                wheel_cx=wheel_box["x"]+wheel_box["width"]/2
                wheel_cy=wheel_box["y"]+wheel_box["height"]/2
                note_cx=note_box["x"]+note_box["width"]/2
                note_cy=note_box["y"]+note_box["height"]/2
                from math import hypot
                wheel_margin=wheel_box["width"]/2-(
                    hypot(wheel_cx-note_cx,wheel_cy-note_cy)+note_box["width"]/2)
                assert wheel_margin>=10,(width,wheel_margin,wheel_box,note_box)
                # Font size applies immediately for crisp text; only the
                # circular material and scale transition over 150ms.
                page.wait_for_timeout(170)
                geometry=note_g.evaluate("""e=>{
                    const s=getComputedStyle(e);
                    return {w:s.width,h:s.height,radius:s.borderRadius,
                      font:parseFloat(s.fontSize),shadow:s.boxShadow,
                      background:s.backgroundImage};
                }""")
                assert geometry["w"]==geometry["h"],geometry
                assert geometry["radius"]=="50%",geometry
                assert geometry["font"]>float(note_c.evaluate(
                    "e=>parseFloat(getComputedStyle(e).fontSize)")),geometry
                assert geometry["shadow"]!="none" and "gradient" in geometry["background"],geometry
                # The groove is a dark conic ring behind the lit arc; the
                # pointer and arc both derive from the SAME accepted knob value.
                dial=page.locator('.mx-harm-slider input[data-field="wet"]')
                def dial_visual():
                    return dial.evaluate("""e=>{
                        const p=e.parentElement;
                        const face=p.querySelector('.mx-harm-dial-face');
                        return {turn:parseFloat(p.style.getPropertyValue('--dial-turn')),
                          sweep:parseFloat(p.style.getPropertyValue('--dial-sweep')),
                          groove:getComputedStyle(face,'::before').backgroundImage,
                          level:getComputedStyle(face,'::after').backgroundImage,
                          mask:getComputedStyle(face,'::after').maskImage};
                    }""")
                arc=dial_visual()
                assert abs(arc["sweep"]-121.5)<0.0001,arc
                assert abs(arc["turn"]+13.5)<0.0001,arc
                assert "conic-gradient" in arc["groove"],arc
                assert "conic-gradient" in arc["level"],arc
                assert "radial-gradient" in arc["mask"],arc
                dial.evaluate("""e=>{
                    e.value='75';e.dispatchEvent(new Event('input',{bubbles:true}));
                }""")
                wet_edit=last_action(page,"nativeHarmonySetSettings")
                assert abs(wet_edit["settings"]["wet"]-.75)<.0001
                changed={**locked,"wet":.75}
                apply_edit(page,state,changed,7,0)
                arc2=dial_visual()
                assert abs(arc2["sweep"]-202.5)<.0001,arc2
                assert abs(arc2["turn"]-67.5)<.0001,arc2
                assert page.locator("#mxHKeyReadout").inner_text()=="G"
                # Tactile Major/Minor: active versus idle surfaces must occupy
                # exactly the same box and have the same outer shadow offset.
                major=page.locator('.mx-harm-scale [data-scale="0"]')
                minor=page.locator('.mx-harm-scale [data-scale="1"]')
                def face_metrics(locator):
                    return locator.evaluate("""e=>{
                      const r=e.getBoundingClientRect(),c=getComputedStyle(e);
                      return {w:r.width,h:r.height,y:r.y,border:c.borderWidth,
                        radius:c.borderRadius,outer:c.boxShadow.match(
                          /\\b0px 1px 0px 0px/)?true:false};
                    }""")
                major_face,minor_face=face_metrics(major),face_metrics(minor)
                assert major_face==minor_face,(major_face,minor_face)
                assert major_face["outer"],major_face
                minor.click()
                minor_edit=last_action(page,"nativeHarmonySetSettings")
                assert minor_edit["settings"]["scale"]==1
                minor_values={**changed,"scale":1}
                apply_edit(page,state,minor_values,7,1)
                assert face_metrics(major)==face_metrics(minor)
                major.click()
                apply_edit(page,state,changed,7,0)
                assert face_metrics(major)==face_metrics(minor)
                # Singer icons are authored once as a tiny shared vector.
                # All three UI choices are REAL audible native voice counts.
                for count in (3,4,5):
                    button=page.locator(f'.mx-harm-voices [data-voices="{count}"]')
                    assert button.locator('svg.mx-harm-voice-glyph').count()==1
                    button.click()
                    voice_edit=last_action(page,"nativeHarmonySetSettings")
                    assert voice_edit["settings"]["voices"]==count
                    values={**changed,"voices":count}
                    apply_edit(page,state,values,7,0)
                    assert button.get_attribute("aria-pressed")=="true"
                    assert page.locator("#mxHKeyReadout").inner_text()=="G"
                # Power is driven by accepted native graph bypass, not local
                # DOM optimism, in both Manual and Music modes.
                state["graph"]["channels"][0]["inserts"][0]["bypassed"]=True
                emit(page,"arMixConsoleSnapshot",state)
                page.wait_for_function(
                    "()=>document.querySelector('#mxHPower')?.dataset.fxState==='off'")
                assert power.get_attribute("aria-pressed")=="false"
                assert power.evaluate("(el)=>getComputedStyle(el).color")=="rgb(233, 155, 159)"
                state["graph"]["channels"][0]["inserts"][0]["bypassed"]=False
                state["graph"]["channels"][0]["fxRackBypassed"]=True
                emit(page,"arMixConsoleSnapshot",state)
                page.wait_for_function(
                    "()=>document.querySelector('#mxHPower')?.dataset.fxState==='rack-off'")
                assert power.is_disabled()
                assert power.evaluate("(el)=>getComputedStyle(el).color")=="rgb(240, 196, 131)"
                state["graph"]["channels"][0]["fxRackBypassed"]=False
                emit(page,"arMixConsoleSnapshot",state)
                page.wait_for_function(
                    "()=>document.querySelector('#mxHPower')?.dataset.fxState==='on'")
                assert not power.is_disabled()
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
                if width==1366:
                    rectClose=close.bounding_box()
                    assert rectClose
                    page.mouse.move(rectClose["x"]+12,rectClose["y"]+12)
                    page.mouse.down()
                    bg=close.evaluate("(el)=>getComputedStyle(el).backgroundImage")
                    assert "204, 71, 83" in bg or "152, 42, 54" in bg,bg
                    page.mouse.up()
                    assert editor.is_hidden(),"Harmony Close button failed"
                page.close()
            browser.close()
    print("PASS Harmony native editor: stable LCD, Music/Manual ACK, throttled readback, three viewports")

if __name__=="__main__":
    main()
