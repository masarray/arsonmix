#!/usr/bin/env python3
"""Actual compiled native Sends-on-Faders GUI: EdgeId intents -> ACK -> readback.

Uses the SAME native-only assembled /preview/ as the Windows WebView2 host.
Playwright emulates the JUCE transport, never PCM or a second audio engine.
PCM PRE/POST, gain slew and enable gates are independently verified in CTest.
No private mixer mutators, reference reducer, or fake browser audio are used.
"""
from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import tempfile

from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS
from approved_native_wire_gain_smoke import MAIN, RECEIVER, SEND, SOURCE, ack, emit, snapshot, select_ribbon_workflow

ROOT = Path(__file__).resolve().parents[1]


def admitted_session():
    state = snapshot()
    graph = state["graph"]
    graph["capabilities"]["supportedOps"] = [
        "SET_FADER", "SET_WIRE_GAIN", "SET_SEND_ENABLED", "APPLY_WIRE_TRANSACTION"
    ]
    graph["channels"][0]["controls"]["faderDb"] = -7.5
    graph["channels"][1]["role"] = "GROUP"
    graph["channels"][1]["name"] = "STAGE BUS"
    graph["edges"][1]["sendEnabled"] = True
    graph["destinations"] = [
        {k: channel[k] for k in ("id", "role", "name")}
        for channel in graph["channels"]
    ]
    state["mixerSession"]["channels"] = copy.deepcopy(graph["channels"])
    state["productionCallbackLive"] = True
    return state


def intent_list(page, operation):
    return page.evaluate(
        "operation=>window.__nativeIntents.filter(x=>x.operation===operation)",
        operation,
    )


def wait_for_intents(page, operation, count):
    page.wait_for_function(
        """([operation,count])=>
          window.__nativeIntents.filter(x=>x.operation===operation).length>=count""",
        arg=[operation, count], timeout=6000,
    )
    values = intent_list(page, operation)
    assert len(values) == count, (operation, values)
    return values[-1]


def exercise(browser, out, width, height):
    page = browser.new_page(viewport={"width":width, "height":height})
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.add_init_script(NATIVE_EVENTS + """window.__nativeIntents=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){
          if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
        },
        ...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled", timeout=15000)
    state = admitted_session()
    emit(page, "arMixConsoleSnapshot", state)
    source = page.locator('#tracks .strip[data-id="1"]')
    source.wait_for(state="attached",timeout=15000)
    target = page.locator("#sofTarget")
    toggle = page.locator("#sofToggle")
    assert toggle.is_visible() and toggle.is_enabled(), (
        "A current native PCM graph with all three SEND capabilities must admit SOF")
    options = target.locator("option").all_text_contents()
    assert any("STAGE BUS" in label for label in options), options
    # The approved compact console reveals the target selector only
    # after SEND MIX is active. Exercise the actual visible operator path.
    toggle.click()
    target.select_option(label=next(label for label in options if "STAGE BUS" in label))
    assert page.locator("#mixContext").inner_text() == "SEND MIX → STAGE BUS"
    receiver=page.locator(f'#tracks .strip[data-id="{target.input_value()}"] .fader')
    assert receiver.is_enabled(), "Selected destination retains its own bus master fader"
    assert abs(float(receiver.input_value()))<.01
    on = source.locator('[data-action="sofOn"]')
    tap = source.locator('[data-action="sofTap"]')
    assert on.is_enabled() and tap.is_enabled()
    assert on.get_attribute("aria-pressed") == "true"
    assert "POST-FADER" in source.locator(".sof-badge").inner_text()
    assert not source.locator(".fader").is_disabled(), "Admitted send must own SOF fader"

    # SEND OFF is a scalar against the EXACT existing EdgeId, not topology.
    on.click()
    off = wait_for_intents(page, "SET_SEND_ENABLED", 1)
    assert off["targetChannelId"] == SOURCE and off["payload"] == {
        "edgeId":SEND,"enabled":False
    }, off
    assert on.get_attribute("aria-pressed") == "true", (
        "An unacknowledged intent must not visibly turn off the accepted send")
    ack(page, state, off)
    assert on.get_attribute("aria-pressed") == "true", "ACK alone is not PCM readback"
    state["graph"]["edges"][1]["sendEnabled"] = False
    emit(page, "arMixConsoleSnapshot", state)
    page.wait_for_function("""()=>document.querySelector(
      '#tracks .strip[data-id="1"] [data-action="sofOn"]')?.getAttribute('aria-pressed')==='false'""")
    assert source.locator(".sof-badge").inner_text() == "POST-FADER"
    assert "OFF" in source.locator(".sof-info small").inner_text()
    assert len(state["graph"]["edges"]) == 3
    assert state["graph"]["edges"][1]["gainLinear"] == .5

    on = source.locator('[data-action="sofOn"]')
    on.click()
    enabled = wait_for_intents(page, "SET_SEND_ENABLED", 2)
    assert enabled["payload"] == {"edgeId":SEND,"enabled":True}
    ack(page, state, enabled)
    state["graph"]["edges"][1]["sendEnabled"] = True
    emit(page, "arMixConsoleSnapshot", state)
    page.wait_for_function("""()=>document.querySelector(
      '#tracks .strip[data-id="1"] [data-action="sofOn"]')?.getAttribute('aria-pressed')==='true'""")

    # A PRE-FADER tap changes the installed graph revision; no DOM optimism.
    source.locator('[data-action="sofTap"]').click()
    structural = wait_for_intents(page, "APPLY_WIRE_TRANSACTION", 1)
    changes = structural["payload"]["changes"]
    assert len(changes)==1 and changes[0] == {
        "op":"SET_TAP","from":SOURCE,"edgeId":SEND,"tap":"PRE_FADER"
    }, structural
    assert "POST-FADER" in source.locator(".sof-badge").inner_text()
    state["graph"].update(revision="3",rosterRevision="3")
    state["mixerSession"].update(routeRevision="3",rosterRevision="3")
    state["graph"]["edges"][1]["tap"] = "PRE_FADER"
    ack(page, state, structural)
    assert "POST-FADER" in source.locator(".sof-badge").inner_text(), (
        "A control ACK alone cannot announce a newly installed PRE tap")
    emit(page, "arMixConsoleSnapshot", state)
    page.wait_for_function("""()=>document.querySelector(
      '#tracks .strip[data-id="1"] .sof-badge')?.textContent==='PRE-FADER'""")
    assert source.locator('[data-action="sofTap"]').get_attribute("data-tap-visual")=="pre"

    # The physical SOF fader controls SEND GAIN only. The accepted main fader
    # remains -7.5 dB and no SET_FADER intent may be generated by a send move.
    fader = source.locator(".fader")
    assert fader.is_enabled()
    fader.evaluate("""el=>{
      el.value='-12';
      el.dispatchEvent(new Event('input',{bubbles:true}));
      el.dispatchEvent(new Event('change',{bubbles:true}));
    }""")
    level = wait_for_intents(page,"SET_WIRE_GAIN",1)
    assert level["targetChannelId"]==SOURCE
    assert level["payload"]["edgeId"]==SEND
    assert abs(level["payload"]["gainLinear"]-10**(-12/20))<1e-8
    assert intent_list(page,"SET_FADER")==[]
    assert state["graph"]["channels"][0]["controls"]["faderDb"]==-7.5
    ack(page,state,level)
    state["graph"]["edges"][1]["gainLinear"] = level["payload"]["gainLinear"]
    emit(page,"arMixConsoleSnapshot",state)
    fader.evaluate("el=>el.blur()")
    emit(page,"arMixConsoleSnapshot",state)
    assert state["graph"]["channels"][0]["controls"]["faderDb"]==-7.5
    assert intent_list(page,"SET_FADER")==[]
    # Real held pointer: an accepted earlier value must not fight the newest drag.
    travel = source.locator(".fader-travel").bounding_box()
    x = travel["x"] + travel["width"]/2
    page.mouse.move(x, travel["y"] + travel["height"]*.65)
    page.mouse.down()
    first = wait_for_intents(page,"SET_WIRE_GAIN",2)
    page.mouse.move(x, travel["y"] + travel["height"]*.25, steps=5)
    desired = float(fader.input_value())
    assert abs(first["payload"]["gainLinear"]-10**(desired/20))>.001
    ack(page,state,first)
    state["graph"]["edges"][1]["gainLinear"] = first["payload"]["gainLinear"]
    emit(page,"arMixConsoleSnapshot",state)
    assert abs(float(fader.input_value())-desired)<.051, "SEND readback fought held drag"
    page.mouse.up()
    final = wait_for_intents(page,"SET_WIRE_GAIN",3)
    assert final["payload"]["edgeId"]==SEND
    assert abs(final["payload"]["gainLinear"]-10**(desired/20))<1e-8
    assert abs(float(fader.input_value())-desired)<.051, "SEND cap jumped after release"
    ack(page,state,final)
    state["graph"]["edges"][1]["gainLinear"] = final["payload"]["gainLinear"]
    emit(page,"arMixConsoleSnapshot",state)
    assert abs(float(fader.input_value())-desired)<.051
    assert intent_list(page,"SET_FADER")==[]
    page.screenshot(path=f"arsonmix-native-sof-{width}.png")

    # The absence of an admitted EdgeId must never trigger an automatic
    # patch, a ghost send, nor an unintended MAIN fader write.
    state["graph"].update(revision="4",rosterRevision="4")
    state["mixerSession"].update(routeRevision="4",rosterRevision="4")
    removed_send=state["graph"]["edges"].pop(1)
    emit(page,"arMixConsoleSnapshot",state)
    assert "NO SEND" in source.locator(".sof-badge").inner_text()
    assert source.get_by_role("button",name="Assign PRE send").is_enabled()
    assert source.get_by_role("button",name="Assign POST send").is_enabled()
    before=len(page.evaluate("()=>window.__nativeIntents"))
    assert source.locator(".fader").is_enabled()
    travel=source.locator(".fader-travel").bounding_box()
    page.mouse.move(travel["x"]+travel["width"]/2,travel["y"]+travel["height"]*.9)
    page.mouse.down()
    page.mouse.move(travel["x"]+travel["width"]/2,travel["y"]+travel["height"]*.45,steps=5)
    page.mouse.up()
    draft_db=float(source.locator(".fader").input_value())
    assert draft_db>-60 and "DRAFT" in source.locator(".sof-info small").inner_text()
    assert len(page.evaluate("()=>window.__nativeIntents"))==before
    source.get_by_role("button",name="Assign PRE send").click()
    assignment=wait_for_intents(page,"APPLY_WIRE_TRANSACTION",2)
    assert assignment["targetChannelId"]==SOURCE
    change=assignment["payload"]["changes"][0]
    assert change["op"]=="CONNECT" and change["from"]==SOURCE and change["to"]==RECEIVER and change["tap"]=="PRE_FADER",change
    assert abs(change["gainLinear"]-10**(draft_db/20))<1e-8
    assert "NO SEND" in source.locator(".sof-badge").inner_text(), "No speculative graph"
    state["graph"].update(revision="5",rosterRevision="5")
    state["mixerSession"].update(routeRevision="5",rosterRevision="5")
    ack(page,state,assignment)
    assert "NO SEND" in source.locator(".sof-badge").inner_text(), "ACK alone is not routing evidence"
    state["graph"]["edges"].append({**removed_send,"edgeId":"9223372036854775899","tap":"PRE_FADER","gainLinear":change["gainLinear"]})
    emit(page,"arMixConsoleSnapshot",state)
    assert source.locator(".fader").is_enabled()
    assert abs(float(source.locator(".fader").input_value())-draft_db)<.051
    assert intent_list(page,"SET_FADER")==[]

    toggle.click()
    assert page.locator("#mixContext").inner_text()=="MAIN MIX"
    assert abs(float(source.locator(".fader").input_value())-(-7.5))<.01
    assert not errors, errors
    page.close()


def exercise_dmx(browser, out, width, height):
    """Actual existing console, one FX return, MAIN fader vs MONITOR PRE."""
    page = browser.new_page(viewport={"width":width, "height":height})
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.add_init_script(NATIVE_EVENTS + """window.__nativeIntents=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){
          if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
        },
        ...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled", timeout=15000)
    state = admitted_session()
    graph = state["graph"]
    graph["channels"][1].update(role="FX_RETURN",name="DELAY RETURN")
    graph["channels"][1]["controls"]["faderDb"]=-6
    monitor="9007199254740997"
    mon_edge="9223372036854775814"
    mon=copy.deepcopy(graph["channels"][1])
    mon.update(id=monitor,name="SINGER MONITOR",role="MONITOR")
    mon.pop("primaryOutId",None)
    mon["controls"]["faderDb"]=-3
    graph["channels"].append(mon)
    graph["capabilities"]["monitor"]=True
    graph["edges"].append(dict(graph["edges"][1],edgeId=mon_edge,
        **{"from":RECEIVER,"to":monitor,"gainLinear":.125,
           "tap":"PRE_FADER","primaryOutput":False,"sendEnabled":True}))
    graph["destinations"]=[{k:c[k] for k in ("id","role","name")}
                          for c in graph["channels"]]
    state["mixerSession"]["channels"]=copy.deepcopy(graph["channels"])
    emit(page,"arMixConsoleSnapshot",state)
    wet=page.locator('#tracks .strip[data-native-role="FX_RETURN"]')
    cue=page.locator('#tracks .strip[data-native-role="MONITOR"]')
    wet.wait_for(state="attached",timeout=15000)
    cue.wait_for(state="attached",timeout=15000)
    assert abs(float(wet.locator(".fader").input_value())+6)<.01
    target=page.locator("#sofTarget")
    toggle=page.locator("#sofToggle")
    toggle.click()
    target.select_option(label="SINGER MONITOR")
    assert page.locator("#mixContext").inner_text()=="MONITOR MIX"
    assert wet.locator(".sof-badge").inner_text()=="PRE-FADER"
    assert wet.locator(".fader").is_enabled()
    assert cue.locator(".sof-badge").count()==0, "terminal monitor must not flip into send"
    assert abs(float(cue.locator(".fader").input_value())+3)<.01
    assert abs(float(wet.locator(".fader").input_value())-20*__import__("math").log10(.125))<.06
    source=page.locator('#tracks .strip[data-native-role="INPUT"]')
    assert source.locator(".fader").is_enabled(), "missing monitor send can be staged without auto-patching"
    fader=wet.locator(".fader")
    fader.evaluate("""el=>{
      el.value='-12';
      el.dispatchEvent(new Event('input',{bubbles:true}));
      el.dispatchEvent(new Event('change',{bubbles:true}));
    }""")
    level=wait_for_intents(page,"SET_WIRE_GAIN",1)
    assert level["targetChannelId"]==RECEIVER
    assert level["payload"]["edgeId"]==mon_edge
    assert abs(level["payload"]["gainLinear"]-10**(-12/20))<1e-8
    assert intent_list(page,"SET_FADER")==[]
    ack(page,state,level)
    graph["edges"][-1]["gainLinear"]=level["payload"]["gainLinear"]
    fader.evaluate("el=>el.blur()")
    emit(page,"arMixConsoleSnapshot",state)
    page.wait_for_function("""()=>document.querySelector(
      '#tracks .strip[data-native-role="FX_RETURN"] .fader-readout')?.textContent.includes('-12.0')""")
    assert "-12.0" in wet.locator(".sof-info small").inner_text(), "stale accepted send level"
    page.screenshot(path=f"arsonmix-native-dmx-monitor-{width}.png")
    toggle.click()
    assert page.locator("#mixContext").inner_text()=="MAIN MIX"
    assert abs(float(wet.locator(".fader").input_value())+6)<.01
    assert abs(float(cue.locator(".fader").input_value())+3)<.01
    assert intent_list(page,"SET_FADER")==[]
    page.screenshot(path=f"arsonmix-native-dmx-main-{width}.png")
    assert not errors,errors
    page.close()


def exercise_dmx_conversion(browser, out):
    page=browser.new_page(viewport={"width":1366,"height":900})
    errors=[];page.on("pageerror",lambda error:errors.append(str(error)))
    page.add_init_script(NATIVE_EVENTS+"""window.__nativeIntents=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){
          if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
        },...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled",timeout=15000)
    state=admitted_session()
    graph=state["graph"]
    graph["capabilities"]["supportedOps"].append("CONVERT_REVERB_RETURN")
    graph["channels"][0]["inserts"]=[{"id":"g5c-96021","catalogId":"armix.builtin.vocal-reverb.v1",
                                      "name":"Reverb-Sis","bypassed":False}]
    state["mixerSession"]["channels"]=copy.deepcopy(graph["channels"])
    emit(page,"arMixConsoleSnapshot",state)
    source=page.locator('#tracks .strip[data-native-role="INPUT"]')
    row=source.locator('.fxslot-row[data-slot="0"]')
    row.wait_for(state="visible",timeout=15000)
    row.click(button="right")
    menu=page.get_by_role("menuitem",name="Move to FX Return…")
    menu.click()
    request=wait_for_intents(page,"CONVERT_REVERB_RETURN",1)
    assert request["targetChannelId"]==SOURCE
    assert request["payload"]=={"slot":0,"instanceId":"g5c-96021"}
    # Actual native transport is emulated; native CTest independently proves
    # the candidate. A positive ACK without installed readback must not move FX.
    graph.update(revision="3",rosterRevision="4")
    state["mixerSession"].update(routeRevision="3",rosterRevision="4")
    ack(page,state,request)
    assert "Reverb-Sis" in row.inner_text()
    wet=copy.deepcopy(graph["channels"][1])
    wet.update(id="9007199254740999",role="FX_RETURN",name="Reverb-Sis Return")
    wet["inserts"]=[{"id":"g5c-96022","catalogId":"armix.builtin.vocal-reverb-return.v1",
                     "name":"Reverb-Sis Return","bypassed":False}]
    graph["channels"][0]["inserts"]=[]
    graph["channels"].append(wet)
    graph["edges"].extend([
       dict(graph["edges"][1],edgeId="9223372036854775990",
            **{"from":SOURCE,"to":wet["id"],"gainLinear":1,"tap":"POST_FADER","primaryOutput":False}),
       dict(graph["edges"][0],edgeId="9223372036854775991",
            **{"from":wet["id"],"to":MAIN,"gainLinear":1,"tap":"POST_FADER","primaryOutput":True})])
    graph["destinations"]=[{k:c[k] for k in ("id","role","name")} for c in graph["channels"]]
    state["mixerSession"]["channels"]=copy.deepcopy(graph["channels"])
    emit(page,"arMixConsoleSnapshot",state)
    page.locator('#tracks .strip[data-native-role="FX_RETURN"]').wait_for(state="attached",timeout=15000)
    assert "Reverb-Sis Return" in page.locator('#tracks .strip[data-native-role="FX_RETURN"]').inner_text()
    assert "Reverb-Sis" not in source.locator(".fx-section").inner_text()
    page.screenshot(path="arsonmix-native-dmx-conversion-1366.png")
    assert not errors,errors
    page.close()


def exercise_create_fx_return(browser,out):
    page=browser.new_page(viewport={"width":1366,"height":900})
    errors=[];page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script(NATIVE_EVENTS+"""window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods}};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled",timeout=15000)
    state=admitted_session()
    graph=state["graph"]
    graph["capabilities"]["supportedOps"].append("CREATE_GROUP")
    emit(page,"arMixConsoleSnapshot",state)
    # V6 moves native CREATE_GROUP controls into the Routing ribbon.
    select_ribbon_workflow(page, "routing")
    add_fx=page.locator("#addFx")
    assert add_fx.is_visible() and add_fx.is_enabled()
    add_fx.click()
    request=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeBusCreate').at(-1)")
    assert request and request["purpose"]=="FX" and request["graphRevision"]=="2"
    wet_id="9007199254740999"
    wet=copy.deepcopy(graph["channels"][1])
    wet.update(id=wet_id,name="FX Return 1",role="FX_RETURN",primaryOutId=MAIN,
        inserts=[{"id":"new-wet-lease-1","catalogId":"armix.builtin.vocal-reverb-return.v1",
                  "name":"Reverb-Sis Return","bypassed":False}])
    assert page.locator('#tracks .strip[data-native-role="FX_RETURN"]').count()==0
    emit(page,"arMixNativeBusAck",{"requestId":request["requestId"],"ok":True,
        "createdId":wet_id,"graphRevision":"3"})
    assert page.locator('#tracks .strip[data-native-role="FX_RETURN"]').count()==0
    graph.update(revision="3",rosterRevision="4")
    graph["channels"].append(wet)
    graph["edges"].append(dict(graph["edges"][0],edgeId="9223372036854775991",
        **{"from":wet_id,"to":MAIN,"primaryOutput":True,"gainLinear":1}))
    graph["destinations"]=[{k:c[k] for k in ("id","role","name")} for c in graph["channels"]]
    state["mixerSession"].update(routeRevision="3",rosterRevision="4",
        channels=copy.deepcopy(graph["channels"]))
    emit(page,"arMixConsoleSnapshot",state)
    strip=page.locator('#tracks .strip[data-native-role="FX_RETURN"]')
    strip.wait_for(state="attached",timeout=15000)
    assert "Reverb-Sis Return" in strip.inner_text()
    assert not errors,errors
    page.close()


def exercise_offline_sof(browser,out):
    from approved_native_offline_logical_smoke import snapshot as offline_snapshot
    page=browser.new_page(viewport={"width":1366,"height":900})
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.add_init_script(NATIVE_EVENTS + """window.__events=[];
      window.__JUCE__={backend:{emitEvent(name,payload){window.__events.push({name,payload});},
        ...window.__nativeListenerMethods}};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=offline_snapshot()
    graph=state["logicalMixer"]["graph"]
    assert "SET_WIRE_GAIN" in graph["capabilities"]["supportedOps"]
    send={"edgeId":"123456","from":"1","to":"11","signalKind":"AUDIO",
        "enabled":True,"primaryOutput":False,"tap":"POST_FADER","tapStage":"POST_FADER",
        "sendEnabled":True,"gainLinear":.5}
    graph["edges"].append(send)
    graph["channels"][0]["sends"].append(copy.deepcopy(send))
    emit(page,"arMixConsoleSnapshot",state)
    page.wait_for_function("()=>document.documentElement.dataset.armixNativeAuthority==='logical-editable'")
    page.locator("#sofToggle").click()
    page.locator("#sofTarget").select_option(label="AUX A")
    source=page.locator('#tracks .strip[data-id="1"]')
    fader=source.locator(".fader")
    assert fader.is_enabled()
    def release(db):
        fader.evaluate("""(el,db)=>{el.value=String(db);
          el.dispatchEvent(new Event('input',{bubbles:true}));
          el.dispatchEvent(new Event('change',{bubbles:true}));}""",db)
    def sent():
        return page.evaluate("()=>window.__events.filter(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='SET_WIRE_GAIN').map(e=>e.payload)")
    release(-12)
    page.wait_for_function("()=>window.__events.some(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='SET_WIRE_GAIN')")
    first=sent()[0]
    assert first["channelId"]=="1" and first["payload"]["edgeId"]=="123456"
    release(-3)
    assert len(sent())==1, "durable commit remains serialized, pointer does not block"
    assert abs(float(fader.input_value())+3)<.05
    def accept(request,revision):
        count=len(sent())
        emit(page,"arMixLogicalAck",{"requestId":request["requestId"],"status":"ACCEPTED",
            "errorCode":"NONE","revision":revision,"physicalOutputAdmitted":False})
        assert len(sent())==count, "ACK alone must not drain a queued write"
        graph.update(revision=revision,rosterRevision=revision)
        state["logicalMixer"]["revision"]=revision
        graph["edges"][-1]["gainLinear"]=request["payload"]["gainLinear"]
        graph["channels"][0]["sends"][-1]["gainLinear"]=request["payload"]["gainLinear"]
        emit(page,"arMixConsoleSnapshot",state)
    accept(first,"2")
    page.wait_for_function("()=>window.__events.filter(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='SET_WIRE_GAIN').length===2")
    second=sent()[-1]
    assert second["baseRevision"]=="2" and abs(second["payload"]["gainLinear"]-10**(-3/20))<1e-8
    accept(second,"3")
    assert abs(float(fader.input_value())+3)<.05
    assert page.evaluate("()=>window.__events.every(e=>e.name!=='arMixConsoleIntent')")
    assert graph["channels"][0]["controls"]["faderDb"]==0
    assert not errors,errors
    page.close()


def main():
    spec=importlib.util.spec_from_file_location(
        "native_bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="armix-native-sof-") as directory:
        out=Path(directory)
        bundle.stage(out)
        with sync_playwright() as pw:
            browser=pw.chromium.launch(headless=True)
            try:
                for width,height in ((1366,900),(1024,768)):
                    exercise(browser,out,width,height)
                    exercise_dmx(browser,out,width,height)
                exercise_dmx_conversion(browser,out)
                exercise_create_fx_return(browser,out)
                exercise_offline_sof(browser,out)
            finally:
                browser.close()
    print("PASS native SOF actual Chromium: exact EdgeId ON/OFF, PRE tap, gain, ACK/readback, no ghost patch or MAIN fader write; two viewports")


if __name__=="__main__":
    main()
