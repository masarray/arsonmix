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
    open_verified_audio_io(page,state,"inputs")
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
    page.screenshot(path="audio-io-custom-stereo.png")
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
