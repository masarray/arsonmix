"""Anti Noiz actual approved native bundle, with mock IPC (NOT audio/FAT).
The real catalog picker, graph ACK/readback, inline editor, typed transport
and shared preset bar run in Chromium. No DSP or audio device is mocked as
a measured result. The Windows FxInstance CTest is the separate PCM gate.
"""
import copy
import sys
from pathlib import Path
from playwright.sync_api import sync_playwright, expect
from approved_native_wire_gain_smoke import snapshot, SOURCE, ack, emit

ID = "armix.builtin.anti-noiz.v1"
INSTANCE = "g5c-900"
DEFAULTS = dict(mode="Auto", cleanDb=14, silenceDb=32, musicProtect=1, enabled=True)
OBS = dict(serial="1", profile=False, learning=False, learnStatus="Idle",
           rackBypassed=False, effectiveMode="Auto", latency=256,
           spectralGain=0.5, residualGain=0.25)
# Explicit mock-IPC fixture, NOT measured audio. Distinct curves exercise UI.
SPECTRUM = dict(bins=129, binHz=187.5, candidate=True, progress=.5,
                input=[-28-abs(i-17)*.4 for i in range(129)],
                noise=[-48-abs(i-4)*.25 for i in range(129)])
BACKEND = """(()=>{
 const subs=new Map(), dispatch=new Map(); let counter=0;
 window.__nativeListeners=dispatch;window.__nativeIntents=[];window.__actions=[];
 window.__anListenerCount=name=>subs.get(name)?.size||0;
 window.__JUCE__={backend:{
  emitEvent(name,payload){
   if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
   if(name==='arMixAction')window.__actions.push(payload);
  },
  addEventListener(name,fn){
   const id=++counter;
   if(!subs.has(name)){
    subs.set(name,new Map());
    dispatch.set(name,payload=>{for(const f of [...subs.get(name).values()])f(payload);});
   }
   subs.get(name).set(id,fn);return {name,id};
  },
  removeEventListener(token){subs.get(token?.name)?.delete(token?.id);}
 }};
})();"""

def last_action(page, action):
    page.wait_for_function("a=>window.__actions.some(x=>x.action===a)", arg=action)
    return page.evaluate("a=>window.__actions.filter(x=>x.action===a).at(-1)", action)

def respond(page, action, event, **extra):
    keys=("requestId","channelId","deviceEpoch","graphRevision","instanceId","slot","workspaceToken")
    emit(page,event,{**{k:action[k] for k in keys if k in action},**extra})

def readback(page, settings, observation=OBS):
    request=last_action(page,"nativeAntiNoizSnapshot")
    assert request["channelId"]==SOURCE and request["instanceId"]==INSTANCE
    respond(page,request,"arMixBuiltinAntiNoizSnapshot",status="ACCEPTED",
            settings=settings,observation=observation,sampleRate=48000,addedLatencySamples=256)

def assert_p1_geometry(root):
    # Verify composition, not merely element visibility. All three controls
    # share the same vertical baselines and their widths agree to 1px.
    geometry=root.evaluate("""el=>{
      const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height};};
      const cs=[...el.querySelectorAll('.an-control')];
      const selectors=['.an-dial','.an-readout','.an-control-name','.an-range-label'];
      const modes=[...el.querySelectorAll('.an-modes button')];
      const tokens=getComputedStyle(el.querySelector('.an-panel'));
      return {cols:cs.map(c=>selectors.map(s=>rect(c.querySelector(s)))),
        modes:modes.map(rect),pressed:modes.filter(b=>b.getAttribute('aria-pressed')==='true').length,
        radii:['.an-display','.an-readout','[data-power]','[data-close]','[data-learn]','.an-presets select','.an-modes'].map(s=>getComputedStyle(el.querySelector(s)).borderTopLeftRadius),
        radius:tokens.getPropertyValue('--an-radius').trim()};
    }""")
    assert len(geometry["cols"])==3
    for i in range(4):
        row=[c[i] for c in geometry["cols"]]
        assert max(r["y"] for r in row)-min(r["y"] for r in row)<=1,geometry
        assert max(r["h"] for r in row)-min(r["h"] for r in row)<=1,geometry
    for c in geometry["cols"]:
        assert all(c[i]["y"]+c[i]["h"]<=c[i+1]["y"]+1 for i in range(3)),geometry
    modes=geometry["modes"]
    assert len(modes)==4 and geometry["pressed"]==1
    assert max(m["w"] for m in modes)-min(m["w"] for m in modes)<=1,geometry
    assert max(m["h"] for m in modes)-min(m["h"] for m in modes)<=1,geometry
    assert all(abs(modes[i]["x"]+modes[i]["w"]-modes[i+1]["x"])<=1 for i in range(3)),geometry
    assert all(r==geometry["radius"] for r in geometry["radii"]),geometry

def assert_full_composition(root):
    g=root.evaluate("""el=>{
      const r=e=>{const b=e.getBoundingClientRect();return {x:b.x,y:b.y,w:b.width,h:b.height,right:b.right,bottom:b.bottom};};
      const q=s=>el.querySelector(s);
      const rows=[...q('.an-panel').children].filter(e=>!e.hidden).map(r);
      const controls=['[data-power]','[data-close]','[data-learn]','.an-presets select','.an-presets .mx-fxp-save-as','.an-presets .mx-fxp-manage'].map(s=>r(q(s)));
      return {rows,controls,program:r(q('.an-presets select')),source:r(q('.an-modes')),
        power:r(q('[data-power]')),close:r(q('[data-close]')),footer:r(q('footer')),
        status:r(q('[data-status]')),legend:[...q('.an-legend').children].map(r),
        overflow:[...q('.an-panel').querySelectorAll('.an-control-name,.an-modes button,.an-legend span')].filter(e=>e.scrollWidth>e.clientWidth+1).map(e=>e.textContent)};
    }""")
    rows=g["rows"]
    assert max(x["x"] for x in rows)-min(x["x"] for x in rows)<=1,g
    assert max(x["right"] for x in rows)-min(x["right"] for x in rows)<=1,g
    assert all(abs(rows[i+1]["y"]-rows[i]["bottom"]-8)<=1 for i in range(len(rows)-1)),g
    assert abs(g["program"]["x"]-g["source"]["x"])<=1,g
    assert abs(g["power"]["y"]-g["close"]["y"])<=1,g
    assert all(abs(x["h"]-30)<=1 for x in g["controls"]),g
    assert max(x["y"] for x in g["legend"])-min(x["y"] for x in g["legend"])<=1,g
    assert g["status"]["h"]==24 and not g["overflow"],g

def main():
    bundle=Path(sys.argv[1]).resolve()
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        page=browser.new_page(viewport={"width":1366,"height":768})
        errors=[]
        page.on("pageerror",lambda e:errors.append(str(e)))
        page.add_init_script(BACKEND)
        page.goto((bundle/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeAntiNoiz")
        root=page.locator("#mxNativeAntiNoiz")
        expect(root).to_have_count(1)
        expect(root).to_be_hidden()
        count=lambda name:page.evaluate("n=>window.__anListenerCount(n)",name)
        preset_event="arMixBuiltinFxPresetResult"
        baseline_preset_count=count(preset_event)
        state=snapshot()
        # Global staging requires an explicit host master power readback.
        # Missing is UNAVAILABLE, never inferred as false.
        state["globalFxBypassed"]=False
        state["graph"]["capabilities"]["supportedOps"]+=["LOAD_INSERT","MOVE_INSERT","REMOVE_INSERT"]
        emit(page,"arMixConsoleSnapshot",state)
        # Anti Noiz only appears from actual native catalog projection.
        emit(page,"arMixCatalog",{"items":[],"builtIns":[
            {"id":ID,"name":"Anti Noiz","category":"Dynamics","showInPicker":True}]})
        strip=page.locator('#tracks .strip[data-id="1"]')
        strip.locator('.fx-pick.empty').first.click()
        picker=page.locator('#fastResults [data-fast-id="'+ID+'"]')
        expect(picker).to_be_visible()
        expect(picker).to_have_count(1)
        picker.click()
        page.wait_for_function("()=>window.__nativeIntents.some(x=>x.operation==='LOAD_INSERT')")
        intent=page.evaluate("()=>window.__nativeIntents.filter(x=>x.operation==='LOAD_INSERT').at(-1)")
        assert intent["payload"]["catalogId"]==ID
        expect(root).to_be_hidden()
        state["graph"].update(revision="3",rosterRevision="3")
        channel=state["graph"]["channels"][0]
        channel["fxRackBypassed"]=False
        channel["inserts"].append(dict(id=INSTANCE,catalogId=ID,name="Anti Noiz",bypassed=False))
        state["mixerSession"].update(routeRevision="3",rosterRevision="3",
            channels=copy.deepcopy(state["graph"]["channels"]))
        ack(page,state,intent)
        emit(page,"arMixConsoleSnapshot",state)
        row=strip.locator('.fxslot-row[data-insert-id="'+INSTANCE+'"]')
        expect(row).to_be_visible()
        expect(root).to_be_visible()
        expect(root.locator('[data-learn]')).to_be_disabled()
        assert count("arMixBuiltinAntiNoizAck")==1
        assert count(preset_event)==baseline_preset_count+1
        readback(page,DEFAULTS)
        expect(root.locator('[data-spectral]')).to_have_text("6.0 dB")
        expect(root.locator('[data-residual]')).to_have_text("12.0 dB")
        expect(root.locator('[data-learn]')).to_be_enabled()
        assert "is-on" not in (root.locator(".armix-native-fx-bypass").get_attribute("class") or "").split()
        # Current host power overrides stale DSP telemetry without mutating
        # settings, even between two 20 Hz native observations.
        for kind,label in (("GLOBAL","Global FX bypassed"),("RACK","Rack bypassed"),("SLOT","Slot bypassed")):
            if kind=="GLOBAL": state["globalFxBypassed"]=True
            elif kind=="RACK": channel["fxRackBypassed"]=True
            else: channel["inserts"][0]["bypassed"]=True
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            expect(root.locator('[data-state]')).to_have_text(label)
            expect(root.locator('[data-learn]')).to_be_disabled()
            expect(root.locator('[data-spectral]')).to_have_text("—")
            expect(root.locator('[data-residual]')).to_have_text("—")
            state["globalFxBypassed"]=False
            channel["fxRackBypassed"]=False
            channel["inserts"][0]["bypassed"]=False
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            page.wait_for_timeout(210)
            readback(page,DEFAULTS,{**OBS,"serial":str(100+len(label))})
            expect(root.locator('[data-learn]')).to_be_enabled()
        listing=last_action(page,"nativeBuiltinFxUserPreset")
        assert listing["operation"]=="list" and listing["catalogId"]==ID
        respond(page,listing,preset_event,status="OK",operation="list",catalogId=ID,items=[])
        expect(root.locator(".mx-fxp-save-as")).to_be_enabled()
        # One actual control gesture; foreign ACK cannot repaint accepted mode.
        root.locator('[data-mode="Voice"]').click()
        edit=last_action(page,"nativeAntiNoizSet")
        assert edit["settings"]["mode"]=="Voice"
        expect(root.locator('[data-mode="Auto"]')).to_have_attribute("aria-pressed","true")
        respond(page,edit,"arMixBuiltinAntiNoizQueue",status="QUEUED",settings=edit["settings"])
        expect(root.locator('[data-mode="Auto"]')).to_have_attribute("aria-pressed","true")
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",
                settings=edit["settings"],instanceId="foreign-instance")
        expect(root.locator('[data-mode="Auto"]')).to_have_attribute("aria-pressed","true")
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        expect(root.locator('[data-mode="Voice"]')).to_have_attribute("aria-pressed","true")
        readback(page,accepted,{**OBS,"serial":"2","effectiveMode":"Voice"})
        root.locator('[data-learn]').click()
        learn=last_action(page,"nativeAntiNoizLearn")
        assert learn["instanceId"]==INSTANCE
        respond(page,learn,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=accepted)
        readback(page,accepted,{**OBS,"serial":"3","effectiveMode":"Voice","learning":True,"learnStatus":"Learning","spectrum":SPECTRUM})
        expect(root.locator('[data-learn]')).to_be_disabled()
        expect(root.locator('[data-observation]')).to_contain_text("Learning")
        expect(root.locator('[data-learn]')).to_have_attribute("data-learning","true")
        expect(root.locator(".an-spectrum-view")).to_be_visible()
        expect(root.locator(".an-meter-view")).to_be_hidden()
        expect(root.locator('[data-progress]')).to_have_text("50%")
        assert root.locator('[data-input-curve]').get_attribute("d").startswith("M")
        assert root.locator('[data-noise-curve]').get_attribute("d").startswith("M")
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-learning-1366.png"))
        page.wait_for_timeout(210)
        readback(page,accepted,{**OBS,"serial":"4","effectiveMode":"Voice","profile":True,"learnStatus":"Accepted","spectrum":{**SPECTRUM,"candidate":False,"progress":0}})
        expect(root.locator('[data-observation]')).to_contain_text("Profile ready")
        expect(root.locator('[data-learn]')).to_have_attribute("data-learning","false")
        expect(root.locator('[data-profile]')).to_have_attribute("data-ready","true")
        expect(root.locator('[data-latency]')).to_have_text("256 samples")
        expect(root.locator('[data-curve-label]')).to_have_text("Trusted noise")
        expect(root.locator(".an-spectrum-view")).to_be_visible()
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-profile-1366.png"))
        root.locator('[data-view]').click()
        expect(root.locator(".an-meter-view")).to_be_visible()
        expect(root.locator('[data-learn]')).to_be_enabled()
        # Actual rotary drag with delayed ACK, stale readback and coalescing.
        # Original approved artwork must actually load in the native bundle;
        # a missing asset must never silently degrade to a decorative circle.
        expect(root.locator(".an-knob-art")).to_have_count(3)
        page.wait_for_function("""()=>[...document.querySelectorAll('#mxNativeAntiNoiz .an-knob-art')].every(i=>i.complete&&i.naturalWidth===96)""")
        assert root.evaluate("""el=>getComputedStyle(el.querySelector('.an-panel')).backgroundImage.includes('geq31-brushed-metal.jpg')""")
        assert (bundle/"assets"/"geq31-brushed-metal.jpg").stat().st_size>0
        assert root.locator(".an-led-off").first.get_attribute("d").count("M")==27
        assert root.locator(".an-led-on").first.get_attribute("d").count("M")==16
        assert root.locator(".an-knob-art").first.get_attribute("src").endswith("mixer-gain-knob-brushed-steel.png")
        knob=root.locator('input[aria-label="Clean"]')
        bounds=knob.bounding_box()
        assert bounds and bounds["width"]>=54
        x,y=bounds["x"]+bounds["width"]/2,bounds["y"]+bounds["height"]/2
        page.mouse.move(x,y);page.mouse.down();page.mouse.move(x,y-30,steps=6)
        page.wait_for_timeout(70)
        first=last_action(page,"nativeAntiNoizSet")
        assert first["settings"]["cleanDb"]>accepted["cleanDb"]
        page.mouse.move(x,y-65,steps=7);page.wait_for_timeout(70)
        desired=float(knob.input_value())
        assert desired>first["settings"]["cleanDb"]
        assert root.locator(".an-led-on").first.get_attribute("d").count("M")==1+int(26*desired/24)
        expect(knob).to_be_enabled()
        # Require a new snapshot request, rather than silently responding
        # to the previously consumed request.
        page.wait_for_timeout(210)
        stale=last_action(page,"nativeAntiNoizSnapshot")
        respond(page,stale,"arMixBuiltinAntiNoizSnapshot",status="ACCEPTED",
                settings=accepted,observation={**OBS,"serial":"5","effectiveMode":"Voice","profile":True})
        assert float(knob.input_value())==desired
        assert page.evaluate("()=>window.__actions.filter(x=>x.action==='nativeAntiNoizSet').at(-1).requestId")==first["requestId"]
        page.mouse.up()
        assert float(knob.input_value())==desired
        respond(page,first,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=first["settings"])
        second=last_action(page,"nativeAntiNoizSet")
        assert second["requestId"]!=first["requestId"] and second["settings"]["cleanDb"]==desired
        assert float(knob.input_value())==desired
        respond(page,second,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=second["settings"])
        accepted=second["settings"]
        readback(page,accepted,{**OBS,"serial":"6","effectiveMode":"Voice","profile":True})
        assert float(knob.input_value())==desired
        # Keyboard still uses accessible native range semantics.
        knob.focus();page.keyboard.press("ArrowLeft")
        key=last_action(page,"nativeAntiNoizSet")
        assert key["settings"]["cleanDb"]==desired-.5
        respond(page,key,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=key["settings"])
        accepted=key["settings"]
        readback(page,accepted,{**OBS,"serial":"7","effectiveMode":"Voice","profile":True})
        # Capture actual approved UI, not an illustrative mockup.
        assert_p1_geometry(root)
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-1366.png"))
        assert_full_composition(root)
        # Narrow viewport: exercise actual responsive layout, not a mockup.
        page.set_viewport_size({"width":360,"height":720})
        panel=root.locator(".an-panel")
        # Width alone missed a panel stranded offscreen after resizing.
        # Require the actual visible rectangle AND unoccluded hit targets.
        page.wait_for_function("""()=>{const p=document.querySelector('#mxNativeAntiNoiz .an-panel');
          const r=p.getBoundingClientRect();return r.left>=-1&&r.top>=-1&&
            r.right<=innerWidth+1&&r.bottom<=innerHeight+1;}""")
        assert panel.evaluate("(el)=>el.scrollWidth<=el.clientWidth+2")
        assert root.evaluate("""el=>[...el.querySelectorAll('.an-dial input,[data-power],[data-close],[data-learn],[data-mode]')].every(c=>{
          const r=c.getBoundingClientRect(),hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);
          return hit===c||c.contains(hit);})"""),"narrow editor hit targets occluded"
        assert_p1_geometry(root)
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-360.png"))
        assert_full_composition(root)
        page.set_viewport_size({"width":1366,"height":768})
        # Show-store/preset plumbing is not mocked as disk success: verify
        # actual UI intent is for the same accepted instance and receipt.
        root.locator(".mx-fxp-save-as").click()
        root.locator(".mx-fxp-name").fill("Vocal noise")
        root.locator(".mx-fxp-confirm").click()
        save=last_action(page,"nativeBuiltinFxUserPreset")
        assert save["operation"]=="saveAs" and save["name"]=="Vocal noise" and save["instanceId"]==INSTANCE
        # Three real close/reopen gestures must leave one DOM and one set
        # of callbacks. No new browser engine/window/PCM graph.
        for _ in range(3):
            root.locator('[data-close]').click()
            expect(root).to_be_hidden()
            assert count("arMixBuiltinAntiNoizAck")==0
            assert count(preset_event)==baseline_preset_count
            row.locator('[data-action="openPlugin"]').click()
            expect(root).to_be_visible()
            assert count("arMixBuiltinAntiNoizAck")==1
            assert count(preset_event)==baseline_preset_count+1
            expect(root.locator('[data-learn]')).to_be_disabled()
        root.locator('[data-close]').click()
        assert not errors,errors
        browser.close()
        print("ANTI_NOIZ_APPROVED_CHROMIUM picker/insert/readback/ACK/Learn/preset/close PASS (mock IPC)")
if __name__=="__main__":
    main()
