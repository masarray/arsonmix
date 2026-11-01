"""V6 native-authority console: Playwright source/FX/send/ACK/geometry gates.

The native host is deliberately stubbed. This verifies UI-only semantics;
it does NOT certify PCM, ASIO/WASAPI hardware, VST loading or sound.
"""
import asyncio
import json
import hashlib
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]
OPS = [
    "SET_SOURCE", "SET_PROCESSING_FORMAT", "SET_PRIMARY_OUT",
    "PICK_INSERT_FILE", "SET_INSERT_BYPASS", "OPEN_INSERT_EDITOR", "MOVE_INSERT",
    "ADD_SEND", "SET_SEND_TAP", "SET_SEND_LEVEL", "SET_FADER",
    "SET_PAN", "SET_MUTE", "SET_SOLO", "CREATE_CHANNEL",
]


def channel(identity, name, role="INPUT", source=None):
    return {"id":identity,"role":role,"name":name,
      "processingFormat":"STEREO" if role=="MAIN" else "MONO",
      "source":{"portId":source,"physicalPins":[int(source.split(":")[1])]} if source else None,
      "primaryOutId":"1000" if role!="MAIN" else None,
      "inserts":[],"sends":[],
      "controls":{"faderDb":-3,"pan":0,"mute":False,"solo":False}}


def snapshot(revision=3, epoch=7, new_channel=False):
    channels=[channel(str(i), f"CH {i:02d}") for i in range(1,11)]
    channels[1]["source"]={"portId":"in:1","physicalPins":[1]}
    channels[6]["inserts"]=[{"id":"fx-07","name":"Vendor VST3","bypassed":False}]
    channels[6]["sends"]=[{"id":"send-07","name":"Reverb","targetId":"1001","tap":"POST","levelDb":-6}]
    if new_channel: channels.append(channel("18446744073709551610","CH 11"))
    channels.extend([channel("1001","FX Return","FX_RETURN"),
       channel("1002","Vocal Group","GROUP"),channel("1000","MAIN","MAIN")])
    return {"schemaVersion":6,"productionCallbackLive":True,
      "device":{"epoch":str(epoch),"backend":"ASIO VERIFIED","captureDevice":"TEST IN",
        "playbackDevice":"TEST OUT","sampleRate":48000,"bufferFrames":128,"outputArmed":True,
        "enabledInputs":[0,1],"enabledOutputs":[0,1],
        "inputEndpoints":[{"id":"in:0","port":0,"callback":0,"label":"IN1","active":True},
          {"id":"in:1","port":1,"callback":1,"label":"IN2","active":True}],
        "outputEndpoints":[{"id":"out:0","port":0,"callback":0,"label":"OUT1","active":True},
          {"id":"out:1","port":1,"callback":1,"label":"OUT2","active":True}]},
      "graph":{"revision":str(revision),"rosterRevision":str(revision),
        "channels":channels,"destinations":[{"id":"1000","name":"MAIN"},
          {"id":"1001","name":"FX Return","acceptsSend":True},
          {"id":"1002","name":"Group"}],
        "capabilities":{"callbackOwnsChannelIds":True,"fullVendorFxMigration":True,
          "showAndOutputParity":True,"physicalCaptureMapped":True,"dynamicChannels":True,
          "groups":True,"fxReturns":True,"monitor":True,"sendsOnFaders":True,
          "supportedOps":OPS}},
      "telemetry":{"rawCapturePresent":False,"rawCapturePeak":0,
        "mainPcmPresent":False,"monitorPcmPresent":False,"channelMeterRmsPeak":{}}}


async def main():
    async with async_playwright() as play:
        browser = await play.chromium.launch(headless=True)
        page = await browser.new_page(viewport={"width": 1440, "height": 768})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        await page.add_init_script("""(() => {
          const listeners = {};
          const sent = [];
          window.__v6Test = {
            sent,
            publish: (event, payload) => (listeners[event] || []).forEach(cb => cb(payload)),
          };
          window.__JUCE__ = { backend: {
            emitEvent: (event, payload) => sent.push({event, payload}),
            addEventListener: (event, cb) => (listeners[event] ||= []).push(cb),
          }};
        })()""")
        await page.goto((ROOT / "web/index.html").as_uri())
        assert await page.locator("#arV6Console").is_hidden()
        assert await page.locator("#mixer > .strip").count() == 13
        assert await page.evaluate(
            "()=>window.__v6Test.sent.some(x=>x.event==='arMixConsoleSubscribe')")
        # Exact C++ ENGINE v8 fixture: S0 must remain readback-only.
        raw=(ROOT/"tests/fixtures/console-v8-native-unready.json").read_bytes()
        assert hashlib.sha256(raw).hexdigest()=="3eadfec4ee331f908ef2d98a2930273646e9893dfa5c00755f620d6faa917142"
        native=json.loads(raw)
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",native["snapshot"])
        assert await page.locator("#arV6Console").is_hidden()
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().visibleChannelIds")==[]
        bad=json.loads(raw)["snapshot"];del bad["device"]["enabledInputs"]
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",bad)
        assert await page.locator("#arV6Console").is_hidden()
        # Integration's implemented flat graph readback is NOT a live JUCE
        # callback. Accept identity but never replace the legacy console.
        flat = {
            "schemaVersion": 6, "deviceEpoch": "6", "graphRevision": "2",
            "rosterRevision": "2", "productionCallbackLive": False,
            "capabilities": {
                "callbackOwnsChannelIds": False, "addChannel": False,
                "group": False, "fxReturn": False, "monitor": False},
            "channels": [{
                "id": "7", "role": "input", "processingFormat": "mono",
                "primaryOutId": "13", "sourcePinVerified": False,
                "actualPcmPresent": False, "sends": []}]
        }
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",flat)
        assert await page.locator("#arV6Console").is_hidden()
        assert await page.locator("#mixer > .strip").count() == 13
        first = snapshot()
        # A native publisher must use exact decimal-string ChannelIds,
        # including values beyond JavaScript's 53-bit integer precision.
        assert all(c["id"].isdigit() for c in first["graph"]["channels"])
        malformed = snapshot(revision=99)
        malformed["graph"]["channels"][0]["id"] = "01"
        # A leading-zero alias must not be accepted even from the JUCE bridge.
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", malformed)
        assert await page.locator("#arV6Console").is_hidden()
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", first)
        await page.locator("#arV6Console").wait_for(state="visible")
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 12
        assert await page.locator("#arV6Main .arV6Master").count() == 1
        assert "ARMED" in await page.locator(".arV6State").inner_text()
        assert "SILENT / NO SOURCE" in await page.locator(
            '[data-status-id="7"]').inner_text()
        assert await page.locator('#arV6Console [data-op="CREATE_CHANNEL"]').is_enabled()

        # A source selection is one typed intent, not a JS route update.
        await page.locator('[data-op="SET_SOURCE"][data-id="7"]').select_option("in:0")
        sent = await page.evaluate("""() => window.__v6Test.sent.filter(
            x => x.event === 'arMixConsoleIntent').at(-1).payload""")
        assert sent["operation"] == "SET_SOURCE" and sent["targetChannelId"] == "7"
        assert sent["baseGraphRevision"] == "3" and sent["deviceEpoch"] == "7"
        assert sent["baseRosterRevision"] == "3" and sent["requestId"].startswith("gui-") and sent["requestId"].endswith(":1")
        assert sent["payload"] == {"portId":"in:0","physicalPins":[0]}
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').input_value() == ""
        assert "PENDING" in await page.locator(".arV6Message").inner_text()

        # Failed intent leaves accepted route untouched, with no invented LIVE.
        await page.evaluate("""(requestId) => window.__v6Test.publish(
          'arMixConsoleAck',{requestId,status:'REJECTED',
          deviceEpoch:'7',graphRevision:'3',rosterRevision:'3',errorCode:'STALE_EPOCH'})""", sent["requestId"])
        assert "REJECTED" in await page.locator(".arV6Message").inner_text()
        assert await page.locator('[data-op="SET_SOURCE"][data-id="2"]').input_value() == "in:1"
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').input_value() == ""

        # A second native intent can be accepted; ACK alone cannot change route.
        await page.locator('[data-op="SET_SOURCE"][data-id="7"]').select_option("in:0")
        accepted_id = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload.requestId""")
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'ACCEPTED',deviceEpoch:'7',graphRevision:'4',rosterRevision:'4',errorCode:''})""", accepted_id)
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').input_value() == ""
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').is_disabled()
        next_state = snapshot(revision=4)
        next_state["graph"]["channels"][6]["source"] = {"portId":"in:0","physicalPins":[0]}
        next_state["graph"]["channels"][6]["inserts"].append(
            {"id": "fx-07b", "name": "Second VST3", "bypassed": False})
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", next_state)
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').input_value() == "in:0"
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').is_enabled()
        assert "NO RAW CAPTURE PCM" in await page.locator(
            '[data-status-id="7"]').inner_text()

        # Direct OUT, native FX open/bypass, SEND tap and Sends on Faders.
        await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="7"]').select_option("1002")
        op = await page.evaluate("""() => window.__v6Test.sent.filter(
            x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert op["operation"] == "SET_PRIMARY_OUT" and op["payload"] == {"destinationId": "1002"}
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'4',rosterRevision:'4',errorCode:'LOOP'})""", op["requestId"])
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="7"]').input_value() == "1000"
        await page.locator('[data-op="OPEN_INSERT_EDITOR"][data-id="7"]').first.click()
        editor_debug = await page.evaluate("""() => ({
          intents:window.__v6Test.sent.filter(x=>x.event==='arMixConsoleIntent').slice(-3),
          state:window.__arMixV6ReadOnly(),
          node:[...document.querySelectorAll('[data-op="OPEN_INSERT_EDITOR"][data-id="7"]')]
            .map(el=>({enabled:!el.disabled,html:el.outerHTML.slice(0,250)})),
          focus:document.activeElement?.outerHTML?.slice(0,250)
        })""")
        assert editor_debug["intents"][-1]["payload"]["operation"] == "OPEN_INSERT_EDITOR", editor_debug
        current = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload.requestId""")
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'4',rosterRevision:'4',errorCode:'EDITOR_UNAVAILABLE'})""", current)
        fx_rows = page.locator('[data-channel-id="7"] .arV6FxSlot')
        await fx_rows.first.drag_to(fx_rows.nth(1))
        move = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert move["operation"] == "MOVE_INSERT"
        assert move["payload"] == {"insertId": "fx-07", "beforeInsertId": "fx-07b"}
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'4',rosterRevision:'4',errorCode:'ORDER_UNSAFE'})""", move["requestId"])
        assert await fx_rows.first.get_attribute("data-fx-id") == "fx-07"
        await page.locator('[data-op="SET_SEND_TAP"][data-id="7"]').click()
        tap = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert tap["payload"] == {"sendId": "send-07", "tap": "PRE"}
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'4',rosterRevision:'4',errorCode:'UNSUPPORTED'})""", tap["requestId"])
        await page.locator(".arV6SendMode").select_option("1001")
        await page.locator('[data-op="SET_FADER"][data-id="7"]').evaluate(
            "(el)=>{el.value='-12';el.dispatchEvent(new Event('change',{bubbles:true}))}")
        send = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert send["operation"] == "SET_SEND_LEVEL" and send["payload"]["sendId"] == "send-07"
        await page.locator(".arV6SendMode").select_option("")
        assert await page.locator('[data-op="SET_FADER"][data-id="7"]').input_value() == "-3"
        await page.evaluate("""() => window.__v6FaderReference =
          document.querySelector('[data-op="SET_FADER"][data-id="7"]')""")
        param = snapshot(revision=4)
        param["graph"]["channels"][6]["source"] = {"portId":"in:0","physicalPins":[0]}
        param["graph"]["channels"][6]["inserts"].append(
            {"id": "fx-07b", "name": "Second VST3", "bypassed": False})
        param["graph"]["channels"][6]["controls"]["faderDb"] = -11
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", param)
        assert await page.locator('[data-op="SET_FADER"][data-id="7"]').input_value() == "-11"
        assert await page.evaluate("""() => window.__v6FaderReference ===
          document.querySelector('[data-op="SET_FADER"][data-id="7"]')""")

        # A new channel exists only after a full native roster snapshot.
        page.on("dialog", lambda d: asyncio.create_task(d.accept("MONO")))
        await page.locator('[data-op="CREATE_CHANNEL"]').click()
        create = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert create["operation"] == "CREATE_CHANNEL" and create["targetChannelId"] is None
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'ACCEPTED',deviceEpoch:'7',graphRevision:'5',rosterRevision:'5',errorCode:''})""", create["requestId"])
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 12
        updated = snapshot(revision=5, new_channel=True)
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", updated)
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 13
        assert await page.locator(
            '[data-channel-id="18446744073709551610"]').count() == 1

        # Device epoch regression is refused; no downgrade or fake readiness.
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", snapshot(revision=4))
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", snapshot(revision=99,epoch=6))
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 13

        # 4 desktop widths and 3 heights: meter/fader zones aligned, MAIN pinned.
        for width in (1024, 1366, 1440, 1680):
            for height in (600, 768, 900):
                await page.set_viewport_size({"width": width, "height": height})
                geometry = await page.evaluate("""() => {
                  const strips=[...document.querySelectorAll('#arV6Lanes .arV6Strip,#arV6Main .arV6Strip')];
                  const boxes=strips.map(s=>{
                    const a=s.querySelector('.arV6Lower').getBoundingClientRect();
                    return {top:a.top,bottom:a.bottom,height:a.height};
                  });
                  const m=document.querySelector('#arV6Main').getBoundingClientRect();
                  const v=document.querySelector('#arV6Console').getBoundingClientRect();
                  return {boxes,mainLeft:m.left,mainRight:m.right,right:v.right};
                }""")
                assert len(geometry["boxes"]) == 14
                assert max(b["top"] for b in geometry["boxes"]) - min(
                    b["top"] for b in geometry["boxes"]) < 1.5, (width,height,geometry)
                assert geometry["mainRight"] <= geometry["right"] + 2, geometry
                assert geometry["boxes"][0]["height"] > 65, (width,height,geometry)
                if (width,height) in ((1680,900),(1366,768)):
                    await page.screenshot(path=f"operator-console-v6-{width}-{height}.png")
        await page.locator('[data-op="CREATE_CHANNEL"]').click()
        # Native support removed on new epoch: dynamic channel creation locked.
        no_cap = snapshot(revision=6,epoch=8,new_channel=True)
        no_cap["graph"]["capabilities"]["dynamicChannels"] = False
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",no_cap)
        assert await page.locator('[data-op="CREATE_CHANNEL"]').is_disabled()
        assert not errors, errors
        # Every new editor session needs a fresh opaque requestId prefix.
        # A bare counter '1' may collide with a completed native ACK after a
        # WebView reconnect, even if the graph/device revision is unchanged.
        first_id = sent["requestId"]
        await page.reload()
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", snapshot())
        await page.locator('[data-op="SET_SOURCE"][data-id="7"]').select_option("in:0")
        reconnected = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload.requestId""")
        assert reconnected.startswith("gui-") and reconnected.endswith(":1")
        assert reconnected != first_id
        await browser.close()
        print("PASS V6: real ACK semantics mocked only; 12 geometries, roster, pinned MAIN")


if __name__ == "__main__":
    asyncio.run(main())
