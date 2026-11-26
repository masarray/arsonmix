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
from approved_native_wire_gain_smoke import MAIN, RECEIVER, SEND, SOURCE, ack, emit, snapshot

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
    page.screenshot(path=f"arsonmix-native-sof-{width}.png")

    # The absence of an admitted EdgeId must never trigger an automatic
    # patch, a ghost send, nor an unintended MAIN fader write.
    state["graph"].update(revision="4",rosterRevision="4")
    state["mixerSession"].update(routeRevision="4",rosterRevision="4")
    state["graph"]["edges"].pop(1)
    emit(page,"arMixConsoleSnapshot",state)
    assert "NO SEND" in source.locator(".sof-badge").inner_text()
    assert source.locator('[data-action="sofOn"]').is_disabled()
    assert source.locator('[data-action="sofTap"]').is_disabled()
    before=len(page.evaluate("()=>window.__nativeIntents"))
    assert source.locator(".fader").is_disabled()
    assert len(page.evaluate("()=>window.__nativeIntents"))==before

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
    assert source.locator(".fader").is_disabled(), "no auto-patching missing monitor send"
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
    page.screenshot(path=f"arsonmix-native-dmx-monitor-{width}.png")
    toggle.click()
    assert page.locator("#mixContext").inner_text()=="MAIN MIX"
    assert abs(float(wet.locator(".fader").input_value())+6)<.01
    assert abs(float(cue.locator(".fader").input_value())+3)<.01
    assert intent_list(page,"SET_FADER")==[]
    page.screenshot(path=f"arsonmix-native-dmx-main-{width}.png")
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
            finally:
                browser.close()
    print("PASS native SOF actual Chromium: exact EdgeId ON/OFF, PRE tap, gain, ACK/readback, no ghost patch or MAIN fader write; two viewports")


if __name__=="__main__":
    main()
