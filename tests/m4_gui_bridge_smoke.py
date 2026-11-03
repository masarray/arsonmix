"""Integration-only: real M4 native readback -> graphical gesture -> fenced S1
intent -> ACK -> native readback. Browser is stubbed; physical sound NOT tested.
"""
import asyncio
import copy
import json
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]

def native():
    v=json.loads((ROOT/"tests/fixtures/console-v8-native-unready.json").read_text())["events"][0]["payload"]
    v=copy.deepcopy(v)
    v["device"].update({"epoch":"47","backend":"ASIO","captureDevice":"MIC",
        "playbackDevice":"LINE","sampleRate":48000,"bufferFrames":128,"outputArmed":True,
        "enabledInputs":[0,1],"enabledOutputs":[0,1],
        "inputEndpoints":[{"id":"in:0","port":0,"callback":0,"label":"MIC 1","active":True},
                          {"id":"in:1","port":1,"callback":1,"label":"MIC 2","active":True}],
        "outputEndpoints":[{"id":"out:0","port":0,"callback":0,"label":"OUT 1","active":True},
                           {"id":"out:1","port":1,"callback":1,"label":"OUT 2","active":True}]})
    rows=[{"mode":"MONO" if i==0 else "NONE","left":0 if i==0 else -1,
           "right":-1,"main":True,"channelFormat":"STEREO"} for i in range(10)]
    controls=[{"db":-6,"linear":.5,"pan":0,"mute":False,"solo":False} for _ in range(10)]
    racks=[[] for _ in range(14)]
    racks[0]=[{"instanceId":"vst:1","catalogId":"Actual Vendor Effect","bypassed":False}]
    v["legacyM4"].update({"active":True,"routeAccepted":True,"routingRevision":"9",
        "controlPlane":"LEGACY_M4_ORDINAL","acceptedChannels":rows,
        "channelControls":controls,"channelPeaksL":[0.125]*10,
        "channelPeaksR":[0.1]*10,"fxInstances":racks,
        "auxReturns":[{"enabled":True,"main":True,"mute":False,"gain":1.0}] +
                     [{"enabled":False,"main":True,"mute":False,"gain":1.0} for _ in range(3)],
        "acceptedSends":[{"from":0,"to":10,"tap":"POST","gain":.5,"pan":0}],
        "supportedOps":["SET_SOURCE","SET_PROCESSING_FORMAT","SET_PRIMARY_OUT",
            "SET_FADER","SET_PAN","SET_MUTE","SET_SOLO","SET_SEND_LEVEL",
            "SET_INSERT_BYPASS","OPEN_INSERT_EDITOR"]})
    v["telemetry"].update({"rawCapturePresent":True,"rawCapturePeak":.25,
        "mainPcmPresent":True,"channelMeterRmsPeak":[]})
    return v

async def main():
    async with async_playwright() as playwright:
        browser=await playwright.chromium.launch(headless=True)
        page=await browser.new_page(viewport={"width":1440,"height":800})
        errors=[]
        page.on("pageerror", lambda e: errors.append(str(e)))
        await page.add_init_script("""(() => {
          const listeners={};const sent=[];
          window.__m4Test={sent,publish:(name,value)=>(listeners[name]||[]).forEach(fn=>fn(value))};
          window.__JUCE__={backend:{
            emitEvent:(name,payload)=>sent.push({name,payload}),
            addEventListener:(name,fn)=>(listeners[name]||=[]).push(fn)
          }};
        })()""")
        await page.goto((ROOT/"web/index.html").as_uri())
        original=native()
        await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",original)
        assert await page.locator("#arV6Console").is_visible()
        assert await page.locator(".arV6Strip[data-channel-id^='M4:']").count()==12
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().controlPlane")=="LEGACY_M4_ORDINAL"
        assert await page.locator(".arV6Strip[data-channel-id='M4:1'] [data-op='SET_FADER']").is_enabled()
        assert await page.locator(".arV6Strip[data-channel-id='M4:MAIN'] [data-op='SET_FADER']").is_disabled()
        assert await page.locator("[data-op='CREATE_CHANNEL']").is_disabled()
        assert await page.locator(".arV6Strip[data-channel-id='M4:1'] [data-op='OPEN_INSERT_EDITOR']").is_enabled()
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().visibleChannelIds.includes('1')")==False

        mute=page.locator(".arV6Strip[data-channel-id='M4:1'] button[data-op='SET_MUTE']")
        await mute.click()
        command=await page.evaluate("""()=>window.__m4Test.sent
          .filter(x=>x.name==='arMixConsoleIntent').at(-1).payload""")
        assert command["operation"]=="SET_MUTE" and command["targetChannelId"]=="1",command
        assert command["legacyRoutingRevision"]=="9" and command["deviceEpoch"]=="47",command
        assert command["baseGraphRevision"]=="0" and command["baseRosterRevision"]=="0"
        assert command["payload"]=={"value":True}
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().pendingCount")==1
        await page.evaluate("""(id)=>window.__m4Test.publish('arMixConsoleAck',{
          requestId:id,status:'ACCEPTED',deviceEpoch:'47',
          graphRevision:'0',rosterRevision:'0',errorCode:''})""",command["requestId"])
        # A newer snapshot with the SAME epoch/revisions but unchanged MUTE
        # must NOT unlock an apparently successful request. The native value,
        # not an ACK or freshly incremented telemetry serial, owns the UI.
        await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",original)
        assert await mute.is_disabled(), "ACK + unchanged M4 control is not verified"
        assert await mute.get_attribute("aria-pressed")=="false"
        revised=copy.deepcopy(original)
        revised["legacyM4"]["channelControls"][0]["mute"]=True
        await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",revised)
        assert await mute.get_attribute("aria-pressed")=="true"
        assert await mute.is_enabled(), "Matching native readback releases the M4 control"
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().pendingCount")==0

        # PAN uses native -100..100, not the 0..1 normalized DSP coefficient.
        pan=page.locator(".arV6Strip[data-channel-id='M4:1'] input[data-op='SET_PAN']")
        await pan.evaluate("""n=>{n.value='30';n.dispatchEvent(new Event('change',{bubbles:true}))}""")
        pan_cmd=await page.evaluate("""()=>window.__m4Test.sent
          .filter(x=>x.name==='arMixConsoleIntent').at(-1).payload""")
        assert pan_cmd["operation"]=="SET_PAN" and pan_cmd["targetChannelId"]=="1"
        assert pan_cmd["payload"]["value"]==30

        # A faulted device drops the M4 authority. Never offer a phantom LIVE
        # universal graph, replay a queued PAN, or preserve enabled faders.
        poisoned=copy.deepcopy(revised)
        poisoned["legacyM4"].pop("controlPlane")
        poisoned["legacyM4"].pop("acceptedChannels")
        poisoned["legacyM4"].pop("supportedOps")
        await page.evaluate("(s)=>window.__m4Test.publish('arMixConsoleSnapshot',s)",poisoned)
        assert await page.locator("#arV6Console").is_hidden()
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().visibleChannelIds")==[]
        assert await page.evaluate("()=>window.__arMixV6ReadOnly().pendingCount")==0
        assert not errors,errors
        await browser.close()

if __name__=="__main__":
    asyncio.run(main())
