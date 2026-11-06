"""Real assembled WebView2 DOM: native EQ picker, instance, SVG, gestures, ACK.

This runs in Chromium against the EXACT approved GUI bundle used by Windows.
The isolated backend is an *explicit test double* for native event timing;
this is NOT evidence of audio DSP/ASIO performance. DSP is tested by CTest.
Actual PNGs are rendered by Chromium and uploaded only on successful tests.
"""
from pathlib import Path
import copy
import importlib.util
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE

ROOT=Path(__file__).resolve().parents[1]
EQ_ID="armix.builtin.channel-eq.v1"
INSTANCE="eq-native-1"
DEFAULTS=[80,160,250,500,1500,4500,9500,18000]

def bands_initial():
    return [{"enabled":False,"frequencyHz":float(f),"gainDb":0.,
             "q":1. if 2<=i<=5 else 0.70710678,"slope":12}
            for i,f in enumerate(DEFAULTS)]

def eq_context(s):
    return {"channelId":SOURCE,"instanceId":INSTANCE,"slot":0,
            "deviceEpoch":s["device"]["epoch"],
            "graphRevision":s["graph"]["revision"]}

def last_action(page,kind):
    all_actions=page.evaluate(
        "kind=>window.__actions.filter(x=>x.action===kind)",kind)
    assert all_actions,kind+" action not emitted"
    return all_actions[-1]

def emit_snapshot(page,state,bands,applied=None):
    action=last_action(page,"nativeEqSnapshot")
    payload={**eq_context(state),"requestId":action["requestId"],
             "status":"ACCEPTED","schemaVersion":1,"sampleRate":48000,
             "bands":copy.deepcopy(bands)}
    if applied:payload["appliedRequestId"]=applied
    emit(page,"arMixBuiltinEqSnapshot",payload)

def flush_edits(page,state,bands,handled):
    # Coalesced gestures may queue one last edit AFTER an audio readback.
    for _ in range(8):
        actions=page.evaluate("""()=>window.__actions.filter(x=>
            x.action==='nativeEqSetBand')""")
        unseen=[a for a in actions if a["requestId"] not in handled]
        if not unseen:break
        edit=unseen[-1];handled.add(edit["requestId"])
        assert edit["instanceId"]==INSTANCE
        assert edit["channelId"]==SOURCE
        assert edit["deviceEpoch"]==state["device"]["epoch"]
        assert edit["graphRevision"]==state["graph"]["revision"]
        # The control request itself must not count as a DSP ACK.
        emit(page,"arMixBuiltinEqQueue",
             {"requestId":edit["requestId"],"status":"QUEUED"})
        emit(page,"arMixBuiltinEqAck",
             {"requestId":edit["requestId"],"status":"APPLIED"})
        bands[edit["bandIndex"]]=edit["band"]
        emit_snapshot(page,state,bands,edit["requestId"])
    else:
        raise AssertionError("EQ control coalescing failed to settle")
    return handled

def main():
    spec=importlib.util.spec_from_file_location("bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-true-eq-") as directory, sync_playwright() as pw:
        out=Path(directory);bundle.stage(out)
        browser=pw.chromium.launch(headless=True)
        for width,height in [(1366,768),(1680,900),(1920,1080),(390,844)]:
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.add_init_script("""window.__nativeListeners=new Map();window.__nativeIntents=[];window.__actions=[];
              window.__JUCE__={backend:{
                emitEvent(name,payload){
                  if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
                  if(name==='arMixAction')window.__actions.push(payload);},
                addEventListener(name,callback){window.__nativeListeners.set(name,callback);}}};""")
            page.goto((out/"index.html").as_uri())
            page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeEq")
            state=snapshot()
            state["graph"]["capabilities"]["supportedOps"]+=["LOAD_INSERT","MOVE_INSERT","REMOVE_INSERT"]
            emit(page,"arMixConsoleSnapshot",state)
            strip=page.locator('#tracks .strip[data-id="1"]')
            if width<=500:
                # This is the existing operator bank UI, not a second mixer.
                page.locator("#bankSelect").select_option("0")
            strip.locator('.fx-pick.empty').first.click()
            picker=page.locator("#fastResults [data-fast-native-eq]")
            picker.wait_for(state="visible")
            assert picker.count()==1
            picker.click()
            intents=page.evaluate("""()=>window.__nativeIntents.filter(x=>
                x.operation==='LOAD_INSERT')""")
            assert len(intents)==1,("missing ordered native insert",intents)
            assert intents[0]["payload"]["catalogId"]==EQ_ID
            assert strip.locator(".fxslot-row[data-insert-id='eq-native-1']").count()==0,\
                "GUI marked EQ active before native accept"
            assert page.locator("#mxNativePeq").is_hidden(),\
                "editor auto-opened on INSERT"
            state["graph"].update(revision="3",rosterRevision="3")
            state["graph"]["channels"][0]["inserts"].append({
                "id":INSTANCE,"catalogId":EQ_ID,"name":"Parametric EQ","bypassed":False})
            state["mixerSession"].update(routeRevision="3",rosterRevision="3",
                channels=copy.deepcopy(state["graph"]["channels"]))
            ack(page,state,intents[0])
            emit(page,"arMixConsoleSnapshot",state)
            row=strip.locator('.fxslot-row[data-insert-id="eq-native-1"]')
            row.wait_for(state="visible")
            assert strip.locator(".fxslot-row:not(.is-empty)").count()==1
            page.keyboard.press("Escape")
            if width==1366:
                page.screenshot(path="native-peq-01-accepted-rack.png")
            row.locator('[data-action="openPlugin"]').click()
            editor=page.locator("#mxNativePeq")
            editor.wait_for(state="visible")
            values=bands_initial()
            handled=set()
            emit_snapshot(page,state,values)
            assert page.locator(".mx-peq-knob").count()==3
            assert page.locator(".mx-peq-band").count()==8
            assert page.locator(".mx-peq-graph svg").count()==1
            assert page.locator(".mx-peq-zero").count()==1
            assert page.locator(".mx-peq-graph circle[data-eq-node]").count()==16
            bbox=editor.bounding_box()
            assert bbox and bbox["x"]>=0 and bbox["y"]>=0,\
                "EQ window starts outside viewport"
            if width==1366:
                page.screenshot(path="native-peq-02-open-editor.png")
                # Enable bell band with an acknowledged native write.
                page.locator(".mx-peq-band").nth(2).locator(".mx-peq-band-power").click()
                # Rapid independent band changes must not overwrite one
                # another while the FIRST DSP ACK is still outstanding.
                page.locator(".mx-peq-band").nth(3).locator(".mx-peq-band-power").click()
                page.locator(".mx-peq-band").nth(4).locator(".mx-peq-band-power").click()
                flush_edits(page,state,values,handled)
                assert all(values[i]["enabled"] for i in (2,3,4)),values
                gain=page.locator('.mx-peq-knob[data-kind="gainDb"] .mx-peq-value')
                gain.fill("5.5");gain.press("Tab")
                flush_edits(page,state,values,handled)
                assert values[2]["enabled"] and values[2]["gainDb"]==5.5
                page.screenshot(path="native-peq-03-bell-colors.png")
                # One real native EQ instance containing HPF + LS + bell + HS.
                for index,gain_value in [(0,None),(1,3.0),(6,-4.0)]:
                    band=page.locator(".mx-peq-band").nth(index)
                    band.locator(".mx-peq-select").click()
                    band.locator(".mx-peq-band-power").click()
                    flush_edits(page,state,values,handled)
                    if gain_value is not None:
                        knob=page.locator('.mx-peq-knob[data-kind="gainDb"] .mx-peq-value')
                        knob.fill(str(gain_value));knob.press("Tab")
                        flush_edits(page,state,values,handled)
                assert values[0]["enabled"] and values[1]["enabled"] and values[6]["enabled"]
                page.screenshot(path="native-peq-04-combined-curve.png")
                page.locator(".mx-peq-band").nth(2).locator(".mx-peq-select").click()
                page.screenshot(path="native-peq-05-three-knobs.png")
                # Actual operator drag and wheel; first send is PENDING until DSP ACK.
                node=page.locator('.mx-peq-graph circle[data-eq-node="2"]').last
                box=node.bounding_box();assert box
                x=box["x"]+box["width"]/2;y=box["y"]+box["height"]/2
                page.mouse.move(x,y);page.mouse.down()
                page.mouse.move(x+70,y-24,steps=9);page.mouse.up()
                flush_edits(page,state,values,handled)
                node=page.locator('.mx-peq-graph circle[data-eq-node="2"]').last
                box=node.bounding_box();assert box
                page.mouse.move(box["x"]+box["width"]/2,box["y"]+box["height"]/2)
                page.mouse.wheel(0,-100)
                flush_edits(page,state,values,handled)
                assert values[2]["frequencyHz"]>250,values[2]
                assert values[2]["q"]>1,values[2]
                page.screenshot(path="native-peq-06-drag-wheel-q.png")
                # Modeless window can move and reopening reads real accepted snapshot.
                bar=page.locator(".mx-peq-title");rect=bar.bounding_box();assert rect
                page.mouse.move(rect["x"]+100,rect["y"]+15);page.mouse.down()
                page.mouse.move(rect["x"]+140,rect["y"]+30,steps=5);page.mouse.up()
                assert editor.bounding_box()["x"]>bbox["x"]+25
                editor.locator(".mx-peq-close").click();assert editor.is_hidden()
                row.locator('[data-action="openPlugin"]').click()
                emit_snapshot(page,state,values)
                assert page.locator('.mx-peq-knob[data-kind="frequencyHz"] .mx-peq-value').input_value(),\
                    "parameter readback lost after reopen"
            else:
                # Narrow + desktop screenshots come from the exact compiled GUI.
                rect=editor.bounding_box();assert rect and rect["width"]<=width and rect["height"]<=height,\
                    (width,height,rect)
                page.screenshot(path=f"native-peq-editor-{width}.png")
            assert not errors,(width,height,errors)
            page.close()
        browser.close()
    print("PASS assembled native EQ GUI: one ordered FX insert; SVG log graph;"
          " native emulated apply/ACK/readback; gestures; 4 viewports."
          " Physical PCM/ASIO remains hardware FAT PENDING.")

if __name__=="__main__":
    main()
