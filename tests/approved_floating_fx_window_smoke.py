"""FUX-1: actual Chromium smoke of SIX separately hosted original native FX UIs.

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
                            assert not errors,(catalog,errors)
                            print("PASS approved native FX popup",catalog)
                        finally:
                            page.close()
                finally:
                    browser.close()
        finally:
            server.shutdown()
    print("PASS six original /preview/ FX editors loaded in separate native-only browser surface; no hardware certification")


if __name__=="__main__":
    main()
