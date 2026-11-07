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

ROOT = Path(__file__).resolve().parents[1]
SOURCE, RECEIVER, MAIN = "9007199254740993", "9007199254740995", "18446744073709551615"
SEND = "9223372036854775810"


def snapshot():
    s = json.loads((ROOT / "integration/console-v6/fixtures.json").read_text())["g0"]
    g = s["graph"]
    g.update(revision="2", rosterRevision="2", mainChannelId=MAIN)
    g["capabilities"].update(callbackOwnsChannelIds=True,
        supportedOps=["SET_WIRE_GAIN", "APPLY_WIRE_TRANSACTION"])
    g["channels"] = [
        {"id": ident, "name": name, "role": role, "processingFormat": "STEREO",
         "controls": {"faderDb": 0, "pan": 0, "mute": False, "solo": False},
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


def main():
    spec = importlib.util.spec_from_file_location("native_bundle", ROOT / "scripts/build_approved_native_gui.py")
    bundle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-native-send-") as directory:
        out = Path(directory)
        bundle.stage(out)
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            for width, height in [(1366, 768), (1680, 900), (1920, 1080)]:
                page = browser.new_page(viewport={"width": width, "height": height})
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.add_init_script("""window.__nativeListeners=new Map();window.__nativeIntents=[];
                    window.__JUCE__={backend:{
                      emitEvent(name,payload){if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);},
                      addEventListener(name,callback){window.__nativeListeners.set(name,callback);}}};""")
                page.goto((out / "index.html").as_uri())
                page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
                state = snapshot()
                emit(page, "arMixConsoleSnapshot", state)
                page.wait_for_selector('#tracks .strip[data-id="2"]')
                if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                    page.locator('.section-rail [data-section="send"]').click()
                page.locator('#tracks .strip[data-id="1"] .wire-node-out').click()
                knob = page.locator('#tracks .strip[data-id="2"] .wire-gain')
                knob.wait_for(state="visible")
                page.evaluate("""()=>{window.__heldWireInput=document.querySelector('#tracks .strip[data-id="2"] .wire-gain');
                    window.__heldStrip=document.querySelector('#tracks .strip[data-id="2"]');}""")
                knob.focus()
                knob.press("End")
                page.wait_for_function("()=>window.__nativeIntents.filter(x=>x.operation==='SET_WIRE_GAIN').length===1")
                first = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert first["payload"]["edgeId"] == SEND
                knob.press("Home")
                page.wait_for_timeout(70)
                assert page.evaluate("()=>window.__nativeIntents.length") == 1, "must coalesce behind ACK"
                state["graph"]["edges"][1]["gainLinear"] = first["payload"]["gainLinear"]
                ack(page, state, first)
                emit(page, "arMixConsoleSnapshot", state)
                page.wait_for_function("()=>window.__nativeIntents.length===2")
                assert page.evaluate("()=>window.__heldWireInput===document.querySelector('#tracks .strip[data-id=\"2\"] .wire-gain')"), "ACK detached held gain control"
                latest = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert abs(latest["payload"]["gainLinear"] - .001) < 1e-8
                state["graph"]["edges"][1]["gainLinear"] = latest["payload"]["gainLinear"]
                ack(page, state, latest)
                emit(page, "arMixConsoleSnapshot", state)
                knob.press("Tab")
                emit(page, "arMixConsoleSnapshot", state)
                assert page.evaluate("()=>window.__heldStrip===document.querySelector('#tracks .strip[data-id=\"2\"]')"), "scalar edit rebuilt strip"
                assert float(page.locator('#tracks .strip[data-id="2"] .wire-gain').input_value()) == -60
                # Drag the actual receiving knob with native ACKs mid-gesture.
                pot = knob.locator("..").bounding_box()
                assert pot
                x, y = pot["x"]+pot["width"]/2, pot["y"]+pot["height"]/2
                page.mouse.move(x,y)
                page.mouse.down()
                page.mouse.move(x,y-32,steps=5)
                page.wait_for_function("()=>window.__nativeIntents.length===3")
                coarse = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert float(knob.input_value()) == -56
                page.keyboard.down("Shift")
                page.mouse.move(x,y-56,steps=4)
                page.keyboard.up("Shift")
                assert float(knob.input_value()) == -55.5
                state["graph"]["edges"][1]["gainLinear"] = coarse["payload"]["gainLinear"]
                ack(page,state,coarse)
                emit(page,"arMixConsoleSnapshot",state)
                assert float(knob.input_value()) == -55.5
                page.mouse.up()
                page.wait_for_function("()=>window.__nativeIntents.length===4")
                final = page.evaluate("()=>window.__nativeIntents.at(-1)")
                assert abs(final["payload"]["gainLinear"] - 10**(-55.5/20)) < 1e-8
                state["graph"]["edges"][1]["gainLinear"] = final["payload"]["gainLinear"]
                ack(page,state,final)
                emit(page,"arMixConsoleSnapshot",state)
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
                assert page.locator('#tracks .strip[data-id="2"] .wire-gain').is_disabled(), "primary OUT gain must stay unity"
                assert not errors, errors
                page.screenshot(path=f"native-wire-gain-{width}.png")
                page.close()
            browser.close()
    print("PASS native WIRE gain: real keyboard, coalesced ACK/readback, stable DOM, INPUT OUT, 3 viewports (UI only)")


if __name__ == "__main__":
    main()
