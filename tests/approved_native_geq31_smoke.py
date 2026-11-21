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
        for width,height in [(1680,650),(1430,650),(1000,700),(390,844)]:
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
            assert panel.get_attribute("data-fx-track-id")=="123"
            assert panel.get_attribute("data-fx-slot")=="0"
            assert panel.locator(".mx-geq31-cell").count()==31
            assert panel.locator(".mx-geq31-range").count()==31
            assert panel.locator(".mx-geq31-frequency").all_text_contents()==[
             "20","25","31.5","40","50","63","80","100","125","160","200",
             "250","315","400","500","630","800","1","1.25","1.6","2",
             "2.5","3.15","4","5","6.3","8","10","12.5","16","20"]
            # Owner-requested single left scale: no duplicate '+12 0 −12'
            # to the right of the 31 faders.
            assert panel.locator(".mx-geq31-scale-text").all_text_contents()==[
                "+12","0","−12"]
            assert panel.locator(".mx-geq31-right").count()==0
            assert panel.locator(".mx-geq31-body").evaluate(
                "e=>getComputedStyle(e).gridTemplateColumns.split(' ').length")==3
            assert panel.locator(".mx-geq31-close").count()==1
            assert panel.locator("header.mx-geq31-head").count()==1
            assert panel.locator(".mx-geq31-footer").count()==0
            assert panel.locator(".mx-geq31-message").get_attribute("role")=="status"
            sizes=panel.locator(".mx-geq31-size")
            assert sizes.count()==3
            assert sizes.all_text_contents()==["S","M","L"]
            assert sizes.nth(0).get_attribute("aria-label")=="Graphic EQ size 50%"
            assert sizes.nth(1).get_attribute("aria-label")=="Graphic EQ size 75%"
            assert sizes.nth(2).get_attribute("aria-label")=="Graphic EQ size 100%"
            assert panel.get_attribute("data-fx-size")=="L"
            assert panel.get_attribute("data-fx-scale")=="1"
            assert panel.locator(".mx-geq31-size.is-active").count()==1
            assert panel.locator(".mx-geq31-size.is-active").inner_text()=="L"
            assert panel.locator(".mx-geq31-head").evaluate(
                "e=>e.scrollWidth<=e.clientWidth+1"), width
            # Exact V51 geometry, rotated 90 CW: six 12x6px glass windows
            # per independent L/R channel, with a continuous peak fill BEHIND.
            assert panel.locator(".mx-geq31-level-led").count()==12
            assert panel.locator(".mx-geq31-level-led.is-lit").count()==0
            assert panel.locator(".mx-geq31-level-meter").get_attribute("role")=="group"
            rows=panel.locator(".mx-geq31-level-row")
            assert rows.count()==2
            assert rows.nth(0).get_attribute("data-side")=="left"
            assert rows.nth(1).get_attribute("data-side")=="right"
            assert rows.nth(0).get_attribute("role")=="meter"
            assert rows.nth(1).get_attribute("role")=="meter"
            for side in ("left","right"):
                row=panel.locator(f'.mx-geq31-level-row[data-side="{side}"]')
                leds=row.locator(".mx-geq31-level-led")
                assert leds.count()==6
                boxes=[leds.nth(i).bounding_box() for i in range(6)]
                assert all(b and abs(b["width"]-12)<.25
                           and abs(b["height"]-6)<.25 for b in boxes)
                assert all(abs(boxes[i+1]["x"]-boxes[i]["x"]-14)<.25
                           for i in range(5))
                assert row.locator(".mx-geq31-level-cells").evaluate(
                    "e=>Math.abs(e.getBoundingClientRect().width-82)<.25")
                assert row.locator(".mx-geq31-level-fill").count()==1
                assert row.locator(".mx-geq31-level-fill").evaluate(
                    "e=>getComputedStyle(e).backgroundImage.includes('linear-gradient')")
                assert row.locator(".mx-geq31-level-fill").evaluate(
                    "e=>e.nextElementSibling?.classList.contains('mx-geq31-level-led')")
                assert leds.first.evaluate(
                    "e=>getComputedStyle(e).borderTopWidth")=="0px"
            # DOM existence is NOT visual acceptance: the first screenshot
            # proved a branded Close and Output can be clipped offscreen
            # even while all source/contract assertions pass.
            panel_box=panel.bounding_box()
            close_box=panel.locator(".mx-geq31-close").bounding_box()
            output_box=panel.locator(".mx-geq31-output").bounding_box()
            assert panel_box and close_box and output_box
            assert abs(panel_box["height"]-280)<1
            assert 57<=panel_box["y"]<=67, panel_box
            assert panel.locator(".mx-geq31-head").evaluate(
                "e=>Math.abs(e.getBoundingClientRect().height-44)<.5")
            led_box=panel.locator(".mx-geq31-level-meter").bounding_box()
            knob_y=panel.locator(".mx-geq31-dial").bounding_box()
            caption_box=panel.locator(".mx-geq31-output-value").bounding_box()
            freq_caption=panel.locator(".mx-geq31-freq").last.bounding_box()
            label_box=panel.locator(".mx-geq31-output-label").bounding_box()
            assert led_box and knob_y and caption_box and freq_caption and label_box
            # One true center axis, not the four unrelated -14px CSS offsets.
            cx=output_box["x"]+output_box["width"]/2
            for label,rect in (("OUTPUT",label_box),("stereo LCD",led_box),
                               ("knob",knob_y),("dB caption",caption_box)):
                actual=rect["x"]+rect["width"]/2
                assert abs(actual-cx)<.65,(width,label,actual,cx)
            assert abs(led_box["height"]-31)<.5
            assert abs(led_box["width"]-100)<.5
            assert knob_y["y"]>=led_box["y"]+led_box["height"]+15
            # Lift both LCD and knob by 6px while retaining the close
            # optical dB caption and pre-existing fader-caption baseline.
            assert panel.locator(".mx-geq31-output").evaluate(
                "e=>getComputedStyle(e).paddingTop")=="19px"
            knob_to_db=caption_box["y"]-(knob_y["y"]+knob_y["height"])
            assert 10<=knob_to_db<=14, knob_to_db
            assert panel.locator(".mx-geq31-level-meter").evaluate(
                "e=>getComputedStyle(e).borderTopWidth")=="1px"
            assert abs(caption_box["y"]-freq_caption["y"])<=4, (
                caption_box,freq_caption)
            assert panel_box["x"]>=0 and panel_box["x"]+panel_box["width"]<=width+1
            assert close_box["x"]>=0 and close_box["x"]+close_box["width"]<=width
            assert output_box["x"]>=0 and output_box["x"]+output_box["width"]<=width
            assert panel.locator(".mx-geq31-close").is_visible()
            assert panel.locator(".mx-geq31-dial").is_visible()
            assert panel.locator(".mx-geq31-message").inner_text().startswith("BROWSER PREVIEW")
            assert panel.locator(".mx-geq31-scroll").evaluate(
                "e=>getComputedStyle(e).overflowX")=="auto"
            # V13 source authority unchanged: the 31 hardware SVGs and
            # exact 192px PNG. The 92px knob uses smooth image interpolation.
            assert panel.locator(".mx-geq31-track svg").count()==31
            assert panel.locator(".mx-geq31-knob-image").count()==1
            knob_image=panel.locator(".mx-geq31-knob-image")
            assert knob_image.evaluate(
                "e=>e.complete&&e.naturalWidth===192&&e.naturalHeight===192")
            assert knob_image.evaluate(
                "e=>getComputedStyle(e).imageRendering")=="auto"
            # Blue arc/pointer must remain VECTOR on all three S/M/L modes,
            # not a fixed-resolution canvas bitmap scaled by WebView2.
            arc=panel.locator("svg.mx-geq31-dial-pointer")
            assert arc.count()==1
            assert arc.get_attribute("viewBox")=="0 0 84 84"
            assert panel.locator(".mx-geq31-dial canvas").count()==0
            assert arc.locator("path").count()==1
            assert arc.locator("circle").count()==2
            assert " A " in arc.locator("path").get_attribute("d")
            assert arc.locator("path").get_attribute("stroke")=="#26b8f6"
            for asset in ("geq31-owner-slider.svg","geq31-brushed-metal.jpg","geq31-knob-black.png"):
                assert (out/"assets"/asset).is_file(),asset
            for idx in [0,15,30]:
                band=panel.locator(".mx-geq31-cell").nth(idx)
                box=band.locator(".mx-geq31-track").bounding_box()
                assert box and abs(box["height"]-229)<.5 and abs(box["width"]-40)<.5
                assert band.locator(".mx-geq31-freq").is_visible()
                assert band.locator('g[id="THUMB"]').count()==1
                assert band.locator('use[id="_Image3"]').count()==1
                assert band.locator("path.s5,path.s6").count()==0
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
            assert dial_box and abs(dial_box["width"]-92)<.5
            first=panel.locator(".mx-geq31-range").first
            first.evaluate("""e=>{
                e.value='6';e.dispatchEvent(new Event('input',{bubbles:true}));
            }""")
            assert first.input_value()=="6"
            assert panel.locator(".mx-geq31-message").inner_text().startswith("BROWSER PREVIEW")
            panel.locator(".mx-geq31-flat").click()
            assert first.input_value()=="0"
            # v13 zero detent must allow ONE outbound wheel step, not stick
            # forever at zero as the old browser snap0 implementation did.
            panel.locator(".mx-geq31-cell").first.evaluate(
                "e=>e.dispatchEvent(new WheelEvent('wheel',{deltaY:-100,bubbles:true,cancelable:true}))")
            assert first.input_value()=="0.5"
            panel.locator(".mx-geq31-cell").first.evaluate(
                "e=>e.dispatchEvent(new WheelEvent('wheel',{deltaY:-100,bubbles:true,cancelable:true}))")
            assert first.input_value()=="1"
            panel.locator(".mx-geq31-flat").click()
            knob=panel.locator(".mx-geq31-dial")
            knob.evaluate(
                "e=>e.dispatchEvent(new WheelEvent('wheel',{deltaY:-100,bubbles:true,cancelable:true}))")
            assert panel.locator(".mx-geq31-output-value").inner_text()=="+0.5 dB"
            mid_path=arc.locator("path").get_attribute("d")
            assert " A " in mid_path
            knob.dblclick()
            assert panel.locator(".mx-geq31-output-value").inner_text()=="0.0 dB"
            assert " A " in arc.locator("path").get_attribute("d")
            assert arc.locator("path").get_attribute("d")!=mid_path
            # Same compiled WebView binary uses these exact CSS/JS files;
            # new JS cannot be hand-restyled in a separate native renderer.
            assert (out/"assets/mixer-native-geq31.js").is_file()
            assert (out/"assets/mixer-fx-rack.css").is_file()
            if width in (1680,1430):
                page.screenshot(path=f"arsonmix-geq31-one-webview-{width}.png")
            # S/M/L are literal visual scales over the SAME 31 SVGs/PCM
            # component, not reflowed, not a cloned editor or CSS-only text.
            base_width=panel_box["width"]
            for mode,scale in (("S",.5),("M",.75),("L",1)):
                panel.locator(f'.mx-geq31-size[data-size="{mode}"]').click()
                assert panel.get_attribute("data-fx-size")==mode
                assert panel.get_attribute("data-fx-scale")==str(scale if scale!=1 else 1)
                assert panel.locator(".mx-geq31-size.is-active").count()==1
                assert panel.locator(".mx-geq31-size.is-active").inner_text()==mode
                assert panel.locator(f'.mx-geq31-size[data-size="{mode}"]').get_attribute(
                    "aria-pressed")=="true"
                scaled=panel.bounding_box()
                assert scaled and abs(scaled["width"]-base_width*scale)<1.5
                assert abs(scaled["height"]-280*scale)<1
                assert abs((scaled["x"]+scaled["width"]/2)-width/2)<2
                assert scaled["y"]>=0 and scaled["y"]+scaled["height"]<=height
                close_scaled=panel.locator(".mx-geq31-close").bounding_box()
                assert close_scaled and close_scaled["x"]>=scaled["x"]
                assert close_scaled["x"]+close_scaled["width"]<=scaled["x"]+scaled["width"]
                assert panel.locator(".mx-geq31-track svg").count()==31
                assert panel.locator(".mx-geq31-level-led").count()==12
                assert panel.locator(".mx-geq31-dial").evaluate(
                    "e=>e.getBoundingClientRect().width")>89*scale
                assert arc.locator("path").get_attribute("d")
                assert knob_image.evaluate(
                    "e=>getComputedStyle(e).imageRendering")=="auto"
                if width==1680:
                    page.screenshot(path=f"arsonmix-geq31-one-webview-{mode.lower()}-1680.png")
            assert page.evaluate("""()=>localStorage.getItem(
                'arsonmix.geq31.editor-size.v1')""")=="L"
            # All adjustments survive a size change; tiny S/M do not break
            # the keyboard, wheel, flat and Native ACK workflows.
            panel.locator('.mx-geq31-size[data-size="S"]').click()
            first.evaluate("""e=>e.dispatchEvent(new WheelEvent('wheel',{
                deltaY:-100,bubbles:true,cancelable:true}))""")
            assert first.input_value()=="0.5"
            panel.locator(".mx-geq31-flat").click()
            assert first.input_value()=="0"
            panel.locator('.mx-geq31-size[data-size="M"]').click()
            knob.evaluate("""e=>e.dispatchEvent(new WheelEvent('wheel',{
                deltaY:-100,bubbles:true,cancelable:true}))""")
            assert panel.locator(".mx-geq31-output-value").inner_text()=="+0.5 dB"
            knob.dblclick()
            assert panel.locator(".mx-geq31-output-value").inner_text()=="0.0 dB"
            panel.locator('.mx-geq31-size[data-size="L"]').click()
            # Accessible recovery from a parked/offscreen editor by double-click
            # on the actual mixer FX rack (same approved Pages DOM).
            panel.evaluate("e=>{e.style.left='-900px';e.style.top='12px';}")
            rack=page.locator(".fx-section .subhead").first
            assert rack.count()==1
            rack.evaluate("e=>e.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}))")
            recovered=panel.bounding_box()
            assert recovered and abs((recovered["x"]+recovered["width"]/2)-width/2)<2
            assert abs((recovered["y"]+recovered["height"]/2)-height/2)<2
            # Rack double-click MUST preserve selected GEQ scale, not force
            # reset to 100% through the global FX editor workspace helper.
            panel.locator('.mx-geq31-size[data-size="M"]').click()
            panel.evaluate("""e=>e.style.setProperty('left','-900px','important')""")
            rack.evaluate("e=>e.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}))")
            recovered_small=panel.bounding_box()
            assert recovered_small and abs(recovered_small["height"]-210)<1
            assert abs((recovered_small["x"]+recovered_small["width"]/2)-width/2)<2
            assert panel.get_attribute("data-fx-size")=="M"
            panel.locator('.mx-geq31-size[data-size="L"]').click()
            # If a separate built-in editor is in front, double-clicking
            # a SPECIFIC occupied slot recovers its matching instance, not
            # whichever other editor has the highest z-index.
            panel.evaluate("e=>{e.style.setProperty('left','-700px','important');e.style.setProperty('top','12px','important');}")
            page.evaluate("""()=>{
              const distractor=document.createElement('section');
              distractor.id='mxNativeComp';
              distractor.dataset.fxTrackId='77';distractor.dataset.fxSlot='4';
              distractor.style='position:fixed;top:80px;left:80px;width:200px;height:180px;z-index:9999';
              document.body.append(distractor);
              const strip=document.createElement('section');strip.className='strip';
              strip.dataset.id='123';strip.id='fxRecoveryFixture';
              const row=document.createElement('div');row.className='fxslot-row';
              const btn=document.createElement('button');btn.dataset.slot='0';
              row.append(btn);strip.append(row);document.body.append(strip);
              row.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}));
            }""")
            matched=panel.bounding_box()
            assert matched and abs(matched["x"]+matched["width"]/2-width/2)<2
            assert panel.get_attribute("data-fx-recovered")=="center"
            page.evaluate("""()=>{
              document.getElementById('fxRecoveryFixture')?.remove();
              document.getElementById('mxNativeComp')?.remove();
            }""")
            assert not errors,errors
            page.close()

        # High-density WebView2/Chromium rasterization: verify the vector
        # pointer stays SVG, while the original PNG retains 192px source
        # resolution; capture a REAL 2x DPR PNG for visual review.
        hi=browser.new_page(viewport={"width":1680,"height":650},
                            device_scale_factor=2)
        hi.goto(url)
        hi.wait_for_function("()=>!!window.ArSonMixGraphicEq31")
        hi.evaluate("""()=>window.ArSonMixGraphicEq31.open(
            {id:999,name:'DPI 2x Preview',slotBypass:[false]},0)""")
        hi.wait_for_function(
            "()=>document.querySelector('#mxNativeGeq31')?.dataset.artwork==='v13'")
        hp=hi.locator("#mxNativeGeq31")
        assert hi.evaluate("()=>window.devicePixelRatio")==2
        assert hp.locator("svg.mx-geq31-dial-pointer").count()==1
        assert hp.locator(".mx-geq31-dial canvas").count()==0
        assert hp.locator(".mx-geq31-knob-image").evaluate(
            "e=>e.naturalWidth===192&&e.naturalHeight===192")
        png=hi.screenshot(path="arsonmix-geq31-one-webview-hidpi-vector-2x-1680.png")
        assert png[:8]==b"\x89PNG\r\n\x1a\n"
        assert int.from_bytes(png[16:20],"big")==3360
        assert int.from_bytes(png[20:24],"big")==1300
        hi.close()

        page=browser.new_page(viewport={"width":1680,"height":740})
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
         "telemetry":{"channelMeterRmsPeak":{CHANNEL:{"peakL":0.4,"peakR":0.07}}},
         "graph":{"revision":"7","channels":[{
          "id":CHANNEL,"fxRackBypassed":False,
          "inserts":[{"id":INSTANCE,"catalogId":ID,"bypassed":False}]}]}}
        page.evaluate("""(state)=>{
           window.__testMeterView=state;
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
        left=panel.locator('.mx-geq31-level-row[data-side="left"]')
        right=panel.locator('.mx-geq31-level-row[data-side="right"]')
        assert left.locator(".is-lit").count()==5
        assert right.locator(".is-lit").count()==3
        assert left.locator(".mx-geq31-level-cells").evaluate(
            "e=>e.style.getPropertyValue('--lit-px')")=="68px"
        assert right.locator(".mx-geq31-level-cells").evaluate(
            "e=>e.style.getPropertyValue('--lit-px')")=="40px"
        assert abs(float(left.get_attribute("aria-valuenow"))+8.0)<.2
        assert abs(float(right.get_attribute("aria-valuenow"))+23.1)<.2
        # Native simulator only: screenshot proves L/R are *visually distinct*.
        page.screenshot(path="arsonmix-geq31-one-webview-stereo-native-telemetry-mock-1680.png")
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
        # Independent stereo peaks: low left (-46 dBFS) lights ONE LED;
        # hot right (~-0.9 dBFS) lights SIX. Never duplicate R onto L.
        page.evaluate("""()=>{
          window.__testMeterView.telemetry.channelMeterRmsPeak[
            "9007199254740993"]={peakL:0.005,peakR:0.9};
        }""")
        dispatch("arMixConsoleSnapshot",{})
        assert left.locator(".is-lit").count()==1
        assert left.locator(".mx-geq31-level-cells").evaluate(
            "e=>e.style.getPropertyValue('--lit-px')")=="12px"
        assert right.locator(".is-lit").count()==6
        assert right.locator(".mx-geq31-level-cells").evaluate(
            "e=>e.style.getPropertyValue('--lit-px')")=="82px"
        # Telemetry-loss fail closed: both L/R dark, never fake a VU pulse.
        page.evaluate("()=>{window.__testMeterView.telemetry.channelMeterRmsPeak={};}")
        dispatch("arMixConsoleSnapshot",{})
        assert left.locator(".is-lit").count()==0
        assert right.locator(".is-lit").count()==0
        assert left.locator(".mx-geq31-level-cells").evaluate(
            "e=>e.style.getPropertyValue('--lit-px')")=="0px"
        assert right.locator(".mx-geq31-level-cells").evaluate(
            "e=>e.style.getPropertyValue('--lit-px')")=="0px"
        assert left.get_attribute("aria-valuenow") is None
        assert right.get_attribute("aria-valuenow") is None
        panel.locator(".mx-geq31-flat").click()
        # Drain preset edit ACK before the subsequent band-intent contract.
        action0=page.evaluate("()=>window.__actions.filter(a=>a.action==='nativeGeqSet').at(-1)")
        if action0:
            dispatch("arMixBuiltinGeqAck",{"requestId":action0["requestId"],"status":"APPLIED"})
            report(list(BANDS),applied=action0["requestId"])
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
