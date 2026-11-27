"""FUX-1: actual Chromium smoke of SEVEN separately hosted original native FX UIs.

The tested URL is the native-only fx-window.html embedded in the same approved
/preview/ resource bundle. Browser backend is an explicit CONTROL-only fake:
NO audio/ASIO/Win32 ownership is claimed by a Chromium result.
"""
from __future__ import annotations
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
import importlib.util
import json
import tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
CASES=(
 ("armix.builtin.channel-eq.v1","#mxNativePeq"),
 ("armix.builtin.channel-compressor.v1","#mxNativeComp"),
 ("armix.builtin.graphic-eq31.v1","#mxNativeGeq31"),
 ("armix.builtin.auto-ducker.v1","#mxNativeDucker"),
 ("armix.builtin.arson-harmony.v1","#mxNativeHarmony"),
 ("armix.builtin.vocal-throw.v1","#mxNativeVocalThrow"),
 ("armix.builtin.8d-sonku.v1","#mxNativeSonKu"),
)
CHANNEL="9007199254740993"
INSTANCE="logical:offline-smix-fux:12:"+CHANNEL+":0"


def document(catalog):
    fixture=json.loads((ROOT/"integration/console-v6/fixtures.json").read_text("utf-8"))["g0"]
    raw=json.loads(json.dumps(fixture))
    raw["mixerSession"]={"authority":"NONE","deviceEpoch":raw["device"]["epoch"]}
    raw["logicalMixer"]={
      "schema":"arsonmix.native.logical-mixer.v1","authority":"LOGICAL_ONLY",
      "workspaceToken":"offline-smix-fux","readOnly":False,"revision":"12",
      "physicalOutputAdmitted":False,"outputArmed":False,
      "graph":{"revision":"12","rosterRevision":"12",
       "capabilities":{"callbackOwnsChannelIds":False,
                       "supportedOps":["SET_FX_SLOT_BYPASS"]},
       "channels":[{"id":CHANNEL,"role":"INPUT","name":"FX Test",
          "processingFormat":"STEREO","controls":{"faderDb":0,"pan":0,
          "mute":False,"solo":False},
          "fxRackBypassed":False,
          "inserts":[{"id":INSTANCE,"catalogId":catalog,
                     "name":"Approved built-in","bypassed":False}],"sends":[]}],
       "edges":[]}
    }
    return raw


INIT="""(() => {
  window.__fxOutbound=[];
  const handlers=new Map();
  window.__fxDispatch=(name,payload)=>{
    for(const listener of handlers.get(name)||[])listener(payload);
  };
  const backend={
    addEventListener(name,listener){
      const list=handlers.get(name)||[];list.push(listener);handlers.set(name,list);
    },
    emitEvent(name,payload){window.__fxOutbound.push({name,payload});}
  };
  window.__JUCE__={backend};
})()"""


def main():
    spec=importlib.util.spec_from_file_location(
        "approved_bundle",ROOT/"scripts/build_approved_native_gui.py")
    mod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    with tempfile.TemporaryDirectory(prefix="armix-owned-fx-") as directory:
        out=Path(directory)
        mod.stage(out)
        assert (out/"fx-window.html").is_file()
        assert (out/"assets/approved-floating-fx-bridge.js").is_file()
        class Handler(SimpleHTTPRequestHandler):
            def log_message(self,*args):pass
        server=ThreadingHTTPServer(("127.0.0.1",0),
               partial(Handler,directory=str(out)))
        thread=Thread(target=server.serve_forever,daemon=True)
        thread.start()
        try:
            with sync_playwright() as pw:
                browser=pw.chromium.launch(headless=True)
                try:
                    url=f"http://127.0.0.1:{server.server_address[1]}/fx-window.html"
                    chrome_reference=None
                    for catalog,selector in CASES:
                        page=browser.new_page(viewport={"width":1580,"height":900},
                                               reduced_motion="reduce")
                        errors=[]
                        page.on("pageerror",lambda error:errors.append(str(error)))
                        try:
                            page.add_init_script(INIT)
                            page.goto(url)
                            page.wait_for_function("""()=>
                              window.__fxOutbound?.some(x=>x.name==='arMixFloatingReady')""")
                            payload={"snapshot":document(catalog),
                                "channelId":CHANNEL,"slot":0,"catalogId":catalog,
                                "instanceId":INSTANCE}
                            page.evaluate("(p)=>window.__fxDispatch('arMixFloatingOpen',p)",payload)
                            root=page.locator(selector)
                            root.wait_for(state="visible")
                            assert root.get_attribute("role")=="dialog",catalog
                            assert root.get_attribute("aria-modal")=="false",catalog
                            assert page.evaluate("""()=>
                                window.__ARMIX_NATIVE_M2__?.fxEditorState()?.logical===true"""),catalog
                            assert page.evaluate("""()=>
                                window.__fxOutbound.some(x=>x.name==='arMixFloatingMounted')"""),catalog
                            if "graphic-eq31" in catalog:
                                assert root.locator(".mx-geq31-range").count()==31
                                assert root.locator(".mx-geq31-size").count()==3
                            # Native HWND resizes from original panel content.
                            page.wait_for_function("""()=>
                                window.__fxOutbound.some(x=>x.name==='arMixFloatingSize')""")
                            # Seven originals are painted edge-to-edge. The only
                            # visible Close is the original hardware editor,
                            # never a second JUCE caption or fallback key.
                            assert page.locator("#fxWindowAbort").is_hidden(),catalog
                            if "graphic-eq31" in catalog:
                                assert root.locator(".mx-geq31-close").count()==1
                                actual=root.bounding_box()
                                native_size=page.evaluate("""()=>window.__fxOutbound
                                  .filter(x=>x.name==='arMixFloatingSize').at(-1)?.payload""")
                                assert actual and native_size
                                assert abs(actual["width"]-native_size["width"])<=2
                                assert abs(actual["height"]-native_size["height"])<=2
                                # The original header must initiate OS HWND drag,
                                # not move a CSS card inside its fixed WebView.
                                page.mouse.move(actual["x"]+30,actual["y"]+18)
                                page.mouse.down()
                                page.mouse.up()
                                assert page.evaluate("""()=>window.__fxOutbound.some(
                                  e=>e.name==='arMixFloatingDrag')""")
                                moved=root.bounding_box()
                                assert abs(moved["x"]-actual["x"])<1
                                assert abs(moved["y"]-actual["y"])<1
                                # S/M/L remeasure the real frameless HWND. No
                                # titlebar/body padding or bogus 1500x520 resize.
                                root.locator(".mx-geq31-size[data-size='S']").click()
                                page.wait_for_function("""()=>window.__fxOutbound.some(
                                  e=>e.name==='arMixFloatingSize'&&
                                  e.payload.width>=710&&e.payload.width<=720&&
                                  e.payload.height>=138&&e.payload.height<=142)""")
                                root.locator(".mx-geq31-size[data-size='L']").click()
                                page.wait_for_function("""()=>window.__fxOutbound
                                  .filter(e=>e.name==='arMixFloatingSize').at(-1)
                                  ?.payload.width>=1428""")
                            # Exact shared chrome: compare actual rendered CSS and
                            # physical hit boxes, not class names or a mockup.
                            if catalog in ("armix.builtin.channel-eq.v1",
                                "armix.builtin.graphic-eq31.v1","armix.builtin.auto-ducker.v1"):
                                chrome=root.evaluate("""root=>{
                                  const power=root.querySelector('.armix-builtin-fx-power');
                                  const close=root.querySelector('.armix-builtin-fx-close');
                                  const props=['width','height','minWidth','maxWidth','minHeight','maxHeight',
                                    'borderRadius','borderTopColor','borderTopWidth','backgroundImage',
                                    'boxShadow','color','fontSize','lineHeight','filter','opacity'];
                                  const read=el=>Object.fromEntries(props.map(k=>[k,getComputedStyle(el)[k]]));
                                  const before=power.dataset.fxState,disabled=power.disabled;
                                  const result={close:read(close),icon:getComputedStyle(close,'::after').backgroundImage};
                                  for(const state of ['on','off','rack-off','unknown']){
                                    power.dataset.fxState=state;power.disabled=['rack-off','unknown'].includes(state);
                                    result[state]={...read(power),fontFamily:getComputedStyle(power).fontFamily,
                                      fontWeight:getComputedStyle(power).fontWeight};
                                  }
                                  power.dataset.fxState=before;power.disabled=disabled;
                                  return result;
                                }""")
                                if chrome_reference is None: chrome_reference=chrome
                                else: assert chrome==chrome_reference,(catalog,chrome,chrome_reference)
                                if "auto-ducker" in catalog:
                                    assert root.locator(".armix-builtin-fx-power").get_attribute("data-fx-state")=="on"
                                    root.locator(".armix-builtin-fx-power").click()
                                    assert root.locator(".armix-builtin-fx-power").get_attribute("data-fx-state")=="on"
                                    root.screenshot(path=str(ROOT/"auto-ducker-native-owned.png"))
                            # A popup has no independent PCM. Power goes to
                            # exactly the native ChannelId/slot/instance and
                            # requires later ACK/readback to change appearance.
                            power=page.evaluate("""(channel)=>{
                                window.__ARMIX_NATIVE_M2__.consumeAction(
                                  {nativeId:channel},"slotPower",
                                  {dataset:{slot:"0"}});
                                return window.__fxOutbound.filter(
                                  x=>x.name==='arMixFloatingPower').at(-1)?.payload;
                            }""",CHANNEL)
                            assert power is not None,(catalog,"no native power intent")
                            assert power["channelId"]==CHANNEL
                            assert power["slot"]==0
                            assert power["instanceId"]==INSTANCE
                            assert power["catalogId"]==catalog
                            assert power["workspaceToken"]=="offline-smix-fux"
                            assert power["logical"] is True
                            assert power["value"] is True
                            # Closing the original editor must hide its HWND;
                            # a double-click must reopen the *same* module.
                            handle=page.evaluate("""(catalog)=>{
                                const key={
                                  "armix.builtin.channel-eq.v1":"ArSonMixNativeEq",
                                  "armix.builtin.channel-compressor.v1":"ArSonMixNativeCompressor",
                                  "armix.builtin.auto-ducker.v1":"ArSonMixNativeDucker",
                                  "armix.builtin.graphic-eq31.v1":"ArSonMixGraphicEq31",
                                  "armix.builtin.arson-harmony.v1":"ArSonMixNativeHarmony",
                                  "armix.builtin.vocal-throw.v1":"ArSonMixNativeVocalThrow",
                                  "armix.builtin.8d-sonku.v1":"ArSonMixNativeSonKu"
                                }[catalog];
                                if(catalog==="armix.builtin.auto-ducker.v1")
                                  document.querySelector("#mxNativeDucker .armix-builtin-fx-close").click();
                                else window[key].close();
                                return key;
                            }""",catalog)
                            root.wait_for(state="hidden")
                            page.evaluate("(p)=>window.__fxDispatch('arMixFloatingOpen',p)",payload)
                            root.wait_for(state="visible")
                            assert page.evaluate("(key)=>!!window[key]",handle)
                            # A different instance cannot steal this editor.
                            forged={**payload,"instanceId":"wrong-instance"}
                            page.evaluate("(p)=>window.__fxDispatch('arMixFloatingOpen',p)",forged)
                            assert root.is_visible(),catalog
                            accepted_id=page.evaluate("""()=>window.__ARMIX_NATIVE_M2__
                              .fxEditorState()?.graph?.channels?.[0]?.inserts?.[0]?.id""")
                            assert accepted_id==INSTANCE,(catalog,accepted_id)
                            # A native-owned logical Power commit bumps the
                            # revision-scoped insert token. Reject arbitrary
                            # identity changes, but accept a rekey explicitly
                            # authorized by the matching durable native ACK.
                            revised=document(catalog)
                            revised_id="logical:offline-smix-fux:13:"+CHANNEL+":0"
                            revised["logicalMixer"]["revision"]="13"
                            revised["logicalMixer"]["graph"]["revision"]="13"
                            revised["logicalMixer"]["graph"]["rosterRevision"]="13"
                            revised["logicalMixer"]["graph"]["channels"][0]["inserts"][0]["id"]=revised_id
                            revised["logicalMixer"]["graph"]["channels"][0]["inserts"][0]["bypassed"]=True
                            rekey={**payload,"snapshot":revised,"instanceId":revised_id,
                                   "workspaceToken":"offline-smix-fux","rekeyFrom":INSTANCE}
                            wrong={**rekey,"rekeyFrom":"unrelated-insert"}
                            page.evaluate("(p)=>window.__fxDispatch('arMixFloatingOpen',p)",wrong)
                            assert page.evaluate("""()=>window.__ARMIX_NATIVE_M2__
                              .fxEditorState()?.graph?.channels?.[0]?.inserts?.[0]?.id""")==INSTANCE
                            page.evaluate("(p)=>window.__fxDispatch('arMixFloatingOpen',p)",rekey)
                            root.wait_for(state="visible")
                            assert page.evaluate("""()=>window.__ARMIX_NATIVE_M2__
                              .fxEditorState()?.graph?.channels?.[0]?.inserts?.[0]?.id""")==revised_id
                            next_power=page.evaluate("""(channel)=>{
                                window.__ARMIX_NATIVE_M2__.consumeAction(
                                  {nativeId:channel},"slotPower",
                                  {dataset:{slot:"0"}});
                                return window.__fxOutbound.filter(
                                  x=>x.name==='arMixFloatingPower').at(-1)?.payload;
                            }""",CHANNEL)
                            assert next_power["instanceId"]==revised_id
                            assert next_power["graphRevision"]=="13"
                            assert next_power["value"] is False
                            if "auto-ducker" in catalog:
                                assert root.locator(".armix-builtin-fx-power").get_attribute("data-fx-state")=="off"
                            if "graphic-eq31" in catalog:
                                assert root.locator(".mx-geq31-power").get_attribute(
                                    "data-fx-state")=="off"
                            assert not errors,(catalog,errors)
                            print("PASS approved native FX popup",catalog)
                        finally:
                            page.close()
                finally:
                    browser.close()
        finally:
            server.shutdown()
    print("PASS seven original /preview/ FX editors loaded in separate native-only browser surface; no hardware certification")


if __name__=="__main__":
    main()
