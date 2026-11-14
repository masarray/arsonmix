"""Actual compiled /preview/ WebView2 mixer FX picker, ImerSip catalog, intent.
The browser test supplies a host catalog event but never fabricates DSP state.
Real JUCE registry, PCM insert and state are validated separately by CTest.
"""
from pathlib import Path
import copy
import importlib.util
import tempfile
from playwright.sync_api import sync_playwright
from approved_native_wire_gain_smoke import snapshot, emit

ROOT=Path(__file__).resolve().parents[1]
ID="armix.builtin.imersip.v1"

def main():
    spec=importlib.util.spec_from_file_location(
        "approved_native_bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="imersip-picker-") as directory, sync_playwright() as p:
        out=Path(directory)/"gui"
        bundle.stage(out)
        browser=p.chromium.launch()
        page=browser.new_page(viewport={"width":1646,"height":928})
        errors=[]
        page.on("pageerror",lambda error:errors.append(str(error)))
        page.add_init_script("""window.__nativeListeners=new Map();
window.__nativeIntents=[];
window.__JUCE__={backend:{
 emitEvent(name,payload){
   if(name==='arMixConsoleIntent')window.__nativeIntents.push(payload);
 },
 addEventListener(name,callback){window.__nativeListeners.set(name,callback);}
}};""")
        page.goto((out/"index.html").as_uri())
        page.wait_for_function("()=>window.__ARMIX_NATIVE_M2__?.enabled")
        state=snapshot()
        state["graph"]["capabilities"]["supportedOps"].append("LOAD_INSERT")
        emit(page,"arMixConsoleSnapshot",state)
        # Catalog must be emitted by the NATIVE host; never show an effect
        # that the actual executable has not compiled.
        page.evaluate("""()=>{
 const s=window.__ARMIX_NATIVE_SCOPE__;
 s.openFastPicker(s.tracks.find(t=>t.id===1),0,null);
}""")
        selector=f'#fastResults button[data-fast-id="{ID}"]'
        assert page.locator(selector).count()==0, "ImerSip falsely advertised before native catalog"
        emit(page,"arMixCatalog",{
            "items":[],"scanning":False,
            "builtIns":[{"id":ID,"name":"ImerSip","category":"Spatial",
                         "showInPicker":True}]})
        page.wait_for_function(
            """id=>document.querySelectorAll('#fastResults button[data-fast-id="'+id+'"]').length===1""",
            arg=ID)
        assert page.locator(selector).count()==1, "ImerSip missing or duplicated"
        assert page.locator(selector).inner_text().startswith("ImerSip")
        page.locator("#fastSearch").fill("imersip")
        assert page.locator(selector).count()==1, "native catalog search lost ImerSip"
        assert page.locator("#fastResults [data-fast-native-sonku]").count()==0
        page.locator(selector).click()
        page.wait_for_function("()=>window.__nativeIntents.length===1")
        intent=page.evaluate("()=>window.__nativeIntents[0]")
        assert intent["operation"]=="LOAD_INSERT", intent
        assert intent["payload"]["catalogId"]==ID, intent
        assert intent["payload"]["slot"]==0, intent
        # No premature GUI ACTIVE state without native callback/readback.
        row=page.locator('#tracks .strip[data-id="1"] .fxslot-row[data-slot="0"]')
        assert "ImerSip" not in row.inner_text(), "UI claimed unaccepted audio insert"
        page.screenshot(path=str(Path(directory)/"imersip-visible-in-native-picker.png"))
        assert not errors,errors
        browser.close()
    print("PASS: compiled native Pages mixer picker filters ImerSip, submits LOAD_INSERT, never fakes an accepted audio instance")

if __name__=="__main__":
    main()
