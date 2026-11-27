"""Actual compiled native DOM/keyboard -> intent -> ACK -> accepted readback.

The WebView backend is emulated for UI isolation. Native PCM/sample/ramp
evidence comes from P0ApprovedDecoderChannelToChannelAndSourceOnlyPCM CTest.
"""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS

ROOT = Path(__file__).resolve().parents[1]
SOURCE, RECEIVER, MAIN = "9007199254740993", "9007199254740995", "18446744073709551615"
SEND = "9223372036854775810"


def snapshot():
    s = json.loads((ROOT / "integration/console-v6/fixtures.json").read_text())["g0"]
    # Match the physical native snapshot envelope emitted by Main.cpp.
    # Missing global bypass is intentionally fail-closed in FX editors.
    s["globalFxBypassed"] = False
    g = s["graph"]
    g.update(revision="2", rosterRevision="2", mainChannelId=MAIN)
    g["capabilities"].update(callbackOwnsChannelIds=True,
        supportedOps=["SET_WIRE_GAIN", "APPLY_WIRE_TRANSACTION"])
    g["channels"] = [
        {"id": ident, "name": name, "role": role, "processingFormat": "STEREO",
         "controls": {"faderDb": 0, "wireSumGainDb": 0, "pan": 0, "mute": False, "solo": False},
         "inserts": [], "sends": []}
        for ident, name, role in [(SOURCE, "CH03", "INPUT"),
                                   (RECEIVER, "CH04", "INPUT"), (MAIN, "MAIN", "MAIN")]]
    for ch in g["channels"][:2]:
        ch["primaryOutId"] = MAIN
    g["edges"] = [
        {"edgeId": "9223372036854775808", "from": SOURCE, "to": MAIN, "primaryOutput": True, "gainLinear": 1},
        {"edgeId": SEND, "from": SOURCE, "to": RECEIVER, "primaryOutput": False, "gainLinear": .5},
        {"edgeId": "9223372036854775812", "from": RECEIVER, "to": MAIN, "primaryOutput": True, "gainLinear": 1}]
    for edge in g["edges"]:
        edge.update(tap="POST_FADER", enabled=True, signalKind="AUDIO")
    g["destinations"] = [{k: ch[k] for k in ("id", "role", "name")} for ch in g["channels"]]
    s["mixerSession"] = {"authority": "CHANNEL_ID", "deviceEpoch": s["device"]["epoch"],
        "routeRevision": "2", "rosterRevision": "2", "channels": copy.deepcopy(g["channels"]),
        "main": {"db": 0, "mute": False}}
    return s


def emit(page, name, payload):
    page.evaluate("([name,payload])=>window.__nativeListeners.get(name)?.(payload)", [name, payload])


def ack(page, state, intent):
    emit(page, "arMixConsoleAck", {"requestId": intent["requestId"], "status": "ACCEPTED",
        "errorCode": "NONE", "deviceEpoch": state["device"]["epoch"],
        "graphRevision": state["graph"]["revision"], "rosterRevision": state["graph"]["rosterRevision"]})


def verify_socket_impact(page, out):
    page.add_init_script(NATIVE_EVENTS + """window.__nativeIntents=[];
        window.__JUCE__={backend:{
          emitEvent(name,payload){if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);},
          ...window.__nativeListenerMethods}};""")
    page.goto((out / "index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state = snapshot()
    accepted_edge = state["graph"]["edges"].pop(1)
    emit(page, "arMixConsoleSnapshot", state)
    if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
        page.locator('.section-rail [data-section="send"]').click()
    source = page.locator('#tracks .strip[data-id="1"] .wire-node-out')
    receiver = page.locator('#tracks .strip[data-id="2"] .wire-node-in')
    def center(jack):
        box = jack.bounding_box()
        assert box
        return box["x"] + box["width"]/2, box["y"] + box["height"]/2
    selector = '#wireLayer path.wire-outgoing-line[data-cable-key="1-2"]'
    page.evaluate("""selector=>{
      window.__wireImpact=[];
      new MutationObserver(records=>{
        for(const {target} of records){
          const d=target.getAttribute('d');
          if(target.matches(selector)&&d?.includes(' L ')&&window.__wireImpact.at(-1)!==d)
            window.__wireImpact.push(d);
        }
      }).observe(document.getElementById('wireLayer'),
          {attributes:true,attributeFilter:['d'],subtree:true});
    }""", selector)
    def drag():
        x0, y0 = center(source)
        x1, y1 = center(receiver)
        page.mouse.move(x0, y0)
        page.mouse.down()
        page.mouse.move(x1, y1, steps=8)
        assert "is-snap-target" in receiver.get_attribute("class")
        page.mouse.up()
        page.mouse.move(x1, y1+65)

    drag()
    page.wait_for_function("()=>window.__nativeIntents.length===1")
    intent = page.evaluate("()=>window.__nativeIntents.at(-1)")
    assert intent["operation"] == "APPLY_WIRE_TRANSACTION"
    rejected = {"requestId": intent["requestId"], "status": "REJECTED",
        "errorCode": "TEST_REJECTION", "deviceEpoch": state["device"]["epoch"],
        "graphRevision": "2", "rosterRevision": "2"}
    emit(page, "arMixConsoleAck", rejected)
    emit(page, "arMixConsoleSnapshot", state)
    assert page.locator(selector).count() == 0
    assert page.evaluate("()=>window.__wireImpact.length") == 0

    drag()
    page.wait_for_function("()=>window.__nativeIntents.length===2")
    intent = page.evaluate("()=>window.__nativeIntents.at(-1)")
    change = intent["payload"]["changes"][0]
    assert change["from"] == SOURCE and change["to"] == RECEIVER
    after = copy.deepcopy(state)
    after["graph"]["revision"] = "3"
    after["mixerSession"]["routeRevision"] = "3"
    if change["op"] == "ROUTE_ONLY":
        after["graph"]["edges"].pop(0)
        after["graph"]["channels"][0]["primaryOutId"] = RECEIVER
        accepted_edge.update(primaryOutput=True, gainLinear=1)
    else:
        assert change["op"] == "CONNECT"
        accepted_edge["gainLinear"] = change["gainLinear"]
    after["graph"]["edges"].append(accepted_edge)
    ack(page, after, intent)
    assert page.locator(selector).count() == 0, "ACK alone drew a cable"
    emit(page, "arMixConsoleSnapshot", after)
    page.wait_for_function("()=>window.__wireImpact.length>=6", timeout=3500)
    cable = page.locator(selector)
    page.wait_for_function("selector=>document.querySelector(selector)?.getAttribute('d')?.includes(' C ')",arg=selector)
    settled = cable.get_attribute("d")
    # The settled cubic hangs below the jacks. The impact moves the path,
    # while its endpoints stay identical through all sampled animation frames.
    geometry = cable.evaluate("""path=>{
      const length=path.getTotalLength();
      return [path.getPointAtLength(0).y,path.getPointAtLength(length).y,
        path.getPointAtLength(length/2).y];
    }""")
    assert geometry[2] > max(geometry[:2]) + 5, geometry
    frames = page.evaluate("()=>window.__wireImpact")
    assert all(d.split()[:3] == settled.split()[:3] and
               d.split()[-2:] == settled.split()[-2:] for d in frames)
    emit(page, "arMixConsoleSnapshot", after)
    assert cable.get_attribute("d") == settled, "telemetry replay retriggered impact"




def verify_inspector_rejected_gain(page, out):
    """Real compiled native GUI: rejected ACK with NO new snapshot must not
    strand PENDING or detach the inspector slider held in keyboard focus."""
    page.add_init_script(NATIVE_EVENTS + """window.__nativeIntents=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){
          if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
        },
        ...window.__nativeListenerMethods
      }};""")
    errors=[]
    page.on("pageerror",lambda exc:errors.append(str(exc)))
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    # Use an exactly representable accepted -6 dB gain to prove the
    # native coalescer's equality/no-op path emits no redundant intent.
    state["graph"]["edges"][1]["gainLinear"]=10**(-6/20)
    emit(page,"arMixConsoleSnapshot",state)
    # Operator toolbar deliberately hides legacy #wireMatrixToggle. The
    # production Matrix entry lives in More > WIRE TOOLS. Exercise the
    # actual visible menu, not a hidden compatibility trigger.
    more=page.locator("#moreTools")
    open_menu_category(page,"settings")
    assert more.evaluate("(el)=>el.open")
    menu_matrix=more.locator('[data-wire-open="matrix"]')
    assert menu_matrix.is_visible(), "More must retain accessible Matrix"
    menu_matrix.click()
    assert not more.evaluate("(el)=>el.open"), "Matrix should dismiss More"
    assert page.locator("#wireWorkspace").is_visible()
    route=page.locator('[data-ws-inspect="1:2"]')
    route.wait_for(state="visible")
    route.click()
    slider=page.locator("#wireWsGain")
    slider.wait_for(state="visible")
    assert slider.is_enabled(),"Selected parallel SEND must allow native gain"
    assert "PRIMARY" not in page.locator("#wireWsGainText").inner_text()
    assert abs(float(slider.input_value())-(-6.0))<.11
    page.evaluate("()=>window.__heldInspectorGain=document.getElementById('wireWsGain')")
    slider.focus()
    slider.evaluate("""el=>{
       el.value='-12';
       el.dispatchEvent(new Event('input',{bubbles:true}));
    }""")
    assert page.locator("#wireWsGainText").inner_text()=="PENDING · -12 dB"
    page.wait_for_function(
      "()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_GAIN').length===1")
    request=page.evaluate(
      "()=>window.__nativeIntents.find(x=>x.operation==='SET_WIRE_GAIN')")
    assert request["targetChannelId"]==SOURCE,request
    assert request["payload"]["edgeId"]==SEND,request
    assert abs(request["payload"]["gainLinear"]-10**(-12/20))<1e-8,request
    emit(page,"arMixConsoleAck",{
      "requestId":request["requestId"],"status":"REJECTED",
      "errorCode":"TEST_REJECTION","deviceEpoch":state["device"]["epoch"],
      "graphRevision":state["graph"]["revision"],
      "rosterRevision":state["graph"]["rosterRevision"]})
    # NO independent later snapshot is intentionally emitted.
    # A rejected request cannot wait for one to repaint its LCD.
    assert page.locator("#wireWsGainText").inner_text()=="UNCHANGED · -6 dB"
    assert abs(float(slider.input_value())-(-6))<.11
    assert page.evaluate(
      "()=>document.getElementById('wireWsGain')===window.__heldInspectorGain")
    assert slider.evaluate("el=>document.activeElement===el"), (
      "Rejection must not drop keyboard focus on the original range input")
    assert len(page.evaluate(
      "()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_GAIN')"))==1
    assert abs(float(page.evaluate("""()=>{
      // EdgeId is allocated by the native graph projector; never assume 2.
      const id=Number(document.getElementById('wireWsGain').dataset.wsGain);
      return window.__ARMIX_NATIVE_SCOPE__.wireEdges.find(e=>e.id===id)?.levelDb;
    }"""))-(-6))<.11
    # Requesting the exact same accepted -6 dB is a no-op: native must
    # suppress redundant PCM writes AND settle the pending LCD by itself.
    # No ACK, new snapshot or hidden user recovery action is available.
    slider.evaluate("""el=>{
      el.value='-6';
      el.dispatchEvent(new Event('input',{bubbles:true}));
    }""")
    assert page.locator("#wireWsGainText").inner_text()=="PENDING · -6 dB"
    page.wait_for_function("""()=>{
      return document.getElementById('wireWsGainText')?.textContent==='-6 dB';
    }""",timeout=5000)
    assert len(page.evaluate(
      "()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_GAIN')"))==1, (
      "Exact accepted gain no-op must not resend a native PCM command")
    assert slider.evaluate("el=>document.activeElement===el")
    assert not errors,errors

def select_ribbon_workflow(page, workflow):
    """Navigate the actual visible ribbon; do not force-click hidden legacy controls."""
    tab=page.locator("#ribbon-tab-"+workflow)
    tab.wait_for(state="visible")
    if tab.get_attribute("aria-selected")!="true" or page.locator("#moreTools").get_attribute("open") is not None:
        tab.click()
    assert tab.get_attribute("aria-selected")=="true"


def open_audio_io_from_ribbon(page):
    select_ribbon_workflow(page,"io")
    action=page.locator(".command-bar [data-native-io]")
    assert action.is_visible()
    action.click()
    assert page.locator(".armix-audio-dialog").is_visible()


def open_menu_category(page, category):
    menu=page.locator("#moreTools")
    panel=menu.locator(".more-tools-panel")
    if menu.get_attribute("open") is None:
        menu.locator(":scope > summary").click()
    current=panel.get_attribute("data-armix-menu-page")
    if current==category:
        return panel
    if current!="root":
        panel.locator(".armix-menu-back").click()
    panel.locator('[data-menu-category="'+category+'"]').click()
    assert panel.get_attribute("data-armix-menu-page")==category
    return panel


def open_verified_audio_io(page, state, tab):
    """Driver inventory is independent of installed native graph readback."""
    d=state["device"]
    open_audio_io_from_ribbon(page)
    assert page.locator('button[data-audio-tab="inputs"]').is_disabled()
    assert page.locator('[data-audio-pins]').is_hidden()
    def names(side):
        ports=d[side+"Endpoints"]
        result=[""]*(max((p["port"] for p in ports),default=-1)+1)
        for p in ports: result[p["port"]]=p["label"]
        return result
    inventory={"active":{"open":True,"authorized":True,
        "backend":d["backend"],"captureDevice":d["captureDevice"],
        "playbackDevice":d["playbackDevice"],"deviceGeneration":int(d["epoch"]),
        "rate":d["sampleRate"],"buffer":d["bufferFrames"],
        "inputNames":names("input"),"outputNames":names("output"),
        "enabledInputs":d["enabledInputs"],"enabledOutputs":d["enabledOutputs"],
        "mainOutputs":state.get("physicalMainOutputs",[])},
        "types":[{"name":d["backend"],"separate":True,
            "inputs":[d["captureDevice"]],"outputs":[d["playbackDevice"]]}]}
    emit(page,"arMixAudioDeviceInventory",inventory)
    page.locator('button[data-audio-tab="'+tab+'"]').click()
    return inventory

def verify_external_output(page, out):
    """Actual /preview/ native WebView: select -> native ACK -> callback readback.

    The backend is a controlled mock. This proves the UI transaction and
    presentation, NOT actual physical sound on an eight-pin ASIO device.
    """
    page.add_init_script(NATIVE_EVENTS + """window.__outActions=[];
        window.confirm=()=>{throw Error('Stock confirmation must not be used');};
        window.__JUCE__={backend:{
          emitEvent(name,payload){if(name==='arMixAction')window.__outActions.push(payload);},
          ...window.__nativeListenerMethods}};""")
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.goto((out / "index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",outputArmed=True,
        enabledOutputs=list(range(8)),
        outputEndpoints=[{"id":"out:"+str(n),"port":n,"callback":n,
                          "label":"ASIO "+str(n+1),"active":True} for n in range(8)])
    state["physicalMainOutputs"]=[0,1]
    state["physicalMonitorOutputs"]=[2,3]
    state["externalRevision"]="2"
    state["externalOutputs"]=[]
    emit(page,"arMixConsoleSnapshot",state)
    page.wait_for_selector('#tracks .strip[data-id="2"] select[data-field="external"]',state="attached")
    inventory=open_verified_audio_io(page,state,"routing")
    inventory["namedPatches"]={"revision":1,"readOnly":False,"entries":[
        {"id":"stream-pair","name":"Streaming","side":"output","mode":"STEREO",
         "left":4,"right":5,"leftName":"ASIO 5","rightName":"ASIO 6","available":True},
        {"id":"reserved-pair","name":"Reserved","side":"output","mode":"STEREO",
         "left":0,"right":1,"leftName":"ASIO 1","rightName":"ASIO 2","available":True}]}
    emit(page,"arMixAudioDeviceInventory",inventory)
    matrix=page.locator("[data-audio-output-matrix] .armix-output-row")
    assert matrix.count()==4,[x.inner_text() for x in matrix.all()]
    assert "MAIN" in matrix.nth(0).inner_text()
    assert "MONITOR" in matrix.nth(1).inner_text()
    assert "FREE" in matrix.nth(2).inner_text()
    page.locator("[data-audio-close]").click()
    first=page.locator('#tracks .strip[data-id="1"] select[data-field="external"]')
    second=page.locator('#tracks .strip[data-id="2"] select[data-field="external"]')
    choices=first.locator("option").all_text_contents()
    choice=next(x for x in choices if x.startswith("OUT 5/6"))
    preset=next(x for x in choices if x.startswith("Preset · Streaming"))
    assert not any(x.startswith("Preset · Reserved") for x in choices)
    first.select_option(label=preset)
    dialog=page.get_by_role("dialog",name="External output")
    assert dialog.is_visible()
    assert not page.evaluate("()=>window.__outActions.some(x=>x.action==='routingExternalOutput')")
    dialog.get_by_role("button",name="Cancel",exact=True).click()
    assert first.input_value()=="None"
    assert not page.evaluate("()=>window.__outActions.some(x=>x.action==='routingExternalOutput')")
    first.select_option(label=preset)
    dialog.get_by_role("button",name="Enable output",exact=True).click()
    actions=page.evaluate("()=>window.__outActions.filter(x=>x.action==='routingExternalOutput')")
    assert len(actions)==1,actions
    requested=actions[0]
    assert (requested["channelId"]==SOURCE and requested["deviceEpoch"]==state["device"]["epoch"]
            and requested["revision"]=="2" and requested["left"]==4 and requested["right"]==5
            and requested["confirmed"] is True),requested
    assert first.get_attribute("aria-busy")=="true"
    # Failed ACK only clears the affected busy state; PCM remains unchanged.
    emit(page,"arMixExternalOutputAck",{"requestId":requested["requestId"],
        "ok":False,"error":"TEST_REJECTED"})
    assert first.get_attribute("aria-busy") is None
    assert first.input_value()=="None"
    first.select_option(label=choice)
    dialog.get_by_role("button",name="Enable output",exact=True).click()
    actions=page.evaluate("()=>window.__outActions.filter(x=>x.action==='routingExternalOutput')")
    assert len(actions)==2,actions
    accepted=actions[-1]
    emit(page,"arMixExternalOutputAck",{"requestId":accepted["requestId"],"ok":True})
    # ACK is NOT yet a callback-applied LIVE patch.
    state["externalOutputs"]=[{"channelId":SOURCE,"physicalLeft":4,"physicalRight":5,
                               "status":"PENDING"}]
    emit(page,"arMixConsoleSnapshot",state)
    assert first.get_attribute("data-status")=="PENDING"
    assert first.get_attribute("aria-busy")=="true"
    state["externalOutputs"][0]["status"]="LIVE"
    emit(page,"arMixConsoleSnapshot",state)
    assert first.get_attribute("data-status")=="LIVE"
    assert first.get_attribute("aria-busy") is None
    # A second independent ChannelId may deliberately share a physical pair.
    second.select_option(label=choice)
    assert "SHARED" in dialog.inner_text()
    dialog.get_by_role("button",name="Enable output",exact=True).click()
    actions=page.evaluate("()=>window.__outActions.filter(x=>x.action==='routingExternalOutput')")
    assert len(actions)==3,actions
    assert actions[-1]["channelId"]==RECEIVER
    emit(page,"arMixExternalOutputAck",{"requestId":actions[-1]["requestId"],"ok":True})
    state["externalOutputs"].append({"channelId":RECEIVER,"physicalLeft":4,
                                     "physicalRight":5,"status":"LIVE"})
    emit(page,"arMixConsoleSnapshot",state)
    assert second.get_attribute("data-status")=="LIVE"
    assert "SHARED 2 SOURCES" in first.input_value(),first.input_value()
    open_verified_audio_io(page,state,"routing")
    row=page.locator("[data-audio-output-matrix] .armix-output-row").filter(has_text="OUT 5/6")
    assert "SHARED 2" in row.inner_text(),row.inner_text()
    assert "CH03" in row.inner_text() and "CH04" in row.inner_text()
    state["device"]["outputArmed"]=False
    emit(page,"arMixConsoleSnapshot",state)
    assert first.get_attribute("data-status")=="MUTED"
    assert "MUTED" in row.inner_text(),row.inner_text()
    # Driver no longer attests a pin: remembered intent is REMAP, never LIVE.
    state["device"]["outputEndpoints"][5]["active"]=False
    for patch in state["externalOutputs"]:
        patch["status"]="NEEDS_REMAP"
    emit(page,"arMixConsoleSnapshot",state)
    assert first.get_attribute("data-status")=="NEEDS_REMAP"
    assert "NEEDS REMAP" in row.inner_text(),row.inner_text()
    assert not errors,errors
    page.screenshot(path="native-external-output-matrix.png")

def main():
    spec = importlib.util.spec_from_file_location("native_bundle", ROOT / "scripts/build_approved_native_gui.py")
    bundle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-native-send-") as directory:
        out = Path(directory)
        bundle.stage(out)
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            impact = browser.new_page(viewport={"width": 1366, "height": 768})
            verify_socket_impact(impact, out)
            impact.close()
            inspector_page = browser.new_page(viewport={"width":1366,"height":900})
            verify_inspector_rejected_gain(inspector_page,out)
            inspector_page.close()
            output_page = browser.new_page(viewport={"width":1366,"height":900})
            verify_external_output(output_page,out)
            output_page.close()
            for width, height in [(1366, 768), (1680, 900), (1920, 1080)]:
                page = browser.new_page(viewport={"width": width, "height": height})
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.add_init_script(NATIVE_EVENTS + """window.__nativeIntents=[];
                    window.__JUCE__={backend:{
                      emitEvent(name,payload){if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);},
                      ...window.__nativeListenerMethods}};""")
                page.goto((out / "index.html").as_uri())
                page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
                state = snapshot()
                # The receiving WIRE LVL is now the receiver's post-SUM
                # SET_WIRE_SUM_GAIN, not the selected incoming EdgeId's SEND GAIN.
                # Exact SEND gain still has separate inspector regression.
                state["graph"]["capabilities"]["supportedOps"].append("SET_WIRE_SUM_GAIN")
                emit(page, "arMixConsoleSnapshot", state)
                page.wait_for_selector('#tracks .strip[data-id="2"]')
                if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                    page.locator('.section-rail [data-section="send"]').click()
                page.locator('#tracks .strip[data-id="1"] .wire-node-out').click()
                knob = page.locator('#tracks .strip[data-id="2"] .wire-gain')
                knob.wait_for(state="visible")
                page.evaluate("""()=>{window.__heldWireInput=document.querySelector('#tracks .strip[data-id="2"] .wire-gain');
                    window.__heldStrip=document.querySelector('#tracks .strip[data-id="2"]');}""")
                # Keyboard gesture: one native ChannelId SET_WIRE_SUM_GAIN at a time.
                # When the first is in flight, the latest (Home) queues
                # behind its independent native ACK and published scalar.
                knob.focus()
                assert knob.get_attribute("data-field")=="wireChannelLevel"
                assert knob.is_enabled(), "accepted receiver SET_WIRE_SUM_GAIN unavailable"
                knob.press("End")
                page.wait_for_function(
                    "()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_SUM_GAIN').length===1")
                first = page.evaluate(
                    "()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_SUM_GAIN').at(-1)")
                assert first["targetChannelId"] == RECEIVER, first
                assert first["payload"]["valueDb"] == 6, first
                assert page.evaluate(
                    "()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_GAIN').length")==0
                knob.press("Home")
                page.wait_for_timeout(70)
                assert page.evaluate("()=>window.__nativeIntents.length") == 1, (
                    "pending receiver SUM gain should coalesce behind native ACK")
                state["graph"]["channels"][1]["controls"]["wireSumGainDb"] = first["payload"]["valueDb"]
                state["mixerSession"]["channels"][1]["controls"]["wireSumGainDb"] = first["payload"]["valueDb"]
                ack(page, state, first)
                emit(page, "arMixConsoleSnapshot", state)
                page.wait_for_function("()=>window.__nativeIntents.length===2")
                assert page.evaluate(
                    "()=>window.__heldWireInput===document.querySelector("
                    "'#tracks .strip[data-id=\\\"2\\\"] .wire-gain')"), (
                    "ACK detached held post-SUM receiver knob")
                second = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert second["operation"]=="SET_WIRE_SUM_GAIN" and second["targetChannelId"]==RECEIVER
                assert second["payload"]["valueDb"]==-60, second
                state["graph"]["channels"][1]["controls"]["wireSumGainDb"] = -60
                state["mixerSession"]["channels"][1]["controls"]["wireSumGainDb"] = -60
                ack(page, state, second)
                emit(page, "arMixConsoleSnapshot", state)
                knob.press("Tab")
                emit(page, "arMixConsoleSnapshot", state)
                assert page.evaluate(
                    "()=>window.__heldStrip===document.querySelector("
                    "'#tracks .strip[data-id=\\\"2\\\"]')"), (
                    "post-SUM scalar edit rebuilt the strip")
                assert float(knob.input_value())==-60
                assert float(page.locator('#tracks .strip[data-id="2"] .fader').input_value())==0, "SUM must not move channel fader"
                # Changing receiver volume cannot ever mutate the source
                # SEND gain, even if multiple incoming wires share this SUM.
                assert state["graph"]["edges"][1]["gainLinear"]==.5

                # Real pointer drag + SHIFT fine adjustment while ACK is
                # pending. The visual knob tracks the hand continuously;
                # the real PCM value remains on accepted producer readback.
                pot = knob.locator("..").bounding_box()
                assert pot
                x, y = pot["x"]+pot["width"]/2, pot["y"]+pot["height"]/2
                page.mouse.move(x,y)
                page.mouse.down()
                page.mouse.move(x,y-32,steps=5)
                page.wait_for_function("()=>window.__nativeIntents.length===3")
                coarse = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert coarse["operation"]=="SET_WIRE_SUM_GAIN", coarse
                assert coarse["targetChannelId"]==RECEIVER, coarse
                assert float(knob.input_value())==-56
                page.keyboard.down("Shift")
                page.mouse.move(x,y-56,steps=4)
                page.keyboard.up("Shift")
                assert float(knob.input_value())==-55.5
                state["graph"]["channels"][1]["controls"]["wireSumGainDb"] = coarse["payload"]["valueDb"]
                state["mixerSession"]["channels"][1]["controls"]["wireSumGainDb"] = coarse["payload"]["valueDb"]
                ack(page,state,coarse)
                emit(page,"arMixConsoleSnapshot",state)
                assert float(knob.input_value())==-55.5, "mid-drag ACK moved knob backwards"
                page.mouse.up()
                page.wait_for_function("()=>window.__nativeIntents.length===4")
                final = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert final["operation"]=="SET_WIRE_SUM_GAIN" and final["targetChannelId"]==RECEIVER
                assert abs(final["payload"]["valueDb"] - (-55.5))<1e-7, final
                state["graph"]["channels"][1]["controls"]["wireSumGainDb"] = final["payload"]["valueDb"]
                state["mixerSession"]["channels"][1]["controls"]["wireSumGainDb"] = final["payload"]["valueDb"]
                ack(page,state,final)
                emit(page,"arMixConsoleSnapshot",state)
                assert state["graph"]["edges"][1]["gainLinear"]==.5, (
                    "receiver LVL incorrectly changed incoming SEND gain")
                assert page.evaluate(
                    "()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_GAIN').length")==0

                # OUT to the same INPUT is the existing exclusive native transaction.
                page.locator('#tracks .strip[data-id="1"] select[data-field="output"]').select_option(label="CH04")
                route = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert route["operation"] == "APPLY_WIRE_TRANSACTION"
                assert route["payload"]["changes"][0]["to"] == RECEIVER
                assert route["payload"]["changes"][0]["op"] == "ROUTE_ONLY"
                state["graph"].update(revision="3", rosterRevision="3")
                state["graph"]["channels"][0]["primaryOutId"] = RECEIVER
                state["graph"]["edges"].pop(0)
                state["graph"]["edges"][0].update(primaryOutput=True, gainLinear=1)
                state["mixerSession"].update(routeRevision="3", rosterRevision="3",
                    channels=copy.deepcopy(state["graph"]["channels"]))
                ack(page, state, route)
                emit(page, "arMixConsoleSnapshot", state)
                receiver_pot=page.locator('#tracks .strip[data-id="2"] .wire-gain')
                assert receiver_pot.is_enabled(), "receiver post-SUM level was locked after primary route"
                assert receiver_pot.get_attribute("data-field")=="wireChannelLevel"
                assert abs(float(receiver_pot.input_value())-(-55.5))<.11
                assert state["graph"]["edges"][0]["primaryOutput"] is True
                assert state["graph"]["edges"][0]["gainLinear"]==1, (
                    "PRIMARY WIRE must remain UNITY; receiver SUM gain is separate")
                assert not errors, errors
                page.screenshot(path=f"native-wire-gain-{width}.png")
                page.close()
            browser.close()
    print("PASS native WIRE receiver post-SUM ChannelId LEVEL and separate SEND GAIN: real keyboard/drag ACK/readback, stable DOM, PRIMARY unity, INPUT OUT, 3 viewports (UI only)")


if __name__ == "__main__":
    main()
