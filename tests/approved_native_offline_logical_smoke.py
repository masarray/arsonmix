"""Compiled native Pages GUI: no physical device, separate honest logical readback.
JS backend is emulated only; physical ASIO and real C++ save are tested separately.
"""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS
from operator_wire_menu import open_application_menu
from approved_native_wire_gain_smoke import select_ribbon_workflow
from approved_floating_fx_component_harness import project_approved_editor_for_component_smoke

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
                "WIRE_TRANSACTION","SET_SEND_ENABLED","SET_SEND_TAP","SET_WIRE_GAIN",
                "ADD_FX_REFERENCE","REMOVE_FX_REFERENCE",
                "SET_FX_SLOT_BYPASS","SET_FX_RACK_BYPASS"],
            "dynamicChannels":True,"groups":True,
            "fxReturns":True,"monitor":False}
    }


def snapshot(revision="1",extra=False,source_only=False,workspace_token="workspace-a"):
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
    raw["globalFxBypassed"]=False  # Published on every native mixer snapshot.
    raw["logicalMixer"]={"schema":"arsonmix.native.logical-mixer.v1",
        "authority":"LOGICAL_ONLY","workspaceToken":workspace_token,
        "revision":revision,"graph":graph,
        "physicalOutputAdmitted":False,"outputArmed":False}
    return raw


def retained_snapshot(stale_scalar=False):
    """True PCM-retained readback, not a fabricated admitted audio device."""
    raw=snapshot(revision="2",extra=True)
    raw["device"]["epoch"]="69"
    raw["graph"]=copy.deepcopy(raw["logicalMixer"]["graph"])
    raw["graph"]["bindingState"]="NEEDS_REMAP"
    raw["graph"]["capabilities"]["callbackOwnsChannelIds"]=False
    raw["graph"]["capabilities"]["supportedOps"]=[]
    raw["mixerSession"]={"authority":"CHANNEL_ID",
        "bindingState":"NEEDS_REMAP","deviceEpoch":"69",
        "routeRevision":"2","rosterRevision":"2",
        "channels":copy.deepcopy(raw["graph"]["channels"])}
    if stale_scalar:
        # Same ChannelId graph generation, newer unsaved physical fader.
        raw["graph"]["channels"][0]["controls"]["faderDb"]=-7
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
        page.add_init_script(NATIVE_EVENTS + """window.__events=[];
          window.__JUCE__={backend:{
             emitEvent(name,payload){window.__events.push({name,payload});},
             ...window.__nativeListenerMethods
          }};""")
        page.goto((out/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
        # This mock injects only native DSP readback for the ORIGINAL
        # mixer-hosted FX editor; there is no second JUCE HWND, WebView,
        # browser FX copy or alternate PCM renderer in production.
        project_approved_editor_for_component_smoke(page)
        state=snapshot()
        state["logicalMixer"]["graph"]["channels"][0]["inserts"]=[
            {"id":"logical-eq","catalogId":"armix.builtin.channel-eq.v1",
             "name":"Parametric EQ","bypassed":False},
            {"id":"logical-vendor","catalogId":"vst3.fixture",
             "name":"Vendor FX","bypassed":False},
        ]
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_function("""()=>document.documentElement.dataset.armixNativeAuthority==='logical-editable'""")
        rack=page.locator("#tracks .strip").first
        rack.locator('[data-action="openPlugin"][data-slot="0"]').click()
        page.wait_for_function("()=>document.querySelector('#mxNativePeq')?.hidden===false")
        assert page.locator("#mxNativePeq").is_visible()
        assert not page.evaluate("""()=>window.__fux1FloatingDispatches.some(e=>
           e.catalogId==='armix.builtin.channel-eq.v1'&&e.slot===0&&
           e.instanceId==='logical-eq')"""),"built-in FX reopened a forbidden second JUCE window"
        assert page.locator("#mxNativePeq").evaluate(
            "e=>Math.abs(e.getBoundingClientRect().width-795)<2"),(
            "offline FX must retain native full-size hardware inside mixer")
        assert page.evaluate("""()=>Boolean(window.__ARMIX_NATIVE_INLINE_FX_CLIP__)""")
        assert not page.locator("#fastPicker").is_visible()
        eq=page.evaluate("""()=>window.__events.find(e=>
            e.payload?.action==='nativeEqSnapshot')?.payload""")
        assert eq["workspaceToken"]=="workspace-a" and eq["deviceEpoch"]=="0"
        assert eq["instanceId"]=="logical-eq"
        page.evaluate("()=>window.ArSonMixNativeEq.close()")
        rack.locator('[data-action="openPlugin"][data-slot="1"]').click()
        vendor=page.evaluate("""()=>window.__events.find(e=>
            e.payload?.action==='logicalVendorFxEditor')?.payload""")
        assert vendor["instanceId"]=="logical-vendor" and vendor["deviceEpoch"]=="0"
        assert not page.locator("#fastPicker").is_visible()
        # Genuine native GFX must be exposed before any asynchronous VST
        # discovery. The static fallback is validated against the same
        # catalog ID compiled into the GuitarRigProcessor.
        def assert_guitar_in_builtin_group():
            # Regression for real installed 03ba6ecc screenshot: the picker
            # hardcoded four BUILT-IN FX buttons and placed Guitar FX under
            # VST3 PLUGINS, potentially below the visible scroll viewport.
            assert page.locator("#fastResults").evaluate("""root=>{
                const guitar=root.querySelector('[data-fast-id="armix.builtin.guitar-rig.v1"]');
                const headings=[...root.querySelectorAll('.fast-fx-category')];
                if(!guitar||headings.length<2)return false;
                return headings[0].textContent.trim()==='BUILT-IN FX' &&
                    headings[1].textContent.trim()==='VST3 PLUGINS' &&
                    !!(headings[0].compareDocumentPosition(guitar)&Node.DOCUMENT_POSITION_FOLLOWING) &&
                    !!(guitar.compareDocumentPosition(headings[1])&Node.DOCUMENT_POSITION_FOLLOWING);
            }"""), "Guitar FX must be inside first BUILT-IN FX group, not VST3"
            assert page.locator(
                '#fastResults [data-fast-id="armix.builtin.guitar-rig.v1"]').count()==1, (
                "Guitar FX should be shown once, not duplicated in VST3")

        rack.locator('[data-action="openPlugin"][data-slot="2"]').click()
        assert page.locator('[data-fast-id="armix.builtin.guitar-rig.v1"]').is_visible(), (
            "Guitar FX missing at first native picker open")
        assert_guitar_in_builtin_group()
        page.locator("#fastClose").click()
        emit(page, "arMixCatalog", {"items": [], "builtIns": [
            {"id": "armix.builtin.fixture.v1", "name": "Fixture processor", "category": "Utility"},
            {"id": "armix.builtin.guitar-rig.v1", "name": "Guitar FX Rig",
             "category": "Guitar", "showInPicker": True},
            {"id": "armix.builtin.hidden.v1", "name": "Hidden processor", "showInPicker": False},
        ]})
        rack.locator('[data-action="openPlugin"][data-slot="2"]').click()
        assert page.locator("#fastPicker").is_visible()
        assert page.locator('[data-fast-id="armix.builtin.fixture.v1"]').is_visible()
        assert page.locator('[data-fast-id="armix.builtin.hidden.v1"]').count() == 0
        guitar=page.locator('[data-fast-id="armix.builtin.guitar-rig.v1"]')
        assert guitar.is_visible(), "Guitar FX Rig missing from user-facing FX picker"
        assert_guitar_in_builtin_group() # actual host arMixCatalog refresh retains BUILT-IN placement
        search=page.locator("#fastSearch")
        search.fill("guitar")
        assert guitar.is_visible(), "Guitar FX Rig must be searchable by name/category"
        assert page.locator('[data-fast-id="armix.builtin.fixture.v1"]').count() == 0
        page.locator("#fastClose").click()
        # We test pending guitar INSERT at the END of this workspace.
        # Creating a pending logical edit here would otherwise contaminate
        # subsequent CREATE_CHANNEL/WIRE_TRANSACTION assertions.
        # The approved mixer virtualizes/banks strips. Count native-owned
        # model channels, not only the currently mounted DOM bank.
        assert page.evaluate("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length")==14
        # The approved Pages tool tray is a real collapsible <details>.
        # Open it as the operator would; never assert hidden children visible.
        assert page.locator("#addTrack").is_visible()
        assert page.locator("#addTrack").is_enabled()
        # Native V6 places Add BUS on the Routing ribbon, not in the cascade.
        select_ribbon_workflow(page, "routing")
        add_bus=page.locator(".command-bar > #addBus")
        assert add_bus.count()==1 and add_bus.is_visible() and add_bus.is_enabled()
        select_ribbon_workflow(page, "mixer")
        open_application_menu(page, "session")
        # Archive action requires accepted offline authority and confirmation.
        archive=page.get_by_role("button",name="Open session archive",exact=True)
        assert archive.is_visible()
        archive.click()
        assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='nativeMixerOpenArchive')")
        state["offlineNewSessionAvailable"]=True
        emit(page,"arMixConsoleSnapshot",state)
        def open_menu(category="session"):
            open_application_menu(page, category)

        def confirm_menu_action(button, choice, category="session"):
            open_menu(category)
            button.click()
            page.get_by_role("dialog").get_by_role("button", name=choice, exact=True).click()

        confirm_menu_action(archive, "Cancel")
        assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='nativeMixerOpenArchive')")
        confirm_menu_action(archive, "Open archive")
        event=page.evaluate("()=>window.__events.find(e=>e.payload?.action==='nativeMixerOpenArchive')?.payload")
        assert event["confirmed"] is True and event["workspaceToken"]=="workspace-a" and event["baseRevision"]=="1", event
        new_project=page.get_by_role("button",name="New Project · empty mixer",exact=True)
        open_menu()
        assert new_project.is_visible()
        confirm_menu_action(new_project, "Cancel")
        assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='nativeMixerNewOffline')")
        # The leftmost New shortcut delegates to exactly the same transaction.
        toolbar_new=page.locator("#moreTools [data-native-new-project]")
        open_menu(None)
        assert toolbar_new.inner_text()=="New" and toolbar_new.is_visible()
        confirm_menu_action(toolbar_new, "Cancel", None)
        assert not page.evaluate("()=>window.__events.some(e=>e.payload?.action==='nativeMixerNewOffline')")
        confirm_menu_action(toolbar_new, "Continue", None)
        assert page.evaluate("()=>window.__events.filter(e=>e.payload?.action==='nativeMixerNewOffline').length")==1
        new_event=page.evaluate("()=>window.__events.find(e=>e.payload?.action==='nativeMixerNewOffline')?.payload")
        assert new_event["mode"]=="EMPTY" and new_event["confirmed"] is True, new_event
        assert new_event["workspaceToken"]=="workspace-a" and new_event["baseRevision"]=="1", new_event
        assert page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixConsoleIntent').length""")==0
        page.locator("#addTrack").click()
        page.wait_for_function("""()=>window.__events.some(e=>e.name==='arMixLogicalIntent')""")
        event=page.evaluate("""()=>window.__events.filter(
            e=>e.name==='arMixLogicalIntent').at(-1).payload""")
        assert event["operation"]=="CREATE_CHANNEL"
        assert event["baseRevision"]=="1"
        assert event["workspaceToken"]=="workspace-a"
        roster_controls=["addTrack","addBus","addFx","addGroupBus","addGroupSelected"]
        def assert_roster_enabled(enabled):
            for ident in roster_controls:
                assert page.locator("#"+ident).is_enabled()==enabled, (ident,enabled)
        assert_roster_enabled(False)
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
        assert_roster_enabled(False)
        state=snapshot("2",True)
        emit(page,"arMixConsoleSnapshot",state)
        page.wait_for_function("""()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length===15""")
        new_track=page.locator("#tracks .strip").filter(has_text="Track 16").first
        assert new_track.count()==1
        assert_roster_enabled(True)
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
        assert_roster_enabled(False)
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
        assert page.evaluate("""()=>window.__ARMIX_NATIVE_SCOPE__.tracks.find(
            t=>t.name==='Track 16')?.output""")=="SOURCE ONLY (No Main)"
        assert page.locator("#addTrack").is_enabled()
        # A same-revision Open changes workspace identity. Late ACKs for the
        # original file must never block or mutate the newly opened mixer.
        page.locator("#addTrack").click()
        stale=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixLogicalIntent').at(-1).payload""")
        assert stale["baseRevision"]=="3"
        emit(page,"arMixConsoleSnapshot",snapshot("3",workspace_token="workspace-b"))
        page.wait_for_function("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length===14")
        emit(page,"arMixLogicalAck",{"requestId":stale["requestId"],
            "status":"ACCEPTED","errorCode":"NONE","revision":"4","createdId":"17"})
        assert page.evaluate("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length")==14
        page.locator("#addTrack").click()
        fresh=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixLogicalIntent').at(-1).payload""")
        assert fresh["workspaceToken"]=="workspace-b"
        assert_roster_enabled(False)
        emit(page,"arMixLogicalAck",{"requestId":fresh["requestId"],
            "status":"REJECTED","errorCode":"TEST_DISK_WRITE_FAILED","revision":"3"})
        assert page.evaluate("()=>window.__ARMIX_NATIVE_SCOPE__.tracks.length")==14
        assert_roster_enabled(True)
        # Now assert actual selection dispatches the correct Guitar FX id.
        # Leave pending ADD_FX_REFERENCE as the LAST edit; it must not
        # falsely advertise a physical processor or block other fixtures.
        page.locator("#tracks .strip").first.locator(
            '[data-action="openPlugin"][data-slot="0"]').click()
        guitar=page.locator('[data-fast-id="armix.builtin.guitar-rig.v1"]')
        assert guitar.is_visible(), "Guitar FX missing after changing workspaces"
        page.locator("#fastSearch").fill("guitar")
        guitar.click()
        page.wait_for_function("""()=>window.__events.some(e=>
            e.name==='arMixLogicalIntent' &&
            e.payload?.operation==='ADD_FX_REFERENCE' &&
            e.payload?.payload?.catalogId==='armix.builtin.guitar-rig.v1')""")
        assert page.locator("#fastPicker").is_visible(), (
            "Unacknowledged Guitar FX insert must not look accepted")
        assert not errors,errors
        page.close()
        # A physical device is NOT required to author a parallel SEND tap.
        # Before this change, canWireTap() rejected all offline wires and
        # consumeWire(MODE) forwarded an unsupported WIRE_TRANSACTION instead
        # of the saved-document SET_SEND_TAP. Test the actual compiled GUI.
        page=browser.new_page(viewport={"width":1366,"height":768})
        page.on("pageerror",lambda error:errors.append(str(error)))
        page.add_init_script(NATIVE_EVENTS + """window.__events=[];
          window.__JUCE__={backend:{
             emitEvent(name,payload){window.__events.push({name,payload});},
             ...window.__nativeListenerMethods
          }};""")
        page.goto((out/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
        staged=snapshot("1",workspace_token="wire-offline")
        graph=staged["logicalMixer"]["graph"]
        extra={"edgeId":"1001","from":"1","to":"11","signalKind":"AUDIO",
               "enabled":True,"sendEnabled":True,"gainLinear":0.5,
               "primaryOutput":False,"tap":"POST_FADER","tapStage":"POST_FADER"}
        graph["channels"][0]["sends"].append(copy.deepcopy(extra))
        graph["edges"].append(copy.deepcopy(extra))
        emit(page,"arMixConsoleSnapshot",staged)
        page.wait_for_function("""()=>window.__ARMIX_NATIVE_SCOPE__.wireEdges.some(
            e=>e.kind==='send' && e.enabled)""")
        wire_ids=page.evaluate("""()=>{
          const s=window.__ARMIX_NATIVE_SCOPE__;
          const src=s.tracks.find(t=>t.name==='CH 01');
          const bus=s.tracks.find(t=>t.name==='AUX A');
          const edge=s.wireEdges.find(e=>e.sourceId===src?.id&&
            e.destId===bus?.id&&e.kind==='send');
          const primary=s.wireEdges.find(e=>e.sourceId===src?.id&&e.kind==='primary');
          if(!edge||!primary)throw Error('Missing exact accepted logical SEND/primary');
          return {source:src.id,parallel:edge.id,primary:primary.id};
        }""")
        # Offline SOF is a saved EdgeId transaction, not a second PCM graph.
        select_ribbon_workflow(page, "mixer")
        sof=page.locator("#sofToggle")
        assert sof.is_enabled()
        sof.click()
        page.locator("#sofTarget").select_option(str(page.evaluate(
            "()=>window.__ARMIX_NATIVE_SCOPE__.tracks.find(t=>t.name==='AUX A').id")))
        fader=page.locator(f'#tracks .strip[data-id="{wire_ids["source"]}"] .fader')
        assert fader.is_enabled(), "Saved SEND must be editable without PCM"
        page.screenshot(path="arsonmix-offline-sof.png")
        gain_events="()=>window.__events.filter(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='SET_WIRE_GAIN')"
        fader.evaluate("(el)=>{el.value='-12';el.dispatchEvent(new Event('input',{bubbles:true}));}")
        assert len(page.evaluate(gain_events))==0, "Offline disk edit must wait for release"
        fader.evaluate("(el)=>{el.dispatchEvent(new Event('change',{bubbles:true}));}")
        page.wait_for_function(f"()=>({gain_events})().length===1")
        gain_req=page.evaluate(gain_events)[-1]["payload"]
        assert gain_req["channelId"]=="1" and gain_req["baseRevision"]=="1"
        assert gain_req["payload"]["edgeId"]=="1001"
        assert abs(gain_req["payload"]["gainLinear"]-10**(-12/20))<0.00001
        emit(page,"arMixLogicalAck",{"requestId":gain_req["requestId"],"status":"REJECTED","errorCode":"FIXTURE"})
        emit(page,"arMixConsoleSnapshot",staged)
        # Exercise the actual delegated double-click handler on the fader box.
        fader.locator("xpath=ancestor::*[contains(concat(' ',normalize-space(@class),' '),' fader-box ')][1]").dispatch_event("dblclick")
        page.wait_for_function(f"()=>({gain_events})().length===2")
        reset_req=page.evaluate(gain_events)[-1]["payload"]
        assert reset_req["channelId"]=="1" and reset_req["baseRevision"]=="1"
        assert reset_req["payload"]=={"edgeId":"1001","gainLinear":1}
        assert not page.evaluate("()=>window.__events.some(e=>e.name==='arMixConsoleIntent'||e.payload?.operation==='SET_FADER'||e.payload?.action==='monitor')")
        emit(page,"arMixLogicalAck",{"requestId":reset_req["requestId"],"status":"REJECTED","errorCode":"FIXTURE"})
        emit(page,"arMixConsoleSnapshot",staged)
        sof.click()
        assert page.evaluate("""([src,edge,primary])=>{
          const hooks=window.__ARMIX_NATIVE_M2__;
          return hooks.canWireTap(edge,src) && !hooks.canWireTap(primary,src);
        }""",[wire_ids["source"],wire_ids["parallel"],wire_ids["primary"]])
        page.evaluate("""([src,edge])=>
          window.__ARMIX_NATIVE_M2__.consumeWire(
            [{type:'MODE',sourceId:src,edgeId:edge,mode:'split'}])""",
            [wire_ids["source"],wire_ids["parallel"]])
        page.wait_for_function("""()=>window.__events.some(e=>
            e.name==='arMixLogicalIntent' && e.payload?.operation==='SET_SEND_TAP')""")
        request=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixLogicalIntent').at(-1).payload""")
        assert request["channelId"]=="1"
        assert request["baseRevision"]=="1"
        assert request["payload"]=={"edgeId":"1001","tap":"PRE_FADER"}
        assert page.evaluate("""edgeId=>window.__ARMIX_NATIVE_SCOPE__.wireEdges.some(
            e=>e.id===edgeId && e.mode==='send' && e.nativeTap==='POST_FADER')""",
            wire_ids["parallel"]), (
            "Offline WIRE must retain the old tap until native ACK + saved readback")
        assert not page.evaluate("""()=>window.__events.some(e=>e.name==='arMixConsoleIntent')""")
        emit(page,"arMixLogicalAck",{"requestId":request["requestId"],"status":"ACCEPTED",
            "errorCode":"NONE","revision":"2","physicalOutputAdmitted":False})
        applied=snapshot("2",workspace_token="wire-offline")
        applied_extra=copy.deepcopy(extra)
        applied_extra["tap"]="SPLIT_PRE"
        applied_extra["tapStage"]="PRE_FADER"
        applied["logicalMixer"]["graph"]["channels"][0]["sends"].append(
            copy.deepcopy(applied_extra))
        applied["logicalMixer"]["graph"]["edges"].append(applied_extra)
        emit(page,"arMixConsoleSnapshot",applied)
        page.wait_for_function("""edgeId=>
            window.__ARMIX_NATIVE_SCOPE__.wireEdges.some(e=>
              e.id===edgeId && e.nativeTap==='PRE_FADER')""",
              arg=wire_ids["parallel"])
        assert page.evaluate("""([edge,source])=>window.__ARMIX_NATIVE_M2__.canWireTap(
          edge,source)""",[wire_ids["parallel"],wire_ids["source"]])
        assert page.evaluate("""()=>document.documentElement.dataset.armixNativeAuthority""")==\
            "logical-editable"
        assert not errors,errors
        page.close()
        # Device-loss boundary: the retained PCM graph may still carry a
        # newer scalar at IDENTICAL topology revision. It must never
        # authorize editing an outdated disk mirror or report PCM admitted.
        page=browser.new_page(viewport={"width":1366,"height":768})
        page.on("pageerror",lambda error:errors.append(str(error)))
        page.add_init_script(NATIVE_EVENTS + """window.__events=[];
          window.__JUCE__={backend:{
             emitEvent(name,payload){window.__events.push({name,payload});},
             ...window.__nativeListenerMethods
          }};""")
        page.goto((out/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
        emit(page,"arMixConsoleSnapshot",retained_snapshot())
        page.wait_for_function("""()=>document.querySelector('#addTrack').disabled""")
        assert page.evaluate("""()=>document.documentElement.dataset.armixNativeAuthority""")!="logical-editable"
        emit(page,"arMixConsoleSnapshot",retained_snapshot(stale_scalar=True))
        page.wait_for_function("""()=>document.querySelector('#addTrack').disabled""")
        assert page.evaluate("""()=>document.documentElement.dataset.armixNativeAuthority""")!="logical-editable"
        assert page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixLogicalIntent').length""")==0
        # Power on a retained empty rack must capture the authoritative
        # workspace, not show an I/O repair modal or send a PCM command.
        rack=page.locator("#tracks .strip").first
        rack.locator('[data-action="fxOn"]').click()
        page.wait_for_function("""()=>window.__events.some(e=>
            e.payload?.action==='retainedFxOfflineOpen')""")
        request=page.evaluate("""()=>window.__events.find(e=>
            e.payload?.action==='retainedFxOfflineOpen').payload""")
        assert request["channelId"]=="1" and request["slot"]==0
        assert request["deviceEpoch"]=="69" and request["graphRevision"]=="2"
        assert request["instanceId"]==""
        assert not page.evaluate("""()=>window.__events.some(e=>
            e.name==='arMixLogicalIntent' || e.name==='arMixConsoleIntent')""")
        # Native capture ACK alone cannot make a stale saved mirror editable.
        emit(page,"arMixRetainedFxOfflineAck",{"requestId":request["requestId"],"ok":True})
        assert page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixLogicalIntent').length""")==0
        captured=retained_snapshot(stale_scalar=True)
        offline=captured["logicalMixer"]
        offline.update(retainedOffline=True,retainedDeviceEpoch="69",
            retainedGraphRevision="2",retainedCaptureDeviceEpoch="69",
            retainedCaptureGraphRevision="2")
        offline["graph"]["channels"][0]["controls"]["faderDb"]=-7
        emit(page,"arMixConsoleSnapshot",captured)
        page.wait_for_function("""()=>window.__events.some(e=>
            e.name==='arMixLogicalIntent'&&e.payload?.operation==='SET_FX_RACK_BYPASS')""")
        intent=page.evaluate("""()=>window.__events.filter(e=>
            e.name==='arMixLogicalIntent').at(-1).payload""")
        assert intent["channelId"]=="1" and intent["payload"]=={"enabled":True}
        assert not page.evaluate("""()=>window.__events.some(e=>e.name==='arMixConsoleIntent')""")
        emit(page,"arMixLogicalAck",{"requestId":intent["requestId"],"status":"ACCEPTED",
            "errorCode":"NONE","revision":"3","physicalOutputAdmitted":False})
        applied=copy.deepcopy(captured)
        applied["logicalMixer"]["revision"]="3"
        applied["logicalMixer"]["graph"]["revision"]="3"
        applied["logicalMixer"]["graph"]["rosterRevision"]="3"
        applied["logicalMixer"]["graph"]["channels"][0]["fxRackBypassed"]=True
        emit(page,"arMixConsoleSnapshot",applied)
        page.wait_for_function("""()=>window.__ARMIX_NATIVE_SCOPE__.tracks[0].fxOn===false""")
        assert not errors,errors
        page.close()

        # Workspace-only edit must be available without touching a vendor FX.
        page=browser.new_page(viewport={"width":1366,"height":768})
        page.on("pageerror",lambda error:errors.append(str(error)))
        page.add_init_script(NATIVE_EVENTS + """window.__events=[];
          window.__JUCE__={backend:{
             emitEvent(name,payload){window.__events.push({name,payload});},
             ...window.__nativeListenerMethods
          }};""")
        page.goto((out/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
        detached=retained_snapshot(stale_scalar=True)
        detached["graph"]["channels"][0]["inserts"]=[
            {"id":"vendor-instance","catalogId":"vst3.fixture","name":"Vendor FX","bypassed":False}]
        detached["mixerSession"]["channels"]=copy.deepcopy(detached["graph"]["channels"])
        emit(page,"arMixConsoleSnapshot",detached)
        edit=page.get_by_role("button",name="Edit mixer offline",exact=True)
        assert edit.is_visible() and edit.is_enabled()
        edit.click()
        assert page.get_by_role("button",name="Preparing offline mixer…",exact=True).is_disabled()
        req=page.evaluate("""()=>window.__events.find(e=>
          e.payload?.action==='retainedFxOfflineOpen').payload""")
        assert req["workspaceOnly"] is True and req["deviceEpoch"]=="69"
        assert req["graphRevision"]=="2"
        assert not any(k in req for k in ["channelId","slot","instanceId"])
        emit(page,"arMixRetainedFxOfflineAck",{"requestId":req["requestId"],"ok":False,
            "error":"Fixture: device transaction busy"})
        assert edit.is_enabled(), "Rejected capture must unlock the workspace action"
        edit.click()
        req=page.evaluate("""()=>window.__events.filter(e=>
          e.payload?.action==='retainedFxOfflineOpen').at(-1).payload""")
        emit(page,"arMixRetainedFxOfflineAck",{"requestId":req["requestId"],"ok":True})
        assert page.locator("#addTrack").is_disabled()
        saved=copy.deepcopy(detached)
        saved["logicalMixer"]["graph"]["channels"][0]["inserts"]=copy.deepcopy(
            detached["graph"]["channels"][0]["inserts"])
        saved["logicalMixer"]["graph"]["channels"][0]["controls"]["faderDb"]=-7
        saved["logicalMixer"].update(retainedOffline=True,retainedDeviceEpoch="69",
            retainedGraphRevision="2",retainedCaptureDeviceEpoch="69",
            retainedCaptureGraphRevision="2")
        emit(page,"arMixConsoleSnapshot",saved)
        page.wait_for_function("()=>document.documentElement.dataset.armixNativeAuthority==='logical-editable'")
        assert not edit.is_visible() and page.locator("#addTrack").is_enabled()
        assert not page.evaluate("""()=>window.__events.some(e=>e.name==='arMixLogicalIntent'||
            e.name==='arMixConsoleIntent'||e.payload?.action==='monitor')""")
        fader=page.locator("#tracks .strip").first.locator("input.fader")
        for value in [-12,-15,-18]:
            fader.evaluate("(el,v)=>{el.value=String(v);el.dispatchEvent(new Event('input',{bubbles:true}));}",value)
            emit(page,"arMixConsoleSnapshot",saved)
        commands="()=>window.__events.filter(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='SET_FADER')"
        assert len(page.evaluate(commands))==0, "Readback during offline drag cannot persist preview"
        fader.evaluate("(el)=>{el.value='-18';el.dispatchEvent(new Event('change',{bubbles:true}));}")
        page.wait_for_function(f"()=>({commands})().length===1")
        intent=page.evaluate(commands)[0]["payload"]
        assert intent["payload"]["valueDb"]==-18
        emit(page,"arMixLogicalAck",{"requestId":intent["requestId"],"status":"REJECTED","errorCode":"FIXTURE"})
        emit(page,"arMixConsoleSnapshot",saved)
        assert float(fader.input_value())==-7, "Rejected preview must restore saved fader"
        assert not errors,errors
        page.close()
        browser.close()
    print("PASS native compiled GUI: offline ACK, workspace replacement, WIRE SOURCE ONLY and retained PCM isolation")


if __name__=="__main__":
    main()
