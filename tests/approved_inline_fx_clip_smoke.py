"""FUX-1 operator-corrected native FX: ONE WebView, full-width clipped hardware.

This is a real Chromium geometry/pointer regression of the shipped native
/preview/ bundle. Fake backend has NO PCM authority; it is not hardware FAT.
"""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
import importlib.util
import tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
SIX=(
    ("ArSonMixNativeEq","#mxNativePeq",795),
    ("ArSonMixNativeCompressor","#mxNativeComp",795),
    ("ArSonMixGraphicEq31","#mxNativeGeq31",1430),
    ("ArSonMixNativeHarmony","#mxNativeHarmony",800),
    ("ArSonMixNativeVocalThrow","#mxNativeVocalThrow",778),
    ("ArSonMixNativeSonKu","#mxNativeSonKu",790),
)
BACKEND="""()=>{
    const listeners=new Map();
    window.__nativeMessages=[];
    window.__JUCE__={backend:{
        emitEvent(name,payload){window.__nativeMessages.push({name,payload});},
        addEventListener(name,fn){listeners.set(name,fn);}
    }};
}"""

def main():
    spec=importlib.util.spec_from_file_location("bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-inline-fx-") as tmp,sync_playwright() as pw:
        directory=Path(tmp)
        bundle.stage(directory)
        class Quiet(SimpleHTTPRequestHandler):
            def log_message(self,*args):pass
        server=ThreadingHTTPServer(("127.0.0.1",0),partial(Quiet,directory=str(directory)))
        Thread(target=server.serve_forever,daemon=True).start()
        browser=pw.chromium.launch(headless=True)
        try:
            for width,height in ((390,844),(1040,700),(1680,850)):
                page=browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror",lambda error: errors.append(str(error)))
                page.add_init_script(BACKEND)
                page.goto(f"http://127.0.0.1:{server.server_address[1]}/index.html")
                page.wait_for_function("""()=>Boolean(
                    window.__ARMIX_NATIVE_INLINE_FX_CLIP__ &&
                    window.ArSonMixGraphicEq31 && window.ArSonMixNativeSonKu)""")
                assert page.locator("#armix-native-inline-fx-clip").count()==1
                assert page.evaluate("""()=>document.documentElement.classList
                    .contains('armix-native-desktop')""")
                # Each original FX face stays full native hardware width even
                # when the mixer is only 390px. Never fold a 1430px GEQ into
                # four faders and a tiny output meter.
                for host,selector,expected in SIX:
                    page.evaluate("""(name)=>window[name].open(
                        {id:22,name:'TEST CHANNEL',slotBypass:[false]},0)""",host)
                    root=page.locator(selector)
                    root.wait_for(state="visible")
                    size=root.bounding_box()
                    assert size and abs(size["width"]-expected)<2,(width,host,size)
                    assert root.locator(".armix-builtin-fx-close, .mx-geq31-close, .sk-close").count()>=1,(width,host)
                    # Native-only popup HUD and second native titlebar never
                    # exist: the browser frame already IS the only mixer GUI.
                    assert page.locator("#fxWindowAbort").count()==0
                    page.evaluate("""selector=>window.__ARMIX_NATIVE_INLINE_FX_CLIP__
                        .center(document.querySelector(selector))""",selector)
                    if expected>width:
                        assert root.bounding_box()["x"]>=0,(width,host)
                    assert not any("TypeError" in e for e in errors),errors
                    page.evaluate("""name=>window[name].close()""",host)
                root=page.locator("#mxNativeGeq31")
                page.evaluate("""()=>window.ArSonMixGraphicEq31.open(
                    {id:22,name:'TEST CHANNEL',slotBypass:[false]},0)""")
                page.evaluate("""()=>window.__ARMIX_NATIVE_INLINE_FX_CLIP__
                    .center(document.getElementById('mxNativeGeq31'))""")
                root.wait_for(state="visible")
                assert root.locator(".mx-geq31-cell").count()==31
                assert root.locator(".mx-geq31-size").count()==3
                assert root.locator(".mx-geq31-close").count()==1
                assert abs(root.bounding_box()["width"]-1430)<1
                cell_first=root.locator(".mx-geq31-cell").first.bounding_box()
                cell_last=root.locator(".mx-geq31-cell").last.bounding_box()
                assert cell_last["x"]-cell_first["x"]>1190,(width,cell_first,cell_last)
                before=root.bounding_box()
                # Browser still clips on the right. Drag the actual header
                # left and verify that the UNCHANGED physical face moves.
                at_x=min(width-40,235)
                at_y=before["y"]+15
                page.mouse.move(at_x,at_y)
                page.mouse.down()
                page.mouse.move(max(12,at_x-110),at_y,steps=7)
                page.mouse.up()
                moved=root.bounding_box()
                assert moved["x"]<before["x"]-30,(width,before,moved)
                assert abs(moved["width"]-1430)<1
                # Shift+wheel pans the hardware across the mixer viewport
                # even if a cropped header ends in controls or Close.
                page.mouse.move(min(width-50,190),moved["y"]+85)
                page.keyboard.down("Shift")
                page.mouse.wheel(0,260)
                page.keyboard.up("Shift")
                page.wait_for_timeout(80)
                after_wheel=root.bounding_box()
                assert after_wheel["x"]<moved["x"]-60,(width,moved,after_wheel)
                # Recover/recenter and S/M/L retain original full-sized
                # artwork with compositor scaling, not CSS width compression.
                page.evaluate("""()=>window.__ARMIX_NATIVE_INLINE_FX_CLIP__
                    .center(document.getElementById('mxNativeGeq31'))""")
                for mode,ratio in (("S",.5),("M",.75),("L",1.)):
                    page.evaluate("(mode)=>window.ArSonMixGraphicEq31.setSize(mode)",mode)
                    bounds=root.bounding_box()
                    assert abs(bounds["width"]-1430*ratio)<2,(width,mode,bounds)
                    assert root.locator(".mx-geq31-cell").count()==31
                    assert root.locator(".mx-geq31-close").count()==1
                # Built-in DOM open never issues a request to construct the
                # rejected detached JUCE host (other snapshot intents allowed).
                assert not page.evaluate("""()=>window.__nativeMessages.some(
                    x=>x.name==='arMixAction'&&
                    x.payload?.action==='floatingBuiltinOpen')""")
                assert not errors,(width,errors)
                page.close()
            print("PASS FUX-1 exact one-WebView clipped overlay, six native faces, "
                  "31 GEQ faders, pointer drag, Shift-wheel pan and S/M/L")
        finally:
            browser.close()
            server.shutdown()

if __name__=="__main__":
    main()
