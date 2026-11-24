"""Targeted real Chromium test of the approved Audio I/O panel.
Mock native transport proves DOM -> intent -> ACK -> installed readback, not PCM.
"""
import copy
from pathlib import Path
import sys
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, SOURCE, open_verified_audio_io, verify_external_output

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
import build_approved_native_gui as bundle

def verify_custom_input(page,out):
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script("""window.__nativeListeners=new Map();window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
        addEventListener(name,callback){window.__nativeListeners.set(name,callback);}
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
    inventory["namedPatches"]={"revision":2,"readOnly":False,"entries":[entry]}
    emit(page,"arMixNamedPatchReadback",{"requestId":rename["requestId"],
        "active":inventory["active"],"namedPatches":inventory["namedPatches"]})
    assert page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').length")==1
    assert row.locator('[data-mark="L"]').count()==1
    page.screenshot(path="audio-io-custom-stereo.png")
    # Preset appears on another input strip without creating a new graph
    # authority. Choose it directly; actual wire payload remains physical.
    page.locator("[data-audio-close]").click()
    direct=page.locator('.strip select[data-field="input"]').filter(has=page.locator('option',has_text="Preset · Studio Vocal")).last
    direct.wait_for(state="visible")
    label=direct.locator('option',has_text="Preset · Studio Vocal").first.get_attribute("value")
    direct.select_option(label)
    second=page.evaluate("()=>window.__actions.filter(x=>x.action==='routingNativeSourcePatch').at(-1)")
    assert second["requestId"]!=req["requestId"] and (second["left"],second["right"])==(3,0),second
    assert not errors,errors

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
        browser.close()
    print("PASS compiled Audio I/O pin discovery, stereo draft/ACK/readback and external output")

if __name__=="__main__":main()
