"""Real assembled Update Center presentation, fixture transport (no install/network/audio)."""
import importlib.util
import tempfile
from pathlib import Path
from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS

ROOT=Path(__file__).resolve().parents[1]
def exercise(browser,dst,width,height):
    page=browser.new_page(viewport={"width":width,"height":height})
    errors=[]
    page.on("pageerror",lambda e:errors.append(str(e)))
    page.add_init_script(NATIVE_EVENTS+"""window.__events=[];window.__updateFixtureStatus='IDLE';
      window.__JUCE__={backend:{emitEvent(name,payload){
        window.__events.push({name,payload});
        if(name==='arMixUpdateCenterIntent'){
          const status=payload.operation==='CHECK'?'CHECKING':window.__updateFixtureStatus;
          queueMicrotask(()=>window.__nativeListeners.get('arMixUpdateCenterState')?.({
            requestId:payload.requestId,version:'0.5.0-alpha.fixture',supported:true,
            manualAllowed:status==='IDLE'||status==='READY'||status==='PORTABLE_READY',status,portable:status==='PORTABLE_READY',
            candidateTag:status==='READY'?'v0.5.0-alpha.fixture-next':''
          }));
        }
      },...window.__nativeListenerMethods}};""")
    page.goto((dst/"index.html").as_uri())
    page.locator("#ribbon-tab-mixer").wait_for(state="visible")
    before_audio=page.evaluate("()=>window.__events.filter(e=>e.name==='arMixConsoleIntent'||e.name==='arMixAction'||e.name==='arMixLogicalIntent').length")
    menu=page.locator("#moreTools")
    menu.locator(":scope > summary").click()
    menu.locator('[data-menu-category="settings"]').click()
    menu.get_by_role("button",name="Update Center",exact=True).click()
    center=page.locator("#armixUpdateCenter")
    assert center.is_visible()
    center.get_by_text("No update result yet.",exact=True).wait_for()
    assert center.get_by_text("Installed version · 0.5.0-alpha.fixture",exact=True).is_visible()
    bounds=center.bounding_box()
    assert bounds and bounds["x"]>=0 and bounds["x"]+bounds["width"]<=width
    assert center.evaluate("e=>!e.matches(':modal')")
    check=center.get_by_role("button",name="Check for updates")
    assert check.is_enabled()
    check.click()
    center.get_by_text("Checking official releases…",exact=True).wait_for()
    assert check.is_disabled()
    page.evaluate("""()=>window.__nativeListeners.get('arMixUpdateCenterState')({
      requestId:'999999999',status:'HEALTHY',supported:true,manualAllowed:true})""")
    assert center.get_by_text("Checking official releases…",exact=True).is_visible()
    page.evaluate("()=>window.__updateFixtureStatus='READY'")
    center.get_by_role("button",name="Review update").wait_for(state="visible")
    assert center.get_by_text("A verified update is ready to review.",exact=True).is_visible()
    page.screenshot(path=f"autonomous-update-center-{width}.png")
    center.get_by_role("button",name="Review update").click()
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixUpdateCenterIntent'&&e.payload.operation==='REVIEW').length")==1
    page.evaluate("()=>window.__updateFixtureStatus='PORTABLE_READY'")
    center.get_by_role("button",name="Open download folder").wait_for(state="visible")
    center.get_by_role("button",name="Open download folder").click()
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixUpdateCenterIntent'&&e.payload.operation==='OPEN_DOWNLOADS').length")==1
    assert center.get_by_text("Portable download only",exact=False).is_visible()
    center.get_by_role("button",name="Close Update Center").click()
    assert center.is_hidden()
    before=page.evaluate("()=>window.__events.filter(e=>e.name==='arMixUpdateCenterIntent').length")
    page.wait_for_timeout(2200)
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixUpdateCenterIntent').length")==before
    assert page.evaluate("()=>window.__events.filter(e=>e.name==='arMixConsoleIntent'||e.name==='arMixAction'||e.name==='arMixLogicalIntent').length")==before_audio
    assert not errors,errors
    page.close()

def main():
    spec=importlib.util.spec_from_file_location("native_bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="armix-update-center-") as d:
        dst=Path(d);bundle.stage(dst)
        with sync_playwright() as pw:
            browser=pw.chromium.launch(headless=True)
            try:
                exercise(browser,dst,1366,768)
                exercise(browser,dst,390,844)
            finally:browser.close()
    print("PASS actual Update Center: menu, query/check/review, stale response, bounded viewport, no PCM intent and closed polling")
if __name__=="__main__":main()
