"""Integration-only browser proof of native M4 PCM control socket (stub host).
This does NOT certify physical sound/VST3 or real ASIO/WASAPI devices.
"""
import asyncio
import copy
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
def accepted():
    rows=[{"mode":"MONO" if i==0 else "NONE","left":0 if i==0 else -1,
           "right":-1,"main":True} for i in range(10)]
    controls=[{"db":-3.0,"linear":.7079,"pan":0.0,
               "mute":False,"solo":False} for _ in range(10)]
    return {
      "schemaVersion":6,"productionCallbackLive":False,
      "device":{"epoch":"7","backend":"ASIO VERIFIED","captureDevice":"TEST CAPTURE",
        "playbackDevice":"TEST RENDER","sampleRate":48000,"bufferFrames":128,
        "outputArmed":True,"enabledInputs":[0,1],"enabledOutputs":[0,1],
        "inputEndpoints":[{"id":"in:0","port":0,"callback":0,"label":"IN 1","active":True},
          {"id":"in:1","port":1,"callback":1,"label":"IN 2","active":True}],
        "outputEndpoints":[{"id":"out:0","port":0,"callback":0,"label":"OUT 1","active":True},
          {"id":"out:1","port":1,"callback":1,"label":"OUT 2","active":True}]},
      "graph":{"revision":"0","rosterRevision":"0","channels":[],"destinations":[],
        "capabilities":{"callbackOwnsChannelIds":False,
          "fullVendorFxMigration":False,"showAndOutputParity":False,
          "physicalCaptureMapped":False,"dynamicChannels":False,
          "groups":False,"fxReturns":False,"monitor":False,
          "sendsOnFaders":False,"supportedOps":[]}},
      "telemetry":{"rawCapturePresent":False,"rawCapturePeak":0.0,
        "mainPcmPresent":False,"monitorPcmPresent":False,"channelMeterRmsPeak":[]},
      "legacyM4":{"active":True,"routeAccepted":True,"routingRevision":"14",
        "controlPlane":"LEGACY_M4_ORDINAL","acceptedChannels":rows,
        "channelControls":controls,"channelPeaksL":[.1]*10,"channelPeaksR":[.05]*10,
        "fxInstances":[[] for _ in range(14)],
        "supportedOps":["SET_FADER","SET_PAN","SET_MUTE","SET_SOLO","SET_SOURCE"]}
    }
async def main():
  async with async_playwright() as playwright:
    browser=await playwright.chromium.launch(headless=True)
    page=await browser.new_page(viewport={"width":1366,"height":768})
    errors=[]
    page.on("pageerror",lambda e: errors.append(str(e)))
    await page.add_init_script("""(() => {
      const listeners = {}, sent=[];
      window.__m4Test={sent,publish:(name,payload)=>
        (listeners[name]||[]).forEach(cb=>cb(payload))};
      window.__JUCE__={backend:{
        emitEvent:(name,payload)=>sent.push({name,payload}),
        addEventListener:(name,cb)=>(listeners[name]||=[]).push(cb)
      }};
    })()""")
    await page.goto((ROOT/"web/index.html").as_uri())
    root=page.locator("#arM4NativeConsole")
    assert await root.count()==1
    assert await root.is_hidden()
    s=accepted()
    # Original unready native v8 graph must remain hidden.
    no=copy.deepcopy(s)
    no["legacyM4"]["controlPlane"]=""
    await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",no)
    assert await root.is_hidden()
    # Only an admitted, healthy fixed-M4 physical graph may open this view.
    await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",s)
    await root.wait_for(state="visible")
    assert await root.locator("[data-m4-lane]").count()==10
    assert await page.evaluate("()=>window.__arMixV6ReadOnly().visibleChannelIds")==[]
    assert await page.evaluate("()=>window.__arMixM4NativeReadOnly().controlPlane")=="LEGACY_M4_ORDINAL"
    # Real GUI gesture uses actual native socket + independent M4 epoch/revision.
    fader=root.locator('[data-m4-lane="1"] [data-m4-op="SET_FADER"]')
    await fader.evaluate("""n=>{n.value='-6';n.dispatchEvent(new Event('change',{bubbles:true}))}""")
    command=await page.evaluate("""()=>window.__m4Test.sent
      .filter(x=>x.name==='arMixConsoleIntent').at(-1).payload""")
    assert command["operation"]=="SET_FADER" and command["targetChannelId"]=="1"
    assert command["legacyRoutingRevision"]=="14"
    assert command["baseGraphRevision"]=="0" and command["baseRosterRevision"]=="0"
    assert command["deviceEpoch"]=="7" and command["payload"]["valueDb"]==-6
    assert await fader.is_disabled()
    # ACK alone cannot make a new DSP state appear on GUI.
    ack={"requestId":command["requestId"],"status":"ACCEPTED",
         "deviceEpoch":"7","graphRevision":"0","rosterRevision":"0","errorCode":""}
    await page.evaluate("(a)=>window.__m4Test.publish('arMixConsoleAck',a)",ack)
    assert await fader.is_disabled()
    assert await page.evaluate("()=>window.__arMixM4NativeReadOnly().pendingCount")==1
    newer=copy.deepcopy(s)
    newer["legacyM4"]["channelControls"][0]["db"]=-6.0
    await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",newer)
    assert await page.evaluate("()=>window.__arMixM4NativeReadOnly().pendingCount")==0
    assert await fader.is_enabled()
    assert await fader.input_value()=="-6"
    # No optimistic mutation on a rejected control.
    mute=root.locator('[data-m4-lane="1"] [data-m4-op="SET_MUTE"]')
    await mute.click()
    m=await page.evaluate("""()=>window.__m4Test.sent
      .filter(x=>x.name==='arMixConsoleIntent').at(-1).payload""")
    assert m["operation"]=="SET_MUTE" and m["payload"]["value"] is True
    await page.evaluate("(a)=>window.__m4Test.publish('arMixConsoleAck',a)",{
      "requestId":m["requestId"],"status":"REJECTED",
      "deviceEpoch":"7","graphRevision":"0","rosterRevision":"0",
      "errorCode":"NATIVE_CONTROL_REJECTED"})
    assert await page.evaluate("()=>window.__arMixM4NativeReadOnly().pendingCount")==0
    assert await mute.inner_text()=="M"
    # Real physical identity changes invalidate all live controls.
    bad=copy.deepcopy(newer)
    bad["legacyM4"]["controlPlane"]=""
    await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",bad)
    assert await root.is_hidden()
    assert await page.evaluate("()=>window.__arMixM4NativeReadOnly().live") is False
    assert not errors, errors
    await browser.close()
    print("PASS: M4 real native ACK/readback, GUI control lock, reject & fail-closed topology")
if __name__=="__main__":
  asyncio.run(main())
