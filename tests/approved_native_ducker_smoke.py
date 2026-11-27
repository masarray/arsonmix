"""Actual compiled native editor, emulated JUCE boundary. Native PCM is separately tested."""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE, RECEIVER, MAIN
from approved_floating_fx_component_harness import project_approved_editor_for_component_smoke

ROOT = Path(__file__).resolve().parents[1]
ID = "armix.builtin.auto-ducker.v1"
INSTANCE = "auto-ducker-native-1"
MIC2 = "9007199254740997"
DEFAULTS = dict(thresholdDb=-34, depthDb=12, attackMs=18, holdMs=220,
                releaseMs=480, hysteresisDb=5, highPassHz=120, lowPassHz=5000)

def latest(page, action):
    return page.evaluate("a=>window.__actions.filter(x=>x.action===a).at(-1)", action)

def packet(action, **kw):
    return {**{k: action[k] for k in ("requestId","channelId","deviceEpoch","graphRevision","instanceId")}, **kw}

def readback(page, sources, settings=DEFAULTS, serial="1", pending=False, available=True, after=None):
    if after is not None:
        page.wait_for_function("id=>window.__actions.filter(x=>x.action==='nativeDuckerSnapshot').at(-1)?.requestId!==id",arg=after)
    req = latest(page, "nativeDuckerSnapshot")
    assert req, "native DSP readback absent"
    emit(page,"arMixBuiltinDuckerSnapshot",packet(req,status="ACCEPTED",offline=False,
        settings=settings,sources=sources,pending=pending,meterSerial=serial,gainReductionDb=8,
        phase=2,meters=[dict(id=x,available=available,dbfs=-18) for x in sources]))
    return req["requestId"]

def main():
    spec=importlib.util.spec_from_file_location("bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-ducker-") as directory,sync_playwright() as pw:
        out=Path(directory);bundle.stage(out)
        browser=pw.chromium.launch(headless=True)
        page=browser.new_page(viewport={"width":1366,"height":768})
        errors=[];page.on("pageerror",lambda e:errors.append(str(e)))
        page.add_init_script("""window.__nativeListeners=new Map();window.__nativeIntents=[];window.__actions=[];
          window.__JUCE__={backend:{emitEvent(n,p){if(n==='arMixConsoleIntent')window.__nativeIntents.push(p);
            if(n==='arMixAction')window.__actions.push(p);},addEventListener(n,c){const old=window.__nativeListeners.get(n);
            window.__nativeListeners.set(n,old?p=>{old(p);c(p);}:c);}}};""")
        page.goto((out/"index.html").as_uri())
        project_approved_editor_for_component_smoke(page)
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeDucker")
        state=snapshot();g=state["graph"]
        g["capabilities"]["supportedOps"]+=["LOAD_INSERT","MOVE_INSERT","REMOVE_INSERT"]
        mic=copy.deepcopy(g["channels"][0]);mic.update(id=MIC2,name="MIC 2",inserts=[],sends=[])
        g["channels"].insert(2,mic)
        g["destinations"]=[{k:c[k] for k in ("id","name","role")} for c in g["channels"]]
        g["edges"].append(dict(edgeId="9223372036854775814",**{"from":MIC2,"to":MAIN},
            primaryOutput=True,gainLinear=1,tap="POST_FADER",enabled=True,signalKind="AUDIO"))
        state["mixerSession"]["channels"]=copy.deepcopy(g["channels"])
        emit(page,"arMixConsoleSnapshot",state)
        emit(page,"arMixCatalog",dict(items=[],builtIns=[dict(id=ID,name="Auto Ducker",category="Dynamics",showInPicker=True)]))
        track=page.evaluate("id=>window.__ARMIX_NATIVE_SCOPE__.tracks.find(t=>window.__ARMIX_NATIVE_M2__.nativeIdFor(t)===id)?.id",RECEIVER)
        assert track is not None
        strip=page.locator(f'#tracks .strip[data-id="{track}"]')
        strip.locator(".fx-pick.empty").first.click()
        picker=page.locator(f'#fastResults [data-fast-id="{ID}"]')
        picker.wait_for(state="visible");assert picker.count()==1
        picker.click()
        intents=page.evaluate("()=>window.__nativeIntents.filter(x=>x.operation==='LOAD_INSERT')")
        assert len(intents)==1 and intents[0]["payload"]["catalogId"]==ID
        editor=page.locator("#mxNativeDucker");assert editor.is_hidden()
        g.update(revision="3",rosterRevision="3")
        g["channels"][1]["fxRackBypassed"]=False
        g["channels"][1]["inserts"]=[dict(id=INSTANCE,catalogId=ID,name="Auto Ducker",bypassed=False)]
        state["mixerSession"].update(routeRevision="3",rosterRevision="3",channels=copy.deepcopy(g["channels"]))
        ack(page,state,intents[0]);emit(page,"arMixConsoleSnapshot",state)
        editor.wait_for(state="visible")
        readback(page,[])
        editor.screenshot(path=str(ROOT/"auto-ducker-native-initial.png"))
        assert editor.locator(".mx-ducker-dial").count()==8
        assert (out/"assets"/"mixer-gain-knob-brushed-steel.png").is_file()
        assert (out/"assets"/"auto-ducker-brushed-v6.png").is_file()
        assert editor.locator(".lcd-glass").count()==2
        assert editor.locator(".lcd-glass").evaluate_all("""els=>els.every(e=>
            getComputedStyle(e,'::before').pointerEvents==='none' &&
            getComputedStyle(e,'::after').pointerEvents==='none' &&
            getComputedStyle(e,'::before').backgroundImage.includes('gradient'))""")
        assert editor.locator(".mx-ducker-body").evaluate_all("""els=>els.every(e=>{
            const c=getComputedStyle(e);
            return c.backgroundImage.includes('mixer-gain-knob-brushed-steel.png') &&
                c.transform==='none' && c.filter.includes('brightness');
        })""")
        assert editor.evaluate("e=>getComputedStyle(e).backgroundImage.includes('auto-ducker-brushed-v6.png')")
        editor.get_by_role("button",name="Edit sources").click()
        editor.screenshot(path=str(ROOT/"auto-ducker-native-initial-picker.png"))
        main_option=editor.locator(f'.mx-ducker-option[data-source-id="{MAIN}"]')
        assert main_option.locator("input").is_disabled()
        assert "Downstream cycle" in main_option.inner_text()
        page.keyboard.press("Escape")
        assert editor.is_visible() and editor.locator(".mx-ducker-picker").is_hidden()
        editor.get_by_role("button",name="Edit sources").click()
        first=editor.locator(f'.mx-ducker-option[data-source-id="{SOURCE}"] input')
        first.evaluate("e=>window.__firstDuckerCheckbox=e")
        first.check()
        assert first.evaluate("e=>e===window.__firstDuckerCheckbox"),"picker replaced clicked checkbox"
        assert first.is_checked()
        editor.locator(f'.mx-ducker-option[data-source-id="{MIC2}"] input').check()
        assert first.is_checked()
        editor.get_by_role("button",name="Apply",exact=True).click()
        req=latest(page,"nativeDuckerSources")
        assert req and set(req["sources"])=={SOURCE,MIC2}
        assert editor.locator(".mx-ducker-source").count()==0,"optimistic trigger readback"
        emit(page,"arMixBuiltinDuckerAck",packet(req,status="REJECTED",errorCode="AUTO_DUCKER_ROUTING_CYCLE"))
        notice=editor.locator(".mx-ducker-picker-notice")
        assert notice.is_visible() and "routing cycle" in notice.inner_text()
        assert first.is_checked() and editor.locator(f'.mx-ducker-option[data-source-id="{MIC2}"] input').is_checked()
        assert editor.locator(".mx-ducker-source").count()==0
        editor.get_by_role("button",name="Apply",exact=True).click()
        retry=latest(page,"nativeDuckerSources")
        assert retry["requestId"]!=req["requestId"] and retry["sources"]==req["sources"]
        req=retry
        # Reopening the same editor while Apply is outstanding must not lose
        # the pending transaction, selected rows, or the current picker.
        assert page.evaluate("""id=>{const t=window.__ARMIX_NATIVE_SCOPE__.tracks.find(t=>window.__ARMIX_NATIVE_M2__.nativeIdFor(t)===id);return window.ArSonMixNativeDucker.open(t,0)}""",RECEIVER)
        assert first.is_checked() and editor.locator(".mx-ducker-picker").is_visible()
        assert editor.get_by_role("button",name="Apply",exact=True).is_disabled()
        second=editor.locator(f'.mx-ducker-option[data-source-id="{MIC2}"] input')
        second.uncheck() # a newer local source draft during the first ACK
        assert latest(page,"nativeDuckerSources")["requestId"]==req["requestId"]
        g["revision"]="4";state["mixerSession"]["routeRevision"]="4"
        emit(page,"arMixConsoleSnapshot",state)
        emit(page,"arMixBuiltinDuckerAck",packet(req,status="APPLIED"))
        readback(page,[SOURCE,MIC2],serial="2")
        assert editor.locator(".mx-ducker-source").count()==2
        assert editor.locator(".mx-ducker-gr").inner_text()=="-8.0 dB"
        assert editor.locator(".mx-ducker-picker").is_visible() and not second.is_checked()
        assert editor.get_by_role("button",name="Apply",exact=True).is_enabled()
        assert latest(page,"nativeDuckerSources")["requestId"]==req["requestId"],"new draft was auto-replayed"
        second.check() # now matches authoritative sources again
        assert editor.get_by_role("button",name="Apply",exact=True).is_disabled()
        editor.get_by_role("button",name="Cancel",exact=True).click()
        editor.get_by_label("Ducking preset").select_option("Gentle")
        req=latest(page,"nativeDuckerSet");assert req["settings"]["depthDb"]==6
        emit(page,"arMixBuiltinDuckerAck",packet(req,status="APPLIED"))
        readback(page,[SOURCE,MIC2],settings=req["settings"],serial="3")
        assert editor.locator(".mx-ducker-source").count()==2
        # Owner FIELD regression: no horizontal overflow or transient footer.
        assert editor.locator(".mx-ducker-status").count()==0
        def assert_geometry():
            assert editor.evaluate("""e=>[e,e.querySelector('.mx-ducker-list'),
              ...e.querySelectorAll('.mx-ducker-source,.mx-ducker-read')].every(
                n=>n.scrollWidth<=n.clientWidth+1)"""),"horizontal overflow"
        assert_geometry()
        # Wheel without click: edit starts while pointer is still over knob.
        depth=editor.locator('input[type="range"][aria-label="Depth dB"]')
        edit_button=editor.get_by_role("button",name="Edit sources")
        edit_button.evaluate("""e=>{
          window.__duckerEditMutations=[];
          window.__duckerEditObserver=new MutationObserver(
            rs=>window.__duckerEditMutations.push(...rs.map(r=>r.attributeName)));
          window.__duckerEditObserver.observe(e,{attributes:true,attributeFilter:['disabled']});
        }""")
        depth.focus()
        assert depth.evaluate("""e=>getComputedStyle(
          e.parentElement.querySelector('.mx-ducker-dial')).outlineStyle==='none'""")
        depth.hover();page.mouse.wheel(0,-120)
        page.wait_for_function("()=>window.__actions.filter(x=>x.action==='nativeDuckerSet').at(-1)?.settings.depthDb>6")
        req=latest(page,"nativeDuckerSet")
        assert edit_button.is_enabled()
        emit(page,"arMixBuiltinDuckerAck",packet(req,status="PENDING"))
        assert edit_button.is_enabled()
        assert editor.locator(".mx-ducker-phase").text_content()!="Check edit"
        page.mouse.wheel(0,-120)
        assert latest(page,"nativeDuckerSet")["requestId"]==req["requestId"]
        emit(page,"arMixBuiltinDuckerAck",packet(req,status="APPLIED"))
        readback(page,[SOURCE,MIC2],settings=req["settings"],serial="4")
        page.wait_for_function("id=>window.__actions.filter(x=>x.action==='nativeDuckerSet').at(-1)?.requestId!==id",arg=req["requestId"])
        final=latest(page,"nativeDuckerSet")
        assert final["settings"]["depthDb"]>req["settings"]["depthDb"]
        emit(page,"arMixBuiltinDuckerAck",packet(final,status="APPLIED"))
        settled_read=readback(page,[SOURCE,MIC2],settings=final["settings"],serial="5")
        assert edit_button.is_enabled()
        assert edit_button.evaluate("()=>window.__duckerEditMutations.length")==0
        assert_geometry()
        # Idle paint must not rewrite the stable power state or accessibility
        # attributes on each polling tick/readback.
        editor.locator(".armix-builtin-fx-power").evaluate("""e=>{window.__powerWrites=[];window.__powerObserver=new MutationObserver(rs=>window.__powerWrites.push(...rs.map(r=>r.attributeName)));window.__powerObserver.observe(e,{attributes:true,attributeFilter:['data-fx-state','disabled','aria-pressed','title','aria-label']});}""")
        settled_read=readback(page,[SOURCE,MIC2],settings=final["settings"],serial="5a",after=settled_read)
        settled_read=readback(page,[SOURCE,MIC2],settings=final["settings"],serial="5b",after=settled_read)
        assert editor.locator(".armix-builtin-fx-power").evaluate("()=>window.__powerWrites.length")==0
        # Source readout must fit the actual long state seen in Windows FIELD.
        readback(page,[SOURCE,MIC2],settings=final["settings"],serial="6",available=False,after=settled_read)
        assert editor.locator(".mx-ducker-read").first.inner_text()=="Unavailable"
        assert_geometry()
        # A missing ACK must not permanently lock the editor or replay held edits.
        # Use real browser gestures and a native quiescence readback, never fake APPLIED.
        depth.hover();page.mouse.wheel(0,-120)
        page.wait_for_function("id=>window.__actions.filter(x=>x.action==='nativeDuckerSet').at(-1)?.requestId!==id",arg=final["requestId"])
        lost=latest(page,"nativeDuckerSet")
        page.mouse.wheel(0,-120)
        page.wait_for_function("()=>document.querySelector('.mx-ducker-phase').textContent==='Edit delayed'",timeout=12000)
        busy_read=readback(page,[SOURCE,MIC2],settings=final["settings"],serial="7",pending=True)
        assert latest(page,"nativeDuckerSet")["requestId"]==lost["requestId"]
        assert editor.locator(".mx-ducker-phase").text_content()=="Edit delayed"
        assert editor.locator(".mx-ducker-phase").inner_text()=="EDIT DELAYED"
        readback(page,[SOURCE,MIC2],settings=final["settings"],serial="8",pending=False,after=busy_read)
        page.wait_for_function("()=>document.querySelector('.mx-ducker-phase').textContent==='Check edit'",timeout=3000)
        assert editor.locator(".mx-ducker-phase").inner_text()=="CHECK EDIT"
        assert abs(float(depth.input_value())-final["settings"]["depthDb"])<.001
        assert latest(page,"nativeDuckerSet")["requestId"]==lost["requestId"]
        emit(page,"arMixBuiltinDuckerAck",packet(lost,status="APPLIED"))
        assert latest(page,"nativeDuckerSet")["requestId"]==lost["requestId"]
        # Numeric LCD keeps range and DSP draft consistent at boundaries.
        numeric=editor.locator('input[type="number"][aria-label="Depth dB"]')
        numeric.fill("");numeric.press("Tab")
        assert abs(float(numeric.input_value())-final["settings"]["depthDb"])<.001
        # Each frame cell must be wider than it is high, not a portrait LED.
        assert editor.locator(".mx-ducker-led-cell").count()>20
        page.wait_for_function("""()=>[...document.querySelectorAll('#mxNativeDucker .mx-ducker-led-cell')]
            .every(e=>{const r=e.getBoundingClientRect();return r.height>2 &&
                Math.abs((r.width-2)/(r.height-2)-2)<.08})""")
        # Ensure no simulated peak/clip state is shown, and the glass never intercepts the picker.
        assert editor.locator(".mx-ducker-led-cell[data-peak=true],.mx-ducker-led-cell[data-clip=true]").count()==0
        editor.screenshot(path=str(ROOT/"auto-ducker-native-editor.png"))
        editor.get_by_role("button",name="Edit sources").click()
        editor.get_by_label("Search trigger sources").fill("MIC 2")
        assert editor.get_by_text("MIC 2",exact=True).count()>=1
        editor.screenshot(path=str(ROOT/"auto-ducker-native-picker.png"))
        page.set_viewport_size({"width":500,"height":740})
        assert_geometry()
        page.set_viewport_size({"width":1366,"height":768})
        editor.get_by_role("button",name="Cancel",exact=True).click()
        editor.get_by_role("button",name="Close Auto Ducker").click()
        assert editor.is_hidden()
        assert not errors,errors
        browser.close()
if __name__=="__main__":main()
