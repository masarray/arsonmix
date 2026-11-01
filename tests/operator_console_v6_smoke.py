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
    "SET_PAN", "SET_MUTE", "SET_SOLO", "SET_TRIM", "SET_CUE", "CREATE_CHANNEL",
]


def channel(identity, name, role="INPUT", source=None):
    return {"id":identity,"role":role,"name":name,
      "processingFormat":"STEREO" if role=="MAIN" else "MONO",
      "source":{"portId":source,"physicalPins":[int(source.split(":")[1])]} if source else None,
      "primaryOutId":"1000" if role!="MAIN" else None,
      "inserts":[],"sends":[],
      "controls":{"faderDb":-3,"pan":0,"mute":False,"solo":False,
                   "trimDb":-2.5,"cue":False}}


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
        assert await page.locator("#mixer > .strip").count() == 11
        assert await page.evaluate(
            "()=>window.__v6Test.sent.some(x=>x.event==='arMixConsoleSubscribe')")
        # Exact C++ ENGINE v8 fixture: S0 must remain readback-only.
        raw=(ROOT/"tests/fixtures/console-v8-native-unready.json").read_bytes()
        assert hashlib.sha256(raw).hexdigest()=="4cbded2c7ada3be511c0e9a09b1949979638eb274c432f89b673a98ce55c7718"
        native=json.loads(raw)
        trace=native["events"]
        assert [x["event"] for x in trace]==[
          "arMixConsoleSnapshot","arMixConsoleIntent",
          "arMixConsoleAck","arMixConsoleSnapshot"]
        assert trace[0]["payload"]==trace[3]["payload"]
        assert trace[1]["payload"]["operation"]=="SET_SOURCE"
        assert trace[2]["payload"]["errorCode"]=="UNIVERSAL_CALLBACK_NOT_READY"
        # The native C++ producer's real four-event control-plane trace is
        # replayed unchanged. The outbound INTENT is inspected, not forged
        # as an inbound GUI event. An unrelated ACK cannot grant native LIVE.
        for event in (trace[0],trace[2],trace[3]):
            await page.evaluate("(x)=>window.__v6Test.publish(x.event,x.payload)",event)
        assert await page.locator("#arV6Console").is_hidden()
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().visibleChannelIds")==[]
        bad=json.loads(raw)["events"][0]["payload"]
        del bad["device"]["enabledInputs"]
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
        assert await page.locator("#mixer > .strip").count() == 11
        first = snapshot()
        # A native publisher must use exact decimal-string ChannelIds,
        # including values beyond JavaScript's 53-bit integer precision.
        assert all(c["id"].isdigit() for c in first["graph"]["channels"])
        malformed = snapshot(revision=99)
        malformed["graph"]["channels"][0]["id"] = "01"
        # A leading-zero alias must not be accepted even from the JUCE bridge.
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", malformed)
        assert await page.locator("#arV6Console").is_hidden()
        # A physical source must name an active endpoint that belongs to
        # the SAME verified pin set, never a different active IN label.
        wrong_pin=snapshot(revision=98)
        wrong_pin["graph"]["channels"][1]["source"]={"portId":"in:0","physicalPins":[1]}
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",wrong_pin)
        assert await page.locator("#arV6Console").is_hidden()
        inactive_pin=snapshot(revision=98)
        inactive_pin["device"]["inputEndpoints"][1]["active"]=False
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",inactive_pin)
        assert await page.locator("#arV6Console").is_hidden()
        # Native feedback-loop defects must fail closed before UI admission.
        cyclic=snapshot(revision=98)
        cyclic["graph"]["channels"][-3]["primaryOutId"]="1002"
        cyclic["graph"]["channels"][-2]["primaryOutId"]="1001"
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",cyclic)
        assert await page.locator("#arV6Console").is_hidden()
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", first)
        await page.locator("#arV6Console").wait_for(state="visible")
        assert await page.locator("#arV6Lanes .arV6Strip").count() == 12
        assert await page.locator("#arV6Main .arV6Master").count() == 1
        # The device drawer contains ONLY native-verified physical endpoints,
        # and never emits a command or invents a physical signal.
        await page.get_by_role("button", name="Verified native audio device inventory").click()
        device=page.locator("#arV6DevicePanel")
        assert await device.is_visible()
        assert "TEST IN" in await device.inner_text()
        assert "IN1" in await device.inner_text() and "IN2" in await device.inner_text()
        assert "OUT1" in await device.inner_text() and "OUT2" in await device.inner_text()
        assert "Read-only" in await device.inner_text()
        await page.get_by_role("button",name="Close native device inventory").click()
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="7"] option[value="out:0"]').count()==0
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="1000"] option[value="out:0"]').count()==1
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="1000"] option[value="out:1"]').count()==1
        # MAIN can request the verified render pin via the same native socket;
        # a browser selecting it does not arm hardware or change PCM.
        await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="1000"]').select_option("out:0")
        main_intent=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert main_intent["operation"]=="SET_PRIMARY_OUT"
        assert main_intent["targetChannelId"]=="1000"
        assert main_intent["payload"]=={"destinationId":"out:0"}
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="1000"]').input_value()==""
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'3',
          rosterRevision:'3',errorCode:'OUTPUT_NOT_ARMED'})""",main_intent["requestId"])
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="1000"]').input_value()==""
        assert await page.locator('[data-meter-id="1000"] .arV6MeterLane').count() == 2
        assert await page.locator('[data-meter-id="7"][data-format="MONO"] .arV6MeterLane').count() == 2
        # L/R ladder must never mirror a single maximum to both sides.
        stereo = snapshot()
        stereo["telemetry"]["channelMeterRmsPeak"] = {
            "1000":{"peakLeft":0.5,"peakRight":0.05}}
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",stereo)
        lit = await page.locator('[data-meter-id="1000"]').evaluate("""el=>
          [...el.querySelectorAll('.arV6MeterLane')].map(x=>Number(x.dataset.lit))""")
        assert lit[0]>lit[1]>0,lit
        # S1: distinct RAW, channel and MAIN truth. MAIN measurements do not
        # turn all unpatched/AUX/group strips into invented active PCM.
        live_meter=snapshot()
        live_meter["telemetry"].update({
          "rawCapturePresent":True,"rawCapturePeak":0.4,"mainPcmPresent":True,
          "channelMeterRmsPeak":{
            "7":{"peak":0.3},"1000":{"peakLeft":0.4,"peakRight":0.2}}})
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",live_meter)
        assert "CHANNEL PCM MEASURED" in await page.locator('[data-status-id="7"]').inner_text()
        assert "MAIN PCM VERIFIED" in await page.locator('[data-status-id="1000"]').inner_text()
        assert "CHANNEL PCM UNVERIFIED" in await page.locator('[data-status-id="1002"]').inner_text()
        assert "SILENT / NO SOURCE" in await page.locator('[data-status-id="3"]').inner_text()
        # Continuous telemetry must not mutate faders, reselect controls,
        # re-run responsive layout geometry, or remount VST3 editors.
        stats_start=await page.evaluate("()=>window.__arMixV6ReadOnly().uiStats")
        await page.evaluate("""()=>window.__meterFader=
          document.querySelector('[data-channel-id="7"] .arV6Fader')""")
        meter_burst=[]
        for n in range(80):
            s=snapshot()
            s["telemetry"]["rawCapturePresent"]=True
            s["telemetry"]["rawCapturePeak"]=0.4
            s["telemetry"]["mainPcmPresent"]=True
            s["telemetry"]["channelMeterRmsPeak"]={
              "7":{"peak":0.1+(n%10)/12},"1000":{"peakLeft":0.2,"peakRight":0.05}}
            meter_burst.append(s)
        await page.evaluate("""states=>states.forEach(s=>
          window.__v6Test.publish('arMixConsoleSnapshot',s))""",meter_burst)
        stats_end=await page.evaluate("()=>window.__arMixV6ReadOnly().uiStats")
        assert stats_end["telemetryFastFrames"]-stats_start["telemetryFastFrames"]==80
        assert stats_end["controlRefreshes"]==stats_start["controlRefreshes"]
        assert stats_end["structureRenders"]==stats_start["structureRenders"]
        assert await page.evaluate("""()=>window.__meterFader===
          document.querySelector('[data-channel-id="7"] .arV6Fader')""")
        # Repeated identical graph revision can STILL contain newly accepted
        # fader controls, so do not mistake a true parameter edit for meters.
        changed=snapshot()
        changed["graph"]["channels"][6]["controls"]["faderDb"]=-17
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",changed)
        assert await page.locator('[data-op="SET_FADER"][data-id="7"]').input_value()=="-17"
        assert (await page.evaluate("()=>window.__arMixV6ReadOnly().uiStats"))[
          "controlRefreshes"]>stats_end["controlRefreshes"]
        # Invalid PCM data never lights LEDs or replaces an accepted snapshot.
        bad_pcm=snapshot(revision=91)
        bad_pcm["telemetry"]["channelMeterRmsPeak"]={"7":{"peak":-0.4}}
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",bad_pcm)
        assert await page.locator('[data-op="SET_FADER"][data-id="7"]').input_value()=="-17"
        bogus=snapshot(revision=91)
        bogus["telemetry"]["channelMeterRmsPeak"]={"999999":{"peak":0.3}}
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",bogus)
        assert await page.locator('[data-op="SET_FADER"][data-id="7"]').input_value()=="-17"
        # Restore baseline before exercising editing transactions later.
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",snapshot())
        # Narrow bank is purely presentational; pinned MAIN and native IDs stay.
        before_width=await page.locator('[data-channel-id="7"]').evaluate(
            "(el)=>el.getBoundingClientRect().width")
        fader_node=await page.locator('[data-channel-id="7"] .arV6Fader').evaluate(
            "el=>{window.__faderBeforeNarrow=el;return true}")
        await page.get_by_role("button", name="Toggle narrow channel bank").click()
        after_width=await page.locator('[data-channel-id="7"]').evaluate(
            "(el)=>el.getBoundingClientRect().width")
        assert after_width<before_width
        assert await page.evaluate("""()=>window.__faderBeforeNarrow===
           document.querySelector('[data-channel-id="7"] .arV6Fader')""")
        assert await page.locator("#arV6Main .arV6Master").count()==1
        await page.get_by_role("button", name="Toggle narrow channel bank").click()
        assert "ARMED" in await page.locator(".arV6State").inner_text()
        assert "SILENT / NO SOURCE" in await page.locator(
            '[data-status-id="7"]').inner_text()
        assert await page.locator('#arV6Console [data-op="CREATE_CHANNEL"]').is_enabled()

        # One PR#278-style synchronized section rail controls ALL strips,
        # without remounting native fader nodes or changing accepted DSP.
        await page.evaluate("""()=>window.__v6RailFader =
          document.querySelector('[data-channel-id="7"] .arV6Fader')""")
        async def aligned_sections():
            return await page.evaluate("""() => {
              const r=sel=>document.querySelector(sel).getBoundingClientRect();
              const selectors=[['io','.arV6Route'],['fx','.arV6Fx'],['send','.arV6Sends']];
              return selectors.map(([name,section])=>{
                const rail=r('.arV6RailToggle[data-section="'+name+'"]');
                const strip=r('[data-channel-id="7"] '+section);
                const master=r('[data-channel-id="1000"] '+section);
                return {name,topDelta:Math.abs(rail.top-strip.top),
                  bottomDelta:Math.abs(rail.bottom-strip.bottom),
                  mainDelta:Math.abs(master.top-strip.top),
                  masterBottom:Math.abs(master.bottom-strip.bottom),
                  height:strip.height};
              });
            }""")
        sections=await aligned_sections()
        assert all(x["topDelta"]<2 and x["bottomDelta"]<2 and
                   x["mainDelta"]<2 and x["masterBottom"]<2
                   for x in sections),sections
        for key in ("fx","send","io"):
            before=next(x["height"] for x in sections if x["name"]==key)
            rail=page.locator(f'.arV6RailToggle[data-section="{key}"]')
            await rail.click()
            assert await rail.get_attribute("aria-expanded")=="false"
            sections=await aligned_sections()
            after=next(x["height"] for x in sections if x["name"]==key)
            assert after<before,(key,before,after)
            assert all(x["topDelta"]<2 and x["bottomDelta"]<2 and
                       x["mainDelta"]<2 and x["masterBottom"]<2
                       for x in sections),sections
            assert await page.evaluate("""()=>window.__v6RailFader===
              document.querySelector('[data-channel-id="7"] .arV6Fader')""")
        for key in ("io","send","fx"):
            await page.locator(f'.arV6RailToggle[data-section="{key}"]').click()
        assert all(x["topDelta"]<2 for x in await aligned_sections())

        # Native stable ID CTRL/SHIFT selection is UI-only; pinned MAIN excluded.
        count_start=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').length""")
        await page.locator('[data-channel-id="2"] .arV6StripHead').click()
        await page.locator('[data-channel-id="7"] .arV6StripHead').click(modifiers=["Control"])
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().selectedChannelIds")==["2","7"]
        await page.locator('[data-channel-id="9"] .arV6StripHead').click(modifiers=["Shift"])
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().selectedChannelIds")==["7","8","9"]
        assert await page.locator('#arV6Main .arV6Master').get_attribute("aria-selected")=="false"
        assert await page.locator('.arV6SelectionCount').inner_text()=="3 SEL"
        assert await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').length""")==count_start

        trim=page.locator('[data-op="SET_TRIM"][data-id="7"]')
        assert await trim.is_enabled() and await trim.input_value()=="-2.5"
        await trim.evaluate("(el)=>{el.value='5';el.dispatchEvent(new Event('change',{bubbles:true}))}")
        trim_cmd=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert trim_cmd["operation"]=="SET_TRIM" and trim_cmd["payload"]=={"valueDb":5}
        assert await trim.input_value()=="-2.5"
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'3',
           rosterRevision:'3',errorCode:'TRIM_NOT_READY'})""",trim_cmd["requestId"])
        cue=page.locator('[data-op="SET_CUE"][data-id="7"]')
        assert await cue.is_enabled()
        await cue.click()
        cue_cmd=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert cue_cmd["operation"]=="SET_CUE" and cue_cmd["payload"]=={"value":True}
        assert await cue.get_attribute("aria-pressed")=="false"
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'3',
           rosterRevision:'3',errorCode:'NO_MONITOR'})""",cue_cmd["requestId"])
        assert await cue.get_attribute("aria-pressed")=="false"

        # Role and SOLO filters are UI-only; MAIN remains pinned, original
        # numeric ChannelIds survive and no console intent is emitted.
        count_before=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').length""")
        await page.locator('.arV6Filter').select_option("GROUP")
        assert await page.locator('#arV6Lanes .arV6Strip:visible').count()==1
        assert await page.locator('#arV6Main .arV6Master:visible').count()==1
        await page.locator('.arV6Filter').select_option("SOLO")
        assert await page.locator('#arV6Lanes .arV6Strip:visible').count()==0
        assert await page.locator('#arV6Main .arV6Master:visible').count()==1
        await page.locator('.arV6Filter').select_option("ALL")
        assert await page.locator('#arV6Lanes .arV6Strip:visible').count()==12
        assert await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').length""")==count_before

        # A source selection is one typed intent, not a JS route update.
        await page.locator('[data-op="SET_SOURCE"][data-id="7"]').select_option("in:0")
        sent = await page.evaluate("""() => window.__v6Test.sent.filter(
            x => x.event === 'arMixConsoleIntent').at(-1).payload""")
        assert sent["operation"] == "SET_SOURCE" and sent["targetChannelId"] == "7"
        assert sent["baseGraphRevision"] == "3" and sent["deviceEpoch"] == "7"
        assert sent["baseRosterRevision"] == "3"
        ids=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').map(x=>x.payload.requestId)""")
        assert len(ids)==len(set(ids)) and all(
          x.startswith("gui-") and x.rsplit(":",1)[-1].isdigit() for x in ids)
        assert ids[-1]==sent["requestId"]
        assert sent["payload"] == {"portId":"in:0"}
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').input_value() == ""
        assert "PENDING" in await page.locator(".arV6Message").inner_text()

        # Incomplete native ACK cannot unlock controls or change accepted SOURCE.
        await page.evaluate("""(requestId) => window.__v6Test.publish(
          'arMixConsoleAck',{requestId,status:'ACCEPTED',newRevision:999})""",
          sent["requestId"])
        assert "INVALID NATIVE ACK" in await page.locator(".arV6Message").inner_text()
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').is_disabled()
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"]').input_value() == ""

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

        # Center PAN snap emits only one native SET_PAN; accepted PAN
        # stays unchanged until ACK + matching snapshot.
        await page.locator('[data-op="SET_PAN"][data-id="7"]').evaluate(
            "(el)=>{el.value='3';el.dispatchEvent(new Event('change',{bubbles:true}))}")
        pan_intent=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert pan_intent["operation"]=="SET_PAN" and pan_intent["payload"]=={"value":0}
        assert await page.locator('[data-op="SET_PAN"][data-id="7"]').input_value()=="0"
        assert await page.locator('[data-op="SET_PAN"][data-id="7"]').evaluate(
            "el=>el.style.getPropertyValue('--v6-pan-start')")=="50%"
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'REJECTED',deviceEpoch:'7',graphRevision:'4',
           rosterRevision:'4',errorCode:'CONTROL_BUSY'})""",pan_intent["requestId"])

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
        # Direct SEND slider uses native SET_SEND_LEVEL; no optimistic DSP.
        gain=page.locator('[data-op="SET_SEND_LEVEL"][data-id="7"][data-send="send-07"]')
        assert await gain.is_enabled()
        assert await gain.input_value()=="-6"
        await gain.evaluate("(el)=>{el.value='-12';el.dispatchEvent(new Event('change',{bubbles:true}))}")
        gain_cmd=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert gain_cmd["operation"]=="SET_SEND_LEVEL"
        assert gain_cmd["payload"]=={"sendId":"send-07","levelDb":-12}
        assert await gain.input_value()=="-6"
        await page.evaluate("""id=>window.__v6Test.publish(
          'arMixConsoleAck',{requestId:id,status:'REJECTED',deviceEpoch:'7',
          graphRevision:'4',rosterRevision:'4',errorCode:'GAIN_BUSY'})""",gain_cmd["requestId"])
        assert await gain.input_value()=="-6"
        assert await page.locator('[data-send-id="send-07"] .arV6SendDb').inner_text()=="-6.0 dB"
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

        # Same device epoch cannot silently remap active pin masks or clock.
        # A new native epoch can legitimately publish a ONE-PIN interface.
        bad_clock=snapshot(revision=6,new_channel=True)
        bad_clock["device"]["sampleRate"]=96000
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)", bad_clock)
        assert "REJECTED DEVICE READBACK" in await page.locator(".arV6Message").inner_text()
        assert "48000 Hz" in await page.locator(".arV6Device").inner_text()
        one_pin=snapshot(revision=6,epoch=8,new_channel=True)
        one_pin["device"]["enabledInputs"]=[0]
        one_pin["device"]["inputEndpoints"]=one_pin["device"]["inputEndpoints"][:1]
        one_pin["device"]["enabledOutputs"]=[0]
        one_pin["device"]["outputEndpoints"]=one_pin["device"]["outputEndpoints"][:1]
        one_pin["graph"]["channels"][1]["source"]=None
        one_pin["graph"]["channels"][-1]["primaryOutId"]="out:0"
        # The positive browser-host catalog is TEST ONLY. ENGINE must supply
        # this exact native-scanned field for any production VST3 availability.
        one_pin["graph"]["capabilities"]["supportedOps"]=OPS+[
          "ADD_INSERT","SET_LINK_GROUP","ROUTE_SELECTED_TO_GROUP","RENAME_CHANNEL"]
        one_pin["catalog"]={"plugins":[
          {"id":"native:hall-reverb","name":"Hall Reverb","vendor":"Verified Vendor",
           "category":"Reverb"},
          {"id":"native:parametric-eq","name":"Parametric EQ","vendor":"Verified Vendor",
           "category":"EQ"}]}
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",one_pin)
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"] option[value="in:1"]').count()==0
        assert await page.locator('[data-op="SET_SOURCE"][data-id="7"] option[value="in:0"]').count()==1
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="1000"] option[value="out:1"]').count()==0
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="1000"]').input_value()=="out:0"
        await page.get_by_role("button",name="Verified native audio device inventory").click()
        assert "NO VERIFIED ACTIVE PIN" not in await device.inner_text()
        assert "IN2" not in await device.inner_text()
        assert "OUT2" not in await device.inner_text()
        await page.get_by_role("button",name="Close native device inventory").click()

        # F8 searches ONLY the native catalog and adds through one typed intent.
        # An epoch change clears prior selection; one native ChannelId is required.
        assert await page.locator(".arV6SelectionCount").inner_text()=="0 SEL"
        await page.keyboard.press("F8")
        manager=page.locator("#arV6PluginPanel")
        assert await manager.is_visible()
        assert "Select exactly ONE" in await manager.inner_text()
        await page.locator("#arV6PluginPanel .arV6PluginSearch").fill("hall")
        assert await manager.locator("button[data-plugin-id]").count()==1
        assert await manager.locator("button[data-plugin-id]").first.is_disabled()
        await page.locator('[data-channel-id="7"] .arV6StripHead').click()
        assert await manager.locator("button[data-plugin-id]").first.is_enabled()
        count_fx=await page.locator('[data-channel-id="7"] .arV6FxSlot').count()
        await manager.locator('button[data-plugin-id="native:hall-reverb"]').click()
        load=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert load["operation"]=="ADD_INSERT"
        assert load["targetChannelId"]=="7" and load["deviceEpoch"]=="8"
        assert load["payload"]=={"pluginId":"native:hall-reverb"}
        assert await page.locator('[data-channel-id="7"] .arV6FxSlot').count()==count_fx
        assert await manager.is_hidden()
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'REJECTED',deviceEpoch:'8',graphRevision:'6',
          rosterRevision:'6',errorCode:'PLUGIN_LOAD_FAILED'})""",load["requestId"])
        assert await page.locator('[data-channel-id="7"] .arV6FxSlot').count()==count_fx
        assert "PLUGIN_LOAD_FAILED" in await page.locator(".arV6Message").inner_text()

        # PRD §9 native operator multiselect: LINK FADERS is a control
        # grouping transaction, NOT a serial audio GROUP BUS.
        await page.locator('[data-channel-id="2"] .arV6StripHead').click()
        await page.locator('[data-channel-id="7"] .arV6StripHead').click(modifiers=["Control"])
        await page.get_by_role("button",name="Selected channels and native group actions").click()
        operator=page.locator("#arV6SelectionPanel")
        assert await operator.is_visible()
        link=page.get_by_role("button",
          name="Link selected faders using native relative control group",exact=True)
        unlink=page.get_by_role("button",
          name="Unlink selected faders using native relative control group",exact=True)
        assert await link.is_enabled() and await unlink.is_disabled()
        await link.click()
        cmd=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert cmd["operation"]=="SET_LINK_GROUP"
        assert cmd["targetChannelId"] is None
        assert cmd["payload"]=={"channelIds":["2","7"],"linked":True}
        assert await link.is_disabled()
        assert "LINK" not in await page.locator('[data-channel-id="7"] .arV6Role').inner_text()
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'ACCEPTED',deviceEpoch:'8',
           graphRevision:'7',rosterRevision:'6',errorCode:''})""",cmd["requestId"])
        assert await link.is_disabled()
        accepted_link=json.loads(json.dumps(one_pin))
        accepted_link["graph"]["revision"]="7"
        accepted_link["graph"]["channels"][1]["linkGroupId"]="A"
        accepted_link["graph"]["channels"][6]["linkGroupId"]="A"
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",accepted_link)
        assert "LINK A" in await page.locator('[data-channel-id="7"] .arV6Role').inner_text()
        assert await unlink.is_enabled()

        # Native unlink waits for native ACK + exact snapshot, no browser
        # fader-copy simulation and no optimistic sound/control group.
        await unlink.click()
        unlink_cmd=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert unlink_cmd["payload"]=={"channelIds":["2","7"],"linked":False}
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'REJECTED',deviceEpoch:'8',
           graphRevision:'7',rosterRevision:'6',errorCode:'LINK_BUSY'})""",unlink_cmd["requestId"])
        assert "LINK A" in await page.locator('[data-channel-id="7"] .arV6Role').inner_text()
        assert await unlink.is_enabled()

        # Route selected to native serial summing bus, not relative fader
        # link. Failed request must leave both primary output paths intact.
        await page.get_by_role("combobox",name="Select actual native summing Group Bus").select_option("1002")
        route=page.get_by_role("button",name="Route selected channels to native summing Group Bus")
        assert await route.is_enabled()
        await route.click()
        group_cmd=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert group_cmd["operation"]=="ROUTE_SELECTED_TO_GROUP"
        assert group_cmd["payload"]=={"channelIds":["2","7"],"destinationId":"1002"}
        assert await route.is_disabled()
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'REJECTED',deviceEpoch:'8',
           graphRevision:'7',rosterRevision:'6',errorCode:'BUS_BUSY'})""",group_cmd["requestId"])
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="7"]').input_value()=="1000"
        assert await route.is_enabled()
        await page.get_by_role("button",name="Close selected channel actions").click()

        # Direct inline rename is a canonical RENAME_CHANNEL intent.
        # No optimistic name change; accepted snapshot updates text while
        # preserving native fader DOM identity and physical I/O.
        await page.evaluate("""()=>window.__renameFader=
          document.querySelector('[data-channel-id="7"] .arV6Fader')""")
        await page.locator('[data-channel-id="7"] .arV6Name').dblclick()
        rename=page.locator('[data-channel-id="7"] .arV6Rename')
        assert await rename.is_visible()
        await rename.fill("Lead Vox")
        await rename.press("Enter")
        rename_cmd=await page.evaluate("""()=>window.__v6Test.sent.filter(
          x=>x.event==='arMixConsoleIntent').at(-1).payload""")
        assert rename_cmd["operation"]=="RENAME_CHANNEL", {
          "last_intent":rename_cmd,
          "ui":await page.evaluate("()=>window.__arMixV6ReadOnly()"),
          "rename_visible":await page.locator('[data-channel-id="7"] .arV6Rename').count(),
          "last_five":await page.evaluate("""()=>window.__v6Test.sent.filter(
            x=>x.event==='arMixConsoleIntent').slice(-5).map(x=>x.payload.operation)""")}
        assert rename_cmd["targetChannelId"]=="7"
        assert rename_cmd["payload"]=={"name":"Lead Vox"}
        assert await page.locator('[data-channel-id="7"] .arV6Name').inner_text()=="CH 07"
        await page.evaluate("""id=>window.__v6Test.publish('arMixConsoleAck',
          {requestId:id,status:'ACCEPTED',deviceEpoch:'8',
           graphRevision:'8',rosterRevision:'6',errorCode:''})""",rename_cmd["requestId"])
        assert await page.locator('[data-channel-id="7"] .arV6Name').inner_text()=="CH 07"
        accepted_name=json.loads(json.dumps(accepted_link))
        accepted_name["graph"]["revision"]="8"
        accepted_name["graph"]["channels"][6]["name"]="Lead Vox"
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",accepted_name)
        assert await page.locator('[data-channel-id="7"] .arV6Name').inner_text()=="Lead Vox"
        assert await page.evaluate("""()=>window.__renameFader===
          document.querySelector('[data-channel-id="7"] .arV6Fader')""")
        assert await page.locator('[data-op="SET_PRIMARY_OUT"][data-id="7"]').input_value()=="1000"
        # Missing native action MUST disable grouping, while UI selection
        # remains possible; no fake native operations on old fixtures.
        no_group=json.loads(json.dumps(accepted_name))
        no_group["graph"]["revision"]="9"
        no_group["graph"]["capabilities"]["supportedOps"]=OPS
        await page.evaluate("(s)=>window.__v6Test.publish('arMixConsoleSnapshot',s)",no_group)
        await page.get_by_role("button",name="Selected channels and native group actions").click()
        assert await link.is_disabled() and await route.is_disabled()

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
