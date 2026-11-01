"""V6 native-authority console: Playwright source/FX/send/ACK/geometry gates.

The native host is deliberately stubbed. This verifies UI-only semantics;
it does NOT certify PCM, ASIO/WASAPI hardware, VST loading or sound.
"""
import asyncio
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
    return {
        "id": identity, "role": role, "name": name,
        "processingFormat": "STEREO" if role == "MAIN" else "MONO",
        "source": {"portId": source} if source else None,
        "primaryOut": {"id": "MAIN"} if role == "INPUT" else None,
        "inserts": [], "sends": [],
        "controls": {"faderDb": -3, "pan": 0, "mute": False, "solo": False},
    }


def snapshot(revision=3, epoch=7, new_channel=False):
    channels = [channel(f"ch-{i:02d}", f"CH {i:02d}") for i in range(1, 11)]
    channels[1]["source"] = {"portId": "IN-2"}
    channels[6]["inserts"] = [{"id": "fx-07", "name": "Vendor VST3", "bypassed": False}]
    channels[6]["sends"] = [{"id": "send-07", "name": "Reverb", "targetId": "BUS-FX",
                            "tap": "POST", "levelDb": -6}]
    if new_channel:
        channels.append(channel("native-64bit:18446744073709551610", "CH 11"))
    channels.append(channel("main:1", "MAIN", "MAIN"))
    return {
        "device": {
            "epoch": epoch, "backend": "ASIO VERIFIED", "sampleRate": 48000,
            "bufferFrames": 128, "outputArmed": True,
            "inputEndpoints": [
                {"id": "IN-1", "label": "Interface IN1", "active": True},
                {"id": "IN-2", "label": "Interface IN2", "active": True}],
            "outputEndpoints": [],
        },
        "graph": {
            "revision": revision, "channels": channels,
            "destinations": [
                {"id": "MAIN", "name": "MAIN"},
                {"id": "BUS-FX", "name": "FX Return", "acceptsSend": True},
                {"id": "GROUP-VOX", "name": "Vocal Group"}],
            "capabilities": {
                "dynamicChannels": True, "groups": True, "fxReturns": True,
                "monitor": True, "sendsOnFaders": True, "supportedOps": OPS,
            },
        },
        "telemetry": {"rawCapturePresent": False, "channelMeterRmsPeak": {}},
    }


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
        assert await page.locator("#mixer > .strip").count() == 11
        assert await page.evaluate(
            "()=>window.__v6Test.sent.some(x=>x.event==='arMixConsoleSubscribe')")
        first = snapshot()
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", first)
        await page.locator("#arV6Console").wait_for(state="visible")
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 10
        assert await page.locator("#arV6Main .arV6Master").count() == 1
        assert "ARMED" in await page.locator(".arV6State").inner_text()
        assert "SILENT / NO SOURCE" in await page.locator(
            '[data-status-id="ch-07"]').inner_text()
        assert await page.locator('#arV6Console [data-op="CREATE_CHANNEL"]').is_enabled()

        # A source selection is one typed intent, not a JS route update.
        await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').select_option("IN-1")
        sent = await page.evaluate("""() => window.__v6Test.sent.filter(
            x => x.event === 'arMixConsoleIntent').at(-1).payload""")
        assert sent["op"] == "SET_SOURCE" and sent["targetChannelId"] == "ch-07"
        assert sent["baseRevision"] == 3 and sent["deviceEpoch"] == 7
        assert sent["payload"] == {"portId": "IN-1"}
        assert await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').input_value() == ""
        assert "PENDING" in await page.locator(".arV6Message").inner_text()

        # Failed intent leaves accepted route untouched, with no invented LIVE.
        await page.evaluate("""(requestId) => window.__v6Test.publish(
          'arMixConsoleAck',{requestId,status:'REJECTED',
          errorCode:'STALE_EPOCH'})""", sent["requestId"])
        assert "REJECTED" in await page.locator(".arV6Message").inner_text()
        assert await page.locator('[data-op="SET_SOURCE"][data-id="ch-02"]').input_value() == "IN-2"
        assert await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').input_value() == ""

        # A second native intent can be accepted; ACK alone cannot change route.
        await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').select_option("IN-1")
        accepted_id = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload.requestId""")
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'ACCEPTED',newRevision:4})""", accepted_id)
        assert await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').input_value() == ""
        assert await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').is_disabled()
        next_state = snapshot(revision=4)
        next_state["graph"]["channels"][6]["source"] = {"portId": "IN-1"}
        next_state["graph"]["channels"][6]["inserts"].append(
            {"id": "fx-07b", "name": "Second VST3", "bypassed": False})
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", next_state)
        assert await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').input_value() == "IN-1"
        assert await page.locator('[data-op="SET_SOURCE"][data-id="ch-07"]').is_enabled()
        assert "NO VERIFIED PCM" in await page.locator(
            '[data-status-id="ch-07"]').inner_text()

        # Direct OUT, native FX open/bypass, SEND tap and Sends on Faders.
        await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="ch-07"]').select_option("GROUP-VOX")
        op = await page.evaluate("""() => window.__v6Test.sent.filter(
            x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert op["op"] == "SET_PRIMARY_OUT" and op["payload"] == {"destinationId": "GROUP-VOX"}
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',errorCode:'LOOP'})""", op["requestId"])
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="ch-07"]').input_value() == "MAIN"
        await page.locator('[data-op="OPEN_INSERT_EDITOR"][data-id="ch-07"]').first.click()
        assert await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload.op""") == "OPEN_INSERT_EDITOR"
        current = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload.requestId""")
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',errorCode:'EDITOR_UNAVAILABLE'})""", current)
        fx_rows = page.locator('[data-channel-id="ch-07"] .arV6FxSlot')
        await fx_rows.first.drag_to(fx_rows.nth(1))
        move = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert move["op"] == "MOVE_INSERT"
        assert move["payload"] == {"insertId": "fx-07", "beforeInsertId": "fx-07b"}
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',errorCode:'ORDER_UNSAFE'})""", move["requestId"])
        assert await fx_rows.first.get_attribute("data-fx-id") == "fx-07"
        await page.locator('[data-op="SET_SEND_TAP"][data-id="ch-07"]').click()
        tap = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert tap["payload"] == {"sendId": "send-07", "tap": "PRE"}
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',errorCode:'UNSUPPORTED'})""", tap["requestId"])
        await page.locator(".arV6SendMode").select_option("BUS-FX")
        await page.locator('[data-op="SET_FADER"][data-id="ch-07"]').evaluate(
            "(el)=>{el.value='-12';el.dispatchEvent(new Event('change',{bubbles:true}))}")
        send = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert send["op"] == "SET_SEND_LEVEL" and send["payload"]["sendId"] == "send-07"
        await page.locator(".arV6SendMode").select_option("")
        assert await page.locator('[data-op="SET_FADER"][data-id="ch-07"]').input_value() == "-3"
        await page.evaluate("""() => window.__v6FaderReference =
          document.querySelector('[data-op="SET_FADER"][data-id="ch-07"]')""")
        param = snapshot(revision=4)
        param["graph"]["channels"][6]["source"] = {"portId": "IN-1"}
        param["graph"]["channels"][6]["inserts"].append(
            {"id": "fx-07b", "name": "Second VST3", "bypassed": False})
        param["graph"]["channels"][6]["controls"]["faderDb"] = -11
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", param)
        assert await page.locator('[data-op="SET_FADER"][data-id="ch-07"]').input_value() == "-11"
        assert await page.evaluate("""() => window.__v6FaderReference ===
          document.querySelector('[data-op="SET_FADER"][data-id="ch-07"]')""")

        # A new channel exists only after a full native roster snapshot.
        page.on("dialog", lambda d: asyncio.create_task(d.accept("MONO")))
        await page.locator('[data-op="CREATE_CHANNEL"]').click()
        create = await page.evaluate("""() => window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert create["op"] == "CREATE_CHANNEL" and create["targetChannelId"] is None
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'ACCEPTED',newRevision:5})""", create["requestId"])
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 10
        updated = snapshot(revision=5, new_channel=True)
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", updated)
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 11
        assert await page.locator(
            '[data-channel-id="native-64bit:18446744073709551610"]').count() == 1

        # Device epoch regression is refused; no downgrade or fake readiness.
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", snapshot(revision=4))
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", snapshot(revision=99,epoch=6))
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 11

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
                assert len(geometry["boxes"]) == 12
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
        no_cap["graph"]["capabilities"]["groups"] = False
        no_cap["graph"]["capabilities"]["fxReturns"] = False
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",no_cap)
        assert await page.locator('[data-op="CREATE_CHANNEL"]').is_disabled()
        assert not errors, errors
        await browser.close()
        print("PASS V6: real ACK semantics mocked only; 12 geometries, roster, pinned MAIN")


if __name__ == "__main__":
    asyncio.run(main())
