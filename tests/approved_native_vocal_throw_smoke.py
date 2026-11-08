"""Real approved /preview/ bundle: ArSon Vocal Throw picker, insert, native ACK.
The JUCE event transport is emulated, NOT the native DSP nor singing quality.
DSP is independently tested in standalone CTest on Windows."""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE, RECEIVER

ROOT=Path(__file__).resolve().parents[1]
ID="armix.builtin.vocal-throw.v1"
INSTANCE="vocal-throw-test-instance"
DEFAULTS={"mode":2,"bpm":120,"division":0,"repeats":3,
 "captureMs":460,"silenceMs":220,"thresholdDb":-35,
 "wet":.42,"decay":.55,"ducking":.7}

def last(page,action):
    records=page.evaluate("name=>window.__actions.filter(x=>x.action===name)",action)
    assert records, "No "+action+" event"
    return records[-1]

def reply_snapshot(page,state,settings,applied=None,count=0,tail=False,music=None):
    act=last(page,"nativeThrowSnapshot")
    data={"requestId":act["requestId"],"channelId":SOURCE,"slot":0,
        "instanceId":INSTANCE,"status":"ACCEPTED","schemaVersion":1,
        "deviceEpoch":state["device"]["epoch"],
        "graphRevision":state["graph"]["revision"],"sampleRate":48000,
        "addedLatencySamples":0,"throwCount":count,"tailActive":tail,
        "musicSourceChannelId":music["source"] if music else "0",
        "musicSourceActive":bool(music),
        "musicLocked":bool(music),
        "musicBpm":music["bpm"] if music else 0,
        "musicConfidence":music["confidence"] if music else 0,
        "musicGridAvailable":music.get("grid",False) if music else False,
        "musicPhaseConfidence":music["confidence"] if music and music.get("grid",False) else 0,
        "settings":copy.deepcopy(settings)}
    if applied:data["appliedRequestId"]=applied
    emit(page,"arMixBuiltinThrowSnapshot",data)

def main():
    spec=importlib.util.spec_from_file_location(
        "bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="armix-throw-") as directory,sync_playwright() as pw:
        out=Path(directory);bundle.stage(out)
        browser=pw.chromium.launch(headless=True)
        for width,height in [(1366,768),(390,844)]:
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.add_init_script("""window.__nativeListeners=new Map();
              window.__nativeIntents=[];window.__actions=[];
              window.__JUCE__={backend:{
                emitEvent(name,payload){
                  if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
                  if(name==='arMixAction')window.__actions.push(payload);},
                addEventListener(name,callback){
                  const old=window.__nativeListeners.get(name);
                  window.__nativeListeners.set(name,old?
                    payload=>{old(payload);callback(payload);}:callback);}}};""")
            page.goto((out/"index.html").as_uri())
            page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeVocalThrow")
            state=snapshot()
            state["graph"]["capabilities"]["supportedOps"]+=["LOAD_INSERT","REMOVE_INSERT","MOVE_INSERT"]
            emit(page,"arMixConsoleSnapshot",state)
            if width<=500:page.locator("#bankSelect").select_option("0")
            strip=page.locator('#tracks .strip[data-id="1"]')
            strip.locator('.fx-pick.empty').first.click()
            picker=page.locator("#fastResults [data-fast-native-throw]")
            picker.wait_for(state="visible")
            assert picker.count()==1,"exactly one Vocal Throw built-in picker option"
            picker.click()
            intents=page.evaluate("""()=>window.__nativeIntents.filter(
                x=>x.operation==='LOAD_INSERT')""")
            assert len(intents)==1,intents
            assert intents[0]["payload"]["catalogId"]==ID
            assert page.locator("#mxNativeVocalThrow").is_hidden(),"no editor before accepted insert"
            state["graph"].update(revision="3",rosterRevision="3")
            state["graph"]["channels"][0]["fxRackBypassed"]=False
            state["graph"]["channels"][0]["inserts"].append({
                "id":INSTANCE,"catalogId":ID,"name":"ArSon Vocal Throw","bypassed":False})
            state["mixerSession"].update(routeRevision="3",rosterRevision="3",
                channels=copy.deepcopy(state["graph"]["channels"]))
            ack(page,state,intents[0]);emit(page,"arMixConsoleSnapshot",state)
            row=strip.locator('.fxslot-row[data-insert-id="'+INSTANCE+'"]')
            row.wait_for(state="visible")
            page.keyboard.press("Escape")
            row.locator('[data-action="openPlugin"]').click()
            editor=page.locator("#mxNativeVocalThrow")
            editor.wait_for(state="visible")
            assert editor.locator("#vtPreset").count()==1
            assert editor.locator("#vtClose.armix-floating-close").count()==1
            assert editor.locator("#vtPower").get_attribute("data-state")=="on"
            assert editor.locator("#vtMode").input_value()=="2"
            initial=last(page,"nativeThrowSnapshot")
            page.wait_for_timeout(300)
            assert last(page,"nativeThrowSnapshot")["requestId"]==initial["requestId"],\
                "snapshot polling overwrote outstanding request too early"
            reply_snapshot(page,state,DEFAULTS)
            assert editor.locator("#vtStatus").inner_text()=="NATIVE DSP"
            assert editor.locator("#vtCount").inner_text()=="0 throws"
            box=editor.bounding_box()
            assert box and box["width"]<=410 and box["width"]<=width and \
                box["x"]>=0 and box["x"]+box["width"]<=width+1,(width,box)
            assert editor.evaluate("(el)=>el.scrollWidth<=el.clientWidth+1"),\
                "compact FX window overflows horizontal viewport"
            page.screenshot(path="arsonmix-vocal-throw-"+str(width)+".png")
            editor.screenshot(path="arsonmix-vocal-throw-editor-"+str(width)+".png")
            if width==1366:
                editor.locator("#vtMode").select_option("0")
                edit=last(page,"nativeThrowSet")
                assert edit["settings"]["mode"]==0 and edit["instanceId"]==INSTANCE
                assert edit["channelId"]==SOURCE
                emit(page,"arMixBuiltinThrowQueue",{"requestId":edit["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinThrowAck",{"requestId":edit["requestId"],"status":"APPLIED"})
                reply_snapshot(page,state,{**DEFAULTS,"mode":0},applied=edit["requestId"])
                assert editor.locator("#vtMode").input_value()=="0"
                editor.locator("#vtThrow").click()
                cue=last(page,"nativeThrowTrigger")
                assert cue["instanceId"]==INSTANCE and cue["channelId"]==SOURCE
                assert cue["slot"]==0
                emit(page,"arMixBuiltinThrowQueue",{"requestId":cue["requestId"],"status":"QUEUED"})
                emit(page,"arMixBuiltinThrowAck",{"requestId":cue["requestId"],"status":"APPLIED"})
                reply_snapshot(page,state,{**DEFAULTS,"mode":0},count=1,tail=True)
                assert editor.locator("#vtCount").inner_text()=="1 throws"
                assert editor.locator("#vtStatus").inner_text()=="ECHO TAIL"
            # Real native tempo-source command (not a browser-only BPM guess).
            source_menu=editor.locator("#vtMusicSource")
            assert source_menu.locator('option[value="'+RECEIVER+'"]').count()==1
            source_menu.select_option(RECEIVER)
            request=last(page,"nativeThrowTempoSource")
            assert request["sourceChannelId"]==RECEIVER and request["channelId"]==SOURCE
            assert editor.locator("#vtMusicStatus").inner_text().startswith("Validating")
            emit(page,"arMixBuiltinThrowTempoAck",{
                "requestId":request["requestId"],"status":"APPLIED",
                "sourceChannelId":RECEIVER})
            reply_snapshot(page,state,
                {**DEFAULTS,"mode":0} if width==1366 else DEFAULTS,
                count=1 if width==1366 else 0,tail=width==1366,
                music={"source":RECEIVER,"bpm":120,"confidence":.85})
            assert "AUTO 120 BPM" in editor.locator("#vtMusicStatus").inner_text()
            assert "BPM only" in editor.locator("#vtMusicStatus").inner_text()
            # Snapshot transport is request/response, not unsolicited push.
            # Wait for a fresh native read request before testing a second
            # DSP status update; a replay of the prior request is ignored.
            previous=last(page,"nativeThrowSnapshot")["requestId"]
            page.wait_for_function(
                """old=>window.__actions.filter(x=>x.action==='nativeThrowSnapshot').at(-1)?.requestId!==old""",
                arg=previous,timeout=4000)
            # Confidence-approved grid is a separate native fact; the GUI
            # must never fabricate it from BPM alone.
            reply_snapshot(page,state,
                {**DEFAULTS,"mode":0} if width==1366 else DEFAULTS,
                count=1 if width==1366 else 0,tail=width==1366,
                music={"source":RECEIVER,"bpm":120,"confidence":.85,"grid":True})
            assert "BEAT GRID" in editor.locator("#vtMusicStatus").inner_text()
            if width==1366:
                # The next changed graph invalidates the old editor instance.
                state["graph"]["channels"][0]["inserts"][0]["id"]="replaced-instance"
                state["graph"].update(revision="4",rosterRevision="4")
                state["mixerSession"].update(routeRevision="4",rosterRevision="4",
                    channels=copy.deepcopy(state["graph"]["channels"]))
                emit(page,"arMixConsoleSnapshot",state)
                page.wait_for_function("""()=>document.querySelector('#vtThrow')?.disabled""",timeout=2000)
            editor.locator("#vtClose").click()
            assert editor.is_hidden(),"standard close must dismiss editor"
            assert not errors,(width,errors)
            page.close()
        browser.close()
    print("PASS: ArSon Vocal Throw native picker/ACK/manual cue/compact editor in 2 viewports")

if __name__=="__main__":
    main()
