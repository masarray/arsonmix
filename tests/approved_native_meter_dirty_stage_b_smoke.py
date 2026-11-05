"""Stage B: native-only cached meter writes preserve exact public /preview/ source.

This test exercises the real assembled JUCE HTML in Chromium. It is a JS DOM
regression, NOT physical Windows GPU/input-to-photon certification.
"""
import asyncio
import importlib.util
from pathlib import Path
import tempfile
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("approved_bundle",ROOT/"scripts/build_approved_native_gui.py")
bundle=importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)

async def main():
    with tempfile.TemporaryDirectory(prefix="armix-stage-b-meter-") as folder:
        out=Path(folder)/"native"
        bundle.stage(out)
        public=bundle.accepted_pages_html()
        native=(out/"index.html").read_text("utf-8")
        assert bundle.public_from_native(native)==public
        assert "nativeMeterPainted" not in public
        assert "nativeMeterPainted" in native
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            try:
                page=await browser.new_page(viewport={"width":1366,"height":768})
                errors=[]
                page.on("pageerror",lambda e:errors.append(str(e)))
                # Host WebView2 is represented only for the native-only JS hook;
                # no synthetic native PCM or fake ACK is provided.
                await page.add_init_script("""window.__JUCE__={backend:{
                    emitEvent(){}, addEventListener(){}}};""")
                await page.goto((out/"index.html").as_uri())
                await page.wait_for_function("""()=>
                    !!window.__ARMIX_NATIVE_SCOPE__ &&
                    !!document.querySelector('#tracks .meter-fill.left')""")
                measured=await page.evaluate("""() => {
                    const scope=window.__ARMIX_NATIVE_SCOPE__;
                    const fill=document.querySelector('#tracks .meter-fill.left');
                    const marker=fill.parentElement.querySelector('.meter-peak-hold');
                    const original=CSSStyleDeclaration.prototype.setProperty;
                    let writes=[];
                    CSSStyleDeclaration.prototype.setProperty=function(k,v,...rest) {
                        if(k==='--lit'||k==='--peak-extent')writes.push([k,String(v)]);
                        return original.call(this,k,v,...rest);
                    };
                    try{
                        const t=performance.now();
                        scope.displayMeterDbfs(fill,-30,t,'native-stage-b-test-L');
                        scope.displayMeterDbfs(fill,-30,t+70,'native-stage-b-test-L');
                        const repeats=writes.slice();
                        scope.displayMeterDbfs(fill,-10,t+140,'native-stage-b-test-L');
                        const raised=writes.slice();
                        // Held peak must still release even with unchanged live level.
                        scope.displayMeterDbfs(fill,-10,t+1100,'native-stage-b-test-L');
                        scope.displayMeterDbfs(fill,-10,t+1350,'native-stage-b-test-L');
                        return {repeats,raised,after:writes,
                           markerOpacity:marker.style.opacity,
                           litValue:fill.style.getPropertyValue('--lit')};
                    } finally {CSSStyleDeclaration.prototype.setProperty=original;}
                }""")
                first=measured["repeats"]
                assert len([w for w in first if w[0]=="--lit"])==1,measured
                assert len([w for w in first if w[0]=="--peak-extent"])==1,measured
                assert len([w for w in measured["raised"] if w[0]=="--lit"])==2,measured
                assert measured["markerOpacity"]=="1",measured
                assert measured["litValue"].endswith("%"),measured
                assert not errors,errors
                await page.close()
            finally:
                await browser.close()
    print("PASS Stage B: native-only meter writes coalesced, real portrait peak held, "
          "approved Pages unchanged, native reconstruction byte-identical")

if __name__=="__main__":
    asyncio.run(main())
