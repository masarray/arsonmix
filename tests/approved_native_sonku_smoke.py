#!/usr/bin/env python3
"""8D-SonKu real compiled WebView2 GUI: native insert -> ACK -> readback -> editor.
Transport is emulated, never PCM. Native DSP is independently tested by CTest.
The test does not invoke private mixer state APIs to fake accepted inserts.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE

ROOT=Path(__file__).resolve().parents[1]
CATALOG="armix.builtin.8d-sonku.v1"
INSTANCE="sonku-accepted-instance"
SETTINGS={
    "startAzimuthDegrees":0.0,
    "revolutionSeconds":12.0,
    "stereoSpreadDegrees":60.0,
    "spatialMix":1.0,
    "outputTrim":0.75,
    "moving":True,
    "clockwise":True,
    "spacePreset":0,
    "sourceDistanceMeters":1.6,
    "roomAmount":0.55,
    "ambience":0.30,
}

def last_action(page,name):
    actions=page.evaluate(
        "name=>window.__actions.filter(a=>a.action===name)",name)
    assert actions, "Expected actual native action "+name
    return actions[-1]

def accepted_snapshot(page,state,settings,applied=None):
    read=last_action(page,"nativeSonKuSnapshot")
    payload={
        "requestId":read["requestId"],"status":"ACCEPTED","schemaVersion":2,
        "channelId":SOURCE,"slot":0,"instanceId":INSTANCE,
        "deviceEpoch":state["device"]["epoch"],
        "graphRevision":state["graph"]["revision"],
        "observedAzimuthDegrees":settings["startAzimuthDegrees"],
        "settings":copy.deepcopy(settings),
    }
    if applied:payload["appliedRequestId"]=applied
    emit(page,"arMixBuiltinSonKuSnapshot",payload)

def exercise(browser,out,width,height):
    page=browser.new_page(viewport={"width":width,"height":height})
    failures=[]
    page.on("pageerror",lambda exc:failures.append(str(exc)))
    page.add_init_script("""window.__nativeListeners=new Map();
      window.__nativeIntents=[];window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){
          if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
          if(name==='arMixAction')window.__actions.push(payload);
        },
        addEventListener(name,callback){
          const prior=window.__nativeListeners.get(name);
          window.__nativeListeners.set(name,prior?
            packet=>{prior(packet);callback(packet)}:callback);
        }
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function(
        "()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeSonKu",
        timeout=15000)
    state=snapshot()
    state["graph"]["capabilities"]["supportedOps"]+=["LOAD_INSERT","REMOVE_INSERT"]
    emit(page,"arMixConsoleSnapshot",state)
    if width<500:page.locator("#bankSelect").select_option("0")
    strip=page.locator('#tracks .strip[data-id="1"]')
    strip.locator(".fx-pick.empty").first.click()
    # Real assembled WebView picker: Guitar FX is a catalog Native processor
    # BEFORE the VST3 divider; SonKu retains its one specialized editor button.
    # This catches the #657 / SonKu M4 cross-branch regression at the operator
    # click surface, not merely by matching assembler source strings.
    guitar=page.locator('#fastResults [data-fast-id="armix.builtin.guitar-rig.v1"]')
    guitar.wait_for(state="visible",timeout=6000)
    assert guitar.count()==1,"Native Guitar FX missing or duplicated in BUILT-IN FX"
    assert page.evaluate("""()=>{
      const node=document.querySelector('#fastResults [data-fast-id="armix.builtin.guitar-rig.v1"]');
      const group=Array.from(document.querySelectorAll('#fastResults .fast-fx-category'));
      return group.length===2 && group[0].textContent==='BUILT-IN FX' &&
        group[1].textContent==='VST3 PLUGINS' &&
        (group[0].compareDocumentPosition(node)&Node.DOCUMENT_POSITION_FOLLOWING)!==0 &&
        (node.compareDocumentPosition(group[1])&Node.DOCUMENT_POSITION_FOLLOWING)!==0;
    }"""),"Guitar FX shown under VST3 instead of first native BUILT-IN shelf"
    assert page.locator('#fastResults [data-fast-id="armix.builtin.8d-sonku.v1"]').count()==0, (
        "SonKu specialized editor must not be duplicated as catalog generic")
    picker=page.locator("#fastResults [data-fast-native-sonku]")
    picker.wait_for(state="visible")
    assert picker.count()==1,"8D-SonKu picker duplicated or missing"
    assert page.locator("#mxNativeSonKu").is_hidden(),"Editor opened before native ACK"
    picker.click()
    requests=page.evaluate(
        "()=>window.__nativeIntents.filter(x=>x.operation==='LOAD_INSERT')")
    assert len(requests)==1,requests
    req=requests[0]
    assert req["payload"]["catalogId"]==CATALOG,req
    assert page.locator("#mxNativeSonKu").is_hidden(),"Editor opened on intent only"
    state["graph"].update(revision="3",rosterRevision="3")
    state["graph"]["channels"][0]["inserts"].append({
        "id":INSTANCE,"catalogId":CATALOG,"name":"8D-SonKu","bypassed":False})
    state["mixerSession"].update(routeRevision="3",rosterRevision="3",
        channels=copy.deepcopy(state["graph"]["channels"]))
    ack(page,state,req)
    # Acceptance alone does not paint active DSP; readback authorizes editor.
    assert page.locator("#mxNativeSonKu").is_hidden()
    emit(page,"arMixConsoleSnapshot",state)
    editor=page.locator("#mxNativeSonKu")
    editor.wait_for(state="visible",timeout=5000)
    assert editor.locator("#skMap .sk-src").count()==2
    assert editor.locator("#skMap .sk-speaker-shell").count()==2
    assert editor.locator("#skMap #skRearArc").count()==1
    assert editor.locator("#skMap #skFrontArc").count()==1
    # Painter's ordering is 3D: far rail -> back-facing listener -> near rail.
    assert page.evaluate("""() => {
      const map=document.querySelector('#skMap');
      const rear=map.querySelector('#skRearArc');
      const art=map.querySelector('#skListenerArt');
      const front=map.querySelector('#skFrontArc');
      return Boolean((rear.compareDocumentPosition(art) & Node.DOCUMENT_POSITION_FOLLOWING) &&
                     (art.compareDocumentPosition(front) & Node.DOCUMENT_POSITION_FOLLOWING));
    }"""), "Front/back ring painter layering incorrect"
    # Painter's algorithm applies to the *speakers*, not only the floor rail.
    assert page.evaluate("""() => {
      const map=document.querySelector('#skMap');
      const far=map.querySelector('#skRearSources');
      const art=map.querySelector('#skListenerArt');
      const near=map.querySelector('#skFrontSources');
      return Boolean((far.compareDocumentPosition(art)&Node.DOCUMENT_POSITION_FOLLOWING) &&
                     (art.compareDocumentPosition(near)&Node.DOCUMENT_POSITION_FOLLOWING));
    }"""),"Rear speakers are incorrectly rendered in FRONT of the head"
    # Every rotation input remains an accessible native range, wrapped in
    # a tactile rotary shell: no browser PCM, no fake processed state.
    assert editor.locator(".sk-rotary-grid .sk-dial").count()==4
    for knob in ("skSpeed","skSpread","skMix","skTrim"):
        assert editor.locator("#"+knob).get_attribute("type")=="range"
        assert editor.locator("#"+knob).evaluate("(n)=>n.closest('.sk-dial')!==null")
    art=editor.locator("#skListenerArt")
    assert art.count()==1 and art.get_attribute("data-orientation")=="front-up-flipped"
    assert art.get_attribute("href")=="assets/sonku-listener-top.png"
    # The vertically flipped PNG must actually resolve in the BUILT
    # native /preview/ bundle, and have transparent palette metadata.
    assert page.evaluate("""async () => {
       const image=document.querySelector('#skListenerArt');
       const href=image.getAttribute('href');
       const asset=await fetch(href);
       if(!asset.ok)return false;
       const bytes=new Uint8Array(await asset.arrayBuffer());
       const sig=[137,80,78,71,13,10,26,10];
       return sig.every((n,i)=>bytes[i]===n) &&
         bytes[16]===0&&bytes[17]===0&&bytes[18]===1&&bytes[19]===0 &&
         bytes[20]===0&&bytes[21]===0&&bytes[22]===1&&bytes[23]===0 &&
         await new Promise(resolve=>{
           const im=new Image();im.onload=()=>resolve(im.naturalWidth===256);
           im.onerror=()=>resolve(false);im.src=href;
         });
    }"""),"Flipped transparent PNG missing from native GUI bundle"
    # SVG <text> is not HTML text: Chromium 158 no longer exposes usable
    # HTMLElement.innerText for these nodes. Read SVG textContent and verify
    # the actual anchors, so front/rear cannot silently flip in the GUI.
    direction=editor.locator("#skMap .sk-direction")
    labels=direction.evaluate_all(
        "(nodes)=>nodes.map(n=>({text:n.textContent.trim(),x:Number(n.getAttribute('x')),y:Number(n.getAttribute('y'))}))")
    assert [x["text"] for x in labels]==["FRONT","REAR","LEFT","RIGHT"],labels
    assert labels[0]["y"]<60 and labels[1]["y"]>290,labels
    assert labels[2]["x"]<50 and labels[3]["x"]>300,labels
    assert editor.locator("#skListenerArt").get_attribute("pointer-events")=="none"
    assert editor.locator("#skMotion").inner_text()=="ORBIT"
    assert editor.locator("#skDirection").inner_text()=="CLOCKWISE"
    assert editor.locator("#skMode").inner_text()=="NATIVE HRTF"
    # 250ms visualization timer must not overwrite an unacknowledged
    # snapshot request (real JUCE message thread can be temporarily busy).
    count=page.evaluate(
        "()=>window.__actions.filter(a=>a.action==='nativeSonKuSnapshot').length")
    page.wait_for_timeout(700)
    after=page.evaluate(
        "()=>window.__actions.filter(a=>a.action==='nativeSonKuSnapshot').length")
    assert after==count, "Unanswered SonKu readback was overwritten"
    accepted_snapshot(page,state,SETTINGS)
    # A true visual animation must advance BETWEEN native readbacks.
    # The old 250ms setInterval was ~4fps despite valid DSP PCM.
    vx0=editor.locator('.sk-src[data-index="0"]').get_attribute("data-visual-x")
    page.wait_for_timeout(150)
    vx1=editor.locator('.sk-src[data-index="0"]').get_attribute("data-visual-x")
    assert abs(float(vx1)-float(vx0))>1.0,("Orbit is frozen between readbacks",vx0,vx1)
    # Close/reopen must use the installed FX rack click, never a private open().
    editor.locator("#skClose").click()
    assert editor.is_hidden()
    row=strip.locator('.fxslot-row[data-insert-id="'+INSTANCE+'"]')
    row.wait_for(state="visible")
    row.locator('[data-action="openPlugin"]').click()
    editor.wait_for(state="visible")
    accepted_snapshot(page,state,SETTINGS)
    editor.locator("#skMotion").click()
    command=last_action(page,"nativeSonKuSet")
    assert command["settings"]["moving"] is False,command
    assert command["instanceId"]==INSTANCE and command["channelId"]==SOURCE
    emit(page,"arMixBuiltinSonKuQueue",{
        "requestId":command["requestId"],"status":"QUEUED"})
    emit(page,"arMixBuiltinSonKuAck",{
        "requestId":command["requestId"],"status":"APPLIED"})
    accepted_snapshot(page,state,{**SETTINGS,"moving":False},
        applied=command["requestId"])
    assert editor.locator("#skMotion").inner_text()=="STATIC"
    assert "ACCEPTED" in editor.locator("#skHint").inner_text()
    # Verify native HRTF azimuth is unchanged while screen orientation
    # now matches the real user: FRONT=TOP, anatomical RIGHT=SCREEN RIGHT.
    def pose(degrees):
        page.wait_for_timeout(270)
        settings={**SETTINGS,"moving":False,"stereoSpreadDegrees":0.0,
                  "startAzimuthDegrees":float(degrees)}
        accepted_snapshot(page,state,settings)
        # Acoustic anchor is distinct from the readable icon's display offset.
        # The offset NEVER alters native azimuth when both sources coincide.
        nodes=editor.locator('#skMap .sk-src')
        data=nodes.evaluate_all("""els=>els.map(n=>({
          ax:Number(n.dataset.acousticX),ay:Number(n.dataset.acousticY),
          vx:Number(n.dataset.visualX),vy:Number(n.dataset.visualY),
          value:Number(n.getAttribute('aria-valuenow'))}))""")
        assert len(data)==2 and data[0]["value"]==data[1]["value"]==degrees,data
        assert abs(data[0]["ax"]-data[1]["ax"])<.01,data
        assert abs(data[0]["ay"]-data[1]["ay"])<.01,data
        separation=((data[0]["vx"]-data[1]["vx"])**2+
                    (data[0]["vy"]-data[1]["vy"])**2)**.5
        assert separation>=44,("Two studio speakers overlap at zero spread",data)
        # Source icon is now INSIDE the front or rear SVG paint layer.
        layer=nodes.first.evaluate("(n)=>n.parentElement.id")
        if degrees in (0,180):
            expected="skFrontSources" if degrees==0 else "skRearSources"
            assert layer==expected,(degrees,layer,expected)
        return data[0]["ax"],data[0]["ay"]
    x,y=pose(0);assert abs(x-200)<1 and y<100,("FRONT",x,y)
    x,y=pose(90);assert x>335 and abs(y-174)<1,("RIGHT",x,y)
    x,y=pose(180);assert abs(x-200)<1 and y>245,("REAR",x,y)
    x,y=pose(-90);assert x<60 and abs(y-174)<1,("LEFT",x,y)
    # All drag paths now STREAM native edits DURING the gesture. A burst
    # must emit only one in-flight command; the most recent sample follows
    # verified ACK+snapshot, not hundreds of SPSC entries or false UI ACKs.
    def command_count():
        return page.evaluate(
            "()=>window.__actions.filter(a=>a.action==='nativeSonKuSet').length")
    def applied_settings(action,settings):
        emit(page,"arMixBuiltinSonKuAck",{
            "requestId":action["requestId"],"status":"APPLIED"})
        accepted_snapshot(page,state,settings,applied=action["requestId"])
    svg=editor.locator("#skMap").bounding_box()
    assert svg
    def screen(x,y):
        return (svg["x"]+x*svg["width"]/400,
                svg["y"]+y*svg["height"]/330)

    # SVG speaker movement must affect the JUCE PCM route BEFORE mouseup.
    # FRONT is now visual TOP and acoustic 0°, not bottom.
    source=editor.locator('.sk-src[data-index="0"]')
    sx,sy=screen(float(source.get_attribute("data-visual-x")),
                 float(source.get_attribute("data-visual-y")))
    ex,ey=screen(200,91)
    before=command_count()
    page.mouse.move(sx,sy)
    page.mouse.down()
    page.mouse.move(ex,ey,steps=10)
    assert command_count()==before+1,"Speaker drag not live / SPSC flooded"
    first=last_action(page,"nativeSonKuSet")
    assert first["settings"]["moving"] is False
    page.mouse.up()
    assert command_count()==before+1,"Unverified sends flooded during drag"
    applied_settings(first,{**SETTINGS,"moving":False,
        "stereoSpreadDegrees":0.0,
        "startAzimuthDegrees":first["settings"]["startAzimuthDegrees"]})
    dragAction=last_action(page,"nativeSonKuSet")
    assert dragAction["settings"]["moving"] is False
    assert abs(dragAction["settings"]["startAzimuthDegrees"])<5,dragAction
    if dragAction["requestId"]!=first["requestId"]:
        assert command_count()==before+2,"Latest-wins did not coalesce"
        applied_settings(dragAction,{**SETTINGS,"moving":False,
            "stereoSpreadDegrees":0.0,"startAzimuthDegrees":
            dragAction["settings"]["startAzimuthDegrees"]})

    # Same global gesture scheduler handles FOUR rotary knobs (pointer, key).
    # The original bug sent NOTHING until pointer release.
    knob=editor.locator("#skSpeed").locator("..")
    box=knob.bounding_box();assert box
    x=box["x"]+box["width"]*.5;y=box["y"]+box["height"]*.5
    before=command_count()
    page.mouse.move(x,y);page.mouse.down()
    page.mouse.move(x,y-17,steps=6)
    assert command_count()==before+1,"Rotary does not stream native edit"
    knobFirst=last_action(page,"nativeSonKuSet")
    page.mouse.up()
    assert command_count()==before+1,"Rotary overran single in-flight"
    firstSettings={**SETTINGS,"moving":False,
        "stereoSpreadDegrees":0.0,"startAzimuthDegrees":0.0,
        "revolutionSeconds":knobFirst["settings"]["revolutionSeconds"]}
    applied_settings(knobFirst,firstSettings)
    dialAction=last_action(page,"nativeSonKuSet")
    assert dialAction["settings"]["revolutionSeconds"]>12,dialAction
    if dialAction["requestId"]!=knobFirst["requestId"]:
        assert command_count()==before+2
        applied_settings(dialAction,{**firstSettings,
            "revolutionSeconds":dialAction["settings"]["revolutionSeconds"]})
    # M4: native SPACE must be an actual typed ACK/readback edit.
    # No browser-only audio, no visual success before PCM applied.
    assert editor.locator(".sk-space-options button").count()==4
    assert editor.locator("#skDistance").is_disabled()
    assert editor.locator("#skAmbience").is_disabled()
    assert editor.locator("#skReflections").is_disabled()
    assert editor.locator("#skTrimValue").inner_text().endswith("dB")
    assert editor.locator("#skTrim").get_attribute("max")=="12"
    editor.locator('[data-sk-room="2"]').click()
    scene=last_action(page,"nativeSonKuSet")
    assert scene["settings"]["spacePreset"]==2,scene
    assert scene["settings"]["sourceDistanceMeters"]==1.6,scene
    assert scene["settings"]["roomAmount"]==0.55,scene
    assert scene["settings"]["ambience"]==0.30,scene
    assert "ACCEPTED" not in editor.locator("#skHint").inner_text()
    emit(page,"arMixBuiltinSonKuAck",{
        "requestId":scene["requestId"],"status":"APPLIED"})
    sceneState={**SETTINGS,"moving":False,"startAzimuthDegrees":0.0,
                "stereoSpreadDegrees":0.0,
                "revolutionSeconds":dialAction["settings"]["revolutionSeconds"],
                "spacePreset":2}
    accepted_snapshot(page,state,sceneState,applied=scene["requestId"])
    assert editor.locator('[data-sk-room="2"]').get_attribute("aria-pressed")=="true"
    assert editor.locator("#skDistance").is_enabled()
    assert editor.locator("#skAmbience").is_enabled()
    assert editor.locator("#skReflections").is_enabled()
    # The 0..100% controls must reach independent native wet parameters,
    # never only browser-side visual labels. Include a >unity GAIN request
    # to prove the formerly capped 0..100% trim has genuinely become dB gain.
    def apply_slider(selector,value,field):
        editor.locator(selector).evaluate("""(n,v)=>{
          n.value=String(v);
          n.dispatchEvent(new Event('input',{bubbles:true}));
          n.dispatchEvent(new Event('change',{bubbles:true}));
        }""",value)
        sent=last_action(page,"nativeSonKuSet")
        assert field in sent["settings"],sent
        emit(page,"arMixBuiltinSonKuAck",{
            "requestId":sent["requestId"],"status":"APPLIED"})
        return sent
    gain=apply_slider("#skTrim",6.0,"outputTrim")
    assert 1.99<gain["settings"]["outputTrim"]<2.0,gain
    sceneState={**sceneState,"outputTrim":gain["settings"]["outputTrim"]}
    accepted_snapshot(page,state,sceneState,applied=gain["requestId"])
    assert "+6.0 dB" in editor.locator("#skTrimValue").inner_text()
    reflections=apply_slider("#skReflections",85,"roomAmount")
    assert abs(reflections["settings"]["roomAmount"]-.85)<.001
    sceneState={**sceneState,"roomAmount":.85}
    accepted_snapshot(page,state,sceneState,applied=reflections["requestId"])
    assert editor.locator("#skReflectionsValue").inner_text()=="85%"
    reverb=apply_slider("#skAmbience",70,"ambience")
    assert abs(reverb["settings"]["ambience"]-.70)<.001
    sceneState={**sceneState,"ambience":.70}
    accepted_snapshot(page,state,sceneState,applied=reverb["requestId"])
    assert editor.locator("#skAmbienceValue").inner_text()=="70%"
    # All ROOM sliders are realtime, independent of final pointer release.
    # Native mock ACK is deliberately withheld while the mouse is moving:
    # there MUST be one, and only one, SPSC edit in flight.
    slider=editor.locator("#skDistance")
    rect=slider.bounding_box();assert rect
    sy=rect["y"]+rect["height"]*.5
    before=command_count()
    page.mouse.move(rect["x"]+rect["width"]*.20,sy)
    page.mouse.down()
    page.mouse.move(rect["x"]+rect["width"]*.76,sy,steps=12)
    held=float(slider.input_value())
    assert held>3.0,("Distance pointer gesture not effective",held)
    assert command_count()==before+1,"Distance changed only after release or flooded DSP"
    distanceFirst=last_action(page,"nativeSonKuSet")
    assert distanceFirst["settings"]["sourceDistanceMeters"]>=1.6
    # Simulate a delayed periodic snapshot DURING the gesture: no thumb snap.
    page.wait_for_timeout(310)
    accepted_snapshot(page,state,sceneState)
    assert abs(float(slider.input_value())-held)<.15,(
        "Native snapshot incorrectly reverted held range thumb",
        held,slider.input_value())
    assert editor.locator("#skDistanceValue").inner_text()==f"{held:.1f} m"
    page.mouse.up()
    assert command_count()==before+1
    firstState={**sceneState,
                "sourceDistanceMeters":distanceFirst["settings"]["sourceDistanceMeters"]}
    applied_settings(distanceFirst,firstState)
    userDistance=last_action(page,"nativeSonKuSet")
    assert userDistance["settings"]["sourceDistanceMeters"]>3.,userDistance
    if userDistance["requestId"]!=distanceFirst["requestId"]:
        assert command_count()==before+2,"Latest range edit not coalesced after ACK"
        sceneState={**sceneState,
                    "sourceDistanceMeters":userDistance["settings"]["sourceDistanceMeters"]}
        applied_settings(userDistance,sceneState)
    else:
        sceneState=firstState
    # Verify a burst of 30 values on REVERB sends bounded realtime packets
    # before a final change. This catches accidental unbounded streaming.
    before=command_count()
    editor.locator("#skAmbience").evaluate("""n=>{
      for(let i=0;i<30;i++){
        n.value=String(i%2?i*3:30+i);
        n.dispatchEvent(new Event('input',{bubbles:true}));
      }
    }""")
    page.wait_for_timeout(60)
    assert command_count()==before+1,"Reverb did not stream / issued uncontrolled edits"
    one=last_action(page,"nativeSonKuSet")
    assert one["settings"]["ambience"]!=sceneState["ambience"]
    # Make a FINAL distinct value while earlier PCM ACK is still pending.
    editor.locator("#skAmbience").evaluate("""n=>{
       n.value="71";n.dispatchEvent(new Event('input',{bubbles:true}));
       n.dispatchEvent(new Event('change',{bubbles:true}));
    }""")
    assert command_count()==before+1,"Reverb flooded the native SPSC"
    applied_settings(one,{**sceneState,"ambience":one["settings"]["ambience"]})
    finalReverb=last_action(page,"nativeSonKuSet")
    assert finalReverb["requestId"]!=one["requestId"],"Latest 71% edit lost"
    assert finalReverb["settings"]["ambience"]==.71
    sceneState={**sceneState,"ambience":.71}
    applied_settings(finalReverb,sceneState)
    # Programmatic range edit emits native DOM input+change for keyboard /
    # automation, using the exact same transaction and ACK fences.
    editor.locator("#skDistance").evaluate("""n=>{
      n.value='5.0';n.dispatchEvent(new Event('input',{bubbles:true}));
      n.dispatchEvent(new Event('change',{bubbles:true}));}""")
    distance=last_action(page,"nativeSonKuSet")
    assert distance["settings"]["spacePreset"]==2,distance
    assert abs(distance["settings"]["sourceDistanceMeters"]-5.0)<.01,distance
    emit(page,"arMixBuiltinSonKuAck",{
        "requestId":distance["requestId"],"status":"APPLIED"})
    sceneState={**sceneState,"sourceDistanceMeters":5.0}
    accepted_snapshot(page,state,sceneState,applied=distance["requestId"])
    editor.locator('[data-sk-room="1"]').click()
    clamped=last_action(page,"nativeSonKuSet")
    assert clamped["settings"]["spacePreset"]==1,clamped
    assert clamped["settings"]["sourceDistanceMeters"]<=3.1,clamped
    emit(page,"arMixBuiltinSonKuAck",{
        "requestId":clamped["requestId"],"status":"APPLIED"})
    sceneState={**sceneState,"spacePreset":1,
                "sourceDistanceMeters":clamped["settings"]["sourceDistanceMeters"]}
    accepted_snapshot(page,state,sceneState,applied=clamped["requestId"])
    editor.locator('[data-sk-room="0"]').click()
    off=last_action(page,"nativeSonKuSet")
    assert off["settings"]["spacePreset"]==0,off
    emit(page,"arMixBuiltinSonKuAck",{
        "requestId":off["requestId"],"status":"APPLIED"})
    accepted_snapshot(page,state,{**sceneState,"spacePreset":0},
                      applied=off["requestId"])
    assert editor.locator("#skDistance").is_disabled()
    assert editor.locator("#skAmbience").is_disabled()
    assert editor.locator("#skReflections").is_disabled()
    # Every remaining SonKu rotary (4/4 total) is tested with INPUT ALONE,
    # not a synthetic input+change pair that could hide release-only audio.
    sceneState={**sceneState,"spacePreset":0}
    for control,value,key in [
        ("skSpeed",18.0,"revolutionSeconds"),
        ("skSpread",45,"stereoSpreadDegrees"),
        ("skMix",65,"spatialMix"),
        ("skTrim",3.0,"outputTrim"),
    ]:
        before=command_count()
        editor.locator("#"+control).evaluate("""(n,v)=>{
            n.value=String(v);
            n.dispatchEvent(new Event('input',{bubbles:true}));
        }""",value)
        page.wait_for_timeout(58)
        assert command_count()==before+1,(control,"no native edit on input")
        cmd=last_action(page,"nativeSonKuSet")
        assert cmd["settings"][key]!=sceneState[key],(control,cmd)
        sceneState={**sceneState,key:cmd["settings"][key]}
        applied_settings(cmd,sceneState)
        # Committing an already-applied value must not create a duplicate
        # SPSC native edit.
        editor.locator("#"+control).evaluate(
            "n=>n.dispatchEvent(new Event('change',{bubbles:true}))")
        assert command_count()==before+1,(control,"duplicate final edit")
    bounds=editor.bounding_box()
    assert bounds and bounds["width"]>260 and bounds["width"]<width+2
    # All listener and rendering JS exceptions remain fatal.
    assert not failures,(width,height,failures)
    page.screenshot(path=f"arsonmix-sonku-editor-{width}.png")
    page.close()

def main():
    spec=importlib.util.spec_from_file_location(
        "bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="armix-sonku-") as temp,sync_playwright() as p:
        out=Path(temp);bundle.stage(out)
        browser=p.chromium.launch(headless=True)
        for width,height in [(1366,768),(390,844)]:
            exercise(browser,out,width,height)
        browser.close()
    print("PASS 8D-SonKu actual native editor: insert intent, no premature GUI,"
          " ACK/readback, reopen, typed settings and 2 viewport screenshots."
          " Browser uses a transport mock; native DSP requires CTest and hardware FAT.")

if __name__=="__main__":
    main()
