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

def node_pixel(page,index):
    """Use the actual SVG user-space->screen transform, not Playwright's
    bounding_box on a transparent SVG circle (Chromium may return None).
    Mouse events below remain real browser pointer interactions.
    """
    selector=f'.mx-peq-graph circle[data-eq-node="{index}"][role="slider"]'
    # The graph replaces its SVG children on rAF. Resolve the current
    # node and measure it in the same browser turn, then require a real hit.
    position=page.wait_for_function("""selector=>{
      const el=document.querySelector(selector);
      const svg=el?.ownerSVGElement,matrix=svg?.getScreenCTM();
      if(!matrix)return null;
      const point=svg.createSVGPoint();
      point.x=Number(el.getAttribute('cx'));
      point.y=Number(el.getAttribute('cy'));
      const {x,y}=point.matrixTransform(matrix);
      return x>0 && y>0 && document.elementFromPoint(x,y)===el ? {x,y} : null;
    }""",arg=selector).json_value()
    return position["x"],position["y"]


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

def preset_result(page,state,command,**details):
    # The native preset library is a controlled disk service, NOT DSP.
    emit(page,"arMixBuiltinFxPresetResult",{
        **eq_context(state),"requestId":command["requestId"],
        "operation":command["operation"],"status":"OK",
        "catalogId":EQ_ID,**details})

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
                addEventListener(name,callback){
                  const prior=window.__nativeListeners.get(name);
                  window.__nativeListeners.set(name,prior
                    ?payload=>{prior(payload);callback(payload);}
                    :callback);}}};""")
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
            state["graph"]["channels"][0]["fxRackBypassed"]=False
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
            # The plugin editor may appear one frame before its first native
            # DSP snapshot. Prove ON from accepted readback, not a DOM race.
            values=bands_initial()
            handled=set()
            emit_snapshot(page,state,values)
            listing=last_action(page,"nativeBuiltinFxUserPreset")
            assert listing["operation"]=="list" and listing["catalogId"]==EQ_ID
            assert listing["instanceId"]==INSTANCE
            preset_result(page,state,listing,items=[])
            assert page.locator(".mx-peq .mx-fxp-save-as").is_enabled()
            assert page.locator(".mx-peq .mx-fxp-manage").is_disabled()
            page.wait_for_function("""()=>document.querySelector('.mx-peq-global')
                ?.dataset.fxState==='on'&&getComputedStyle(
                document.querySelector('.mx-peq-global')).color==='rgb(118, 240, 185)'""")
            assert page.locator(".mx-peq-global").get_attribute("aria-pressed")=="true"
            power=page.locator(".mx-peq-global")
            assert power.evaluate("(el)=>getComputedStyle(el).color")=="rgb(118, 240, 185)"
            # The native EQ Power must reflect accepted insertion bypass and
            # parent rack bypass; do not derive state from optimistic clicks.
            state["graph"]["channels"][0]["inserts"][0]["bypassed"]=True
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            page.wait_for_function("""()=>document.querySelector('.mx-peq-global')
                ?.dataset.fxState==='off'&&getComputedStyle(
                document.querySelector('.mx-peq-global')).color==='rgb(233, 155, 159)'""")
            assert power.get_attribute("aria-pressed")=="false"
            state["graph"]["channels"][0]["inserts"][0]["bypassed"]=False
            state["graph"]["channels"][0]["fxRackBypassed"]=True
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            page.wait_for_function("""()=>document.querySelector('.mx-peq-global')
                ?.dataset.fxState==='rack-off'&&getComputedStyle(
                document.querySelector('.mx-peq-global')).color==='rgb(240, 196, 131)'""")
            assert power.is_disabled()
            state["graph"]["channels"][0]["fxRackBypassed"]=False
            state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
            emit(page,"arMixConsoleSnapshot",state)
            page.wait_for_function("""()=>document.querySelector('.mx-peq-global')
                ?.dataset.fxState==='on'&&getComputedStyle(
                document.querySelector('.mx-peq-global')).color==='rgb(118, 240, 185)'""")
            assert not power.is_disabled()
            assert page.locator(".mx-peq-knob").count()==3
            assert page.locator(".mx-peq-band").count()==8
            assert page.locator(".mx-peq-graph svg").count()==1
            # Native EQ graph is painted by requestAnimationFrame following the
            # accepted-snapshot event. The editor can be visible one frame
            # before the SVG is populated; wait for the real rendered line,
            # without sleeping or weakening the expected graph assertions.
            page.locator(".mx-peq-zero").wait_for(state="attached")
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
                # The three power clicks leave band 4 selected. Explicitly
                # select bell 2 before checking its independent gain edit.
                page.locator(".mx-peq-band").nth(2).locator(".mx-peq-select").click()
                page.wait_for_function("()=>document.querySelectorAll('.mx-peq-band')[2].classList.contains('active')")
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
                        band.locator('.mx-peq-band-power[aria-pressed="true"]').wait_for()
                        knob=page.locator('.mx-peq-knob[data-kind="gainDb"] .mx-peq-value')
                        knob.fill(str(gain_value));knob.press("Tab")
                        flush_edits(page,state,values,handled)
                assert values[0]["enabled"] and values[1]["enabled"] and values[6]["enabled"]
                page.screenshot(path="native-peq-04-combined-curve.png")
                page.locator(".mx-peq-band").nth(2).locator(".mx-peq-select").click()
                page.screenshot(path="native-peq-05-three-knobs.png")
                # Actual operator drag and wheel; first send is PENDING until DSP ACK.
                x,y=node_pixel(page,2)
                page.mouse.move(x,y);page.mouse.down()
                page.mouse.move(x+70,y-24,steps=9);page.mouse.up()
                flush_edits(page,state,values,handled)
                x,y=node_pixel(page,2)
                page.mouse.move(x,y)
                page.mouse.wheel(0,-100)
                flush_edits(page,state,values,handled)
                assert values[2]["frequencyHz"]>250,values[2]
                assert values[2]["q"]>1,values[2]
                page.screenshot(path="native-peq-06-drag-wheel-q.png")
                # M4: one native library across built-in FX. EQ has eight
                # independent AUDIO-applied SPSC band edits: its Recall must
                # await EVERY matching ACK and sample-rate-valid readback.
                saved=copy.deepcopy(values)
                editor.locator(".mx-fxp-save-as").click()
                form=editor.locator(".mx-fxp-form")
                assert form.is_visible()
                form.locator(".mx-fxp-name").fill("Live Vocal EQ")
                form.locator(".mx-fxp-confirm").click()
                command=last_action(page,"nativeBuiltinFxUserPreset")
                assert command["operation"]=="saveAs"
                assert command["catalogId"]==EQ_ID
                assert command["instanceId"]==INSTANCE
                preset_id="ebda8d9a-192b-4102-9b0c-8c5169cf3f55"
                preset_result(page,state,command,presetId=preset_id,name="Live Vocal EQ")
                command=last_action(page,"nativeBuiltinFxUserPreset")
                assert command["operation"]=="list"
                preset_result(page,state,command,items=[
                    {"id":preset_id,"name":"Live Vocal EQ","category":"My Presets"}])
                user_value="user:"+preset_id
                select=editor.locator(".mx-peq-preset-select")
                assert select.input_value()==user_value
                assert editor.locator(".mx-fxp-manage").is_enabled()
                # Three independent deliberate changes after saving. User
                # selection is no longer misrepresented as the saved sound.
                for index in (2,3,4):
                    editor.locator(".mx-peq-band").nth(index).locator(
                        ".mx-peq-band-power").click()
                    flush_edits(page,state,values,handled)
                assert any(values[i]!=saved[i] for i in (2,3,4))
                assert select.input_value()=="CUSTOM"
                select.select_option(user_value)
                assert form.is_visible(), "modified sound must be confirmed"
                assert "DISCARD" in form.locator(".mx-fxp-prompt").inner_text()
                form.locator(".mx-fxp-confirm").click()
                read=last_action(page,"nativeBuiltinFxUserPreset")
                assert read["operation"]=="read" and read["presetId"]==preset_id
                old_count=page.evaluate("""()=>window.__actions.filter(
                    x=>x.action==='nativeEqSetBand').length""")
                # A wrong catalog must NOT release the exact-instance read.
                emit(page,"arMixBuiltinFxPresetResult",{
                    **eq_context(state),"requestId":read["requestId"],
                    "operation":"read","status":"OK",
                    "catalogId":"armix.builtin.channel-compressor.v1",
                    "presetId":preset_id,"settings":{"bands":saved}})
                assert page.evaluate("""()=>window.__actions.filter(
                    x=>x.action==='nativeEqSetBand').length""")==old_count
                preset_result(page,state,read,presetId=preset_id,
                              name="Live Vocal EQ",settings={"bands":saved})
                assert select.input_value()!="user:"+preset_id
                # Each subsequent nativeEqSetBand must appear only AFTER the
                # previous exact AUDIO ACK and accepted DSP readback. No
                # browser optimistic 'preset applied' before the last band.
                new_commands=[]
                for step in range(8):
                    unseen=page.evaluate("""seen=>window.__actions.filter(x=>
                        x.action==='nativeEqSetBand'&&!seen.includes(x.requestId))""",
                        list(handled))
                    if not unseen:break
                    assert len(unseen)==1,(step,unseen)
                    edit=unseen[0]
                    assert edit["bandIndex"] in (2,3,4)
                    handled.add(edit["requestId"])
                    new_commands.append(edit)
                    emit(page,"arMixBuiltinEqQueue",{
                        "requestId":edit["requestId"],"status":"QUEUED"})
                    emit(page,"arMixBuiltinEqAck",{
                        "requestId":edit["requestId"],"status":"APPLIED"})
                    if step==0:
                        emit_snapshot(page,state,values,applied="wrong-audio-id")
                        assert page.evaluate("""()=>window.__actions.filter(x=>
                            x.action==='nativeEqSetBand').length""")==old_count+1
                    values[edit["bandIndex"]]=copy.deepcopy(edit["band"])
                    emit_snapshot(page,state,values,applied=edit["requestId"])
                    if step<2:
                        assert select.input_value()!=user_value
                assert len(new_commands)==3,new_commands
                assert values==saved,(values,saved)
                assert select.input_value()==user_value
                assert "AUDIO-VERIFIED" in editor.locator(".mx-peq-status").inner_text()
                # Delete is disk-only: no new EQ band edit and no DSP mutation.
                editor.locator(".mx-fxp-manage").click()
                editor.locator('.mx-fxp-action[data-operation="delete"]').click()
                assert form.is_visible()
                form.locator(".mx-fxp-confirm").click()
                command=last_action(page,"nativeBuiltinFxUserPreset")
                assert command["operation"]=="delete"
                assert command["presetId"]==preset_id
                preset_result(page,state,command,presetId=preset_id)
                command=last_action(page,"nativeBuiltinFxUserPreset")
                assert command["operation"]=="list"
                preset_result(page,state,command,items=[])
                assert select.locator("option[value^='user:']").count()==0
                assert editor.locator(".mx-fxp-manage").is_disabled()
                assert last_action(page,"nativeEqSetBand")["requestId"]==(
                    new_commands[-1]["requestId"])
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
