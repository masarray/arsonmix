"""Targeted real Chromium test of the approved Audio I/O panel.
Mock native transport proves DOM -> intent -> ACK -> installed readback, not PCM.
"""
import copy
from pathlib import Path
import sys
import tempfile
from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS
from approved_native_wire_gain_smoke import snapshot, emit, SOURCE, open_verified_audio_io, verify_external_output

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
import build_approved_native_gui as bundle

def verify_custom_input(page,out):
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    # Mixer + Anti Noiz may subscribe to the same real JUCE event.
    assert page.evaluate("()=>window.__nativeListenerCount('arMixConsoleSnapshot')") >= 2
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",captureDevice="Test ASIO",playbackDevice="Test ASIO",
        enabledInputs=[0,2,3],inputEndpoints=[
            {"id":"in:"+str(port),"port":port,"callback":index,
             "label":"Mic "+str(port+1),"active":True}
            for index,port in enumerate([0,2,3])])
    emit(page,"arMixConsoleSnapshot",state)
    inventory=open_verified_audio_io(page,state,"inputs")
    # Native store fixture: save does not mutate mixer; ACK alone does not
    # publish the preset. Only independent correlated disk readback does.
    inventory["namedPatches"]={"revision":0,"readOnly":False,"entries":[]}
    emit(page,"arMixAudioDeviceInventory",inventory)
    name=page.get_by_label("Preset name",exact=True)
    name.fill("Vocal Stereo")
    page.get_by_label("Preset format",exact=True).select_option("STEREO")
    page.get_by_label("Preset L pin",exact=True).select_option("3")
    page.get_by_label("Preset R pin",exact=True).select_option("3")
    save=page.get_by_role("button",name="Save preset",exact=True)
    assert save.is_disabled()
    page.get_by_label("Preset R pin",exact=True).select_option("0")
    save.click()
    preset_req=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioNamedPatchEdit').at(-1)")
    assert preset_req and preset_req["operation"]=="SAVE" and preset_req["name"]=="Vocal Stereo"
    assert (preset_req["left"],preset_req["right"],preset_req["leftName"],preset_req["rightName"])==(3,0,"Mic 4","Mic 1")
    assert page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').length")==0
    emit(page,"arMixNamedPatchAck",{"requestId":preset_req["requestId"],"ok":True})
    assert page.locator('option[value="preset:fixture-1"]').count()==0
    assert page.evaluate("()=>window.__actions.some(x=>x.action==='audioNamedPatchRead')")
    entry={k:preset_req[k] for k in ("name","side","mode","left","right","leftName","rightName")}
    entry.update(id="fixture-1",available=True)
    inventory["namedPatches"]={"revision":1,"readOnly":False,"entries":[entry]}
    emit(page,"arMixNamedPatchReadback",{"requestId":preset_req["requestId"],
        "active":inventory["active"],"namedPatches":inventory["namedPatches"]})
    row=page.locator("[data-audio-input-grid] tbody tr").filter(has_text="CH03")
    row.wait_for(state="visible")
    select=row.get_by_label("CH03 assignment",exact=True)
    select.select_option("__custom_stereo__")
    left=row.get_by_label("CH03 L pin",exact=True)
    right=row.get_by_label("CH03 R pin",exact=True)
    left.select_option("3")
    right.select_option("3")
    apply=row.get_by_role("button",name="Apply",exact=True)
    assert apply.is_disabled(),"same physical pin must never become stereo"
    right.select_option("0")
    assert apply.is_enabled()
    # A named preset resolves to the exact same verified physical choice.
    select.select_option("preset:fixture-1")
    assert apply.is_enabled()
    assert row.locator('[data-draft="true"]').count()==2
    assert page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').length")==0
    apply.click()
    req=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').at(-1)")
    assert req and req["channelId"]==SOURCE and req["mode"]=="STEREO",req
    assert (req["left"],req["right"])==(3,0),req
    assert row.locator('[data-mark="L"]').count()==0,"request alone painted installed crosspoint"
    emit(page,"arMixNativeSourceAck",{"requestId":req["requestId"],"channelId":SOURCE,
        "ok":True,"graphRevision":"3"})
    assert row.locator('[data-mark="L"]').count()==0,"ACK alone painted installed crosspoint"
    state["graph"]["revision"]="3"
    state["graph"]["channels"][0]["source"]={"sourceFormat":"STEREO","portId":"in:3","portIdRight":"in:0"}
    state["mixerSession"]["routeRevision"]="3"
    state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
    emit(page,"arMixConsoleSnapshot",state)
    row.locator('[data-mark="L"]').wait_for(state="visible")
    assert row.locator('.armix-patch-cell').nth(0).get_attribute("data-mark")=="R"
    assert row.locator('.armix-patch-cell').nth(2).get_attribute("data-mark")=="L"
    assert row.locator('[data-draft="true"]').count()==0
    # After installed readback the pending lock must clear without another
    # telemetry snapshot (real regression: old render preceded settlement).
    assert page.get_by_label("Saved input preset",exact=True).is_enabled()
    # Rename is metadata-only; no additional SOURCE operation.
    page.get_by_label("Saved input preset",exact=True).select_option("fixture-1")
    page.get_by_label("Preset name",exact=True).fill("Studio Vocal")
    page.get_by_role("button",name="Save preset",exact=True).click()
    rename=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioNamedPatchEdit').at(-1)")
    assert rename["id"]=="fixture-1" and rename["revision"]==1
    assert rename["name"]=="Studio Vocal"
    emit(page,"arMixNamedPatchAck",{"requestId":rename["requestId"],"ok":True})
    entry["name"]="Studio Vocal"
    inventory["namedPatches"]={"revision":2,"readOnly":False,"fileExchange":True,"entries":[entry]}
    emit(page,"arMixNamedPatchReadback",{"requestId":rename["requestId"],
        "active":inventory["active"],"namedPatches":inventory["namedPatches"]})
    assert page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').length")==1
    assert row.locator('[data-mark="L"]').count()==1
    # Interchange uses accepted metadata, not the current route or ARM state.
    page.get_by_text("Import / Export presets",exact=True).click()
    page.get_by_role("button",name="Export profile",exact=True).click()
    import json
    profile=json.loads(page.get_by_label("I/O profile JSON").input_value())
    assert profile["schema"]=="arsonmix.io-profile.v1" and profile["entries"][0]["name"]=="Studio Vocal"
    assert "id" not in profile["entries"][0] and "available" not in profile["entries"][0]
    # Native save/open transport is separate from mutation. File response
    # is a reviewable draft; only explicit Import submits an edit.
    page.get_by_role("button",name="Save .ario file",exact=True).click()
    file_save=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioNamedProfileFile').at(-1)")
    assert file_save["operation"]=="EXPORT" and file_save["revision"]==2 and "profile" not in file_save
    emit(page,"arMixNamedProfileFileResult",{"requestId":file_save["requestId"],"operation":"EXPORT","ok":True})
    page.get_by_role("button",name="Open .ario file",exact=True).click()
    file_open=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioNamedProfileFile').at(-1)")
    assert file_open["operation"]=="OPEN" and file_open["requestId"]!=file_save["requestId"]
    profile["entries"][0]["name"]="Imported Vocal"
    emit(page,"arMixNamedProfileFileResult",{"requestId":file_save["requestId"],"operation":"OPEN","ok":True,"profile":profile})
    assert json.loads(page.get_by_label("I/O profile JSON").input_value())["entries"][0]["name"]=="Studio Vocal"
    emit(page,"arMixNamedProfileFileResult",{"requestId":file_open["requestId"],"operation":"OPEN","ok":True,"profile":profile})
    assert json.loads(page.get_by_label("I/O profile JSON").input_value())==profile
    assert page.evaluate("()=>window.__actions.filter(x=>x.action==='audioNamedPatchEdit').length")==2
    page.get_by_role("button",name="Import profile",exact=True).click()
    imported=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioNamedPatchEdit').at(-1)")
    assert imported["operation"]=="IMPORT" and imported["revision"]==2 and imported["profile"]==profile
    emit(page,"arMixNamedPatchAck",{"requestId":imported["requestId"],"ok":True})
    assert page.locator('option[value="preset:fixture-2"]').count()==0
    new_entry={**entry,"id":"fixture-2","name":"Imported Vocal"}
    inventory["namedPatches"]={"revision":3,"readOnly":False,"entries":[entry,new_entry]}
    emit(page,"arMixNamedPatchReadback",{"requestId":imported["requestId"],
        "active":inventory["active"],"namedPatches":inventory["namedPatches"]})
    assert page.locator('option[value="preset:fixture-2"]').count()>0
    assert page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').length")==1
    page.screenshot(path="audio-io-custom-stereo.png")
    # Preset appears on another input strip without creating a new graph
    # authority. Choose it directly; actual wire payload remains physical.
    page.locator("[data-audio-close]").click()
    direct=page.locator('#tracks .strip[data-id="2"] select[data-field="input"]')
    direct.wait_for(state="visible")
    label=direct.locator('option',has_text="Preset · Studio Vocal").first.get_attribute("value")
    direct.select_option(label)
    second=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').at(-1)")
    assert second["requestId"]!=req["requestId"] and (second["left"],second["right"])==(3,0),second
    assert not errors,errors

def toggle_then_snapshot(page, title, state):
    # Same JS task: exercise disclosure activation immediately followed by
    # native reconciliation, before a deferred details toggle event can run.
    page.evaluate("""([title,state])=>{
        const summary=[...document.querySelectorAll('summary')].find(s=>s.textContent===title);
        if(!summary)throw Error('Missing logical disclosure: '+title);
        summary.click();
        window.__nativeListeners.get('arMixConsoleSnapshot')(state);
    }""",[title,state])

def verify_logical_inputs(page,out):
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.confirm=()=>true;
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",captureDevice="Test ASIO",playbackDevice="Test ASIO",
        enabledInputs=[0,2,3],inputEndpoints=[
            {"id":"in:"+str(port),"port":port,"callback":index,
             "label":"Mic "+str(port+1),"active":True}
            for index,port in enumerate([0,2,3])])
    state["logicalInputRegistry"]={"revision":"1","ports":[],"references":[]}
    emit(page,"arMixConsoleSnapshot",state)
    open_verified_audio_io(page,state,"inputs")
    title="Session logical inputs"
    summary=page.get_by_text(title,exact=True)
    summary.press("Enter")
    name=page.get_by_label("Logical input name",exact=True)
    assert name.is_visible()
    # Change only a registry revision so native reconciliation rebuilds the
    # editor; closing/opening must survive an immediate snapshot in both directions.
    state["logicalInputRegistry"]["revision"]="2"
    toggle_then_snapshot(page,title,state)
    assert not name.is_visible()
    state["logicalInputRegistry"]["revision"]="3"
    toggle_then_snapshot(page,title,state)
    assert name.is_visible()
    name.fill("Live Vocal")
    page.get_by_role("button",name="Add port",exact=True).click()
    req=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingLogicalInputEdit').at(-1)")
    assert req["operation"]=="ADD" and req["logicalRevision"]=="3"
    portid="9007199254740999"
    emit(page,"arMixLogicalInputAck",{"requestId":req["requestId"],"ok":True,
        "logicalRevision":"4","graphRevision":state["graph"]["revision"]})
    select=page.get_by_label("Logical input port",exact=True)
    assert select.locator("option[value='"+portid+"']").count()==0
    port={"id":portid,"name":"Live Vocal","format":"MONO","bound":False}
    state["logicalInputRegistry"]={"revision":"4","ports":[port],"references":[]}
    emit(page,"arMixConsoleSnapshot",state)
    select.select_option(portid)
    page.get_by_label("Logical L pin",exact=True).select_option("0")
    page.get_by_role("button",name="Apply pin binding",exact=True).click()
    bind=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingLogicalInputEdit').at(-1)")
    assert bind["operation"]=="BIND" and bind["portId"]==portid and bind["leftName"]=="Mic 1"
    emit(page,"arMixLogicalInputAck",{"requestId":bind["requestId"],"ok":True,
        "logicalRevision":"5","graphRevision":state["graph"]["revision"]})
    port.update(bound=True,left=0,right=-1,leftName="Mic 1",rightName="",
        backend="ASIO",capture="Test ASIO",playback="Test ASIO")
    state["logicalInputRegistry"]={"revision":"5","ports":[port],"references":[]}
    emit(page,"arMixConsoleSnapshot",state)
    page.get_by_label("Logical input channel",exact=True).select_option(SOURCE)
    page.get_by_role("button",name="Assign to channel",exact=True).click()
    assign=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingLogicalInputEdit').at(-1)")
    assert assign["operation"]=="ASSIGN" and assign["channelId"]==SOURCE and assign["portId"]==portid
    emit(page,"arMixLogicalInputAck",{"requestId":assign["requestId"],"ok":False,"error":"fixture reject"})
    assert page.get_by_label("Logical input port",exact=True).is_enabled()
    assert page.get_by_label("Logical input channel",exact=True).locator("option[value='"+SOURCE+"']").inner_text().find("Live Vocal")==-1
    assert not errors,errors

def verify_bus_remap(page,out):
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.confirm=()=>true;
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",captureDevice="Test ASIO",playbackDevice="Test ASIO",
        outputArmed=True,enabledOutputs=list(range(8)),
        outputEndpoints=[{"id":"out:"+str(n),"port":n,"callback":n,
                          "label":"ASIO "+str(n+1),"active":True} for n in range(8)])
    state.update(physicalMainOutputs=[0,1],physicalMonitorOutputs=[2,3],
        externalOutputs=[{"channelId":SOURCE,"physicalLeft":6,"physicalRight":7,"status":"LIVE"}],
        externalRevision="2",physicalBusPatchSupported=True,physicalBusOutputObserved=True,
        physicalBusSecondaryArmed=False,operatorOutputMuted=False)
    emit(page,"arMixConsoleSnapshot",state)
    open_verified_audio_io(page,state,"routing")
    page.locator("[data-audio-hardware-routing] > summary").click()
    rows=page.locator("[data-audio-output-grid] tbody tr")
    main=rows.nth(0);monitor=rows.nth(1)
    assert main.get_by_label("MAIN assignment",exact=True).count()==0
    assert main.locator(".armix-patch-cell").nth(0).get_attribute("data-mark")=="L"
    state["device"]["outputArmed"]=False
    state["operatorOutputMuted"]=True
    emit(page,"arMixConsoleSnapshot",state)
    select=main.get_by_label("MAIN assignment",exact=True)
    choices=select.locator("option").all_text_contents()
    assert len(choices)==2 and any(x.startswith("OUT 5/6") for x in choices),choices
    assert not any(x.startswith(("OUT 3/4","OUT 7/8")) for x in choices),choices
    select.select_option(label=next(x for x in choices if x.startswith("OUT 5/6")))
    assert main.locator('[data-draft="true"]').count()==4
    main.get_by_role("button",name="Apply",exact=True).click()
    req=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingPhysicalBusOutput').at(-1)")
    assert req and req["bus"]=="MAIN" and req["confirmed"] is True,req
    assert (req["previousLeft"],req["previousRight"],req["left"],req["right"])==(0,1,4,5),req
    assert (req["leftName"],req["rightName"])==("ASIO 5","ASIO 6")
    emit(page,"arMixPhysicalBusOutputAck",{"requestId":req["requestId"],"ok":True})
    assert main.locator(".armix-patch-cell").nth(0).get_attribute("data-mark")=="L"
    state["physicalMainOutputs"]=[4,5]
    state["physicalBusOutputObserved"]=False
    emit(page,"arMixConsoleSnapshot",state)
    assert main.locator('[data-draft="true"]').count()==2
    assert main.get_by_label("MAIN assignment",exact=True).count()==0
    state["physicalBusOutputObserved"]=True
    emit(page,"arMixConsoleSnapshot",state)
    assert main.locator('[data-draft="true"]').count()==0
    assert main.locator(".armix-patch-cell").nth(4).get_attribute("data-mark")=="L"
    assert monitor.locator(".armix-patch-cell").nth(2).get_attribute("data-mark")=="L"
    assert state["device"]["enabledOutputs"]==list(range(8))
    # Freed previous MAIN pair becomes a candidate for existing MONITOR.
    select=monitor.get_by_label("MONITOR assignment",exact=True)
    choices=select.locator("option").all_text_contents()
    assert any(x.startswith("OUT 1/2") for x in choices),choices
    assert not any(x.startswith(("OUT 5/6","OUT 7/8")) for x in choices),choices
    assert not page.evaluate("()=>window.__actions.some(x=>x.action==='audioDeviceApply')")
    assert not errors,errors

def verify_logical_outputs(page,out):
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.confirm=()=>true;
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",captureDevice="Test ASIO",playbackDevice="Test ASIO",
        outputArmed=True,enabledOutputs=list(range(6)),
        outputEndpoints=[{"id":"out:"+str(n),"port":n,"callback":n,
            "label":"ASIO "+str(n+1),"active":True} for n in range(6)])
    state.update(physicalMainOutputs=[0,1],physicalMonitorOutputs=[],
        externalOutputs=[],externalRevision="2",physicalBusPatchSupported=True,
        physicalBusOutputObserved=True,physicalBusSecondaryArmed=False,operatorOutputMuted=False,
        logicalOutputRegistry={"revision":"1","ports":[],"references":[]})
    emit(page,"arMixConsoleSnapshot",state)
    open_verified_audio_io(page,state,"routing")
    page.locator("[data-audio-hardware-routing] > summary").click()
    title="Session logical outputs"
    page.get_by_text(title,exact=True).click()
    name=page.get_by_label("Logical output name",exact=True)
    assert name.is_visible() and name.is_disabled()
    # Close with a real mouse click, then reopen in the same task as the
    # native mute readback that previously made the editor disappear.
    page.get_by_text(title,exact=True).click()
    assert not name.is_visible()
    state["device"]["outputArmed"]=False
    state["operatorOutputMuted"]=True
    toggle_then_snapshot(page,title,state)
    assert name.is_visible() and name.is_enabled()
    name.fill("Stage Amp")
    page.get_by_role("button",name="Add port",exact=True).click()
    req=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingLogicalOutputEdit').at(-1)")
    assert req["operation"]=="ADD" and req["logicalRevision"]=="1"
    portid="9007199254740999"
    rev=state["graph"]["revision"]
    emit(page,"arMixLogicalOutputAck",{"requestId":req["requestId"],"ok":True,
        "logicalRevision":"2","graphRevision":rev})
    select=page.get_by_label("Logical output port",exact=True)
    assert select.locator("option[value='"+portid+"']").count()==0
    port={"id":portid,"name":"Stage Amp","format":"STEREO","bound":False}
    state["logicalOutputRegistry"]={"revision":"2","ports":[port],"references":[]}
    emit(page,"arMixConsoleSnapshot",state)
    select.select_option(portid)
    page.get_by_label("Logical output stereo pair",exact=True).select_option("4")
    page.get_by_role("button",name="Apply output pair",exact=True).click()
    bind=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingLogicalOutputEdit').at(-1)")
    assert bind["operation"]=="BIND" and bind["confirmed"] is True
    assert (bind["left"],bind["right"],bind["leftName"],bind["rightName"])==(4,5,"ASIO 5","ASIO 6")
    emit(page,"arMixLogicalOutputAck",{"requestId":bind["requestId"],"ok":True,
        "logicalRevision":"3","graphRevision":rev})
    port.update(bound=True,left=4,right=5,leftName="ASIO 5",rightName="ASIO 6",
        backend="ASIO",playback="Test ASIO")
    state["logicalOutputRegistry"]={"revision":"3","ports":[port],"references":[]}
    emit(page,"arMixConsoleSnapshot",state)
    channel=page.get_by_label("Logical output channel",exact=True)
    channel.select_option(SOURCE)
    page.get_by_role("button",name="Assign to channel",exact=True).click()
    assign=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingLogicalOutputEdit').at(-1)")
    assert assign["operation"]=="ASSIGN" and assign["channelId"]==SOURCE and assign["portId"]==portid
    emit(page,"arMixLogicalOutputAck",{"requestId":assign["requestId"],"ok":False,"error":"fixture reject"})
    assert channel.is_enabled()
    assert "Stage Amp" not in channel.locator("option[value='"+SOURCE+"']").inner_text()
    page.get_by_role("button",name="Assign to channel",exact=True).click()
    assign=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingLogicalOutputEdit').at(-1)")
    new_rev=str(int(rev)+1)
    emit(page,"arMixLogicalOutputAck",{"requestId":assign["requestId"],"ok":True,
        "logicalRevision":"4","graphRevision":new_rev})
    assert "Stage Amp" not in channel.locator("option[value='"+SOURCE+"']").inner_text()
    state["graph"]["revision"]=new_rev
    state["mixerSession"]["routeRevision"]=new_rev
    state["mixerSession"]["channels"]=copy.deepcopy(state["graph"]["channels"])
    state["logicalOutputRegistry"]={"revision":"4","ports":[port],
        "references":[{"channelId":SOURCE,"portId":portid,"role":"EXTERNAL","state":"PENDING"}]}
    state["externalOutputs"]=[{"channelId":SOURCE,"physicalLeft":4,"physicalRight":5,"status":"PENDING"}]
    state["externalRevision"]=new_rev
    emit(page,"arMixConsoleSnapshot",state)
    assert "Stage Amp" in channel.locator("option[value='"+SOURCE+"']").inner_text()
    row=page.locator("[data-audio-output-grid] tbody tr").filter(has_text="CH03")
    assert row.get_by_label("CH03 assignment",exact=True).count()==0
    assert row.locator(".armix-patch-cell").nth(4).get_attribute("data-mark")=="L"
    assert not page.evaluate("()=>window.__actions.some(x=>x.action==='routingPhysicalExternalOutput')")
    assert not errors,errors

def verify_io_defaults(page,out):
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.confirm=()=>true;
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",captureDevice="Test ASIO",playbackDevice="Test ASIO",
        outputArmed=False,enabledOutputs=list(range(6)),
        outputEndpoints=[{"id":"out:"+str(n),"port":n,"callback":n,
            "label":"ASIO "+str(n+1),"active":True} for n in range(6)])
    port={"id":"9007199254740999","name":"Stage Amp","format":"STEREO","bound":False}
    state.update(physicalMainOutputs=[0,1],physicalMonitorOutputs=[],externalOutputs=[],
        externalRevision="2",physicalBusPatchSupported=True,physicalBusOutputObserved=True,
        physicalBusSecondaryArmed=False,operatorOutputMuted=True,
        logicalInputRegistry={"revision":"1","ports":[],"references":[]},
        logicalOutputRegistry={"revision":"2","ports":[port],"references":[]},
        ioDefaults={"revision":"0","readOnly":False,"canLoad":False,"inputs":0,"outputs":0})
    emit(page,"arMixConsoleSnapshot",state)
    open_verified_audio_io(page,state,"routing")
    page.locator("[data-audio-hardware-routing] > summary").click()
    save=page.get_by_role("button",name="Make Default",exact=True)
    load=page.get_by_role("button",name="Load Default",exact=True)
    clear=page.get_by_role("button",name="Clear Default",exact=True)
    assert save.is_enabled() and load.is_disabled() and clear.is_disabled()
    save.click()
    req=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioIoDefaults').at(-1)")
    assert req["operation"]=="SAVE" and req["revision"]=="0" and req["outputRevision"]=="2"
    assert req["confirmed"] is True and "ports" not in req
    emit(page,"arMixIoDefaultsAck",{"requestId":req["requestId"],"ok":True,"revision":"1",
        "inputRevision":"1","outputRevision":"2"})
    assert save.is_disabled() and clear.is_disabled()
    state["ioDefaults"]={"revision":"1","readOnly":False,"canLoad":False,"inputs":0,"outputs":1}
    emit(page,"arMixConsoleSnapshot",state)
    assert save.is_enabled() and clear.is_enabled() and load.is_disabled()
    # Native authority reports a pristine empty mixer; no browser inference.
    state["logicalOutputRegistry"]={"revision":"3","ports":[],"references":[]}
    state["ioDefaults"]["canLoad"]=True
    emit(page,"arMixConsoleSnapshot",state)
    assert save.is_disabled() and load.is_enabled()
    load.click()
    req=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioIoDefaults').at(-1)")
    assert req["operation"]=="LOAD" and req["revision"]=="1" and req["outputRevision"]=="3"
    emit(page,"arMixIoDefaultsAck",{"requestId":req["requestId"],"ok":True,"revision":"1",
        "inputRevision":"2","outputRevision":"4"})
    assert load.is_disabled() and save.is_disabled()
    state["logicalInputRegistry"]["revision"]="2"
    state["logicalOutputRegistry"]={"revision":"4","ports":[port],"references":[]}
    state["ioDefaults"]["canLoad"]=False
    emit(page,"arMixConsoleSnapshot",state)
    assert save.is_enabled() and load.is_disabled()
    clear.click()
    req=page.evaluate("()=>window.__actions.filter(x=>x.action==='audioIoDefaults').at(-1)")
    emit(page,"arMixIoDefaultsAck",{"requestId":req["requestId"],"ok":False,"error":"fixture reject"})
    assert clear.is_enabled()
    assert not page.evaluate("()=>window.__actions.some(x=>x.action==='routingLogicalOutputEdit')")
    assert not errors,errors

def verify_additional_output(page,out):
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods}};""")
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.goto((out / "index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",outputArmed=True,
        enabledOutputs=list(range(8)),
        outputEndpoints=[{"id":"out:"+str(i),"port":i,"callback":i,
                          "label":"Maono "+str(i+1),"active":True} for i in range(8)])
    state.update(physicalMainOutputs=[0,1],physicalMonitorOutputs=[],
                 externalRevision="2",externalOutputs=[])
    emit(page,"arMixConsoleSnapshot",state)
    live={"monitor":True,"directRouteAuthorized":True,"adaptiveClockActive":False,
          "diagnosticRoute":"NORMAL","dryRoutingLab":{"hasAcceptedRoute":True},
          "secondaryOutputReady":False,"secondaryOutputArmed":False}
    emit(page,"arMixStatus",live)
    page.locator("[data-native-extra-output]").click()
    panel=page.get_by_role("dialog",name="Additional Output")
    assert panel.is_visible()
    assert page.evaluate("()=>typeof window.__arMixOpenAdditionalOutput")== "undefined"
    inventory_req=page.evaluate("()=>window.__actions.at(-1)")
    assert inventory_req["action"]=="secondaryOutputInventory"
    emit(page,"arMixSecondaryOutputInventory",{"requestId":"foreign","outputs":["Speakers"]})
    assert panel.get_by_role("button",name="Enable output",exact=True).is_disabled()
    emit(page,"arMixSecondaryOutputInventory",{"requestId":inventory_req["requestId"],"outputs":["CABLE Input","Speakers"]})
    assert panel.get_by_label("Windows playback device").input_value()=="CABLE Input"
    # Same list, different verified preference must update selection.
    panel.get_by_role("button",name="Refresh").click()
    inventory_req=page.evaluate("()=>window.__actions.at(-1)")
    assert inventory_req["force"] is True
    emit(page,"arMixSecondaryOutputInventory",{"requestId":inventory_req["requestId"],
         "outputs":["CABLE Input","Speakers"],"preferredOutput":"Speakers","hasSavedOutput":True})
    assert panel.get_by_label("Windows playback device").input_value()=="Speakers"
    panel.get_by_role("button",name="Refresh").click()
    inventory_req=page.evaluate("()=>window.__actions.at(-1)")
    emit(page,"arMixSecondaryOutputInventory",{"requestId":inventory_req["requestId"],
         "outputs":["CABLE Input","Speakers"],"preferredOutput":"","hasSavedOutput":True})
    assert panel.get_by_label("Windows playback device").input_value()==""
    assert panel.get_by_role("button",name="Enable output",exact=True).is_disabled()
    panel.get_by_label("Windows playback device").select_option("CABLE Input")
    panel.get_by_role("button",name="Enable output",exact=True).click()
    req=page.evaluate("()=>window.__actions.filter(a=>a.action==='secondaryOutputEnable').at(-1)")
    assert req["value"]=="CABLE Input" and req["requestId"]
    assert req["inventoryId"]==inventory_req["requestId"]
    emit(page,"arMixSecondaryOutputAck",{"requestId":"wrong","action":req["action"],"ok":False})
    assert panel.get_by_role("button",name="Enable output",exact=True).is_disabled()
    emit(page,"arMixSecondaryOutputAck",{"requestId":req["requestId"],"action":req["action"],"ok":False,"error":"Endpoint rejected"})
    assert panel.get_by_role("alert").inner_text()=="Endpoint rejected"
    panel.get_by_role("button",name="Enable output",exact=True).click()
    req=page.evaluate("()=>window.__actions.filter(a=>a.action==='secondaryOutputEnable').at(-1)")
    emit(page,"arMixSecondaryOutputAck",{"requestId":req["requestId"],"action":req["action"],"ok":True})
    assert panel.get_by_role("button",name="Enable output",exact=True).is_disabled()
    live.update(secondaryOutputReady=True,secondaryOutputName="CABLE Input")
    emit(page,"arMixStatus",live)
    assert panel.get_by_role("button",name="Enable output",exact=True).is_disabled()
    live["secondaryOutputArmed"]=True
    emit(page,"arMixStatus",live)
    assert panel.get_by_role("button",name="Disconnect").is_enabled()
    assert not panel.get_by_role("button",name="Separate headphone mix").is_visible()
    assert page.evaluate("()=>window.__actions.filter(a=>['monitor','secondaryOutputArm'].includes(a.action)).length")==0
    panel.get_by_role("button",name="Close Additional Output").click()
    # Audio I/O uses the same control instance inline, without another dialog.
    inventory=open_verified_audio_io(page,state,"routing")
    inline=page.locator("[data-audio-extra-output]")
    assert inline.get_by_label("Windows playback device").is_visible()
    assert not page.locator("[data-audio-hardware-routing]").evaluate("(e)=>e.open")
    assert inline.get_by_role("button",name="Disconnect").is_enabled()
    assert not panel.is_visible()
    assert page.locator("[data-extra-device]").count()==1
    assert page.locator("[data-audio-output-matrix]").get_by_text("MAIN MIRROR",exact=False).is_visible()
    # Fault recovery is native-owned; a lost sink must not look enabled.
    live.update(secondaryOutputReady=False,secondaryOutputArmed=False,
                secondaryOutputFault=True,secondaryOutputRecovering=True,
                secondaryOutputRecoveryName="CABLE Input")
    emit(page,"arMixStatus",live)
    assert inline.locator("[data-extra-status]").inner_text().startswith("Reconnecting")
    assert inline.get_by_role("button",name="Enable output",exact=True).is_disabled()
    assert inline.get_by_role("button",name="Disconnect").is_enabled()
    assert page.locator("[data-audio-output-matrix]").get_by_text("RECOVERING",exact=True).is_visible()
    live.update(secondaryOutputReady=True,secondaryOutputArmed=True,
                secondaryOutputFault=False,secondaryOutputRecovering=False)
    emit(page,"arMixStatus",live)
    assert inline.locator("[data-extra-status]").inner_text().startswith("Enabled")
    # Disconnect is also available during a later recovery.
    live.update(secondaryOutputReady=False,secondaryOutputArmed=False,
                secondaryOutputFault=True,secondaryOutputRecovering=True)
    emit(page,"arMixStatus",live)
    inline.get_by_role("button",name="Disconnect").click()
    req=page.evaluate("()=>window.__actions.filter(a=>a.action==='secondaryOutputStop').at(-1)")
    emit(page,"arMixSecondaryOutputAck",{"requestId":req["requestId"],"action":req["action"],"ok":True})
    live.update(secondaryOutputReady=False,secondaryOutputArmed=False,secondaryOutputName="",
                secondaryOutputFault=False,secondaryOutputRecovering=False,secondaryOutputRecoveryName="")
    emit(page,"arMixStatus",live)
    assert inline.get_by_role("button",name="Disconnect").is_disabled()
    assert not page.locator("[data-audio-output-matrix]").get_by_text("MAIN MIRROR",exact=False).count()
    live["directRouteAuthorized"]=False
    emit(page,"arMixStatus",live)
    assert inline.get_by_role("button",name="Set up audio").is_visible()
    inline.get_by_role("button",name="Set up audio").click()
    assert page.locator('button[data-audio-tab="device"]').get_attribute("aria-selected")=="true"
    live["directRouteAuthorized"]=True
    emit(page,"arMixStatus",live)
    inventory["active"]["hasControlPanel"]=True
    emit(page,"arMixAudioDeviceInventory",inventory)
    page.locator('button[data-audio-tab="device"]').click()
    page.locator("[data-audio-driver-panel]").click()
    req=page.evaluate("()=>window.__actions.filter(a=>a.action==='audioDriverPanel').at(-1)")
    assert req["driver"]==inventory["active"]["playbackDevice"]
    assert req["deviceGeneration"]==inventory["active"]["deviceGeneration"]
    assert page.evaluate("()=>window.__actions.filter(a=>a.action==='audioSetup').length")==0
    assert not errors,errors


def verify_independent_output(page,out,legacy_empty=False):
    """Two-pin native fixture: one mode action, no manual mute/arm ritual."""
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods}};""")
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    state["device"].update(backend="ASIO",captureDevice="Test ASIO",playbackDevice="Test ASIO",
        outputArmed=True,enabledOutputs=[0,1],
        outputEndpoints=[{"id":"out:"+str(i),"port":i,"callback":i,
            "label":"ASIO "+str(i+1),"active":True} for i in range(2)])
    state["graph"]["capabilities"]["supportedOps"].append("SET_SEND_ENABLED")
    state.update(physicalMainOutputs=[0,1],physicalMonitorOutputs=[],externalOutputs=[],
                 physicalBusPatchSupported=True,physicalBusOutputObserved=True,physicalBusSecondaryArmed=True,
                 secondaryMainOnly=False,externalRevision="2")
    emit(page,"arMixConsoleSnapshot",state)
    live={"monitor":True,"directRouteAuthorized":True,"adaptiveClockActive":False,
          "diagnosticRoute":"NORMAL","dryRoutingLab":{"hasAcceptedRoute":True,"busOutputObserved":True,
          "secondaryMainOnly":False,"epoch":state["device"]["epoch"],"nativeBus":{"graphRevision":"2"}},
          "secondaryOutputReady":True,"secondaryOutputArmed":True,"secondaryOutputName":"CABLE Input"}
    emit(page,"arMixStatus",live)
    page.locator("[data-native-extra-output]").click()
    panel=page.get_by_role("dialog",name="Additional Output")
    req=page.evaluate("()=>window.__actions.at(-1)")
    emit(page,"arMixSecondaryOutputInventory",{"requestId":req["requestId"],"outputs":["CABLE Input"]})
    button=panel.get_by_role("button",name="Separate headphone mix")
    assert button.is_enabled()
    button.click()
    req=page.evaluate("()=>window.__actions.at(-1)")
    assert req["action"]=="secondaryOutputSplit"
    assert req["value"]=={"enabled":True,"smart":True,"deviceEpoch":state["device"]["epoch"],
        "graphRevision":"2","rosterRevision":"2"}
    emit(page,"arMixSecondaryOutputAck",{"requestId":"foreign","action":req["action"],"ok":False})
    assert button.is_disabled()
    emit(page,"arMixSecondaryOutputAck",{"requestId":req["requestId"],"action":req["action"],"ok":True,"graphRevision":"3"})
    assert button.is_disabled()
    live["dryRoutingLab"].update(secondaryMainOnly=True,busOutputObserved=False)
    live.update(secondaryOutputArmed=False,secondaryOutputRecovering=True,secondaryOutputRecoveryName="CABLE Input")
    emit(page,"arMixStatus",live)
    assert panel.get_by_role("button",name="Use same mix").is_disabled()
    assert panel.get_by_role("button",name="Disconnect").is_enabled()
    # Even observed live status with new graph revision cannot complete
    # until the accepted console snapshot and physical pair arrive.
    live["dryRoutingLab"].update(busOutputObserved=True,nativeBus={"graphRevision":"3"})
    live.update(secondaryOutputArmed=True,secondaryOutputRecovering=False)
    emit(page,"arMixStatus",live)
    assert panel.get_by_role("button",name="Use same mix").is_disabled()
    state.update(secondaryMainOnly=True,physicalMainOutputs=[],physicalMonitorOutputs=[0,1])
    state["graph"].update(revision="3",rosterRevision="3")
    monitor_id="987654321"
    monitor={"id":monitor_id,"name":"MONITOR","role":"MONITOR","processingFormat":"STEREO",
             "controls":{"faderDb":0,"pan":0,"mute":False,"solo":False},"inserts":[],"sends":[]}
    state["graph"]["channels"].append(monitor)
    state["graph"]["destinations"].append({k:monitor[k] for k in ("id","role","name")})
    monitor_edge={"edgeId":"9223372036854775900","from":SOURCE,"to":monitor_id,
        "primaryOutput":False,"gainLinear":.25,"tap":"PRE_FADER","enabled":True,"sendEnabled":True,"signalKind":"AUDIO"}
    if not legacy_empty:
        state["graph"]["rosterRevision"]="4" # creation + starter in one graph publication
        state["graph"]["edges"].append(monitor_edge)
    state["mixerSession"].update(routeRevision="3",rosterRevision=state["graph"]["rosterRevision"],
        channels=copy.deepcopy(state["graph"]["channels"]))
    emit(page,"arMixConsoleSnapshot",state)
    # The preceding status is fresh; accepted snapshot settles without
    # waiting for another event, including graph projection.
    assert panel.get_by_role("button",name="Use same mix").is_enabled()
    assert panel.locator("[data-extra-status]").inner_text().startswith("Enabled")
    starter=panel.get_by_role("button",name="Create headphone mix")
    if legacy_empty:
        assert starter.is_enabled()
        assert "PRE-fader" in panel.locator("[data-extra-mix-hint]").inner_text()
        starter.click()
        req=page.evaluate("()=>window.__actions.at(-1)")
        assert req["action"]=="secondaryMonitorStarter" and req["value"]["graphRevision"]=="3"
        emit(page,"arMixSecondaryOutputAck",{"requestId":req["requestId"],"action":req["action"],"ok":True,"graphRevision":"4"})
        assert starter.is_disabled()
        state["graph"].update(revision="4",rosterRevision="4")
        state["graph"]["edges"].append(monitor_edge)
        state["mixerSession"].update(routeRevision="4",rosterRevision="4")
        emit(page,"arMixConsoleSnapshot",state)
        live["dryRoutingLab"]["nativeBus"]["graphRevision"]="4"
        emit(page,"arMixStatus",live)
    else:
        assert page.evaluate("()=>window.__actions.filter(a=>a.action==='secondaryMonitorStarter').length")==0
    assert not starter.is_visible()
    assert panel.get_by_role("button",name="Use same mix").is_enabled()
    assert "preserved" in panel.locator("[data-extra-mix-hint]").inner_text()
    # Destination navigation is the existing SOF, not a second mixer or native write.
    edit=panel.get_by_role("button",name="Edit headphone mix")
    assert edit.is_enabled()
    before=page.evaluate("()=>window.__actions.length")
    edit.click()
    assert not panel.is_visible()
    assert page.locator("#mixContext").inner_text()=="MONITOR MIX"
    assert page.evaluate("()=>window.__actions.length")==before
    page.locator("#sofToggle").click()
    assert page.locator("#mixContext").inner_text()=="MAIN MIX"
    page.locator("[data-native-extra-output]").click()
    panel.get_by_role("button",name="Use same mix").click()
    same=page.evaluate("()=>window.__actions.at(-1)")
    assert same["action"]=="secondaryOutputSplit" and same["value"]["enabled"] is False
    next_rev=str(int(state["graph"]["revision"])+1)
    emit(page,"arMixSecondaryOutputAck",{"requestId":same["requestId"],"action":same["action"],"ok":True,"graphRevision":next_rev})
    state.update(secondaryMainOnly=False,physicalMainOutputs=[0,1],physicalMonitorOutputs=[])
    state["graph"]["revision"]=next_rev
    state["mixerSession"]["routeRevision"]=next_rev
    emit(page,"arMixConsoleSnapshot",state)
    live["dryRoutingLab"].update(secondaryMainOnly=False,nativeBus={"graphRevision":next_rev})
    emit(page,"arMixStatus",live)
    assert panel.get_by_role("button",name="Separate headphone mix").is_enabled()
    assert not panel.get_by_role("button",name="Edit headphone mix").is_visible()
    # Re-enter Separate: no starter transaction and no change to the authored send.
    panel.get_by_role("button",name="Separate headphone mix").click()
    again=page.evaluate("()=>window.__actions.at(-1)")
    assert again["action"]=="secondaryOutputSplit" and again["value"]["enabled"] is True
    next_rev=str(int(next_rev)+1)
    state.update(secondaryMainOnly=True,physicalMainOutputs=[],physicalMonitorOutputs=[0,1])
    state["graph"]["revision"]=next_rev
    state["mixerSession"]["routeRevision"]=next_rev
    emit(page,"arMixConsoleSnapshot",state)
    live["dryRoutingLab"].update(secondaryMainOnly=True,nativeBus={"graphRevision":next_rev})
    emit(page,"arMixStatus",live)
    assert panel.get_by_role("button",name="Use same mix").is_disabled()
    # ACK may be last; no extra status cycle is necessary.
    emit(page,"arMixSecondaryOutputAck",{"requestId":again["requestId"],"action":again["action"],"ok":True,"graphRevision":next_rev})
    assert panel.get_by_role("button",name="Use same mix").is_enabled()
    assert not panel.get_by_role("button",name="Create headphone mix").is_visible()
    assert panel.get_by_role("button",name="Edit headphone mix").is_enabled()
    assert any(e==monitor_edge for e in state["graph"]["edges"])
    assert page.evaluate("()=>window.__actions.filter(a=>a.action==='secondaryMonitorStarter').length")==int(legacy_empty)
    assert page.evaluate("()=>window.__actions.filter(a=>['monitor','secondaryOutputArm'].includes(a.action)).length")==0
    assert not errors,errors


def verify_first_sound(page,out,single=False):
    page.add_init_script(NATIVE_EVENTS + """window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        ...window.__nativeListenerMethods}};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    state=snapshot()
    state["productionCallbackLive"]=True
    pins=[2] if single else [0,2]
    state["device"].update(backend="ASIO",captureDevice="Test ASIO",playbackDevice="Test ASIO",
        outputArmed=False,enabledInputs=pins,inputEndpoints=[
            {"id":"in:"+str(i),"port":i,"callback":j,"label":"Mic "+str(i+1),"active":True}
            for j,i in enumerate(pins)])
    emit(page,"arMixConsoleSnapshot",state)
    button=page.locator("[data-native-first-sound]")
    assert button.is_visible()
    assert button.is_enabled(), "First accepted projection must enable input without a second snapshot"
    # Opening the mixer must not automatically patch a microphone.
    assert page.evaluate("()=>window.__actions.filter(a=>a.action==='routingNativeSourcePatch').length")==0
    button.click()
    dialog=page.get_by_role("dialog",name="Connect microphone")
    if single:
        assert not dialog.is_visible(), "Unambiguous input is one explicit click, no extra wizard"
    else:
        connect=dialog.get_by_role("button",name="Connect",exact=True)
        assert connect.is_disabled()
        dialog.get_by_label("Microphone input").select_option("2")
        connect.click()
    assert page.evaluate("()=>window.__actions.filter(a=>a.action==='routingNativeSourcePatch').length")==1
    req=page.evaluate("()=>window.__actions.filter(a=>a.action==='routingNativeSourcePatch').at(-1)")
    assert req["channelId"]==SOURCE and req["left"]==2 and req["mode"]=="MONO"
    assert button.is_disabled()
    emit(page,"arMixNativeSourceAck",{"requestId":req["requestId"],"channelId":SOURCE,"ok":True,"graphRevision":"3"})
    state["graph"].update(revision="3")
    state["graph"]["channels"][0]["source"]={"sourceFormat":"MONO","portId":"in:2"}
    state["mixerSession"].update(routeRevision="3",channels=copy.deepcopy(state["graph"]["channels"]))
    emit(page,"arMixConsoleSnapshot",state)
    assert button.get_attribute("hidden") is not None, "Native source readback must retire first-sound plan"
    assert not button.is_visible(), "Hidden action must not occupy a premium toolbar hitbox"
    assert page.evaluate("()=>window.__actions.filter(a=>a.action==='monitor').length")==0


def main():
    with tempfile.TemporaryDirectory(prefix="armix-audio-io-") as tmp,sync_playwright() as p:
        out=Path(tmp)
        bundle.stage(out)
        browser=p.chromium.launch(headless=True)
        for width,height in [(1366,768),(1024,576)]:
            page=browser.new_page(viewport={"width":width,"height":height})
            verify_custom_input(page,out)
            page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_external_output(page,out)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_additional_output(page,out)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_independent_output(page,out)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_independent_output(page,out,legacy_empty=True)
        page.close()
        page=browser.new_page(viewport={"width":1024,"height":576})
        verify_first_sound(page,out)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":768})
        verify_first_sound(page,out,single=True)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_bus_remap(page,out)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_logical_inputs(page,out)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_logical_outputs(page,out)
        page.close()
        page=browser.new_page(viewport={"width":1366,"height":900})
        verify_io_defaults(page,out)
        page.close()
        browser.close()
    print("PASS compiled Audio I/O pin discovery, stereo draft/ACK/readback, external output, muted bus remap, logical disclosure race and defaults")

if __name__=="__main__":main()
