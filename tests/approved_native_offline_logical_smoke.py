"""Compiled native Pages GUI: no physical device, separate honest logical readback.
JS backend is emulated only; physical ASIO and real C++ save are tested separately.
"""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]


def logical_graph(revision="1", extra=False, source_only=False):
    channels=[]
    edges=[]
    order=list(range(1,15))+([16] if extra else [])+[15]
    for index in order:
        if index==15:
            role,name="MAIN","MAIN"
        elif 11<=index<=14:
            role,name="FX_RETURN",f"AUX {chr(65+index-11)}"
        elif index==16:
            role,name="INPUT","Track 16"
        else:
            role,name="INPUT",f"CH {index:02d}"
        row={"id":str(index),"name":name,"role":role,
             "processingFormat":"STEREO",
             "controls":{"faderDb":0,"pan":0,"mute":False,"solo":False},
             "inserts":[],"sends":[],"source":{"sourceFormat":"NONE"},
             "fxRackBypassed":False}
        if index!=15 and not (source_only and index==16):
            row["primaryOutId"]="15"
            edge={"edgeId":str(index),"from":str(index),"to":"15",
                  "signalKind":"AUDIO","enabled":True,"gainLinear":1,
                  "primaryOutput":True,"tap":"POST_FADER"}
            row["sends"].append(copy.deepcopy(edge))
            edges.append(edge)
        channels.append(row)
    return {
        "revision":revision,"rosterRevision":revision,
        "channels":channels,"edges":edges,
        "mainChannelId":"15","destinations":[],
        "capabilities":{"callbackOwnsChannelIds":False,
            "supportedOps":["CREATE_CHANNEL","DELETE_CHANNEL","CREATE_GROUP",
                "REMOVE_BUS","RENAME_CHANNEL","SET_FADER","SET_PAN",
                "SET_MUTE","SET_SOLO","SET_PROCESSING_FORMAT",
                "WIRE_TRANSACTION","ADD_FX_REFERENCE","REMOVE_FX_REFERENCE",
                "SET_FX_SLOT_BYPASS","SET_FX_RACK_BYPASS"],
            "dynamicChannels":True,"groups":True,
            "fxReturns":True,"monitor":False}
    }


def snapshot(revision="1",extra=False,source_only=False):
    raw=copy.deepcopy(json.loads(
        (ROOT/"integration/console-v6/fixtures.json").read_text())["g0"])
    raw["device"].update(epoch="0",backend="",captureDevice="",playbackDevice="",
        inputEndpoints=[],outputEndpoints=[],enabledInputs=[],enabledOutputs=[],
        sampleRate=0,bufferFrames=0,outputArmed=False)
    raw["graph"].update(revision="0",rosterRevision="0",channels=[],
        destinations=[],edges=[])
    raw["graph"]["capabilities"].update(callbackOwnsChannelIds=False,supportedOps=[])
    raw["productionCallbackLive"]=False
    raw["mixerSession"]={"authority":"NONE","deviceEpoch":"0"}
    graph=logical_graph(revision,extra,source_only)
    raw["logicalMixer"]={"schema":"arsonmix.native.logical-mixer.v1",
        "authority":"LOGICAL_ONLY","revision":revision,"graph":graph,
        "physicalOutputAdmitted":False,"outputArmed":False}
    return raw


def emit(page, name, data):
    page.evaluate("([n,d])=>window.__nativeListeners.get(n)?.(d)",[name,data])


def main():
    spec=importlib.util.spec_from_file_location(
        "native_bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-logical-ui-") as folder, sync_playwright() as p:
        out=Path(folder)
        bundle.stage(out)
        browser=p.chromium.launch(headless=True)
        page=browser.new_page(viewport={"width":1366,"height":768})
        errors=[]
        page.on("pageerror",lambda error:errors.append(str(error)))
        page.add_init_script("""window.__nativeListeners=new Map();window.__events=[];
          window.__JUCE__={backend:{
             emitEvent(name,payload){window.__events.push({name,payload});},
             addEventListener(name,callback){window.__nativeListeners.set(name,callback);}
          }};""")
        page.goto((out/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
        state=snapshot()
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_function("""()=>document.documentElement.dataset.armixNativeAuthority==='logical-editable'""")
        # The approved mixer virtualizes/banks strips. Count native-owned
        # model channels, not only the currently mounted DOM bank.
        assert page.evaluate("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length")==14
        # The approved Pages tool tray is a real collapsible <details>.
        # Open it as the operator would; never assert hidden children visible.
        page.locator("#moreTools summary").click()
        assert page.locator("#addTrack").is_visible()
        assert page.locator("#addTrack").is_enabled()
        assert page.locator("#addBus").is_visible()
        assert page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixConsoleIntent').length""")==0
        page.locator("#addTrack").click()
        page.wait_for_function("""()=>window.__events.some(e=>e.name==='arMixLogicalIntent')""")
        event=page.evaluate("""()=>window.__events.filter(
            e=>e.name==='arMixLogicalIntent').at(-1).payload""")
        assert event["operation"]=="CREATE_CHANNEL"
        assert event["baseRevision"]=="1"
        # The approved mixer virtualizes/banks strips. Count native-owned
        # model channels, not only the currently mounted DOM bank.
        assert page.evaluate("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length")==14
        emit(page,"arMixLogicalAck",{
            "requestId":event["requestId"],"status":"ACCEPTED",
            "errorCode":"NONE","revision":"2","createdId":"16",
            "physicalOutputAdmitted":False})
        # The approved mixer virtualizes/banks strips. Count native-owned
        # model channels, not only the currently mounted DOM bank.
        assert page.evaluate("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length")==14, "ACK alone changed UI"
        state=snapshot("2",True)
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_function("""()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length===15""")
        new_track=page.locator("#tracks .strip").filter(has_text="Track 16").first
        assert new_track.count()==1
        assert page.locator("#addTrack").is_enabled()
        assert page.evaluate("""()=>document.documentElement.dataset.armixNativeAuthority""")=="logical-editable"
        assert page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixConsoleIntent').length""")==0
        # Disconnect just the primary MAIN wire: SOURCE ONLY still keeps the strip.
        new_track.locator('select[data-field="output"]').select_option(
            label="SOURCE ONLY (No Main)")
        page.wait_for_function("""()=>window.__events.filter(e=>
            e.name==='arMixLogicalIntent').length===2""")
        wire=page.evaluate("""()=>window.__events.filter(
            e=>e.name==='arMixLogicalIntent').at(-1).payload""")
        assert wire["operation"]=="WIRE_TRANSACTION"
        assert wire["payload"]["changes"][0]["op"]=="DISCONNECT"
        assert page.evaluate("""()=>window.__ARMIX_NATIVE_SCOPE__.tracks.find(
            t=>t.name==='Track 16')?.output""")=="Master", "Pages model changed before native readback"
        emit(page,"arMixLogicalAck",{"requestId":wire["requestId"],
            "status":"ACCEPTED","errorCode":"NONE","revision":"3",
            "physicalOutputAdmitted":False})
        state=snapshot("3",True,True)
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_function("""()=>document.querySelector(
            '#tracks .strip select[data-field="output"] option')!==null""")
        assert page.evaluate("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length")==15
        assert page.locator("#addTrack").is_enabled()
        assert not errors,errors
        browser.close()
    print("PASS native compiled GUI: no hardware, track edit, ACK-only freeze, WIRE source-only readback")


if __name__=="__main__":
    main()
