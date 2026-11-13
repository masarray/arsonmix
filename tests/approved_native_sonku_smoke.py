#!/usr/bin/env python3
"""8D-SonKu real compiled WebView2 GUI: native insert -> ACK -> readback -> editor.
Transport is emulated, never PCM. Native DSP is independently tested by CTest.
The test does not invoke private mixer state APIs to fake accepted inserts.
"""
import copy
import importlib.util
from pathlib import Path
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit, ack, SOURCE

ROOT=Path(__file__).resolve().parents[1]
CATALOG="armix.builtin.8d-sonku.v1"
INSTANCE="sonku-accepted-instance"
SETTINGS={
    "startAzimuthDegrees":0.0,
    "revolutionSeconds":12.0,
    "stereoSpreadDegrees":60.0,
    "spatialMix":1.0,
    "outputTrim":0.75,
    "moving":True,
    "clockwise":True,
}

def last_action(page,name):
    actions=page.evaluate(
        "name=>window.__actions.filter(a=>a.action===name)",name)
    assert actions, "Expected actual native action "+name
    return actions[-1]

def accepted_snapshot(page,state,settings,applied=None):
    read=last_action(page,"nativeSonKuSnapshot")
    payload={
        "requestId":read["requestId"],"status":"ACCEPTED","schemaVersion":1,
        "channelId":SOURCE,"slot":0,"instanceId":INSTANCE,
        "deviceEpoch":state["device"]["epoch"],
        "graphRevision":state["graph"]["revision"],
        "observedAzimuthDegrees":settings["startAzimuthDegrees"],
        "settings":copy.deepcopy(settings),
    }
    if applied:payload["appliedRequestId"]=applied
    emit(page,"arMixBuiltinSonKuSnapshot",payload)

def exercise(browser,out,width,height):
    page=browser.new_page(viewport={"width":width,"height":height})
    failures=[]
    page.on("pageerror",lambda exc:failures.append(str(exc)))
    page.add_init_script("""window.__nativeListeners=new Map();
      window.__nativeIntents=[];window.__actions=[];
      window.__JUCE__={backend:{
        emitEvent(name,payload){
          if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
          if(name==='arMixAction')window.__actions.push(payload);
        },
        addEventListener(name,callback){
          const prior=window.__nativeListeners.get(name);
          window.__nativeListeners.set(name,prior?
            packet=>{prior(packet);callback(packet)}:callback);
        }
      }};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function(
        "()=>window.__ARMIX_NATIVE_M2__?.enabled && window.ArSonMixNativeSonKu",
        timeout=15000)
    state=snapshot()
    state["graph"]["capabilities"]["supportedOps"]+=["LOAD_INSERT","REMOVE_INSERT"]
    emit(page,"arMixConsoleSnapshot",state)
    if width<500:page.locator("#bankSelect").select_option("0")
    strip=page.locator('#tracks .strip[data-id="1"]')
    strip.locator(".fx-pick.empty").first.click()
    picker=page.locator("#fastResults [data-fast-native-sonku]")
    picker.wait_for(state="visible")
    assert picker.count()==1,"8D-SonKu picker duplicated or missing"
    assert page.locator("#mxNativeSonKu").is_hidden(),"Editor opened before native ACK"
    picker.click()
    requests=page.evaluate(
        "()=>window.__nativeIntents.filter(x=>x.operation==='LOAD_INSERT')")
    assert len(requests)==1,requests
    req=requests[0]
    assert req["payload"]["catalogId"]==CATALOG,req
    assert page.locator("#mxNativeSonKu").is_hidden(),"Editor opened on intent only"
    state["graph"].update(revision="3",rosterRevision="3")
    state["graph"]["channels"][0]["inserts"].append({
        "id":INSTANCE,"catalogId":CATALOG,"name":"8D-SonKu","bypassed":False})
    state["mixerSession"].update(routeRevision="3",rosterRevision="3",
        channels=copy.deepcopy(state["graph"]["channels"]))
    ack(page,state,req)
    # Acceptance alone does not paint active DSP; readback authorizes editor.
    assert page.locator("#mxNativeSonKu").is_hidden()
    emit(page,"arMixConsoleSnapshot",state)
    editor=page.locator("#mxNativeSonKu")
    editor.wait_for(state="visible",timeout=5000)
    assert editor.locator("#skMap .sk-src").count()==2
    art=editor.locator("#skListenerArt")
    assert art.count()==1 and art.get_attribute("href")=="assets/sonku-listener-top.png"
    # Image is actually available inside the assembled native GUI bundle.
    assert page.evaluate("""async () => new Promise(resolve => {
      const image=new Image();
      image.onload=()=>resolve(image.naturalWidth>=128 && image.naturalHeight>=128);
      image.onerror=()=>resolve(false);
      image.src='assets/sonku-listener-top.png';
    })"""), "Listener PNG did not load"
    labels=editor.locator("#skMap .sk-direction").all_inner_texts()
    assert labels==["REAR","FRONT","RIGHT","LEFT"],labels
    assert editor.locator("#skListenerArt").get_attribute("pointer-events")=="none"
    assert editor.locator("#skMotion").inner_text()=="ORBIT"
    assert editor.locator("#skDirection").inner_text()=="CLOCKWISE"
    assert editor.locator("#skMode").inner_text()=="NATIVE HRTF"
    # 250ms visualization timer must not overwrite an unacknowledged
    # snapshot request (real JUCE message thread can be temporarily busy).
    count=page.evaluate(
        "()=>window.__actions.filter(a=>a.action==='nativeSonKuSnapshot').length")
    page.wait_for_timeout(700)
    after=page.evaluate(
        "()=>window.__actions.filter(a=>a.action==='nativeSonKuSnapshot').length")
    assert after==count, "Unanswered SonKu readback was overwritten"
    accepted_snapshot(page,state,SETTINGS)
    # Close/reopen must use the installed FX rack click, never a private open().
    editor.locator("#skClose").click()
    assert editor.is_hidden()
    row=strip.locator('.fxslot-row[data-insert-id="'+INSTANCE+'"]')
    row.wait_for(state="visible")
    row.locator('[data-action="openPlugin"]').click()
    editor.wait_for(state="visible")
    accepted_snapshot(page,state,SETTINGS)
    editor.locator("#skMotion").click()
    command=last_action(page,"nativeSonKuSet")
    assert command["settings"]["moving"] is False,command
    assert command["instanceId"]==INSTANCE and command["channelId"]==SOURCE
    emit(page,"arMixBuiltinSonKuQueue",{
        "requestId":command["requestId"],"status":"QUEUED"})
    emit(page,"arMixBuiltinSonKuAck",{
        "requestId":command["requestId"],"status":"APPLIED"})
    accepted_snapshot(page,state,{**SETTINGS,"moving":False},
        applied=command["requestId"])
    assert editor.locator("#skMotion").inner_text()=="STATIC"
    assert "ACCEPTED" in editor.locator("#skHint").inner_text()
    # Verify all four acoustic directions against the human figure:
    # face points DOWN; anatomical RIGHT appears screen LEFT.
    def pose(degrees):
        page.wait_for_timeout(270)
        settings={**SETTINGS,"moving":False,"stereoSpreadDegrees":0.0,
                  "startAzimuthDegrees":float(degrees)}
        accepted_snapshot(page,state,settings)
        return (
            float(editor.locator('.sk-src[data-index="0"] .sk-source-bg').get_attribute("cx")),
            float(editor.locator('.sk-src[data-index="0"] .sk-source-bg').get_attribute("cy"))
        )
    x,y=pose(0);assert abs(x-200)<1 and y>270,("FRONT",x,y)
    x,y=pose(90);assert x<90 and abs(y-165)<1,("RIGHT",x,y)
    x,y=pose(180);assert abs(x-200)<1 and y<60,("REAR",x,y)
    x,y=pose(-90);assert x>310 and abs(y-165)<1,("LEFT",x,y)
    # Actual pointer drag uses physical stage coordinates: screen bottom
    # means 0° FRONT, not a mirrored/right-facing listener. No simulated
    # DSP authority: the browser must emit a native settings request.
    pose(-90)
    svg=editor.locator("#skMap").bounding_box()
    assert svg
    def screen(x,y):
        return (svg["x"]+x*svg["width"]/400,
                svg["y"]+y*svg["height"]/330)
    sx,sy=screen(320,165)
    ex,ey=screen(200,285)
    page.mouse.move(sx,sy)
    page.mouse.down()
    page.mouse.move(ex,ey,steps=9)
    page.mouse.up()
    dragAction=last_action(page,"nativeSonKuSet")
    assert dragAction["settings"]["moving"] is False
    assert abs(dragAction["settings"]["startAzimuthDegrees"])<5,dragAction
    # Never confuse successful pointer motion with audio-applied ACK.
    emit(page,"arMixBuiltinSonKuAck",{
        "requestId":dragAction["requestId"],"status":"APPLIED"})
    accepted_snapshot(page,state,{**SETTINGS,"moving":False,
        "stereoSpreadDegrees":0.0,"startAzimuthDegrees":0.0},
        applied=dragAction["requestId"])
    bounds=editor.bounding_box()
    assert bounds and bounds["width"]>260 and bounds["width"]<width+2
    # All listener and rendering JS exceptions remain fatal.
    assert not failures,(width,height,failures)
    page.screenshot(path=f"arsonmix-sonku-editor-{width}.png")
    page.close()

def main():
    spec=importlib.util.spec_from_file_location(
        "bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="armix-sonku-") as temp,sync_playwright() as p:
        out=Path(temp);bundle.stage(out)
        browser=p.chromium.launch(headless=True)
        for width,height in [(1366,768),(390,844)]:
            exercise(browser,out,width,height)
        browser.close()
    print("PASS 8D-SonKu actual native editor: insert intent, no premature GUI,"
          " ACK/readback, reopen, typed settings and 2 viewport screenshots."
          " Browser uses a transport mock; native DSP requires CTest and hardware FAT.")

if __name__=="__main__":
    main()
