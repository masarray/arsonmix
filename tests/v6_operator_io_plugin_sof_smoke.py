"""V6 operator actions against the assembled native UI; emulated JUCE transport."""
from pathlib import Path
import importlib.util
import tempfile
from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS
from approved_native_offline_logical_smoke import snapshot, emit

ROOT=Path(__file__).resolve().parents[1]

def exercise(browser,out,width,height,tap):
    page=browser.new_page(viewport={"width":width,"height":height})
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.add_init_script(NATIVE_EVENTS + """window.__events=[];
      window.__JUCE__={backend:{emitEvent(name,payload){window.__events.push({name,payload});},
        ...window.__nativeListenerMethods}};""")
    page.goto((out/"index.html").as_uri())
    page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
    emit(page,"arMixConsoleSnapshot",snapshot(workspace_token="direct-sof"))
    page.wait_for_function("()=>document.documentElement.dataset.armixNativeAuthority==='logical-editable'")
    page.locator("#sofToggle").click()
    page.locator("#sofTarget").select_option(label="AUX A")
    strip=page.locator('#tracks .strip[data-id="1"]')
    pre=strip.get_by_role("button",name="Assign PRE send")
    post=strip.get_by_role("button",name="Assign POST send")
    assert pre.is_enabled() and post.is_enabled()
    assert strip.locator(".mx-sof-tap-key").count()==0
    bounds=strip.bounding_box()
    for button in (pre,post):
        box=button.bounding_box()
        assert box and bounds and box["width"]>=26 and box["height"]>=23
        assert box["x"]>=bounds["x"] and box["x"]+box["width"]<=bounds["x"]+bounds["width"]+1
    assert "NO SEND" in strip.locator(".sof-badge").inner_text()
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixLogicalIntent').length")==0
    (pre if tap=="PRE_FADER" else post).click()
    page.wait_for_function("()=>window.__events.some(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='WIRE_TRANSACTION')")
    request=page.evaluate("()=>window.__events.find(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='WIRE_TRANSACTION').payload")
    assert request["channelId"]=="1" and request["baseRevision"]=="1"
    assert request["payload"]["changes"]==[{"op":"CONNECT","from":"1","to":"11","tap":tap,"gainLinear":0}]
    (post if tap=="PRE_FADER" else pre).click()
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixLogicalIntent'&&e.payload.operation==='WIRE_TRANSACTION').length")==1
    emit(page,"arMixLogicalAck",{"requestId":request["requestId"],"status":"ACCEPTED","errorCode":"NONE","revision":"2","physicalOutputAdmitted":False})
    assert "NO SEND" in strip.locator(".sof-badge").inner_text()
    assert not page.evaluate("()=>window.__events.some(e=>e.name==='arMixConsoleIntent')")
    page.screenshot(path=f"v6-direct-sof-{width}-{tap}.png")
    # New readback is the only event allowed to publish the new send.
    state=snapshot("2",workspace_token="direct-sof")
    edge={"edgeId":"123456","from":"1","to":"11","signalKind":"AUDIO","enabled":True,
        "sendEnabled":True,"gainLinear":0,"primaryOutput":False,
        "tap":"SPLIT_PRE" if tap=="PRE_FADER" else "POST_FADER","tapStage":tap}
    state["logicalMixer"]["graph"]["edges"].append(edge)
    state["logicalMixer"]["graph"]["channels"][0]["sends"].append(edge.copy())
    emit(page,"arMixConsoleSnapshot",state)
    strip.locator('[data-action="sofTap"]').wait_for(state="visible")
    assert strip.locator(".sof-assign-key").count()==0
    page.locator("#sofToggle").click()
    emit(page,"arMixCatalog",{"items":[],"builtIns":[{"id":"armix.builtin.fixture.v1","name":"Fixture processor","category":"Utility"}]})
    page.locator("#pluginsButton").click()
    manager=page.locator("#pluginBrowser")
    assert manager.is_visible()
    assert manager.locator(".plugin-results").evaluate("e=>getComputedStyle(e).backgroundColor")=="rgb(20, 38, 48)"
    assert manager.locator("#pluginScan").evaluate("e=>getComputedStyle(e).backgroundImage")=="none"
    manager.locator("#pluginSearch").fill("zzzz-no-match")
    assert manager.locator(".plugin-empty").is_visible()
    manager.locator("#pluginSearch").fill("")
    assert manager.locator(".plugin-item").count()>0
    page.screenshot(path=f"v6-plugin-manager-{width}.png")
    manager.locator("#pluginClose").click()
    assert manager.is_hidden()
    assert not errors,errors
    page.close()

def main():
    spec=importlib.util.spec_from_file_location("native_bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="v6-operator-") as d:
        out=Path(d);bundle.stage(out)
        with sync_playwright() as pw:
            browser=pw.chromium.launch(headless=True)
            try:
                exercise(browser,out,1366,900,"PRE_FADER")
                exercise(browser,out,1024,768,"POST_FADER")
            finally: browser.close()
    print("PASS V6 direct PRE/POST, no speculative route or duplicate pending intent, manager controls (two viewports)")

if __name__=="__main__":main()
