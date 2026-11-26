"""Anti Noiz actual approved native bundle, with mock IPC (NOT audio/FAT).
The real catalog picker, graph ACK/readback, inline editor, typed transport
and shared preset bar run in Chromium. No DSP or audio device is mocked as
a measured result. The Windows FxInstance CTest is the separate PCM gate.
"""
import copy
import sys
from pathlib import Path
from playwright.sync_api import sync_playwright, expect
from native_event_fixture import NATIVE_EVENTS
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
BACKEND = NATIVE_EVENTS + """(()=>{
 window.__nativeIntents=[];window.__actions=[];
 window.__anListenerCount=window.__nativeListenerCount;
 window.__JUCE__={backend:{
  emitEvent(name,payload){
   if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
   if(name==='arMixAction')window.__actions.push(payload);
  },
  ...window.__nativeListenerMethods
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
    assert all(abs(x["h"]-24)<=1 for x in g["controls"][:2]),g
    assert abs(g["power"]["w"]-26)<=1 and abs(g["close"]["w"]-24)<=1,g
    assert all(abs(x["h"]-30)<=1 for x in g["controls"][2:]),g
    assert max(x["y"] for x in g["legend"])-min(x["y"] for x in g["legend"])<=1,g
    assert g["status"]["h"]==24 and not g["overflow"],g

def lcd_geometry(root):
    return root.evaluate("""el=>{
      const rect=e=>{const r=e.getBoundingClientRect();return [r.x,r.y,r.width,r.height];};
      return [rect(el),...['.an-panel','.an-display','.an-top','.an-lcd-content','.an-legend','.an-knobs','.an-source','footer'].map(s=>rect(el.querySelector(s)))];
    }""")

def assert_lcd_fixed(root,baseline):
    current=lcd_geometry(root)
    assert all(abs(a-b)<=.5 for before,after in zip(baseline,current) for a,b in zip(before,after)), (baseline,current)
    assert root.evaluate("""el=>{
      const lcd=el.querySelector('.an-display'),slot=el.querySelector('.an-lcd-content');
      const page=slot.querySelector(':scope > :not([hidden])');
      const r=slot.getBoundingClientRect(),p=page.getBoundingClientRect();
      const fits=(a,b)=>a.left>=b.left-.5&&a.top>=b.top-.5&&a.right<=b.right+.5&&a.bottom<=b.bottom+.5;
      if(getComputedStyle(lcd).backgroundColor!=='rgb(0, 0, 0)'||Math.abs(r.height-94)>.5||!fits(p,r))return false;
      if(!page.matches('.an-spectrum-view'))return true;
      const plot=page.querySelector('.an-spectrum-plot'),svg=plot.querySelector('svg');
      const axis=page.querySelector('.an-db-axis'),freq=page.querySelector('.an-freq-axis');
      return [plot,svg,axis,freq,page.querySelector('.an-spectrum-key')].every(e=>fits(e.getBoundingClientRect(),r))&&
        plot.clientHeight>=48&&plot.clientWidth>=100&&axis.getBoundingClientRect().left>=plot.getBoundingClientRect().right&&
        Math.abs(freq.getBoundingClientRect().width-plot.getBoundingClientRect().width)<=1&&
        page.scrollWidth<=page.clientWidth+1&&page.scrollHeight<=page.clientHeight+1;
    }"""),"fixed black LCD content and contained spectrum"

def main():
    bundle=Path(sys.argv[1]).resolve()
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        page=browser.new_page(viewport={"width":1366,"height":768},has_touch=True)
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
        # Compare canonical FX chrome styles against the actual PEQ controls.
        page.evaluate("""async()=>{await document.fonts.load('500 12px "ArSonMix Inter"');await document.fonts.ready;}""")
        assert root.evaluate("""el=>{
          const font=e=>getComputedStyle(e).fontFamily.includes('ArSonMix Inter');
          const faces=[...document.fonts].filter(f=>f.family.replace(/["']/g,'')==='ArSonMix Inter');
          if(!faces.some(f=>f.status==='loaded')||!font(el.querySelector('.an-panel'))||!font(el.querySelector('.an-control output')))return false;
          const pair=(a,b,props,pseudo)=>{const x=getComputedStyle(a,pseudo),y=getComputedStyle(b,pseudo);return props.every(k=>x[k]===y[k]);};
          const p=el.querySelector('[data-power]'),c=el.querySelector('[data-close]');
          const eqP=document.querySelector('#mxNativePeq .armix-builtin-fx-power'),eqC=document.querySelector('#mxNativePeq .armix-builtin-fx-close');
          if(!eqP||!eqC||p.textContent!==eqP.textContent)return false;
          const state=eqP.dataset.fxState;eqP.dataset.fxState=p.dataset.fxState;
          const props=['backgroundImage','borderRadius','borderTopColor','color','boxShadow','width','height','fontSize'];
          const same=pair(p,eqP,props)&&pair(c,eqC,props)&&pair(c,eqC,['backgroundImage','backgroundSize'],'::after');
          eqP.dataset.fxState=state;return same;
        }"""),"shared FX chrome and loaded Inter"
        expect(root.locator('[data-power]')).to_have_attribute("data-fx-state","on")
        lcd=root.locator(".an-display")
        switch=root.locator('[data-lcd-switch]')
        expect(root.locator('[data-view]')).to_have_count(0)
        expect(switch).to_have_count(1)
        expect(switch).to_have_attribute("aria-pressed","false")
        expect(root.locator('[data-lcd-hint]')).to_have_text("Tap for spectrum")
        # The gloss layers must not own any pointer events. The single
        # accessible transparent hit surface must cover the entire LCD.
        assert lcd.evaluate("""el=>{
          const r=el.getBoundingClientRect(),b=el.querySelector('[data-lcd-switch]');
          const s=getComputedStyle(b);
          return ['::before','::after'].every(p=>getComputedStyle(el,p).pointerEvents==='none')&&
            getComputedStyle(el,'::before').backgroundImage.includes('linear-gradient')&&
            s.backgroundColor==='rgba(0, 0, 0, 0)'&&
            [[.1,.1],[.5,.5],[.9,.9]].every(([x,y])=>document.elementFromPoint(r.left+r.width*x,r.top+r.height*y)===b);
        }"""),"gloss and full-screen hit target"
        writes=lambda:page.evaluate("()=>window.__actions.filter(x=>['nativeAntiNoizSet','nativeAntiNoizLearn'].includes(x.action)).length")
        before=writes()
        gr_baseline=lcd_geometry(root)
        assert_lcd_fixed(root,gr_baseline)
        switch.click(position={"x":12,"y":12})
        expect(root.locator(".an-spectrum-view")).to_be_visible()
        expect(root.locator('[data-spectrum-empty]')).to_be_visible()
        assert_lcd_fixed(root,gr_baseline)
        expect(switch).to_have_attribute("aria-pressed","true")
        expect(root.locator('[data-lcd-hint]')).to_have_text("Tap for GR")
        switch.focus();page.keyboard.press("Space")
        expect(root.locator(".an-meter-view")).to_be_visible()
        switch.tap()
        expect(root.locator(".an-spectrum-view")).to_be_visible()
        page.keyboard.press("Enter")
        expect(root.locator(".an-meter-view")).to_be_visible()
        assert_lcd_fixed(root,gr_baseline)
        assert writes()==before,"LCD view must not write DSP"
        switch.evaluate("(e)=>e.blur()")
        assert_lcd_fixed(root,gr_baseline)
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
            expect(root.locator('[data-power]')).to_have_attribute("data-fx-state","off" if kind=="SLOT" else "rack-off")
            if kind!="SLOT": expect(root.locator('[data-power]')).to_be_disabled()
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
        expect(root.locator('[data-display-title]')).to_have_text("NOISE SPECTRUM")
        expect(switch).to_be_disabled()
        expect(switch).to_have_attribute("aria-pressed","true")
        expect(root.locator('[data-lcd-hint]')).to_have_text("Capturing…")
        # Physical click during Learn must not leave capture mode.
        bounds=lcd.bounding_box()
        page.mouse.click(bounds["x"]+bounds["width"]/2,bounds["y"]+bounds["height"]/2)
        expect(root.locator(".an-spectrum-view")).to_be_visible()
        assert_lcd_fixed(root,gr_baseline)
        expect(root.locator('[data-progress]')).to_have_text("50%")
        assert root.locator('[data-input-curve]').get_attribute("d").startswith("M")
        assert root.locator('[data-noise-curve]').get_attribute("d").startswith("M")
        assert root.locator('[data-noise-fill]').get_attribute("d").endswith("Z")
        # One line segment per actual non-DC bin, no interpolated extra peaks.
        assert root.locator('[data-input-curve]').get_attribute("d").count("L")==127
        assert root.locator('[data-input-curve]').get_attribute("d").startswith("M0.0 ")
        assert root.locator('[data-noise-curve]').evaluate("el=>getComputedStyle(el).stroke")=="rgb(227, 182, 108)"
        captured_noise_path=root.locator('[data-noise-curve]').get_attribute("d")
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-learning-1366.png"))
        page.wait_for_timeout(210)
        readback(page,accepted,{**OBS,"serial":"4","effectiveMode":"Voice","profile":True,"learnStatus":"Accepted","spectrum":{**SPECTRUM,"candidate":False,"progress":0}})
        expect(root.locator('[data-observation]')).to_contain_text("Profile ready")
        expect(root.locator('[data-learn]')).to_have_attribute("data-learning","false")
        expect(root.locator('[data-profile]')).to_have_attribute("data-ready","true")
        expect(root.locator('[data-latency]')).to_have_text("256 samples")
        expect(root.locator('[data-curve-label]')).to_have_text("Trusted noise")
        assert root.locator('[data-noise-curve]').get_attribute("d")==captured_noise_path
        expect(root.locator(".an-spectrum-view")).to_be_visible()
        assert_lcd_fixed(root,gr_baseline)
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-profile-1366.png"))
        expect(switch).to_be_enabled()
        switch.click()
        expect(root.locator(".an-meter-view")).to_be_visible()
        expect(root.locator('[data-display-title]')).to_have_text("GAIN REDUCTION")
        assert_lcd_fixed(root,gr_baseline)
        expect(root.locator('[data-learn]')).to_be_enabled()
        # Actual rotary drag with delayed ACK, stale readback and coalescing.
        # Original approved artwork must actually load in the native bundle;
        # a missing asset must never silently degrade to a decorative circle.
        expect(root.locator(".an-knob-art")).to_have_count(3)
        page.wait_for_function("""()=>[...document.querySelectorAll('#mxNativeAntiNoiz .an-knob-art')].every(i=>i.complete&&i.naturalWidth===96)""")
        assert root.evaluate("""el=>getComputedStyle(el.querySelector('.an-panel')).backgroundImage.includes('geq31-brushed-metal.jpg')""")
        assert (bundle/"assets"/"geq31-brushed-metal.jpg").stat().st_size>0
        ring=root.locator(".an-led-off").first.get_attribute("d")
        assert ring.count("M")==27 and ring.count("A45.5 45.5")==27 and "L" not in ring
        assert root.locator(".an-led-on").first.get_attribute("d").count("M")==1+int(26*accepted["cleanDb"]/40)
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
        assert root.locator(".an-led-on").first.get_attribute("d").count("M")==1+int(26*desired/40)
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
        knob.evaluate("(e)=>e.blur()")
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
        assert root.evaluate("""el=>[...el.querySelectorAll('.an-dial input,[data-power],[data-close],[data-learn],[data-mode],[data-lcd-switch]')].every(c=>{
          const r=c.getBoundingClientRect(),hit=document.elementFromPoint(r.x+r.width/2,r.y+r.height/2);
          return hit===c||c.contains(hit);})"""),"narrow editor hit targets occluded"
        compact_baseline=lcd_geometry(root)
        assert_lcd_fixed(root,compact_baseline)
        switch.tap()
        expect(root.locator(".an-spectrum-view")).to_be_visible()
        assert_lcd_fixed(root,compact_baseline)
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-spectrum-360.png"))
        switch.tap()
        expect(root.locator(".an-meter-view")).to_be_visible()
        assert_lcd_fixed(root,compact_baseline)
        switch.evaluate("(e)=>e.blur()")
        assert_p1_geometry(root)
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-360.png"))
        assert_full_composition(root)
        page.set_viewport_size({"width":1366,"height":768})
        # Actual advanced controls must reach the same accepted native identity.
        root.locator(".an-pro summary").click()
        manual=root.locator('[data-pro-flag="manualExpander"]')
        manual.click()
        edit=last_action(page,"nativeAntiNoizSet")
        assert edit["settings"]["manualExpander"] is True
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        expect(manual).to_be_checked()
        expect(root.locator('input[aria-label="Silence"]')).to_be_disabled()
        threshold=root.locator('input[aria-label="Threshold"]')
        expect(threshold).to_be_enabled()
        threshold.focus();page.keyboard.press("ArrowRight")
        edit=last_action(page,"nativeAntiNoizSet")
        assert edit["settings"]["thresholdDb"]==-39
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        readback(page,accepted,{**OBS,"serial":"19","effectiveMode":"Voice","profile":True,"detectorLevel":.01})
        expect(root.locator("[data-detector]")).to_have_text("-40.0 dBFS")
        expect(root.locator("[data-pro-dot]")).to_be_visible()
        expect(root.locator("[data-pro-curve]")).to_have_attribute("d",__import__("re").compile("^M"))
        root.locator('[data-pro-tab="sidechain"]').click()
        expect(root.locator('[data-pro-page="expander"]')).to_be_hidden()
        smart=root.locator('[data-pro-flag="smartVoice"]')
        smart.click()
        edit=last_action(page,"nativeAntiNoizSet")
        assert edit["settings"]["smartVoice"] is True
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        expect(smart).to_be_checked()
        margin=root.locator('input[aria-label="Noise margin"]')
        margin.focus();page.keyboard.press("ArrowRight")
        edit=last_action(page,"nativeAntiNoizSet")
        assert edit["settings"]["smartMarginDb"]==10
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        readback(page,accepted,{**OBS,"serial":"190","effectiveMode":"Voice","profile":True,"detectorLevel":.01,"sidechainReference":True,"sidechainNoisePeak":.01,"sidechainThreshold":.0316227766})
        expect(root.locator("[data-smart-noise]")).to_have_text("-40.0 dBFS")
        expect(root.locator("[data-sidechain-effective]")).to_have_text("-30.0 dBFS")
        expect(root.locator("[data-smart-lift]")).to_have_text("9.0 dB")
        assert accepted["thresholdDb"]==-39
        root.locator('[data-pro-tab="spectral"]').click()
        expect(root.locator('[data-pro-page="sidechain"]')).to_be_hidden()
        spectral=root.locator('[data-pro-flag="spectralManual"]')
        spectral.click()
        edit=last_action(page,"nativeAntiNoizSet")
        assert edit["settings"]["spectralManual"] is True
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        expect(spectral).to_be_checked()
        reduction=root.locator('input[aria-label="Reduction value"]')
        reduction.fill("18.5");reduction.press("Enter")
        edit=last_action(page,"nativeAntiNoizSet")
        assert edit["settings"]["cleanDb"]==18.5
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        assert float(root.locator('input[aria-label="Clean"]').input_value())==18.5
        artifact=root.locator('input[aria-label="Artifact control"]')
        artifact.focus();page.keyboard.press("ArrowRight")
        edit=last_action(page,"nativeAntiNoizSet")
        assert abs(edit["settings"]["artifactControl"]-.51)<.0001
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        readback(page,accepted,{**OBS,"serial":"20","effectiveMode":"Voice","profile":True,"detectorLevel":.01})
        root.locator("[data-audition]").click()
        edit=last_action(page,"nativeAntiNoizAudition")
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=accepted)
        readback(page,accepted,{**OBS,"serial":"21","effectiveMode":"Voice","profile":True,"audition":True})
        expect(root.locator("[data-audition]")).to_have_text("Stop noise audition")
        root.locator("[data-clear-profile]").click()
        edit=last_action(page,"nativeAntiNoizClear")
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=accepted)
        readback(page,accepted,{**OBS,"serial":"22","effectiveMode":"Voice","profile":False,"audition":False})
        expect(root.locator("[data-audition]")).to_be_disabled()
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-pro-1366.png"))
        # Numeric editing uses the same acknowledged control path; no optimistic durable state.
        number=root.locator('input[aria-label="Artifact control value"]')
        number.fill("72");number.press("Enter")
        edit=last_action(page,"nativeAntiNoizSet")
        assert abs(edit["settings"]["artifactControl"]-.72)<.0001
        respond(page,edit,"arMixBuiltinAntiNoizAck",status="APPLIED",settings=edit["settings"])
        accepted=edit["settings"]
        root.locator('[data-pro-tab="expander"]').click()
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-pro-expander-1366.png"))
        root.locator('[data-pro-tab="sidechain"]').click()
        root.screenshot(path=str(bundle.parent/"anti-noiz-operator-pro-sidechain-1366.png"))
        page.set_viewport_size({"width":360,"height":720})
        for name in ("expander","spectral","sidechain"):
            tab=root.locator(f'[data-pro-tab="{name}"]');tab.click()
            expect(root.locator(f'[data-pro-page="{name}"]')).to_be_visible()
            assert root.locator(".an-panel").evaluate("el=>el.scrollWidth<=el.clientWidth+2")
            assert root.locator(f'[data-pro-page="{name}"]').evaluate("el=>el.scrollWidth<=el.clientWidth+2")
            assert root.evaluate("""el=>{const panel=el.querySelector('.an-panel'),p=panel.getBoundingClientRect();
              return ['.an-head','footer','.an-pro-tabs'].every(s=>{const r=el.querySelector(s).getBoundingClientRect();
                return r.top>=p.top-.5&&r.bottom<=p.bottom+.5;})&&panel.scrollTop===0;
            }"""),"professional chrome and Learn must remain inside the face"

            root.screenshot(path=str(bundle.parent/f"anti-noiz-operator-pro-{name}-360.png"))
        root.locator('[data-pro-tab="sidechain"]').focus();page.keyboard.press("Home")
        expect(root.locator('[data-pro-tab="expander"]')).to_have_attribute("aria-selected","true")
        page.set_viewport_size({"width":1366,"height":768})
        root.locator(".an-pro summary").click()
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
            readback(page,accepted,{**OBS,"serial":"8","effectiveMode":"Voice","profile":True,"learnStatus":"Accepted","spectrum":{**SPECTRUM,"candidate":False,"progress":0}})
            expect(root.locator('[data-lcd-switch]')).to_have_attribute("aria-pressed","false")
            expect(root.locator(".an-meter-view")).to_be_visible()
        root.locator('[data-close]').click()
        assert not errors,errors
        browser.close()
        print("ANTI_NOIZ_APPROVED_CHROMIUM picker/insert/readback/ACK/Learn/preset/close PASS (mock IPC)")
if __name__=="__main__":
    main()
