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
    "key":0,"scale":0,"voices":3,"voiceMode":0,"correction":False,
    "guideMode":0,"guideChannelId":"0",
    "color":0.5,"space":0.6,"wet":0.45,"gainDb":0.0,
}

def last_action(page,kind):
    actions=page.evaluate(
        "kind=>window.__actions.filter(x=>x.action===kind)",kind)
    assert actions,"native "+kind+" request missing"
    return actions[-1]

def readback(page,state,values,key,scale,detected=(-1,-1),applied=None,
             request=None,instance=INSTANCE,observed=-1):
    action=request or last_action(page,"nativeHarmonySnapshot")
    packet={
        "requestId":action["requestId"],
        "channelId":SOURCE,"instanceId":instance,"status":"ACCEPTED",
        "deviceEpoch":state["device"]["epoch"],
        "graphRevision":state["graph"]["revision"],
        "schemaVersion":3,"sampleRate":48000,"latencySamples":1536,
        "settings":copy.deepcopy(values),"observedNote":observed,
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
                    "id":INSTANCE,"catalogId":ID,"name":"Harmonice","bypassed":False})
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
                assert page.locator(".mx-harm-brand").inner_text()=="Harmonice"
                assert editor.get_attribute("aria-label")=="Harmonice"
                shell=editor.bounding_box()
                assert shell and abs(shell["width"]-min(800,width-14))<=1,(width,shell)
                # Native LCD must not fabricate manual/detected effective key
                # until the exact instance-scoped DSP snapshot arrives.
                assert page.locator("#mxHKeyReadout").inner_text()=="—"
                switch=page.get_by_role("switch",name="PITCH TUNE")
                assert switch.is_disabled() and switch.get_attribute("aria-busy")=="true"
                readback(page,state,DEFAULT,0,0,observed=57)
                # A native library list is a file query, not a DSP mutation.
                library=last_action(page,"nativeHarmonyUserPreset")
                assert library["operation"]=="list" and library["instanceId"]==INSTANCE
                def preset_result(action,op,**payload):
                    emit(page,"arMixBuiltinHarmonyPresetResult",{
                        "requestId":action["requestId"],"operation":op,
                        "status":"OK","instanceId":INSTANCE,
                        "channelId":SOURCE,
                        "deviceEpoch":state["device"]["epoch"],
                        "graphRevision":state["graph"]["revision"],
                        **payload})
                preset_result(library,"list",items=[])
                page.locator("#mxHSaveAs").click()
                form=page.locator("#mxHPresetForm")
                assert form.is_visible()
                form.locator("#mxHPresetName").fill("Singer Wide")
                form.locator("#mxHPresetConfirm").click()
                saving=last_action(page,"nativeHarmonyUserPreset")
                assert saving["operation"]=="saveAs" and saving["name"]=="Singer Wide"
                assert saving["instanceId"]==INSTANCE
                assert page.locator("#mxHPreset option[value^='user:']").count()==0
                saved_id="f4c0109b-54b9-4bb4-baa5-34cd52f7e413"
                preset_result(saving,"saveAs",presetId=saved_id,name="Singer Wide")
                library=last_action(page,"nativeHarmonyUserPreset")
                assert library["operation"]=="list"
                preset_result(library,"list",items=[{
                    "id":saved_id,"name":"Singer Wide","category":"My Presets"}])
                user_option="user:"+saved_id
                assert page.locator('#mxHPreset option[value="'+user_option+'"]').count()==1
                page.locator("#mxHPreset").select_option("custom")
                page.locator("#mxHPreset").select_option(user_option)
                loading=last_action(page,"nativeHarmonyUserPreset")
                assert loading["operation"]=="read" and loading["presetId"]==saved_id
                assert page.locator("#mxHPreset").input_value()=="custom"
                portable={k:v for k,v in DEFAULT.items()
                          if k not in ("guideMode","guideChannelId")}
                preset_result(loading,"read",presetId=saved_id,
                              name="Singer Wide",settings=portable)
                applying=last_action(page,"nativeHarmonySetSettings")
                assert applying["settings"]["guideChannelId"]=="0"
                assert applying["settings"]["voiceMode"]==0
                assert page.locator("#mxHPreset").input_value()=="custom"
                emit(page,"arMixBuiltinHarmonyQueue",
                     {"requestId":applying["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinHarmonyAck",
                     {"requestId":applying["requestId"],"status":"APPLIED"})
                readback(page,state,DEFAULT,0,0,applied=applying["requestId"])
                assert page.locator("#mxHPreset").input_value()==user_option
                assert switch.get_attribute("aria-checked")=="false"
                track=switch.locator(".mx-harm-switch-track").bounding_box()
                label=switch.get_by_text("PITCH TUNE").bounding_box()
                assert track and label and (track["width"],track["height"])==(32,16)
                assert abs(track["y"]+track["height"]/2-label["y"]-label["height"]/2)<1
                if width==1366:
                    editor.screenshot(path="arsonmix-native-harmony-editor-pitch-tune-off.png")
                switch.focus()
                switch.press("Space")
                pitch_edit=last_action(page,"nativeHarmonySetSettings")
                assert pitch_edit["settings"]["correction"] is True
                assert switch.get_attribute("aria-checked")=="false"
                assert switch.get_attribute("aria-busy")=="true"
                emit(page,"arMixBuiltinHarmonyAck",
                     {"requestId":pitch_edit["requestId"],"status":"APPLIED"})
                assert switch.get_attribute("aria-checked")=="false"
                readback(page,state,{**DEFAULT,"correction":True},0,0,
                         applied=pitch_edit["requestId"],observed=57)
                assert switch.get_attribute("aria-checked")=="true"
                assert switch.get_attribute("aria-busy")=="false"
                # P1: A changed user preset is Modified ONLY after DSP ACK +
                # accepted readback. Manager may Save a changed working sound.
                assert page.locator("#mxHPreset").input_value()=="custom"
                assert "Modified" in page.locator(
                    '#mxHPreset option[value="custom"]').inner_text()
                manager=page.locator("#mxHManage")
                assert not manager.is_disabled()
                manager.click()
                assert page.locator("#mxHPresetMenu").is_visible()
                page.locator("#mxHSaveExisting").click()
                assert form.is_visible()
                assert form.locator("#mxHPresetName").is_hidden()
                # Cancel must never send any native preset/disk operation.
                before=page.evaluate("""()=>window.__actions.filter(
                    x=>x.action==='nativeHarmonyUserPreset').length""")
                form.locator("#mxHPresetCancel").click()
                after=page.evaluate("""()=>window.__actions.filter(
                    x=>x.action==='nativeHarmonyUserPreset').length""")
                assert after==before
                manager.click()
                page.locator("#mxHSaveExisting").click()
                assert "OVERWRITE" in page.locator("#mxHPresetPrompt").inner_text()
                form.locator("#mxHPresetConfirm").click()
                overwrite=last_action(page,"nativeHarmonyUserPreset")
                assert overwrite["operation"]=="save" and overwrite["presetId"]==saved_id
                assert manager.is_disabled()
                # Wrong-instance response using correct requestId is ignored,
                # never consumes or falsely completes the genuine request.
                emit(page,"arMixBuiltinHarmonyPresetResult",{
                    "requestId":overwrite["requestId"],"operation":"save",
                    "status":"OK","channelId":SOURCE,
                    "deviceEpoch":state["device"]["epoch"],
                    "graphRevision":state["graph"]["revision"],
                    "instanceId":"WRONG-INSTANCE"})
                assert manager.is_disabled()
                preset_result(overwrite,"save",presetId=saved_id,name="Singer Wide")
                library=last_action(page,"nativeHarmonyUserPreset")
                assert library["operation"]=="list"
                preset_result(library,"list",items=[{
                    "id":saved_id,"name":"Singer Wide","category":"My Presets"}])
                assert page.locator("#mxHPreset").input_value()==user_option
                assert not manager.is_disabled()
                # Rename preserves stable user ID, does not touch audio.
                editor_count=page.evaluate("""()=>window.__actions.filter(
                    x=>x.action==='nativeHarmonySetSettings').length""")
                manager.click()
                page.locator("#mxHRenameExisting").click()
                name=page.locator("#mxHPresetName")
                assert name.is_visible()
                name.fill("Singer Wide 2")
                name.press("Enter")
                renaming=last_action(page,"nativeHarmonyUserPreset")
                assert renaming["operation"]=="rename"
                assert renaming["presetId"]==saved_id and renaming["name"]=="Singer Wide 2"
                preset_result(renaming,"rename")
                library=last_action(page,"nativeHarmonyUserPreset")
                assert library["operation"]=="list"
                preset_result(library,"list",items=[{
                    "id":saved_id,"name":"Singer Wide 2","category":"My Presets"}])
                assert page.locator("#mxHPreset").input_value()==user_option
                assert page.locator('#mxHPreset option[value="'+user_option+'"]').inner_text()=="Singer Wide 2"
                # Delete requires explicit confirmation. The accepted PCM
                # is retained; the disk user record alone is removed.
                manager.click()
                page.locator("#mxHDeleteExisting").click()
                assert "DELETE" in page.locator("#mxHPresetPrompt").inner_text()
                form.locator("#mxHPresetCancel").click()
                assert page.locator("#mxHPreset").input_value()==user_option
                manager.click()
                page.locator("#mxHDeleteExisting").click()
                form.locator("#mxHPresetConfirm").click()
                deletion=last_action(page,"nativeHarmonyUserPreset")
                assert deletion["operation"]=="delete" and deletion["presetId"]==saved_id
                preset_result(deletion,"delete")
                library=last_action(page,"nativeHarmonyUserPreset")
                assert library["operation"]=="list"
                preset_result(library,"list",items=[])
                assert page.locator("#mxHPreset option[value^='user:']").count()==0
                assert page.locator("#mxHPreset").input_value()=="custom"
                assert manager.is_disabled()
                assert page.evaluate("""()=>window.__actions.filter(
                    x=>x.action==='nativeHarmonySetSettings').length""")==editor_count
                switch.evaluate("el=>el.blur()")
                if width==1366:
                    page.wait_for_timeout(160)
                    editor.screenshot(path="arsonmix-native-harmony-editor-pitch-tune-on.png")
                page.locator("#mxHPreset").select_option("quartet")
                preset_settings=last_action(page,"nativeHarmonySetSettings")["settings"]
                assert preset_settings["voices"]==4 and preset_settings["correction"] is True
                apply_edit(page,state,preset_settings,0,0)
                switch.click()
                assert last_action(page,"nativeHarmonySetSettings")["settings"]["correction"] is False
                assert switch.get_attribute("aria-checked")=="true"
                apply_edit(page,state,{**preset_settings,"correction":False},0,0)
                assert switch.get_attribute("aria-checked")=="false"
                # Duo is not only a factory name: it selects the actual
                # two-voice DSP state, and preserves the owner's Tune OFF.
                page.locator("#mxHPreset").select_option("duo")
                duo=last_action(page,"nativeHarmonySetSettings")["settings"]
                assert duo["voices"]==2 and duo["correction"] is False,duo
                apply_edit(page,state,duo,0,0)
                assert page.locator('.mx-harm-voices [data-voices="2"]').get_attribute(
                    "aria-pressed")=="true"
                # Distinct from Duo, DOUBLER changes the *native* mode.
                # Its selected state waits for matching ACK/readback.
                dbl=page.locator('.mx-harm-type [data-voice-mode="1"]')
                dbl.click()
                mode_request=last_action(page,"nativeHarmonySetSettings")
                assert mode_request["settings"]["voiceMode"]==1
                assert dbl.get_attribute("aria-pressed")=="false"
                doubled={**duo,"voiceMode":1}
                apply_edit(page,state,doubled,0,0)
                assert dbl.get_attribute("aria-pressed")=="true"
                assert page.locator('#mxHGuideStatus').inner_text()=="UNISON"
                assert page.locator('[data-voices="2"]').is_disabled()
                page.locator("#mxHPreset").select_option("doubleWide")
                wide=last_action(page,"nativeHarmonySetSettings")["settings"]
                assert wide["voiceMode"]==1 and wide["space"]>.7,wide
                apply_edit(page,state,wide,0,0)
                harmony=page.locator('.mx-harm-type [data-voice-mode="0"]')
                harmony.click()
                restored={**wide,"voiceMode":0}
                apply_edit(page,state,restored,0,0)
                assert harmony.get_attribute("aria-pressed")=="true"
                assert not page.locator('[data-voices="2"]').is_disabled()
                page.locator("#mxHClose").click()
                row.locator('[data-action="openPlugin"]').click()
                assert switch.is_disabled()
                readback(page,state,DEFAULT,0,0,observed=57)
                assert switch.get_attribute("aria-checked")=="false"
                assert page.locator("#mxHTracking").inner_text()=="TRACKING A3"
                assert page.locator("#mxHKeyReadout").inner_text()=="C"
                assert page.locator("#mxHGuideStatus").inner_text()=="MANUAL"
                assert page.locator("#mxHSourcePicker").is_hidden()
                close=page.locator("#mxHClose")
                power=page.locator("#mxHPower")
                # This is the EXACT same shared CSS, SVG Close and header
                # Power material used by both native EQ and Compressor.
                closeSkin=close.evaluate("""el=>{
                  const s=getComputedStyle(el),x=getComputedStyle(el,'::after');
                  return {w:s.width,h:s.height,icon:x.backgroundImage,glyph:s.fontSize};
                }""")
                assert closeSkin["w"]==closeSkin["h"]=="24px",closeSkin
                assert closeSkin["glyph"]=="0px" and "M6.4,19L5,17.6" in closeSkin["icon"],closeSkin
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
                assert "AWAITING DSP ACK" in page.locator("#mxHStatus").inner_text()
                assert "queued" in (page.locator(
                    '.mx-harm-note[data-key="7"]').get_attribute("class") or "")
                locked={**DEFAULT,"key":7}
                apply_edit(page,state,locked,7,0)
                assert page.locator("#mxHKeyReadout").inner_text()=="G"
                assert "queued" not in (page.locator(
                    '.mx-harm-note[data-key="7"]').get_attribute("class") or "")
                assert page.locator("#mxHGuideStatus").inner_text()=="MANUAL"
                assert page.locator("#mxHSourcePicker").is_hidden()
                # G is a literal circle, subtly larger than unselected notes.
                # Check computed visual CSS, not a class-name-only assertion.
                note_g=page.locator('.mx-harm-note[data-key="7"]')
                note_c=page.locator('.mx-harm-note[data-key="0"]')
                assert "active" in (note_g.get_attribute("class") or "")
                # Reduced note rail radius leaves an actual margin inside the
                # wheel, including enlarged selected buttons on mobile.
                # Inner white pseudo-ring used to cross the musical-key
                # labels. The blue annulus is now uninterrupted.
                inner_ring=page.locator("#mxHWheel").evaluate("""e=>{
                    const s=getComputedStyle(e,'::before');
                    return {content:s.content,border:s.borderTopWidth};
                }""")
                assert inner_ring["content"] in ("none","normal"),inner_ring
                assert inner_ring["border"]=="0px",inner_ring
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
                note_ring_ratio=hypot(wheel_cx-note_cx,wheel_cy-note_cy)/(wheel_box["width"]/2)
                # Center the letter inside the blue annulus, not too close
                # to the LCD and not touching the outer silver boundary.
                assert .65<note_ring_ratio<.77,(width,note_ring_ratio)
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
                # All four supported counts publish real native voice edits.
                for count in (2,3,4,5):
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
                # Manual Key also observes audio-owner tracking on existing
                # console ticks. Never infer singing from a UI button state.
                # The preceding bypass-readback tests may have left ONE
                # outstanding (throttled) snapshot request. Complete it as
                # native JUCE would, rather than asserting a second request
                # within the in-flight 2500ms readback fence.
                latest_snap=last_action(page,"nativeHarmonySnapshot")
                readback(page,state,values,7,0,request=latest_snap)
                before=len(page.evaluate(
                    "()=>window.__actions.filter(x=>x.action==='nativeHarmonySnapshot')"))
                # Native host produces ~12 console ticks per second, not one
                # artificially selected 530ms tick after an arbitrary prior
                # music readback. Exercise the real tick cadence for 2s.
                # A single tick may legitimately be coalesced or queued.
                after=before
                for _ in range(24):
                    emit(page,"arMixConsoleSnapshot",state)
                    after=len(page.evaluate(
                        "()=>window.__actions.filter(x=>x.action==='nativeHarmonySnapshot')"))
                    if after>before:break
                    page.wait_for_timeout(85)
                assert after>before,("no Manual Key native tracking poll",
                    page.locator("#mxHStatus").inner_text(),
                    page.locator("#mxHTracking").inner_text())
                monitor=last_action(page,"nativeHarmonySnapshot")
                readback(page,state,changed,7,0,request=monitor,observed=60)
                assert page.locator("#mxHTracking").inner_text()=="TRACKING C4"
                # Reproduce the field screenshot: singer DSP continues playing
                # while a native device/graph revision changes under an open
                # Harmony editor. It MUST auto-rebind; no user reopen/wizard.
                if width==1366:
                    accepted={**changed,"voices":5}
                    def push_graph():
                        state["mixerSession"]["channels"]=copy.deepcopy(
                            state["graph"]["channels"])
                        emit(page,"arMixConsoleSnapshot",state)
                    def bump():
                        graph=state["graph"]
                        graph["revision"]=str(int(graph["revision"])+1)
                    def await_snap(instance,slot):
                        page.wait_for_function("""arg=>window.__actions.some(x=>
                            x.action==='nativeHarmonySnapshot' &&
                            x.graphRevision===arg.revision &&
                            x.instanceId===arg.instance && x.slot===arg.slot)""",
                            arg={"revision":state["graph"]["revision"],
                                 "instance":instance,"slot":slot})
                        result=last_action(page,"nativeHarmonySnapshot")
                        assert result["instanceId"]==instance,result
                        assert result["slot"]==slot,result
                    bump();push_graph()
                    await_snap(INSTANCE,0)
                    assert "REOPEN" not in page.locator("#mxHStatus").inner_text()
                    readback(page,state,accepted,7,0)
                    page.locator('[data-voices="4"]').click()
                    write=last_action(page,"nativeHarmonySetSettings")
                    assert write["graphRevision"]==state["graph"]["revision"]
                    accepted={**accepted,"voices":4}
                    apply_edit(page,state,accepted,7,0)

                    # Recreated *same-channel* FX instance; stale ACKs cannot
                    # commit controls or paint a previous DSP identity.
                    successor="harmony-native-recreated"
                    bump()
                    state["graph"]["channels"][0]["inserts"][0]["id"]=successor
                    push_graph()
                    await_snap(successor,0)
                    emit(page,"arMixBuiltinHarmonyAck",
                         {"requestId":write["requestId"],"status":"APPLIED"})
                    readback(page,state,DEFAULT,0,0,instance=successor)
                    assert page.locator("#mxHKeyReadout").inner_text()=="C"
                    page.locator('[data-key="9"]').click()
                    new_write=last_action(page,"nativeHarmonySetSettings")
                    assert new_write["instanceId"]==successor
                    assert new_write["channelId"]==SOURCE
                    assert new_write["settings"]["key"]==9
                    emit(page,"arMixBuiltinHarmonyQueue",
                         {"requestId":new_write["requestId"],"status":"QUEUED"})
                    emit(page,"arMixBuiltinHarmonyAck",
                         {"requestId":new_write["requestId"],"status":"APPLIED"})
                    accepted={**DEFAULT,"key":9}
                    readback(page,state,accepted,9,0,
                             applied=new_write["requestId"],instance=successor)
                    assert page.locator("#mxHKeyReadout").inner_text()=="A"

                    # Exact same insert identity follows an FX reorder, and
                    # subsequent edits use NEW accepted slot (not old index).
                    bump()
                    other={"id":"eq-accepted-other","catalogId":
                           "armix.builtin.channel-eq.v1","name":"EQ",
                           "bypassed":False}
                    harmony=copy.deepcopy(state["graph"]["channels"][0]["inserts"][0])
                    state["graph"]["channels"][0]["inserts"]=[other,harmony]
                    push_graph()
                    await_snap(successor,1)
                    readback(page,state,accepted,9,0,instance=successor)
                    page.locator('[data-voices="5"]').click()
                    moving=last_action(page,"nativeHarmonySetSettings")
                    assert moving["slot"]==1 and moving["instanceId"]==successor
                    emit(page,"arMixBuiltinHarmonyQueue",
                         {"requestId":moving["requestId"],"status":"QUEUED"})
                    emit(page,"arMixBuiltinHarmonyAck",
                         {"requestId":moving["requestId"],"status":"APPLIED"})
                    accepted={**accepted,"voices":5}
                    readback(page,state,accepted,9,0,
                             applied=moving["requestId"],instance=successor)

                    # A temporarily unpublished FX must NOT accept stale
                    # gestures. Hold the latest edit and apply only after
                    # a fresh accepted instance-scoped DSP readback.
                    bump();state["graph"]["channels"][0]["inserts"]=[other]
                    push_graph()
                    page.wait_for_function("""()=>document.querySelector(
                        '#mxHStatus').textContent.includes('RECONNECTING')""")
                    before=len(page.evaluate("""()=>window.__actions.filter(
                        x=>x.action==='nativeHarmonySetSettings')"""))
                    page.locator('[data-voices="3"]').click()
                    after=len(page.evaluate("""()=>window.__actions.filter(
                        x=>x.action==='nativeHarmonySetSettings')"""))
                    assert before==after,"stale FX received an edit"
                    bump();state["graph"]["channels"][0]["inserts"]=[other,harmony]
                    push_graph();await_snap(successor,1)
                    readback(page,state,accepted,9,0,instance=successor)
                    resumed=last_action(page,"nativeHarmonySetSettings")
                    assert resumed["slot"]==1 and resumed["instanceId"]==successor
                    assert resumed["settings"]["voices"]==3,resumed
                    emit(page,"arMixBuiltinHarmonyQueue",
                         {"requestId":resumed["requestId"],"status":"QUEUED"})
                    emit(page,"arMixBuiltinHarmonyAck",
                         {"requestId":resumed["requestId"],"status":"APPLIED"})
                    accepted={**accepted,"voices":3}
                    readback(page,state,accepted,9,0,
                             applied=resumed["requestId"],instance=successor)
                    assert page.locator('#mxHStatus').inner_text().startswith("NATIVE")
                    assert "FX 2" in page.locator("#mxHChannel").inner_text()
                assert not errors,errors
                # The knob's lit pseudo-ring extends 11px past its face.
                # Test real layout, not only CSS text, at every viewport.
                dial_geometry=page.evaluate("""()=>[...document.querySelectorAll(
                    '#mxNativeHarmony .mx-harm-slider')].map(s=>{
                    const f=s.querySelector('.mx-harm-dial-face').getBoundingClientRect();
                    const c=s.querySelector('.mx-harm-dial-label').getBoundingClientRect();
                    const o=s.querySelector('output').getBoundingClientRect();
                    return {gap:c.top-f.bottom,labelBottom:c.bottom,outputTop:o.top,
                            faceTop:f.top,outputBottom:o.bottom};
                })""")
                assert len(dial_geometry)==4,dial_geometry
                for geom in dial_geometry:
                    assert geom["gap"]>=12.0,("caption overlaps halo",width,geom)
                    assert geom["outputTop"]>=geom["labelBottom"]-0.5,(
                        "caption/value collision",width,geom)
                # Check spacing between rows as well as each knob.
                # The next luminous ring reaches 11px above its face.
                for upper,lower in zip(dial_geometry[:2],dial_geometry[2:]):
                    assert upper["outputBottom"]+2<=lower["faceTop"]-11,(
                        "next-row halo collides with upper value",width,upper,lower)
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
                    assert "rgb(239, 62, 80)" in bg and "rgb(197, 45, 64)" in bg,bg
                    page.mouse.up()
                    assert editor.is_hidden(),"Harmony Close button failed"
                page.close()
            browser.close()
    print("PASS Harmony native editor: stable LCD, Music/Manual ACK, throttled readback, three viewports")

if __name__=="__main__":
    main()
