// SPDX-License-Identifier: AGPL-3.0-only
// FUX-1: a WINDOW for the SAME approved /preview/ built-in FX assets.
// No mixer renderer, audio callback, accepted graph, browser DSP or local PCM.
(() => {
  "use strict";
  const backend=window.__JUCE__?.backend;
  const protocol=window.ArSonApprovedNativeContract;
  if(!backend || !protocol)return; // Never activate this native-only shell on Pages.
  const registry=Object.freeze({
    "armix.builtin.channel-eq.v1":
      ["mixer-native-peq.js","ArSonMixNativeEq","#mxNativePeq"],
    "armix.builtin.channel-compressor.v1":
      ["mixer-native-compressor.js","ArSonMixNativeCompressor","#mxNativeComp"],
    "armix.builtin.graphic-eq31.v1":
      ["mixer-native-geq31.js","ArSonMixGraphicEq31","#mxNativeGeq31"],
    "armix.builtin.arson-harmony.v1":
      ["mixer-native-harmony.js","ArSonMixNativeHarmony","#mxNativeHarmony"],
    "armix.builtin.vocal-throw.v1":
      ["mixer-native-vocal-throw.js","ArSonMixNativeVocalThrow","#mxNativeVocalThrow"],
    "armix.builtin.8d-sonku.v1":
      ["mixer-native-sonku.js","ArSonMixNativeSonKu","#mxNativeSonKu"]
  });
  let current=null,identity=null,busyLoad=null,pending=null;
  const banner=document.getElementById("fxWindowStatus");
  const inform=message=>{
    if(banner){banner.textContent=message;banner.hidden=false;}
    const abort=document.getElementById("fxWindowAbort");
    if(abort)abort.hidden=false;
  };
  document.getElementById("fxWindowAbort")?.addEventListener("click",()=>{
    backend.emitEvent("arMixFloatingHide",{});
  });
  // This bridge exists only inside the native owned FX WebView2. A panel
  // header grabs the REAL HWND through Windows system-move, never an inner
  // CSS card. Preserve every original preset, Power, S/M/L and Close click.
  // Document capture beats each original editor's DOM drag listener.
  const dragHeaders=".mx-peq-title,.mx-comp-head,.mx-geq31-head,"+
    ".mx-harm-head,.vt-face-header,.sk-head";
  document.addEventListener("pointerdown",event=>{
    if(event.button!==0||!event.isTrusted)return;
    const head=event.target.closest?.(dragHeaders);
    if(!head||event.target.closest?.("button,select,input,textarea,a,[role=slider]"))return;
    event.preventDefault();
    event.stopImmediatePropagation();
    backend.emitEvent("arMixFloatingDrag",{});
  },true);
  const same=(a,b)=>String(a)===String(b);
  function project(snapshot,expected) {
    const next=protocol.normalizeSnapshot(snapshot);
    if(!next)return null;
    const selected=protocol.selectNativeReadModel(next,false);
    if(!["CHANNEL_ID","LOGICAL_ONLY","RETAINED_OFFLINE"].includes(selected.kind))
      return null; // NEEDS_REMAP is not an editing authority.
    const view=selected.view;
    if(!view||view.productLive===true && selected.kind!=="CHANNEL_ID")
      return null;
    if(selected.kind==="CHANNEL_ID" &&
       (view.admitted!==true||view.retained===true))return null;
    if(selected.kind!=="CHANNEL_ID" &&
       (view.admitted!==false||view.retained!==false||view.logical!==true))
      return null;
    const slot=expected.slot;
    if(!Number.isInteger(slot)||slot<0||slot>=8)return null;
    const channel=view.graph?.channels?.find(row=>same(row.id,expected.channelId));
    const insert=channel?.inserts?.[slot];
    if(!channel||!insert||insert.catalogId!==expected.catalogId||
       !same(insert.id,expected.instanceId))return null;
    return Object.freeze({view,channel,insert,kind:selected.kind});
  }
  // Every built-in editor uses the original approved native-contract readback.
  // Popup is a view of that source, NEVER a second authoritative mixer model.
  window.__ARMIX_NATIVE_M2__=Object.freeze({
    fxEditorState:()=>current?.view||null,
    state:()=>({current:current?.view||null}),
    nativeIdFor:track=>track && identity &&
      same(track.nativeId,identity.channelId)?identity.channelId:null,
    consumeAction(track,action,btn) {
      if(action!=="slotPower"||!identity||!current)return false;
      const slot=Number(btn?.dataset?.slot);
      if(!Number.isInteger(slot)||slot!==identity.slot ||
         !track||!same(track.nativeId,identity.channelId))return true;
      const verified=project(current.view.raw,identity);
      if(!verified)return true;
      const requestId="fx-window-"+crypto.randomUUID();
      backend.emitEvent("arMixFloatingPower",{
        requestId,channelId:identity.channelId,slot:identity.slot,
        instanceId:identity.instanceId,catalogId:identity.catalogId,
        deviceEpoch:verified.view.device.epoch,
        graphRevision:verified.view.graph.revision,
        workspaceToken:verified.view.workspaceToken||"",
        value:!verified.insert.bypassed,logical:verified.kind!=="CHANNEL_ID"
      });
      return true;
    },
    insertPlugin:()=>false
  });
  const scriptCache=new Map();
  function importEditor(catalog) {
    if(scriptCache.has(catalog))return scriptCache.get(catalog);
    const entry=registry[catalog];
    if(!entry)return Promise.reject(Error("Unapproved built-in FX type"));
    const loader=new Promise((resolve,reject)=>{
      const script=document.createElement("script");
      script.src="assets/"+entry[0]; // ONLY allowlisted, no user-controlled URLs.
      script.onload=()=>resolve(window[entry[1]]);
      script.onerror=()=>reject(Error("Approved FX asset unavailable"));
      document.head.append(script);
    });
    scriptCache.set(catalog,loader);
    return loader;
  }
  function mount(payload) {
    if(!payload||!Object.hasOwn(registry,payload.catalogId))return;
    const proposed=Object.freeze({
      channelId:String(payload.channelId||""),
      instanceId:String(payload.instanceId||""),
      catalogId:payload.catalogId,slot:Number(payload.slot)
    });
    const next=project(payload.snapshot,proposed);
    if(!next){inform("FX session changed · refresh FX Rack");return;}
    // Every revision-scoped logical identity is different after an accepted
    // offline Power edit. Only an explicit native ACK-authorized rekey from
    // THIS exact old instance/workspace may keep the original HWND alive.
    // Arbitrary same-slot/same-catalog replacement is NEVER auto-followed.
    if(identity && Object.keys(proposed).some(key=>identity[key]!==proposed[key])){
      const safeRekey=proposed.channelId===identity.channelId &&
        proposed.slot===identity.slot &&
        proposed.catalogId===identity.catalogId &&
        payload.rekeyFrom===identity.instanceId &&
        typeof payload.workspaceToken==="string" &&
        payload.workspaceToken.length>0 &&
        current?.view?.workspaceToken===payload.workspaceToken &&
        next.kind!=="CHANNEL_ID" &&
        next.view.workspaceToken===payload.workspaceToken;
      if(!safeRekey){inform("FX identity changed · reopen from FX Rack");return;}
    }
    identity=proposed;current=next;
    if(pending===proposed.catalogId) {
      // A repeated rack double-click must re-show the SAME exact editor
      // after its original Close button hid the DOM/owned HWND. Merely
      // raising the HWND would show an empty dark panel.
      const host=window[registry[proposed.catalogId][1]];
      if(host?.open) {
        host.open({id:proposed.channelId,nativeId:proposed.channelId,
          name:next.channel.name||"Channel",slotBypass:[]},proposed.slot);
        if(banner)banner.hidden=true;
      const abort=document.getElementById("fxWindowAbort");
      if(abort)abort.hidden=true;
      }
      return;
    }
    pending=proposed.catalogId;
    const request=proposed;
    importEditor(proposed.catalogId).then(host=>{
      if(identity!==request || !host?.open) {
        inform("Approved FX editor unavailable");return;
      }
      const track={id:request.channelId,nativeId:request.channelId,
        name:current.channel.name||"Channel",slotBypass:[]};
      host.open(track,request.slot); // exactly the approved /preview/ editor code
      const root=document.querySelector(registry[request.catalogId][2]);
      if(!root){inform("FX UI failed to mount");return;}
      if(banner)banner.hidden=true;
      const abort=document.getElementById("fxWindowAbort");
      if(abort)abort.hidden=true;
      // Size the real desktop HWND to the ORIGINAL FX hardware panel, not a
      // generic oversized mixer card. GEQ31 S/M/L updates the same HWND.
      let lastGeometry="";
      const measure=()=>{
        if(identity!==request||root.hidden)return;
        const rect=root.getBoundingClientRect();
        if(rect.width<200||rect.height<100)return;
        const width=Math.ceil(rect.width),height=Math.ceil(rect.height);
        const geometry=width+":"+height;
        if(geometry===lastGeometry)return; // no HWND/ResizeObserver feedback
        lastGeometry=geometry;
        backend.emitEvent("arMixFloatingSize",{width,height});
      };
      // Off-screen WebView2 can throttle rAF. Immediately emit the first
      // real approved FX geometry before revealing the desktop HWND.
      measure();
      requestAnimationFrame(()=>requestAnimationFrame(measure));
      if(typeof ResizeObserver==="function")
        new ResizeObserver(()=>requestAnimationFrame(measure)).observe(root);
      root.addEventListener("click",event=>{
        if(event.target.closest?.("[data-size]"))
          requestAnimationFrame(()=>requestAnimationFrame(measure));
      },true);
      // Editor close hides its owned top-level HWND instead of leaving a blank
      // window. DOM controls remain alive and can be re-presented safely.
      const observer=new MutationObserver(()=>{
        if(root.hidden)backend.emitEvent("arMixFloatingHide",{
          channelId:request.channelId,instanceId:request.instanceId,
          slot:request.slot,catalogId:request.catalogId});
      });
      observer.observe(root,{attributes:true,attributeFilter:["hidden"]});
      backend.emitEvent("arMixFloatingMounted",{
        catalogId:request.catalogId,channelId:request.channelId,
        instanceId:request.instanceId,slot:request.slot});
    }).catch(error=>inform(String(error.message||error)));
  }
  backend.addEventListener("arMixFloatingOpen",mount);
  backend.addEventListener("arMixConsoleSnapshot",snapshot=>{
    if(!identity)return;
    const next=project(snapshot,identity);
    current=next;
    if(!next)inform("FX disconnected · waiting for a matching saved editor");
  });
  backend.addEventListener("arMixNativeFxPowerAck",ack=>{
    if(ack?.ok!==true)inform("FX power unchanged · refresh native FX Rack");
  });
  backend.emitEvent("arMixFloatingReady",{gui:"APPROVED_PAGES_NATIVE_FX_WINDOW_V1"});
})();