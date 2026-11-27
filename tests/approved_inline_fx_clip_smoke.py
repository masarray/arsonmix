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
BACKEND="""(()=>{
    const listeners=new Map();
    window.__nativeMessages=[];
    window.__JUCE__={backend:{
        emitEvent(name,payload){window.__nativeMessages.push({name,payload});},
        addEventListener(name,fn){listeners.set(name,fn);}
    }};
})();"""

def verify_overlapping_editor_focus(page):
    """Real hit-testing, not only style/z-index or fake DOM clicks."""
    page.evaluate("""()=>{
      const clip=window.__ARMIX_NATIVE_INLINE_FX_CLIP__;
      const track={id:22,name:'TEST CHANNEL',slotBypass:[false,false]};
      const comp={authority:"CHANNEL_ID",channelId:"22",slot:0,
        instanceId:"focus-comp",deviceEpoch:"51",graphRevision:"46"};
      const geq={...comp,slot:1,instanceId:"focus-geq"};
      if(!clip.openExact(window.ArSonMixNativeCompressor,'ArSonMixNativeCompressor',track,0,comp.instanceId,comp)||
         !clip.openExact(window.ArSonMixGraphicEq31,'ArSonMixGraphicEq31',track,1,geq.instanceId,geq))
        throw Error("Unable to open both exact editor surfaces");
      const place=(id,x,y)=>{const root=document.getElementById(id),r=root.getBoundingClientRect();
        clip.move(root,x-r.left,y-r.top);};
      place('mxNativeComp',160,205);place('mxNativeGeq31',240,125);
    }""")
    comp=page.locator("#mxNativeComp")
    geq=page.locator("#mxNativeGeq31")
    def active():
        return page.evaluate("()=>window.ArSonMixFxWorkspace.activeEditor()?.id")
    def at(x,y):
        return page.evaluate("""([x,y])=>document.elementFromPoint(x,y)
          ?.closest('#mxNativeComp,#mxNativeGeq31')?.id""",[x,y])
    assert active()=="mxNativeGeq31"
    cb,gb=comp.bounding_box(),geq.bounding_box()
    x=max(cb["x"],gb["x"])+25
    y=max(cb["y"],gb["y"])+65
    assert at(x,y)=="mxNativeGeq31",(cb,gb,x,y)
    # Click an exposed inert section of the real Compressor face, not its
    # header or a knob that would intentionally generate a DSP request.
    def exposed(root):
        return page.evaluate("""id=>{
          const panel=document.getElementById(id),r=panel.getBoundingClientRect();
          for(let y=Math.max(5,r.top+55);y<Math.min(innerHeight-5,r.bottom-5);y+=9)
            for(let x=Math.max(5,r.left+5);x<Math.min(innerWidth-5,r.right-5);x+=9){
              const el=document.elementFromPoint(x,y);
              if(el&&el.closest('#'+id)&&!el.closest('button,input,select,textarea,svg,a,label,[role=slider]'))
                return [x,y];
            }
          return null;
        }""",root)
    point=exposed("mxNativeComp");assert point
    before=page.evaluate("""()=>window.__nativeMessages.filter(m=>
        m.name==='arMixConsoleIntent'||/Set$/.test(m.payload?.action||'')).length""")
    page.mouse.click(*point)
    assert active()=="mxNativeComp" and at(x,y)=="mxNativeComp"
    assert comp.bounding_box()==cb and geq.bounding_box()==gb
    # Keyboard focus of a control in the lower editor raises the exact root.
    geq.locator(".mx-geq31-size").first.focus()
    assert active()=="mxNativeGeq31" and at(x,y)=="mxNativeGeq31"
    # Repeat exact rack open must NOT call host.open or reset DSP drafts.
    assert page.evaluate("""()=>{
      const clip=window.__ARMIX_NATIVE_INLINE_FX_CLIP__;
      const lease={authority:"CHANNEL_ID",channelId:"22",slot:0,
        instanceId:"focus-comp",deviceEpoch:"51",graphRevision:"46"};
      return clip.openExact({open(){throw Error("same instance reinitialized")}},
        'ArSonMixNativeCompressor',{id:22,name:'TEST CHANNEL'},0,lease.instanceId,lease);
    }""")
    assert active()=="mxNativeComp" and comp.bounding_box()==cb
    # Bounded z-order after repeated actual focus transfers.
    for _ in range(32):
        geq.locator(".mx-geq31-size").first.focus()
        comp.locator(".mx-comp-close").focus()
    layers=page.evaluate("""()=>['mxNativeComp','mxNativeGeq31'].map(id=>
      Number(getComputedStyle(document.getElementById(id)).zIndex))""")
    assert min(layers)>=9000 and max(layers)<=9007 and layers[0]>layers[1],layers
    # Real preset cancellation precedes editor close.
    comp.locator(".mx-fxp-form").evaluate("el=>el.hidden=false")
    page.keyboard.press("Escape")
    assert comp.locator(".mx-fxp-form").is_hidden() and comp.is_visible() and geq.is_visible()
    # A higher operator modal keeps focus and Escape out of the FX manager.
    page.evaluate("""()=>{
      const el=document.getElementById('fastPicker');el.hidden=false;
      el.style.setProperty('position','fixed','important');
      el.style.setProperty('left','320px','important');
      el.style.setProperty('top','255px','important');
    }""")
    assert page.evaluate("""()=>document.elementFromPoint(340,270)?.closest('#fastPicker')!==null""")
    page.keyboard.press("Escape")
    assert comp.is_visible() and geq.is_visible()
    page.evaluate("document.getElementById('fastPicker').hidden=true")
    page.keyboard.press("Escape")
    assert comp.is_hidden() and geq.is_visible()
    page.keyboard.press("Escape")
    assert geq.is_hidden()
    after=page.evaluate("""()=>window.__nativeMessages.filter(m=>
        m.name==='arMixConsoleIntent'||/Set$/.test(m.payload?.action||'')).length""")
    assert after==before,(before,after)
    print("PASS P0-C overlap hit-testing, pointer/focus/rack refocus, 64 raises, popup Escape, zero control writes")

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
            # Narrow, tablet, midrange and desktop mixer viewport overlay.
            for width,height in ((390,844),(720,720),(1040,700),(1680,850)):
                page=browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror",lambda error: errors.append(str(error)))
                page.add_init_script(BACKEND)
                page.goto(f"http://127.0.0.1:{server.server_address[1]}/index.html")
                # add_init_script must INVOKE the JUCE transport stub, not
                # merely declare an anonymous function expression. Fail
                # immediately with useful diagnostic if backend boot regresses.
                assert page.evaluate("()=>typeof window.__JUCE__?.backend?.emitEvent==='function'"),(
                    "Native transport mock never started",width,errors)
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
                    assert root.locator(".armix-builtin-fx-close, .mx-geq31-close, .mx-harm-close, .sk-close").count()>=1,(width,host)
                    # The viewport may be narrow, but original control
                    # geometry must stay full-size and horizontally pannable.
                    if width==390:
                        if host=="ArSonMixNativeEq":
                            assert root.locator(".mx-peq-band").first.evaluate(
                                "e=>getComputedStyle(e).flexDirection")=="row"
                            assert root.locator(".mx-peq-dial").first.evaluate(
                                "e=>Math.abs(e.getBoundingClientRect().width-62)<1")
                        if host=="ArSonMixNativeCompressor":
                            assert root.locator(".mx-comp-controls").evaluate(
                                "e=>getComputedStyle(e).gridTemplateColumns.split(' ').length")==3
                        if host=="ArSonMixNativeHarmony":
                            assert root.locator(".mx-harm-body").evaluate(
                                "e=>getComputedStyle(e).gridTemplateColumns.split(' ').length")==3
                        if host=="ArSonMixNativeVocalThrow":
                            assert root.locator(".vt-instrument").evaluate(
                                "e=>getComputedStyle(e).gridTemplateColumns.split(' ').length")==3
                        if host=="ArSonMixNativeSonKu":
                            assert root.locator(".sk-content").evaluate(
                                "e=>getComputedStyle(e).gridTemplateColumns.split(' ').length")==2
                    # Native-only popup HUD and second native titlebar never
                    # exist: the browser frame already IS the only mixer GUI.
                    assert page.locator("#fxWindowAbort").count()==0
                    page.evaluate("""selector=>window.__ARMIX_NATIVE_INLINE_FX_CLIP__
                        .center(document.querySelector(selector))""",selector)
                    if expected>width:
                        assert root.bounding_box()["x"]>=0,(width,host)
                    assert not any("TypeError" in e for e in errors),errors
                    if host!="ArSonMixGraphicEq31":
                        # Exercise the production shared overlay with a deterministic
                        # accepted view fixture; no claim of native PCM/ASIO.
                        result=page.evaluate("""({host,selector})=>{
                          const device={epoch:"51",sampleRate:48000,bufferFrames:256,
                            enabledInputs:[0,1],enabledOutputs:[0,1]};
                          const id="9007199254740993",catalogId=window[host].id;
                          const lease={authority:"CHANNEL_ID",channelId:id,slot:0,
                            instanceId:"exact-owner",catalogId,graphRevision:"46",
                            rosterRevision:"19",workspaceToken:null,deviceEpoch:"51",
                            hardwareConfiguration:JSON.stringify([48000,256,[0,1],[0,1]])};
                          const insert={id:"exact-owner",catalogId,bypassed:false};
                          const row={id,role:"INPUT",fxRackBypassed:false,inserts:[insert]};
                          const view={admitted:true,retained:false,device,raw:{
                            globalFxBypassed:false,mixerSession:{authority:"CHANNEL_ID",deviceEpoch:"51"}},
                            graph:{revision:"46",rosterRevision:"19",
                              capabilities:{callbackOwnsChannelIds:true},channels:[row]}};
                          const clip=window.__ARMIX_NATIVE_INLINE_FX_CLIP__;
                          if(!clip.openExact(window[host],host,
                            {id:22,name:"TEST CHANNEL",slotBypass:[false]},0,"exact-owner",lease))
                            return {error:"not-opened"};
                          const root=document.querySelector(selector),results=[],centering=[];
                          const inspect=()=>{clip.syncBypass(view);
                            const shades=root.querySelectorAll(".armix-native-fx-bypass");
                            const s=shades[0];
                            results.push([shades.length,s.textContent,
                              getComputedStyle(s).display,getComputedStyle(s).pointerEvents]);
                            if(host==="ArSonMixNativeHarmony"&&s.classList.contains("is-on")){
                              const range=document.createRange();range.selectNodeContents(s);
                              const a=range.getBoundingClientRect(),b=root.getBoundingClientRect();
                              const h=root.querySelector(".mx-harm-head").getBoundingClientRect();
                              centering.push([Math.abs((a.left+a.right-b.left-b.right)/2),
                                Math.abs((a.top+a.bottom-b.top-b.bottom)/2),
                                Math.abs(s.getBoundingClientRect().top-h.bottom)]);
                            }};
                          inspect();insert.bypassed=true;inspect();
                          row.fxRackBypassed=true;inspect();
                          view.raw.globalFxBypassed=true;inspect();
                          view.raw.globalFxBypassed=false;inspect();
                          row.fxRackBypassed=false;inspect();
                          insert.bypassed=false;inspect();
                          view.graph.revision="47";inspect();
                          insert.id="replacement";inspect();
                          return {results,centering};
                        }""",{"host":host,"selector":selector})
                        assert result.get("results")==[
                            [1,"BYPASSED","none","none"],
                            [1,"BYPASSED","flex","none"],
                            [1,"FX RACK BYPASSED","flex","none"],
                            [1,"GLOBAL FX BYPASSED","flex","none"],
                            [1,"FX RACK BYPASSED","flex","none"],
                            [1,"BYPASSED","flex","none"],
                            [1,"BYPASSED","none","none"],
                            [1,"BYPASSED","none","none"],
                            [1,"FX UNAVAILABLE","flex","auto"],
                        ],(host,width,result)
                        if host=="ArSonMixNativeHarmony":
                            assert len(result["centering"])==6,(width,result)
                            assert all(x<=2 and y<=3 and header<=2
                                       for x,y,header in result["centering"]),(width,result)
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
                # Native original Close is physically hidden behind mixer
                # clipping until the operator pans to the right-hand controls.
                closer=root.locator(".mx-geq31-close")
                page.evaluate("""()=>{
                  const root=document.getElementById('mxNativeGeq31');
                  const button=root.querySelector('.mx-geq31-close')
                    .getBoundingClientRect();
                  if(button.right>innerWidth-12)
                    window.__ARMIX_NATIVE_INLINE_FX_CLIP__.move(
                        root,innerWidth-button.right-12,0);
                }""")
                box=closer.bounding_box()
                assert box and box["x"]>=0 and box["x"]+box["width"]<=width
                closer.click()
                assert root.is_hidden(),(width,"Close not recoverable by pan")
                page.evaluate("""()=>window.ArSonMixGraphicEq31.open(
                    {id:22,name:'TEST CHANNEL',slotBypass:[false]},0)""")
                page.evaluate("""()=>window.__ARMIX_NATIVE_INLINE_FX_CLIP__
                    .center(document.getElementById('mxNativeGeq31'))""")
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
                page.mouse.move(min(width-50,190),moved["y"]+15)
                page.keyboard.down("Shift")
                page.mouse.wheel(0,260)
                page.keyboard.up("Shift")
                page.wait_for_timeout(80)
                after_wheel=root.bounding_box()
                assert after_wheel["x"]<moved["x"]-60,(width,moved,after_wheel)
                # Real native clipped GEQ31 must NOT jump when FX Rack
                # refocuses the same insert; unlike just reading source,
                # this exercises the original host.open() path repeatedly.
                clip_open="""()=>window.__ARMIX_NATIVE_INLINE_FX_CLIP__.openExact(
                    window.ArSonMixGraphicEq31,'ArSonMixGraphicEq31',
                    {id:22,name:'TEST CHANNEL',slotBypass:[false]},0,'geq-lease-22')"""
                assert page.evaluate(clip_open)
                panel=root.bounding_box()
                page.evaluate("""()=>window.__ARMIX_NATIVE_INLINE_FX_CLIP__.move(
                    document.getElementById('mxNativeGeq31'),-35,0)""")
                placed=root.bounding_box()
                assert placed["x"]<panel["x"]-20,(width,panel,placed)
                for _ in range(3):
                    assert page.evaluate(clip_open)
                    again=root.bounding_box()
                    assert abs(again["x"]-placed["x"])<1.5,(width,placed,again)
                    assert abs(again["y"]-placed["y"])<1.5,(width,placed,again)
                # S/M/L must resize about this location, not silently center.
                page.evaluate("""()=>window.ArSonMixGraphicEq31.setSize('S')""")
                scaled=root.bounding_box()
                assert abs(scaled["x"]-placed["x"])<1.5,(width,placed,scaled)
                page.evaluate("""()=>window.ArSonMixGraphicEq31.setSize('L')""")
                # Plain F11 is emitted to native JUCE. Chromium itself cannot
                # prove the OS HWND mode, which needs Windows host qualification.
                prior=page.evaluate("""()=>window.__nativeMessages.filter(
                    m=>m.name==='arMixWindowFullscreen').length""")
                page.keyboard.press("F11")
                page.keyboard.press("F11")
                sent=page.evaluate("""()=>window.__nativeMessages.filter(
                    m=>m.name==='arMixWindowFullscreen'&&
                    m.payload.operation==='TOGGLE').length""")
                assert sent==prior+2,(width,prior,sent,errors)
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
                    # The shared FX Rack recovery shortcut MUST delegate to
                    # the native clipped geometry owner. The old Pages helper
                    # wrote an independent fixed/transform, breaking pan/scale.
                    transform=root.evaluate("e=>e.style.getPropertyValue('transform')")
                    assert page.evaluate("""()=>window.ArSonMixFxWorkspace.recenter(
                        document.getElementById('mxNativeGeq31'))""")
                    assert root.evaluate("e=>e.style.getPropertyValue('transform')")==transform,(
                        width,mode,"legacy rack recenter altered hardware transform")
                    assert abs(root.bounding_box()["width"]-1430*ratio)<2
                # Built-in DOM open never issues a request to construct the
                # rejected detached JUCE host (other snapshot intents allowed).
                assert not page.evaluate("""()=>window.__nativeMessages.some(
                    x=>x.name==='arMixAction'&&
                    x.payload?.action==='floatingBuiltinOpen')""")
                if width==1680:
                    page.evaluate("window.ArSonMixGraphicEq31.close()")
                    verify_overlapping_editor_focus(page)
                assert not errors,(width,errors)
                page.close()
            print("PASS FUX-1 exact one-WebView clipped overlay, six native faces, "
                  "31 GEQ faders, pointer drag, Shift-wheel pan and S/M/L")
        finally:
            browser.close()
            server.shutdown()

if __name__=="__main__":
    main()
