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
        readback(page,accepted,{**OBS,"serial":"3","effectiveMode":"Voice","learning":True,"learnStatus":"Learning"})
        expect(root.locator('[data-learn]')).to_be_disabled()
        expect(root.locator('[data-observation]')).to_contain_text("Learning")
        page.wait_for_timeout(210)
        readback(page,accepted,{**OBS,"serial":"4","effectiveMode":"Voice","profile":True,"learnStatus":"Accepted"})
        expect(root.locator('[data-observation]')).to_contain_text("Profile ready")
        expect(root.locator('[data-learn]')).to_be_enabled()
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
