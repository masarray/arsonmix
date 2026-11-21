"""GEQ31 exact ONE Pages/WebView2 UI regression: 31 captions, original-SVG
coordinate consistency, DSP lease, AUDIO ACK and final accepted readback.
Playwright's JUCE transport here is a test double; hardware tests remain separate.
"""
from pathlib import Path
import importlib.util
import tempfile
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
ID="armix.builtin.graphic-eq31.v1"
CHANNEL="9007199254740993"
INSTANCE="geq31-native-A"
BANDS=[0.0]*31

def main():
    spec=importlib.util.spec_from_file_location(
        "bundle",ROOT/"scripts/build_approved_native_gui.py")
    bundle=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-geq31-webview-") as directory,sync_playwright() as pw:
        out=Path(directory)
        bundle.stage(out)
        # Serve bundled assets: file:// fetch of SVG is CORS-blocked.
        class QuietHandler(SimpleHTTPRequestHandler):
            def log_message(self,*args):pass
        server=ThreadingHTTPServer(("127.0.0.1",0),partial(QuietHandler,directory=str(out)))
        Thread(target=server.serve_forever,daemon=True).start()
        url=f"http://127.0.0.1:{server.server_address[1]}/index.html"
        browser=pw.chromium.launch(headless=True)
        for width,height in [(1430,650),(1000,700),(390,844)]:
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda err:errors.append(str(err)))
            page.goto(url)
            page.wait_for_function("()=>!!window.ArSonMixGraphicEq31")
            page.evaluate("""()=>{
               window.ArSonMixGraphicEq31.open({id:123,name:'Visual Preview',slotBypass:[false]},0);
            }""")
            panel=page.locator("#mxNativeGeq31")
            panel.wait_for(state="visible")
            page.wait_for_function("()=>document.querySelector('#mxNativeGeq31')?.dataset.artwork==='v13'")
            assert panel.locator(".mx-geq31-cell").count()==31
            assert panel.locator(".mx-geq31-range").count()==31
            assert panel.locator(".mx-geq31-frequency").all_text_contents()==[
             "20","25","31.5","40","50","63","80","100","125","160","200",
             "250","315","400","500","630","800","1","1.25","1.6","2",
             "2.5","3.15","4","5","6.3","8","10","12.5","16","20"]
            assert panel.locator(".mx-geq31-scale-text").all_text_contents()==[
                "+12","0","−12","+12","0","−12"]
            assert panel.locator(".mx-geq31-close").count()==1
            assert panel.locator("header.mx-geq31-head").count()==1
            # DOM existence is NOT visual acceptance: the first screenshot
            # proved a branded Close and Output can be clipped offscreen
            # even while all source/contract assertions pass.
            panel_box=panel.bounding_box()
            close_box=panel.locator(".mx-geq31-close").bounding_box()
            output_box=panel.locator(".mx-geq31-output").bounding_box()
            assert panel_box and close_box and output_box
            assert panel_box["x"]>=0 and panel_box["x"]+panel_box["width"]<=width+1
            assert close_box["x"]>=0 and close_box["x"]+close_box["width"]<=width
            assert output_box["x"]>=0 and output_box["x"]+output_box["width"]<=width
            assert panel.locator(".mx-geq31-close").is_visible()
            assert panel.locator(".mx-geq31-dial").is_visible()
            assert panel.locator(".mx-geq31-message").inner_text().startswith("BROWSER PREVIEW")
            assert panel.locator(".mx-geq31-scroll").evaluate(
                "e=>getComputedStyle(e).overflowX")=="auto"
            # v13 source-authority: 31 original SVG raster THUMBs, 40x229/84 knob.
            assert panel.locator(".mx-geq31-track svg").count()==31
            assert panel.locator(".mx-geq31-knob-image").count()==1
            assert panel.locator(".mx-geq31-dial-pointer").count()==1
            assert panel.locator(".mx-geq31-knob-image").evaluate(
                "e=>e.complete&&e.naturalWidth===192")
            for asset in ("geq31-owner-slider.svg","geq31-brushed-metal.jpg","geq31-knob-black.png"):
                assert (out/"assets"/asset).is_file(),asset
            for idx in [0,15,30]:
                band=panel.locator(".mx-geq31-cell").nth(idx)
                box=band.locator(".mx-geq31-track").bounding_box()
                assert box and abs(box["height"]-229)<.5 and abs(box["width"]-40)<.5
                assert band.locator(".mx-geq31-freq").is_visible()
                assert band.locator('g[id="THUMB"]').count()==1
                assert band.locator('use[id="_Image3"]').count()==1
                # Visual gate: first v13 black thumb gradient (prevents white-metal fallback).
                for gradient, color in (("g3","#090b0e"),("g4","#30383e"),
                                        ("g5","#252e33"),("g6","#646e74")):
                    paint=band.locator(f'linearGradient[id="geq31-{idx}-{gradient}"] stop').first
                    assert paint.get_attribute("stop-color")==color, (idx,gradient,color)
                assert band.locator(".mx-geq31-index").evaluate(
                    "e=>Math.abs(parseFloat(e.style.top)-103)<.5")
            assert len(set(panel.locator('linearGradient[id^="geq31-"]').evaluate_all(
                "es=>es.map(e=>e.id)")))==31*6
            dial_box=panel.locator(".mx-geq31-dial").bounding_box()
            assert dial_box and abs(dial_box["width"]-84)<.5
            first=panel.locator(".mx-geq31-range").first
            first.evaluate("""e=>{
                e.value='6';e.dispatchEvent(new Event('input',{bubbles:true}));
            }""")
            assert first.input_value()=="6"
            assert panel.locator(".mx-geq31-message").inner_text().startswith("BROWSER PREVIEW")
            panel.locator(".mx-geq31-flat").click()
            assert first.input_value()=="0"
            # Same compiled WebView binary uses these exact CSS/JS files;
            # new JS cannot be hand-restyled in a separate native renderer.
            assert (out/"assets/mixer-native-geq31.js").is_file()
            assert (out/"assets/mixer-fx-rack.css").is_file()
            if width==1430:
                page.screenshot(path="arsonmix-geq31-one-webview-1430.png")
            assert not errors,errors
            page.close()

        page=browser.new_page(viewport={"width":1430,"height":740})
        errors=[]
        page.on("pageerror",lambda err:errors.append(str(err)))
        page.add_init_script("""(()=>{
          window.__nativeListeners=new Map();window.__actions=[];
          window.__JUCE__={backend:{
            emitEvent(name,payload){if(name==='arMixAction')window.__actions.push(payload);},
            addEventListener(name,callback){
              const previous=window.__nativeListeners.get(name);
              window.__nativeListeners.set(name,previous
                ?value=>{previous(value);callback(value)}:callback);
            }
          }};
        })()""")
        page.goto(url)
        page.wait_for_function("()=>window.ArSonMixGraphicEq31 && window.__ARMIX_NATIVE_M2__")
        state={"admitted":True,"logical":False,"device":{"epoch":"11"},
         "graph":{"revision":"7","channels":[{
          "id":CHANNEL,"fxRackBypassed":False,
          "inserts":[{"id":INSTANCE,"catalogId":ID,"bypassed":False}]}]}}
        page.evaluate("""(state)=>{
           window.__ARMIX_NATIVE_M2__={
             fxEditorState:()=>state,nativeIdFor:()=>state.graph.channels[0].id,
             consumeAction:()=>true,insertPlugin:()=>true
           };
           window.ArSonMixGraphicEq31.open({
               id:123,name:'Vocal Channel',slotBypass:[false]
           },0);
        }""",state)
        panel=page.locator("#mxNativeGeq31")
        panel.wait_for(state="visible")
        page.wait_for_function("()=>document.querySelector('#mxNativeGeq31')?.dataset.artwork==='v13'")
        def last_action(kind):
            actions=page.evaluate("""kind=>window.__actions.filter(
               a=>a.action===kind)""",kind)
            assert actions,kind+" not emitted"
            return actions[-1]
        def dispatch(event,payload):
            page.evaluate("""({event,payload})=>{
              const listener=window.__nativeListeners.get(event);
              if(!listener)throw Error('missing native listener '+event);
              listener(payload);
            }""",{"event":event,"payload":payload})
        def report(values,applied=None):
            action=last_action("nativeGeqSnapshot")
            payload={"requestId":action["requestId"],"channelId":CHANNEL,
             "deviceEpoch":"11","graphRevision":"7","instanceId":INSTANCE,
             "status":"ACCEPTED","schemaVersion":1,"bands":values,
             "levelDb":0.0,"addedLatencySamples":0}
            if applied:payload["appliedRequestId"]=applied
            dispatch("arMixBuiltinGeqSnapshot",payload)
        report(list(BANDS))
        first=panel.locator(".mx-geq31-range").first
        first.evaluate("""e=>{
           e.value='5.5';e.dispatchEvent(new Event('input',{bubbles:true}));
        }""")
        action=last_action("nativeGeqSet")
        assert action["edit"]=={"kind":"band","index":0,"value":5.5}
        assert action["channelId"]==CHANNEL
        assert action["instanceId"]==INSTANCE
        assert action["deviceEpoch"]=="11"
        assert action["graphRevision"]=="7"
        assert action["requestId"].startswith("geq-")
        assert "APPLYING" in panel.locator(".mx-geq31-message").inner_text()
        dispatch("arMixBuiltinGeqQueue",{
            "requestId":action["requestId"],"status":"QUEUED"})
        assert "APPLYING" in panel.locator(".mx-geq31-message").inner_text()
        dispatch("arMixBuiltinGeqAck",{
            "requestId":action["requestId"],"status":"APPLIED"})
        bands=list(BANDS);bands[0]=5.5
        report(bands,applied=action["requestId"])
        assert "NATIVE DSP ACCEPTED" in panel.locator(".mx-geq31-message").inner_text()
        assert first.input_value()=="5.5"
        assert panel.locator(".mx-geq31-output-value").inner_text()=="0.0 dB"
        assert not errors,errors
        browser.close()
        server.shutdown();server.server_close()
        print("GEQ31_WEBVIEW_ONE_RENDERER_PASS: 31 captions, 3 viewports, lease + DSP ACK/readback")

if __name__=="__main__":
    main()
