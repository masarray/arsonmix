// SPDX-License-Identifier: AGPL-3.0-only
// ONE approved /preview/ DOM, native authority only. No new UI, no JS PCM.
(() => {
  "use strict";
  const backend = window.__JUCE__?.backend;
  const protocol = window.ArSonApprovedNativeContract;
  const deviceVM = window.ArSonApprovedDeviceViewModel;
  const stripVM = window.ArSonApprovedChannelStripViewModel;
  const fxRackVM = window.ArSonApprovedFxRackViewModel;
  const wireVM = window.ArSonApprovedWireViewModel;
  // The public GUI owns one lexical model. Native-only bundle exports a narrow
  // reference to that SAME model; never reconstruct a parallel DOM/DSP.
  const scope = window.__ARMIX_NATIVE_SCOPE__ || null;
  const connected = Boolean(backend?.emitEvent && backend?.addEventListener &&
    protocol && deviceVM && stripVM && fxRackVM && wireVM);
  // F11 belongs to the mixer HWND, not the child WebView2 document.
  // Keep modified F11 available to existing native hotkey capture.
  if(backend?.emitEvent && backend?.addEventListener) {
    // WebView2 starts before the five native ViewModels finish admission.
    // A window command must not silently disappear during that bootstrap.
    window.addEventListener("keydown",event=>{
      if(!(event.key==="F11"||event.code==="F11"||event.keyCode===122)||
         event.repeat||event.altKey||event.ctrlKey||event.metaKey||event.shiftKey)return;
      event.preventDefault();event.stopImmediatePropagation();
      backend.emitEvent("arMixWindowFullscreen",{operation:"TOGGLE"});
    },true);
    backend.addEventListener("arMixWindowFullscreenState",state=>{
      if(typeof state?.active==="boolean")
        document.documentElement.dataset.operatorFullscreen=
          state.active?"true":"false";
    });
  }
  // Public browser preview keeps its header; the native JUCE caption replaces it.
  if (backend?.emitEvent && backend?.addEventListener)
    document.documentElement?.classList?.add?.("armix-native-desktop");
  // The old M1 wording is retained as an explicit boot-time fallback for
  // release-train compatibility, NOT a claim that a demo graph is audio.
  const fallback = "M1_VISUAL_BUNDLE_ONLY__NOT_AUDIO_READY";
  const flags = {acceptedGraphConnected:false,productionCallbackLive:false};
  const nativeByUiId = new Map();       // SMALL UI ordinal -> EXACT decimal-string native ChannelId
  const uiByNativeId = new Map();       // EXACT uint64 string -> local view-only ordinal
  const wireByUiId = new Map();         // view-only EdgeId -> EXACT decimal-string native EdgeId
  const uiByWireId = new Map();

  // Owner-corrected FUX-1: built-in FX are ORIGINAL /preview/ overlays
  // clipped by this mixer's ONE WebView2 viewport, never separate JUCE HWNDs.
  // Their audio, controls, .smix, ACK/readback still use the existing owner.
  const inlineFxClip=connected &&
    typeof document.createElement==="function" &&
    typeof document.head?.appendChild==="function" &&
    typeof document.addEventListener==="function"?(()=>{
    const selectors=Object.freeze({
      ArSonMixNativeEq:"#mxNativePeq",
      ArSonMixNativeCompressor:"#mxNativeComp",
      ArSonMixNativeAntiNoiz:"#mxNativeAntiNoiz",
      ArSonMixGraphicEq31:"#mxNativeGeq31",
      ArSonMixNativeHarmony:"#mxNativeHarmony",
      ArSonMixNativeVocalThrow:"#mxNativeVocalThrow",
      ArSonMixNativeSonKu:"#mxNativeSonKu",
      ArSonMixNativeDeEzzer:"#mxNativeDeEzzerHost"
    });
    const roots=Object.values(selectors).join(",");
    const headers=".mx-peq-title,.mx-comp-head,.mx-geq31-head,"+
      ".mx-harm-head,.vt-face-header,.sk-head,.mx-dez-head,.an-head";
    const style=document.createElement("style");
    style.id="armix-native-inline-fx-clip";
    style.textContent="html.armix-native-desktop,html.armix-native-desktop body{overflow-x:hidden!important}\nhtml.armix-native-desktop #mxNativeGeq31{width:1430px!important;min-width:1430px!important;max-width:none!important;height:280px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-body{grid-template-columns:38px minmax(0,1fr) 100px!important;padding:1px 32px 0 4px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-scale{width:38px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-head{gap:8px!important;padding:0 20px!important;height:44px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-brand{font-size:17px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-meta,html.armix-native-desktop #mxNativeGeq31 .mx-geq31-channel{display:inline!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-head select{flex:0 0 152px!important;width:152px!important;max-width:152px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-save{display:block!important;width:56px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-flat{width:44px!important}\nhtml.armix-native-desktop #mxNativeGeq31 .mx-geq31-head .mx-geq31-close{margin-inline-start:90px!important}\nhtml.armix-native-desktop #mxNativePeq{width:795px!important;max-height:none!important}\nhtml.armix-native-desktop #mxNativeComp{width:795px!important;max-height:none!important}\nhtml.armix-native-desktop #mxNativeHarmony{width:800px!important;max-height:none!important}\nhtml.armix-native-desktop #mxNativeVocalThrow{width:778px!important;max-height:none!important}\nhtml.armix-native-desktop #mxNativeSonKu{width:790px!important;max-height:none!important}\nhtml.armix-native-desktop :is(.mx-peq-title,.mx-comp-head,.mx-geq31-head,.mx-harm-head,.vt-face-header,.sk-head){touch-action:none!important}";
    document.head.appendChild(style);
    // Preserve native FX authored desktop control grids; crop/pan at the
    // mixer viewport instead of running narrow-mobile module media queries.
    style.textContent+="\n"+"html.armix-native-desktop #mxNativePeq .mx-peq-body{grid-template-columns:minmax(0,1fr) 143px!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-title{height:36px!important;flex-basis:36px!important;gap:11px!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-brand{font-size:11px!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-brand::after{content:none!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-bands{gap:3px!important;padding:7px 8px 6px!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-band{flex-direction:row!important;gap:4px!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-select{flex-direction:row!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-band-power{height:26px!important;width:19px!important;flex:0 0 19px!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-dial{width:62px!important;height:62px!important}\nhtml.armix-native-desktop #mxNativePeq .mx-peq-knob{min-height:84px!important}\nhtml.armix-native-desktop #mxNativeComp .mx-comp-controls{grid-template-columns:130px minmax(0,410px) minmax(0,1fr)!important;grid-template-rows:auto!important;gap:0!important;min-height:327px!important}\nhtml.armix-native-desktop #mxNativeComp .mx-comp-visual{grid-column:auto!important;grid-row:auto!important;width:auto!important}\nhtml.armix-native-desktop #mxNativeComp .mx-comp-left,#mxNativeComp .mx-comp-right{grid-column:auto!important;grid-row:auto!important}\nhtml.armix-native-desktop #mxNativeComp .mx-comp-dial{width:47px!important;height:47px!important}\nhtml.armix-native-desktop #mxNativeHarmony .mx-harm-body{grid-template-columns:150px minmax(0,1fr) 234px!important;min-height:232px!important}\nhtml.armix-native-desktop #mxNativeHarmony .mx-harm-left,#mxNativeHarmony .mx-harm-control-deck,#mxNativeHarmony .mx-harm-center{grid-column:auto!important;grid-row:auto!important}\nhtml.armix-native-desktop #mxNativeVocalThrow .vt-instrument{display:grid!important;grid-template-columns:minmax(0,1fr) 212px minmax(0,1fr)!important}\nhtml.armix-native-desktop #mxNativeVocalThrow .vt-center{order:initial!important}\nhtml.armix-native-desktop #mxNativeSonKu .sk-content{grid-template-columns:minmax(0,1.42fr) minmax(270px,1fr)!important}\nhtml.armix-native-desktop #mxNativeSonKu .sk-stage{border-right:1px solid var(--sk-line)!important;border-bottom:0!important}";
    style.textContent+="\n"+"html.armix-native-desktop #mxNativeVocalThrow #vtEncodersLeft,html.armix-native-desktop #mxNativeVocalThrow #vtEncodersRight{order:initial!important}\nhtml.armix-native-desktop #mxNativeVocalThrow .vt-throw-button{width:212px!important;height:212px!important}\nhtml.armix-native-desktop #mxNativeVocalThrow .vt-performance{grid-template-columns:88px minmax(0,1fr) 302px!important}\nhtml.armix-native-desktop #mxNativeSonKu .sk-rotary-grid{grid-template-columns:repeat(4,minmax(0,1fr))!important;min-height:0!important}\nhtml.armix-native-desktop #mxNativeSonKu .sk-dial{width:62px!important;height:62px!important}\nhtml.armix-native-desktop #mxNativeHarmony .mx-harm-center{border-top:0!important;padding:8px 12px!important}";
    const offsets=new WeakMap();
    const identities=new WeakMap(); // exact FX owner, not its slot
    const bypassOwners=new Map(); // navigation lease only
    const editorOwners=new Map(); // includes GEQ, whose bypass UI is internal
    let drag=null;
    const limit=(value,low,high)=>Math.max(low,Math.min(high,value));
    const grip=()=>Math.min(130,Math.max(56,Math.floor(innerWidth*.22)));
    function move(root,dx,dy) {
      if(!root||root.hidden||!Number.isFinite(dx)||!Number.isFinite(dy))return;
      const rect=root.getBoundingClientRect(),handle=grip();
      // Show a recoverable edge while permitting the hardware panel to pass
      // outside either side of the mixer. No layout reflow, no miniaturizing.
      const x=limit(rect.left+dx,handle-rect.width,innerWidth-handle);
      const y=limit(rect.top+dy,Math.min(8,innerHeight-rect.height+48),
        Math.max(8,innerHeight-38));
      const old=offsets.get(root)||{x:0,y:0};
      const next={x:old.x+x-rect.left,y:old.y+y-rect.top};
      offsets.set(root,next);
      // CSS translate does not overwrite GEQ S/M/L scale() or Harmony
      // translateX(-50%): both actual controls retain original geometry.
      root.style.setProperty("translate",next.x+"px "+next.y+"px","important");
    }
    function center(root) {
      if(!root||root.hidden)return false;
      const r=root.getBoundingClientRect();
      if(r.width<100||r.height<30)return false;
      // Wide GEQ begins at 20 Hz; pan to its 20 kHz end by dragging the
      // same single hardware face, clipped at the WebView viewport edge.
      const left=r.width>innerWidth-16?8:(innerWidth-r.width)/2;
      const top=Math.max(8,(innerHeight-r.height)/2);
      move(root,left-r.left,top-r.top);
      return true;
    }
    // Only rescue a panel that can no longer be reached after F11 or a
    // display/work-area resize. Never recenter manually positioned GEQ31.
    function keepReachable(root) {
      if(!root||root.hidden)return;
      const r=root.getBoundingClientRect(),handle=grip();
      // Anti Noiz is responsive and fits the viewport. Rescue the whole
      // face after a resize, not just a draggable edge. Wide hardware
      // editors retain their established partial-pan policy.
      const compact=root.id==="mxNativeAntiNoiz";
      const x=compact&&r.width<=innerWidth?
        limit(r.left,0,innerWidth-r.width):limit(r.left,handle-r.width,innerWidth-handle);
      const y=compact&&r.height<=innerHeight?
        limit(r.top,0,innerHeight-r.height):limit(r.top,Math.min(8,innerHeight-r.height+48),
        Math.max(8,innerHeight-38));
      if(Math.abs(x-r.left)>.5||Math.abs(y-r.top)>.5)
        move(root,x-r.left,y-r.top);
    }
    function syncBypass(view){
      const hardwareConfiguration=protocol.nativeHardwareConfiguration(view?.device);
      for(const [root,lease] of bypassOwners){
        if(!root.isConnected){bypassOwners.delete(root);continue;}
        if(root.hidden)continue;
        let presentationLease=lease;
        // Harmonice already has an explicit same-channel successor/MOVE
        // recovery contract. Reuse its accepted graph location; never
        // relax the generic deferred-navigation lease for other editors.
        if(root.id==="mxNativeHarmony"){
          const binding=window.ArSonMixNativeHarmony?.bypassBinding?.();
          if(binding&&binding.channelId===lease.channelId&&
             binding.graphRevision===view?.graph?.revision){
            const plan=fxRackVM.editorNavigationPlan(view,{
              channelId:binding.channelId,slot:binding.slot,hardwareConfiguration});
            if(plan.kind==="OPEN"&&plan.lease.catalogId===lease.catalogId&&
               plan.lease.instanceId===binding.instanceId&&
               (plan.lease.authority==="LOGICAL"?
                 binding.workspaceToken===plan.lease.workspaceToken&&binding.deviceEpoch==="0":
                 binding.deviceEpoch===plan.lease.deviceEpoch)){
              presentationLease=plan.lease;
              bypassOwners.set(root,presentationLease);
            }
          }
        }
        const value=fxRackVM.editorBypassPresentation(view,presentationLease,{hardwareConfiguration});
        let shade=root.querySelector(".armix-native-fx-bypass");
        if(!shade){
          shade=document.createElement("div");
          shade.className="mx-geq31-bypass-overlay armix-native-fx-bypass";
          shade.setAttribute("aria-hidden","true");
          const header=root.querySelector(headers);
          if(header){
            const height=header.offsetHeight+"px";
            shade.style.top=height;
            shade.style.setProperty("--armix-fx-bypass-header",height);
          }
          root.appendChild(shade);
        }
        const label={GLOBAL:"GLOBAL FX BYPASSED",RACK:"FX RACK BYPASSED",
          SLOT:"BYPASSED",UNAVAILABLE:"FX UNAVAILABLE",ON:"BYPASSED"}[value];
        if(shade.textContent!==label)shade.textContent=label;
        shade.classList.toggle("is-on",value!=="ON");
        const pointer=value==="UNAVAILABLE"?"auto":"none";
        if(shade.style.pointerEvents!==pointer)shade.style.pointerEvents=pointer;
      }
    }
    function closeRackEditors(channelIds=null){
      const affected=channelIds===null?null:new Set(channelIds.map(String));
      for(const [root,owner] of editorOwners){
        if(affected&&!affected.has(String(owner.lease.channelId)))continue;
        editorOwners.delete(root);
        bypassOwners.delete(root);
        if(drag?.root===root){
          try{drag.capture.releasePointerCapture?.(drag.id);}catch(_){}
          drag=null;
        }
        if(root.isConnected&&!root.hidden){
          try{window[owner.key]?.close?.();}finally{root.hidden=true;}
        }
        if(channelIds===null){identities.delete(root);offsets.delete(root);root.style.removeProperty("translate");}
      }
    }
    function openExact(host,key,track,slot,instanceId,lease=null) {
      const selector=selectors[key],root=document.querySelector(selector||"#__none__");
      if(!root||typeof host?.open!=="function"||!instanceId)return false;
      const identity=JSON.stringify([lease?.authority,lease?.workspaceToken,
        lease?.deviceEpoch,String(instanceId)]);
      const previous=editorOwners.get(root)?.lease;
      const sameOwner=identities.get(root)===identity;
      // Same exact visible instance: focus only. Do not reset pending DSP
      // edits, recreate Anti Noiz/De-Ezzer, or re-read default parameters.
      // A changed binding (MOVE or revision-bound De-Ezzer transport) must
      // still use the original verified open lifecycle.
      const sameBinding=sameOwner&&previous&&lease&&
        previous.channelId===lease.channelId&&previous.slot===lease.slot&&
        (key!=="ArSonMixNativeDeEzzer"||previous.graphRevision===lease.graphRevision);
      if(!root.hidden&&sameBinding){
        editorOwners.set(root,{key,lease});
        if(key!=="ArSonMixGraphicEq31")bypassOwners.set(root,lease);
        window.ArSonMixFxWorkspace?.raise(root);
        return true;
      }
      // A new FX identity gets first-open placement; a hidden SAME owner
      // retains the existing geometry and only rebinds its module lifecycle.
      if(identities.get(root)!==identity) {
        offsets.delete(root);
        root.style.removeProperty("translate");
        identities.set(root,identity);
      }
      if(host.open(track,slot)===false)return false;
      if(root.hidden)return false; // a no-op open is not a successful editor
      if(lease){
        editorOwners.set(root,{key,lease});
        if(key!=="ArSonMixGraphicEq31")bypassOwners.set(root,lease);
      }
      window.ArSonMixFxWorkspace?.raise(root);
      if(!offsets.has(root))center(root);
      else keepReachable(root);
      return true;
    }
    function visibleRoot(target) {
      const root=target?.closest?.(roots);
      return root&&!root.hidden?root:null;
    }
    document.addEventListener("pointerdown",event=>{
      if(event.button!==0||!event.isTrusted||drag)return;
      const root=visibleRoot(event.target);
      if(!root)return;
      const header=event.target.closest?.(headers);
      if(!header&&!event.altKey)return;
      if(!event.altKey&&event.target.closest?.(
        "button,select,input,textarea,a,[role=slider],[contenteditable]"))return;
      const r=root.getBoundingClientRect();
      drag={root,id:event.pointerId,startX:event.clientX,startY:event.clientY,
        left:r.left,top:r.top,capture:header||root};
      try{drag.capture.setPointerCapture?.(event.pointerId);}catch(_){}
      event.preventDefault();
      event.stopImmediatePropagation(); // prevent old viewport-clamped module drag
    },true);
    document.addEventListener("pointermove",event=>{
      if(!drag||drag.id!==event.pointerId)return;
      const x=drag.left+event.clientX-drag.startX;
      const y=drag.top+event.clientY-drag.startY;
      const r=drag.root.getBoundingClientRect();
      move(drag.root,x-r.left,y-r.top);
      event.preventDefault();event.stopImmediatePropagation();
    },true);
    function cancelDrag(){
      if(!drag)return false;
      const held=drag;drag=null;
      try{held.capture.releasePointerCapture?.(held.id);}catch(_){}
      return true;
    }
    function finishDrag(event){
      if(!drag||drag.id!==event.pointerId)return;
      cancelDrag();
      event.stopImmediatePropagation();
    }
    document.addEventListener("pointerup",finishDrag,true);
    document.addEventListener("pointercancel",finishDrag,true);
    window.addEventListener("blur",cancelDrag);
    // When the whole header is cropped into its right-side control buttons,
    // Shift+wheel still slides the exact same FX; Alt+drag works anywhere.
    document.addEventListener("wheel",event=>{
      if(!event.shiftKey)return;
      const root=visibleRoot(event.target);
      if(!root)return;
      event.preventDefault();event.stopImmediatePropagation();
      const amount=Math.abs(event.deltaY)>Math.abs(event.deltaX)?
        event.deltaY:event.deltaX;
      if(Number.isFinite(amount))move(root,-amount*.8,0);
    },{capture:true,passive:false});
    document.addEventListener("dblclick",event=>{
      const root=visibleRoot(event.target);
      if(root&&event.target.closest?.(headers) &&
         !event.target.closest?.("button,select,input"))
        requestAnimationFrame(()=>center(root));
    });
    document.addEventListener("click",event=>{
      if(!event.target.closest?.("#mxNativeGeq31 .mx-geq31-size"))return;
      requestAnimationFrame(()=>{
        const root=document.getElementById("mxNativeGeq31");
        if(root&&!root.hidden)keepReachable(root);
      });
    });
    window.addEventListener("resize",()=>{
      for(const root of document.querySelectorAll(roots))
        if(!root.hidden)requestAnimationFrame(()=>keepReachable(root));
    });
    return Object.freeze({openExact,center,move,syncBypass,closeRackEditors,cancelDrag});
  })():null;
  if(inlineFxClip)window.__ARMIX_NATIVE_INLINE_FX_CLIP__=inlineFxClip;


  // Native owns durable channel presentation. The approved Pages model only
  // renders native readback; no localStorage authority, fake PCM rename or
  // UI-ordinal-dependent alias. All IDs remain exact uint64 decimal strings.
  const channelPresentation=new Map();
  let appearanceRevision="0",appearanceWritable=false;
  let pendingIdentity=null,identityRequestSerial=0;
  // Pure policy shared with Node tests; native controls still own the edit.
  const legalName=protocol.validPresentationName;
  const legalColor=protocol.validPresentationColor;
  const legalIcon=protocol.validPresentationIcon;
  const displayNameFor=ch=>channelPresentation.get(ch.id)?.name||ch.name;
  // Legacy WebView aliases are an import queue ONLY, never rendered or written
  // as authoritative state. Each imported row must receive a native ACK plus
  // readback before its browser cache entry can be retired.
  const legacyIdentityKey="arsonmix.native.channel-presentation.v1";
  const legacyImport=new Map(), legacyDeferred=new Set();
  try {
    const prior=JSON.parse(window.localStorage?.getItem(legacyIdentityKey)||"{}");
    if(prior?.version===1&&prior.channels&&typeof prior.channels==="object"){
      for(const [id,p] of Object.entries(prior.channels).slice(0,128)){
        if(protocol.decimal(id)===null||!p||!legalName(p.name)||
           !legalColor(p.color)||!legalIcon(p.icon))continue;
        legacyImport.set(id,{name:p.name,color:p.color,icon:p.icon});
      }
    }
  }catch(_){ /* Native readback remains authoritative on corrupt WebView cache. */ }
  function retireLegacyRow(id){
    legacyImport.delete(id);
    try {
      const prior=JSON.parse(window.localStorage?.getItem(legacyIdentityKey)||"{}");
      if(prior?.version!==1||!prior.channels||typeof prior.channels!=="object")return;
      delete prior.channels[id];
      if(Object.keys(prior.channels).length)
        window.localStorage.setItem(legacyIdentityKey,JSON.stringify(prior));
      else window.localStorage.removeItem(legacyIdentityKey);
    }catch(_){status("Native identity saved; legacy cache cleanup unavailable");}
  }
  function maybeMigrateLegacy(next){
    if(!legacyImport.size)return;
    for(const [id,p] of legacyImport){
      const decision=stripVM.legacyIdentityDecision({snapshot:next,id,
        identity:p,appearance:channelPresentation,deferred:legacyDeferred,
        writable:appearanceWritable,pending:!!pendingIdentity,valid:protocol});
      if(decision.kind==="SKIP")continue;
      if(decision.kind==="RETIRE"){retireLegacyRow(id);continue;}
      if(decision.kind==="DEFER"||decision.kind==="CONFLICT"){
        legacyDeferred.add(id);
        if(decision.kind==="CONFLICT")
          status("Legacy channel identity name conflicts with native graph; cache preserved");
        continue;
      }
      if(decision.kind!=="SEND")continue;
      const requestId="appearance-import-"+Date.now().toString(36)+"-"+(++identityRequestSerial);
      pendingIdentity={...decision.plan.pending,requestId,migration:true};
      backend.emitEvent("arMixChannelIdentitySet",{
        ...decision.plan.payload,requestId
      });
      window.setTimeout(()=>{
        if(pendingIdentity?.requestId!==requestId)return;
        legacyDeferred.add(id);
        rejectIdentity("Legacy channel identity import timed out; old cache preserved");
        controller?.subscribe();
      },8000);
      return; // one crash-recoverable, revision-serialized transaction
    }
  }
  function appearanceFromNative(next){
    const projection=stripVM.identityReadback(next,protocol);
    if(projection.kind!=="VALID"){
      appearanceWritable=false;
      status("Native channel identity readback invalid; no UI change");
      return;
    }
    // Rendering cache is only a projection of the one accepted JUCE document.
    channelPresentation.clear();
    for(const row of projection.entries)
      channelPresentation.set(row.id,{name:row.name,color:row.color,icon:row.icon});
    appearanceRevision=projection.revision;
    appearanceWritable=projection.writable;
    const verdict=stripVM.identitySettlement(pendingIdentity,next,projection);
    if(verdict==="STALE"){
      if(pendingIdentity?.migration)legacyDeferred.add(pendingIdentity.channelId);
      rejectIdentity("Channel identity transaction expired · native graph changed");
    }else if(verdict==="CONFIRMED"){
      const p=pendingIdentity;pendingIdentity=null;
      if(p.migration){
        retireLegacyRow(p.channelId);
        status("Legacy channel identity migrated to native workspace · "+p.name);
      }else{
        const apply=document.getElementById("identitySave");
        if(apply)apply.disabled=false;
        scope?.closeChannelIdentity?.();
        status("Channel updated and stored in native workspace · "+p.name);
      }
    }
    maybeMigrateLegacy(next);
  }
  function rejectIdentity(reason){
    pendingIdentity=null;
    const apply=document.getElementById("identitySave");
    if(apply)apply.disabled=false;
    status(reason);
  }
  // A channel's cosmetic identity can be edited when its exact native id is
  // present in the current acknowledged OR retained graph. No physical
  // audio device, output arm or PCM graph mutation is required.
  const identityChannelFor=t=>{
    const id=nativeIdFor(t),current=activeView();
    return id && (current?.admitted||current?.retained||current?.logical) &&
      current.graph?.channels?.some(ch=>ch.id===id)?id:null;
  };
  const identityEligible=t=>Boolean(identityChannelFor(t));
  let lastEditedChannelId=null;
  function openNativeIdentity(t,anchor){
    if(!identityEligible(t)){
      nativeToast("Channel identity unavailable until its native channel is loaded");
      return false;
    }
    lastEditedChannelId=t.id;
    scope?.openChannelIdentity?.(t,anchor);
    return true;
  }
  function acceptIdentityPresentation(){
    if(!connected)return false;
    const localId=scope?.identityTrackId,
      track=scope?.tracks?.concat(scope.master||[])?.find(x=>x.id===localId),
      channelId=nativeIdFor(track),current=activeView();
    const plan=stripVM.identityEditPlan({native:current,logical:logicalView,
      channelId,rawName:document.getElementById("identityName")?.value,
      color:scope?.identityColor,icon:scope?.identityIcon,
      appearance:channelPresentation,appearanceRevision,appearanceWritable,
      pending:!!(controller?.state()?.pending||pendingIdentity),valid:protocol});
    if(plan.kind==="LOGICAL"){
      const sent=logicalIntent(plan.operation,plan.channelId,plan.payload);
      if(!sent.ok){status("Channel rename not saved");return false;}
      logicalRenameId=channelId;
      return false; // Native ACK + later readback closes the editor
    }
    if(plan.kind!=="READY"){
      status(plan.kind==="DUPLICATE"?"Channel name is already used":
        plan.kind==="INVALID_NAME"?"Enter a unique valid channel name":
        plan.kind==="INVALID_FIELDS"?"Enter a valid channel name, color and icon":
        plan.kind==="BUSY"?"Finish the current native operation before editing identity":
        "Native channel identity unavailable · verify saved native graph");
      return false;
    }
    const requestId="appearance-"+Date.now().toString(36)+"-"+(++identityRequestSerial);
    pendingIdentity={...plan.pending,requestId};
    const apply=document.getElementById("identitySave");
    if(apply)apply.disabled=true;
    backend.emitEvent("arMixChannelIdentitySet",{...plan.payload,requestId});
    status("Saving native channel identity · "+plan.pending.name);
    window.setTimeout(()=>{
      if(pendingIdentity?.requestId!==requestId)return;
      rejectIdentity("Native identity ACK/readback timed out; original identity retained");
      controller?.subscribe();
    },8000);
    return false; // Prevent original Pages demo mutation until authoritative readback.
  }
  const pendingWireGains=new Map();
  const wireGainVisualTargets=new Map(); // latest gesture, including in-flight readback
  let wireGainFlushQueued=false;
  let startupWire=null, startupWireSerial=0, activeWireAttempt=null;
  function deferStartupWire(ops,retries=0){
    if(!Array.isArray(ops)||ops.length!==1||
       !["CONNECT","ROUTE_ONLY"].includes(ops[0].type)||!deviceReady())return false;
    const current=controller?.state()?.current,op=ops[0];
    const epoch=current?.device?.epoch||String(hardware?.device?.epoch||"");
    if(!/^[1-9][0-9]*$/.test(epoch))return false;
    const token=++startupWireSerial;
    startupWire={ops:ops.map(x=>({...x})),epoch,token,retries,identity:current?.identity,
      source:nativeByUiId.get(op.sourceId),destination:nativeByUiId.get(op.destId)};
    status("WIRE · completing initial native input admission");
    controller?.subscribe();
    window.setTimeout(()=>{
      if(startupWire?.token!==token)return;
      startupWire=null;status("WIRE not applied · native admission timed out; previous audio preserved");
    },8000);
    return true;
  }
  function flushStartupWire(){
    if(!startupWire)return;
    const state=controller?.state(),current=state?.current,p=startupWire,op=p.ops[0];
    if(current?.device?.epoch!==p.epoch){startupWire=null;return;}
    if(state.pending||activeNativeSource||nativeUiSourcePending||
       pendingNativeSources.size||pendingSourceQueue.size||
       !flags.acceptedGraphConnected||!current?.admitted)return;
    if(p.retries&&current.identity===p.identity)return; // require a later revision before rebasing
    startupWire=null;
    if((p.source&&nativeByUiId.get(op.sourceId)!==p.source)||
       (p.destination&&nativeByUiId.get(op.destId)!==p.destination))return;
    const from=nativeByUiId.get(op.sourceId),to=nativeByUiId.get(op.destId);
    if(op.type==="CONNECT"&&current.graph.edges.some(e=>e.enabled&&e.from===from&&e.to===to)) {
      status("WIRE connected · independently accepted native route");return;
    }
    nativeHooks.consumeWire(p.ops,p.retries);
  }

  let lastPaintedSignature = "",lastNativeTopologySignature = "",
      lastNativeError = "NATIVE_NOT_SUBSCRIBED";
  // B-stage: attached-node-only meter caches; a structural render invalidates
  // them atomically. Never retain detached WebView2 strips or stale channel IDs.
  const meterElements = new Map();
  let nativeFxPowerSerial=0,pendingNativeFxPower=null;
  let globalFxSerial=0,pendingGlobalFx=null;
  const pendingM4RackPowers=new Map();
  let controller = null;
  let logicalView=null,pendingLogical=null,logicalSerial=0,logicalRenameId=null;
  // Disk-backed offline edits are release-only; retain one newest released
  // value per ChannelId while the single durable logical transaction is busy.
  const latestLogicalInputGains=new Map();
  // Retained requests prepare a control-only .smix FX editor after ACK, never
  // authorize PCM or silently rebind disconnected hardware.
  let pendingRetainedEditor=null,retainedEditorSerial=0;
  const logicalClientToken=Date.now().toString(36)+"-"+
    Math.random().toString(36).slice(2,10);
  // Human-facing presentation is isolated from native diagnostic state.
  // Internal identifiers remain in engine logs, never footer, dialog or toast.
  // Exact human-facing status policy, independently testable without WebView.
  const operatorMessage=protocol.operatorMessage;
  const status = message => {
    lastNativeError = message; // raw engine state remains inspectable internally
    const text = operatorMessage(message);
    const info = document.getElementById("info");
    if (info && info.textContent !== text) info.textContent = text;
  };
  // The original preview's say() may still populate this footer.
  // Observe the three footer nodes, no polling, audio callback or DOM-wide scans.
  if(connected){
    const transport=document.getElementById?.("transport");
    const info=document.getElementById?.("info");
    const warning=document.querySelector?.(".status-bar .warning");
    if(transport)transport.textContent="Opening mixer…";
    if(info)info.textContent="Opening mixer…";
    if(warning)warning.textContent="CONNECTING AUDIO…";
    if(typeof MutationObserver==="function"){
      // The preview, legacy host and new bridge can all write into the
      // transport/last-action/warning footer. Guard *all three* output nodes.
      // Native only; public browser preview retains its instructional copy.
      const nodes=[transport,info,warning].filter(Boolean);
      const sanitize=()=>{
        for(const node of nodes){
          const clean=operatorMessage(node.textContent);
          if(node.textContent!==clean)node.textContent=clean;
        }
      };
      const observer=new MutationObserver(sanitize);
      for(const node of nodes)
        observer.observe(node,{childList:true,characterData:true,subtree:true});
    }
    const demoFoot=document.querySelector?.(".route-choice-foot");
    if(demoFoot)demoFoot.textContent="Your routing changes take effect after you confirm.";
  }
  const stopDemo = () => {
    // The public Pages demo remains unchanged. In native, DEMO meters and
    // reference-only physical operations are never passed off as live audio.
    const demo = document.getElementById("preview");
    if (demo) {
      if (!demo.disabled) demo.disabled = true;
      const title = "Demo meters disabled in native Windows application";
      if (demo.title !== title) demo.title = title;
    }
    scope?.stopDemo?.();
    const authority = flags.productionCallbackLive ? "product-live" :
      flags.acceptedGraphConnected ? "pcm-accepted" :
      logicalView ? "logical-editable" : "not-admitted";
    if (document.documentElement.dataset.armixNativeAuthority !== authority)
      document.documentElement.dataset.armixNativeAuthority = authority;
  };
  const nativeIdFor = t => nativeByUiId.get(t?.id) || null;
  const activeView=()=>logicalView||controller?.state()?.current;
  // Dedicated logical transactions never advertise physical PCM admission.
  // Native commits to .smix BEFORE ACK; the NEXT subscribe proves readback.
  function logicalIntent(operation,channelId,payload={}) {
    if(!logicalView || logicalView.readOnly || pendingLogical)
      return {ok:false,error:logicalView?.readOnly?"SAVED_MIXER_RECOVERY_REQUIRED":
        pendingLogical?"PENDING_LOGICAL_ACK":"LOGICAL_NOT_READY"};
    if(!logicalView.graph.capabilities.supportedOps.includes(operation))
      return {ok:false,error:"UNSUPPORTED_LOGICAL_OPERATION"};
    const requestId="logical-"+logicalClientToken+"-"+(++logicalSerial);
    const value=["SET_FADER","SET_INPUT_GAIN","SET_WIRE_SUM_GAIN"].includes(operation)?payload.valueDb:
      operation==="SET_PAN"?payload.value:
      ["SET_MUTE","SET_SOLO","SET_FX_RACK_BYPASS"].includes(operation)?
        payload.enabled:undefined;
    const previous=logicalView.graph.channels.find(ch=>ch.id===channelId);
    pendingLogical={requestId,operation,channelId,payload,
      workspaceToken:logicalView.workspaceToken,
      beforeRevision:logicalView.graph.revision,ackRevision:null,createdId:null,
      beforeChannelExists:!!previous,
      beforeInsertCatalogs:Array.isArray(previous?.inserts)?
        previous.inserts.map(fx=>fx.catalogId):null,
      beforeEdges:logicalView.graph.edges.filter(e=>e.from===channelId).map(e=>({
        edgeId:e.edgeId,to:e.to,primaryOutput:e.primaryOutput,tap:e.tap,
        tapStage:e.tapStage,sendEnabled:e.sendEnabled
      }))};
    backend.emitEvent("arMixLogicalIntent",{
      requestId,workspaceToken:logicalView.workspaceToken,
      baseRevision:logicalView.graph.revision,operation,
      ...(channelId?{channelId}:{}),payload,
      ...(value===undefined?{}:{value})
    });
    window.setTimeout(()=>{
      if(pendingLogical?.requestId!==requestId)return;
      pendingLogical=null;
      latestLogicalInputGains.clear(); // unknown commit outcome: never blindly replay
      lastPaintedSignature="";lastNativeTopologySignature="";
      status("Mixer edit not confirmed · refreshing saved settings");
      controller?.subscribe();
    },8000);
    return {ok:true,requestId};
  }
  function queueWireGainFlush(){
    if(wireGainFlushQueued)return;
    wireGainFlushQueued=true;
    window.setTimeout(()=>{wireGainFlushQueued=false;flushWireGains();},32);
  }
  function flushWireGains(){
    const state=controller?.state(),current=state?.current;
    if(state?.pending||!current?.admitted)return;
    for(const [edgeId,edit] of pendingWireGains){
      pendingWireGains.delete(edgeId);
      const plan=wireVM.gainDispatchPlan(current,edgeId,edit);
      if(plan.kind==="ROLLBACK"){
        // Stale identity, malformed gain, or duplicate EdgeId: fail closed.
        wireGainVisualTargets.delete(edgeId);
        scope?.rollbackNativeWireInspector?.(nativeWireGainReadback(current));
        paintSofGainReadback(current);
        continue;
      }
      if(plan.kind==="CONFIRMED"){
        wireGainVisualTargets.delete(edgeId);
        paintSofGainReadback(current);
        scope?.syncNativeWireGains?.(nativeWireGainReadback(current));
        continue;
      }
      const result=controller.intent("SET_WIRE_GAIN",plan.source,{
        edgeId:plan.edgeId,gainLinear:plan.gainLinear});
      if(result.ok)window.setTimeout(()=>controller?.expire(result.requestId),8000);
      else {
        wireGainVisualTargets.delete(edgeId);
        paintSofGainReadback(current);
        status("WIRE gain rejected · "+result.error);
      }
      return; // next coalesced target follows ACK + independent readback
    }
  }
  // Every editable WIRE scalar must resolve an exact native ChannelId +
  // EdgeId, not a destination kind, strip label, or reference graph index.
  // A PARALLEL_SEND can legally point to MAIN; a PRIMARY_OUT can point to BUS.
  function admittedParallelWire(uiSourceId,uiEdgeId,operation){
    return wireVM.acceptedParallelEdge(controller?.state()?.current,
      flags.acceptedGraphConnected,nativeByUiId.get(uiSourceId),
      wireByUiId.get(uiEdgeId),operation);
  }
  function requestWireGain(op){
    const db=Number(op.levelDb);
    if(!Number.isFinite(db)||db< -60||db>6)return false;
    const current=controller?.state()?.current;
    const edgeId=wireByUiId.get(op.edgeId),source=nativeByUiId.get(op.sourceId);
    if(!admittedParallelWire(op.sourceId,op.edgeId,"SET_WIRE_GAIN"))return false;
    // A physical SEND fader floor is -infinity (zero PCM), NOT -60 dB linear.
    const edit={source,gain:db<=-60?0:Math.pow(10,db/20),db,identity:current.identity};
    pendingWireGains.set(edgeId,edit);
    wireGainVisualTargets.set(edgeId,edit);
    queueWireGainFlush();return true;
  }
  function nativeOutputTargets(t,channels=activeView()?.graph?.channels||[]){
    return stripVM.outputTargets(channels,nativeIdFor(t),displayNameFor);
  }
  let pendingNativeGainIntent=false;
  const latestInputGainDrafts=new Map();
  let gainDraftFlushScheduled=false;
  function flushLiveInputGains(){
    if(!controller||controller.state()?.pending||pendingLogical)return;
    for(const [key,edit] of latestInputGainDrafts){
      latestInputGainDrafts.delete(key);
      if(edit.epoch!==String(hardware?.device?.epoch)||
         nativeIdFor(edit.track)!==edit.id||!deviceReady()||
         (edit.operation==="SET_INPUT_GAIN"?!edit.track.nativeGainAvailable:
          wireVM.channelSumLevelPlan(controller.state()?.current,flags.acceptedGraphConnected,edit.id,edit.db).kind!=="READY")) {
        status("GAIN gesture retired · audio owner changed");
        continue;
      }
      const result=intent(edit.operation,edit.track,{valueDb:edit.db});
      if(!result){
        status("GAIN rejected · restoring accepted channel value");
        if(controller.state()?.current)
          reconcileAcceptedInputGainSnapshot(controller.state().current);
      }else pendingNativeGainIntent=true;
      // One owner/ACK at a time; latest other channel intents drain after
      // installed readback instead of racing an older graph revision.
      break;
    }
  }
  function scheduleLiveInputGainFlush(){
    if(gainDraftFlushScheduled)return;
    gainDraftFlushScheduled=true;
    window.setTimeout(()=>{
      gainDraftFlushScheduled=false;
      flushLiveInputGains();
    },32);
  }
  function logicalGainRow(view,id){
    if(!view||view.readOnly||!id||
       !view.graph.capabilities.supportedOps.includes("SET_INPUT_GAIN"))return null;
    const row=view.graph.channels.find(ch=>ch.id===id);
    return row?.role==="INPUT"&&Number.isFinite(row.controls?.inputGainDb)?row:null;
  }
  function flushLogicalInputGains(){
    if(!logicalView||pendingLogical)return;
    for(const [id,edit] of latestLogicalInputGains){
      latestLogicalInputGains.delete(id);
      const row=logicalGainRow(logicalView,id);
      if(!row||edit.workspaceToken!==logicalView.workspaceToken)continue;
      // Cancellation / release to the already saved value needs no disk write.
      if(Math.abs(row.controls.inputGainDb-edit.db)<.001){
        const t=scope?.tracks?.find(t=>nativeIdFor(t)===id);
        if(t)paintAcceptedInputGain(t,row.controls.inputGainDb);
        continue;
      }
      const result=logicalIntent("SET_INPUT_GAIN",id,{valueDb:edit.db});
      if(result.ok)return; // one durable ACK + independent readback at a time
      latestLogicalInputGains.clear();
      lastPaintedSignature="";lastNativeTopologySignature="";
      status("Offline GAIN not saved · refreshing accepted settings");
      controller?.subscribe();
      return;
    }
  }
  function requestLiveInputGain(track,db,final){
    if(!track||!Number.isFinite(db)||db< -24||db>24)return false;
    if(logicalView){
      const id=nativeIdFor(track);
      if(!logicalGainRow(logicalView,id))return false;
      if(!final)return true; // visual preview only; no intermediate .smix writes
      latestLogicalInputGains.set(id,{db,workspaceToken:logicalView.workspaceToken});
      flushLogicalInputGains();
      return true;
    }
    const id=nativeIdFor(track);
    if(!id||!deviceReady())return false;
    latestInputGainDrafts.set("SET_INPUT_GAIN:"+id,{
      id,operation:"SET_INPUT_GAIN",track,db,epoch:String(hardware?.device?.epoch)});
    if(final&&!controller.state()?.pending)flushLiveInputGains();
    else scheduleLiveInputGainFlush();
    return true;
  }
  function requestLiveWireSumGain(track,db,final){
    if(!track||!Number.isFinite(db)||db< -60||db>6)return false;
    if(logicalView) return final?intent("SET_WIRE_SUM_GAIN",track,{valueDb:db}):true;
    const id=nativeIdFor(track);
    if(!id||!deviceReady()||wireVM.channelSumLevelPlan(controller.state()?.current,
       flags.acceptedGraphConnected,id,db).kind!=="READY")return false;
    latestInputGainDrafts.set("SET_WIRE_SUM_GAIN:"+id,{
      id,operation:"SET_WIRE_SUM_GAIN",track,db,epoch:String(hardware?.device?.epoch)});
    if(!controller.state()?.pending)flushLiveInputGains();
    else scheduleLiveInputGainFlush();
    return true;
  }
  const intent = (operation,t,payload) => {
    if(logicalView)return logicalIntent(operation,nativeIdFor(t),payload).ok;
    if (!flags.acceptedGraphConnected) {
      status("NATIVE: accepted graph not projected; control unchanged");
      return false;
    }
    const id = nativeIdFor(t);
    const result = controller?.intent(operation,id,payload) || {ok:false,error:"NATIVE_UNAVAILABLE"};
    if (!result.ok) status("NATIVE: "+result.error+" — controls unchanged");
    else window.setTimeout(()=>controller?.expire(result.requestId),8000);
    return result.ok;
  };
  // One accepted EdgeId/tap command shared by SOF and the WIRE inspector.
  // A typed SET_TAP recompiles the existing PCM graph; neither GUI may
  // repaint PRE/POST until the subsequent independent accepted readback.
  function requestNativeTap(source,edgeId,tap){
    const state=controller?.state(),current=state?.current;
    const plan=wireVM.nativeTapPlan(current,{sourceId:source,edgeId,
      targetTap:tap,projected:flags.acceptedGraphConnected,
      pending:!!state?.pending});
    if(plan.kind==="NO_CHANGE")return true;
    if(plan.kind!=="DISPATCH"){
      status("PRE/POST unavailable · select an admitted parallel SEND");
      return false;
    }
    const result=controller.intent(plan.operation,plan.source,plan.payload);
    if(!result.ok){status("PRE/POST unchanged · "+result.error);return false;}
    window.setTimeout(()=>controller?.expire(result.requestId),8000);
    return true;
  }
  function nativeSofSource(t) {
    return !!t && (t.nativeRole?wireVM.sofSourceRole(t.nativeRole):t.kind==="audio");
  }
  function selectedNativeSofEdge(t) {
    if(!scope?.sofEnabled||!nativeSofSource(t))return null;
    const current=controller?.state()?.current;
    const matches=scope?.tracks?.filter(x=>x.id===scope.sofTargetId&&
      (x.nativeRole?wireVM.sofTargetRole(x.nativeRole):x.kind==="bus"||x.kind==="fx"))||[];
    if(matches.length!==1)return null;
    return wireVM.sofSendEdge(current,{sourceId:nativeIdFor(t),
      destinationId:nativeIdFor(matches[0])});
  }
  function syncNativeSofAvailability(next=controller?.state()?.current) {
    const admitted=wireVM.sofAvailable(next,!!scope?.toggleSof);
    const toggle=document.getElementById("sofToggle"),target=document.getElementById("sofTarget");
    for(const el of [toggle,target])if(el){
      el.style?.setProperty?.("display",el===toggle||admitted?"":"none","important");
      el.disabled=!admitted;
      el.title=admitted?"Flip channel faders to selected SEND levels":
        "Sends on Faders requires installed PCM WIRE gain, enable and tap ACK";
    }
    if(!admitted&&scope?.sofEnabled)scope.toggleSof(false);
  }
  function projection(native) {
    if ((!native?.admitted && !native?.retained && !native?.logical) ||
        !Array.isArray(native.graph.edges) ||
        !scope?.make || !scope?.render || !scope?.wireGraph ||
        !scope?.installGraph || !scope?.allocateWireId) return false;
    const accepted = native.graph;
    // No fake default fader/mute/solo when the producer omitted real readback.
    if (accepted.channels.some(ch => !ch.controls ||
        !["faderDb","pan","mute","solo"].every(k => Object.hasOwn(ch.controls,k)))) return false;
    const previous = new Map(scope.tracks.concat(scope.master).map(t=>[t.id,t]));
    const nextTracks = [], localByNative = new Map(), nextNativeByUi = new Map();
    let nextMaster = null, inputOrdinal=0;
    // Reserve surviving ChannelIds before assigning any new ordinal. A new
    // first row must never steal the DOM/model ID of a later surviving row.
    const reserved=new Set(accepted.channels.map(ch=>uiByNativeId.get(ch.id)).filter(id=>id!==undefined));
    for (const [index,ch] of accepted.channels.entries()) {
      const kind = ch.role==="MAIN"?"master":ch.role==="GROUP"?"bus":
        ch.role==="FX_RETURN"?"fx":"audio";
      // Preserve approved CH01..CH10 DOM/model identities across M4 G0
      // -> native ChannelId admission -> any later G0 fallback. A new
      // temporary track ID would silently move hardware to another strip.
      const m4Id=ch.role==="INPUT"?m4TrackIds[inputOrdinal++]:undefined;
      const heldId=uiByNativeId.get(ch.id);
      const savedId=heldId??(m4Id!==undefined&&!reserved.has(m4Id)&&!nextNativeByUi.has(m4Id)?m4Id:undefined);
      // Stage a detached model until the complete WIRE graph validates.
      // Never mutate the visible prior project on a rejected candidate.
      const t=savedId!==undefined&&previous.has(savedId)?{...previous.get(savedId)}:
        scope.make(ch.name,kind,scope.colors[index%scope.colors.length]||"#568ead");
      if(savedId!==undefined)t.id=savedId;
      if(nextNativeByUi.has(t.id))return false;
      // No Number(ch.id): all native identities stay canonical uint64 strings.
      localByNative.set(ch.id,t.id);
      nextNativeByUi.set(t.id,ch.id);
      t.name=displayNameFor(ch);t.kind=kind;t.nativeRole=ch.role;
      const appearance=channelPresentation.get(ch.id);
      if(appearance?.color)t.color=appearance.color;
      if(appearance?.icon)t.icon=appearance.icon;
      t.format=ch.processingFormat.toLowerCase();
      t.level=ch.controls.faderDb;
      t.wireSumGainDb=Number.isFinite(ch.controls?.wireSumGainDb)?ch.controls.wireSumGainDb:0;
      t.trim=Number.isFinite(ch.controls.inputGainDb)?ch.controls.inputGainDb:0;
      t.nativeGainAvailable=(native.admitted===true||native.logical===true) && kind==="audio" &&
        native.graph.capabilities.supportedOps.includes("SET_INPUT_GAIN") &&
        Number.isFinite(ch.controls.inputGainDb);
      t.nativeTapAvailable=native.admitted===true&&nativeSofSource(t)&&
        native.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION");
      t.nativeSendAvailable=native.admitted===true&&nativeSofSource(t)&&
        native.graph.capabilities.supportedOps.includes("SET_SEND_ENABLED");
      // Native PAN is normalized [-1,+1]; approved fader console uses [-100,+100].
      t.pan=ch.controls.pan*100;
      t.mute=ch.controls.mute;
      t.solo=ch.controls.solo;
      // Derive SOURCE/PRIMARY OUT labels only from the native installed readback.
      const labels=stripVM.readbackBindings({native,channel:ch,
        channels:accepted.channels,
        targets:nativeOutputTargets(t,accepted.channels),
        inputLabel:inputLabelFor});
      if(labels.input!==undefined)t.input=labels.input;
      if(labels.output!==undefined)t.output=labels.output;
      const direct=Array.isArray(native.raw?.externalOutputs)?
        native.raw.externalOutputs.find(x=>x.channelId===ch.id):null;
      const pair=direct && deviceVM.outputChoices(hardware,true).find(p=>
        p.left===direct.physicalLeft&&p.right===direct.physicalRight);
      t.externalOutput=direct?(direct.status==="LIVE"&&pair?pair.label:
        (direct.status==="PENDING"?"Pending · ":"Needs Remap · ")+
        "OUT "+(direct.physicalLeft+1)+"/"+(direct.physicalRight+1)):"None";
      t.selected=previous.get(t.id)?.selected || false;
      if(typeof ch.fxRackBypassed==="boolean")t.fxOn=!ch.fxRackBypassed;
      if (Array.isArray(ch.inserts)) {
        // Stable catalog identity, including restored sessions carrying the old name.
        const displayFxName=fx=>fx?.catalogId==="armix.builtin.vocal-reverb.v1"?
          "Reverb-Sis":fx?.name||"None";
        t.fx=Array.from({length:8},(_,i)=>displayFxName(ch.inserts[i]));
        t.slotBypass=Array.from({length:8},(_,i)=>Boolean(ch.inserts[i]?.bypassed));
        t.fxInsertIds=Array.from({length:8},(_,i)=>ch.inserts[i]?
          {name:displayFxName(ch.inserts[i]),id:ch.inserts[i].id}:null);
      }
      if (ch.role==="MAIN") nextMaster=t;
      else nextTracks.push(t);
    }
    if (!nextMaster || nextMaster.id !== localByNative.get(accepted.mainChannelId)) return false;
    const edges = [], newWireByUi = new Map(), newUiByWire = new Map();
    for (const edge of accepted.edges) {
      if (!edge.enabled) continue;
      const src=localByNative.get(edge.from), dst=localByNative.get(edge.to);
      if (!src || !dst || edge.from===accepted.mainChannelId) return false;
      const source=accepted.channels.find(ch=>ch.id===edge.from);
      const view=wireVM.edgePresentation(edge,source?.primaryOutId,
        accepted.mainChannelId);
      if(!view)return false; // invalid native WIRE identity: no demo fallback
      let uiId=uiByWireId.get(edge.edgeId);
      if (uiId===undefined) uiId=scope.allocateWireId();
      newWireByUi.set(uiId,edge.edgeId);
      newUiByWire.set(edge.edgeId,uiId);
      edges.push({id:uiId,sourceId:src,destId:dst,
        kind:view.kind,mode:view.mode,nativeTap:view.nativeTap,
        sendEnabled:view.sendEnabled,enabled:true,levelDb:view.levelDb});
    }
    if (!scope.wireGraph.validateCandidateGraph(nextTracks,nextMaster.id,edges).ok) return false;
    const previouslySelected = nextTracks.filter(t=>t.selected);
    if (!previouslySelected.length && nextTracks.length) nextTracks[0].selected=true;
    // One atomic accepted readback projection. Demo wire reducer NEVER runs.
    uiByNativeId.clear();for (const [id,ui] of localByNative) uiByNativeId.set(id,ui);
    nativeByUiId.clear();for(const [ui,id] of nextNativeByUi)nativeByUiId.set(ui,id);
    wireByUiId.clear();for(const [ui,id] of newWireByUi)wireByUiId.set(ui,id);
    uiByWireId.clear();for(const [id,ui] of newUiByWire)uiByWireId.set(id,ui);
    meterElements.clear(); // installGraph() replaces strip HTML and meter nodes
    // Retain object identity for surviving strips only after validation.
    const commitTrack=t=>previous.has(t.id)?Object.assign(previous.get(t.id),t):t;
    scope.installGraph(nextTracks.map(commitTrack),commitTrack(nextMaster),edges);
    syncNativeSofAvailability(native);
    return true;
  }
  // Rack and individual FX power are scalar native DSP controls, NOT a
  // structural graph change. Updating them must not replace DOM strips,
  // steal focus, interrupt a fader drag or reallocate WIRE render objects.
  // The verified individual bypass flag stays distinct from rack bypass.
  function paintNativeFxPowerReadback(ch,t,globalFxBypassed){
    const readback=fxRackVM.powerReadback(ch,globalFxBypassed,t.name);
    if(!readback)return true; // no authoritative DSP value to paint
    const strip=document.querySelector('.strip[data-id="'+t.id+'"]');
    // GAIN/SOF can hide the rack without losing its accepted power model.
    const rackArea=strip?.querySelector(".fx-section");
    const rack=rackArea?.classList?.contains?.("is-gain-view") ||
      rackArea?.classList?.contains?.("is-sof-view")?null:rackArea;
    let header=null,rows=[];
    if(rack){
      header=rack.querySelector('[data-action="fxOn"]');
      rows=[...rack.querySelectorAll('.fxslot-row:not(.is-empty)')];
      const mounted=rows.map(row=>({
        index:Number(row.dataset.slot),insertId:row.dataset.insertId,
        hasPower:row.dataset.slot===String(Number(row.dataset.slot)) &&
          !!row.querySelector('[data-action="slotPower"]')
      }));
      if(!fxRackVM.mountedRackMatches(readback,mounted,!!header))
        return false; // existing graph projection repairs replaced FX instance
    }
    t.fxOn=readback.enabled;
    if(!Array.isArray(t.slotBypass))t.slotBypass=Array(8).fill(false);
    for(const slot of readback.slots)t.slotBypass[slot.index]=slot.bypassed;
    if(!rack)return true; // virtual bank: native snapshot still owns model
    rack.dataset.fxChainEnabled=String(readback.enabled);
    rack.dataset.globalFxEnabled=String(readback.globalEnabled);
    rack.classList.toggle("is-chain-bypassed",readback.chainBypassed);
    header.setAttribute("aria-pressed",readback.headerPressed);
    header.title=readback.headerTitle;
    header.setAttribute("aria-label",readback.headerLabel);
    for(const slot of readback.slots){
      const row=rows.find(r=>r.dataset.slot===String(slot.index));
      if(slot.latencyTitle!==null)row.title=slot.latencyTitle;
      row.dataset.fxSlotEnabled=String(!slot.bypassed);
      row.dataset.fxEffective=String(slot.effective);
      row.classList.toggle("is-active",slot.effective);
      row.classList.toggle("is-bypassed",!slot.effective);
      const button=row.querySelector('[data-action="slotPower"]');
      button.setAttribute("aria-pressed",slot.pressed);
      button.title=slot.title;
      button.setAttribute("aria-label",slot.label);
    }
    return true;
  }
  // Reconcile the live SUM receiver's control availability AFTER a new
  // native ChannelId roster has been projected and admitted. The native
  // /preview/ render runs before flags.acceptedGraphConnected flips true,
  // so its initial fail-closed disabled state must not become permanently
  // latched after device repair. Never infer authority from HTML alone.
  function syncReceiverSumLevelAdmission(next){
    const inputs=document.querySelectorAll?.('input[data-field="wireChannelLevel"]')||[];
    for(const input of inputs){
      const ui=Number(input.dataset.channelId),id=nativeByUiId.get(ui);
      const channel=next?.graph?.channels?.find(ch=>ch.id===id);
      const plan=wireVM.channelSumLevelPlan(next,flags.acceptedGraphConnected,
        id,channel?.controls?.wireSumGainDb);
      const ready=Number.isSafeInteger(ui)&&plan.kind==="READY";
      if(input.disabled===ready)input.disabled=!ready;
      const title=ready?
        "WIRE SUM LVL · independent of GAIN and channel fader":
        "WIRE SUM LVL locked · verify native audio device and scalar admission";
      if(input.title!==title)input.title=title;
    }
  }

  // One authoritative EdgeId gain projection for normal readback AND failure
  // rollback. No duplicate DSP math or synthetic accepted control state.
  function nativeWireGainReadback(next){
    return wireVM.gainReadback(next?.graph?.edges||[],
      uiByWireId,wireGainVisualTargets,next?.graph?.channels||[]);
  }
  // Accepted PCM remains authoritative. Keep the latest desired visual level
  // across a held pointer, queued target and in-flight ACK; never write PCM here.
  function settleWireGainVisuals(next,confirmed){
    for(const [id,edit] of wireGainVisualTargets){
      const edge=next?.graph?.edges?.find(e=>e.edgeId===id&&e.from===edit.source&&e.enabled&&e.primaryOutput===false);
      if(!next?.admitted||next.identity!==edit.identity||!edge){
        wireGainVisualTargets.delete(id);pendingWireGains.delete(id);continue;
      }
      if(confirmed?.operation==="SET_WIRE_GAIN"&&confirmed.payload.edgeId===id&&
         !pendingWireGains.has(id)&&Math.abs(edge.gainLinear-edit.gain)<0.00001)
        wireGainVisualTargets.delete(id);
    }
  }
  function paintSofGainReadback(next){
    if(!scope?.sofEnabled||!next?.admitted)return;
    for(const t of scope.tracks||[]){
      if(!nativeSofSource(t)||scope.activeFaderId===t.id)continue;
      const edge=selectedNativeSofEdge(t);
      if(!edge)continue;
      const draft=wireGainVisualTargets.get(edge.edgeId);
      const db=draft?.identity===next.identity?draft.db:undefined;
      const root=document.querySelector('.strip[data-id="'+t.id+'"]');
      if(!root)continue;
      scope.syncFaderVisual?.(t,root,db);
    }
  }
  // Paint ONLY an independently accepted ChannelId input scalar. Comparing
  // the actual DOM as well as the model also repairs a rejected/expired gain
  // intent whose accepted value did not change (the old gainChanged check
  // skipped this case). Do no work if presentation is already identical.
  function paintAcceptedInputGain(t,valueDb){
    if(!Number.isFinite(valueDb))return;
    t.trim=valueDb;
    const root=document.querySelector('.strip[data-id="'+t.id+'"]');
    const slider=root?.querySelector('input[data-field="trim"]');
    if(!slider||slider.dataset.gainDragging==="true")return;
    if(!Number.isFinite(Number(slider.value))||
       Math.abs(Number(slider.value)-valueDb)>.011)
      slider.value=String(valueDb);
    const dial=slider.closest(".gain-dial");
    const rotation=(valueDb*5.625).toFixed(2)+"deg";
    if(dial?.style.getPropertyValue("--gain-rotation")!==rotation)
      dial?.style.setProperty("--gain-rotation",rotation);
    const digits=root.querySelector(".gain-lcd-digits")||
      root.querySelector(".gain-readout");
    const label=(valueDb>0?"+":"")+valueDb.toFixed(1)+" dB";
    if(digits&&digits.textContent!==label)digits.textContent=label;
  }
  function reconcileAcceptedInputGainSnapshot(next){
    if(!next?.admitted||controller?.state()?.pending?.operation==="SET_INPUT_GAIN")
      return;
    for(const ch of next.graph.channels){
      if(ch.role!=="INPUT"||!Number.isFinite(ch.controls?.inputGainDb)||
         !next.graph.capabilities.supportedOps.includes("SET_INPUT_GAIN"))
        continue;
      const uiId=uiByNativeId.get(ch.id);
      const t=scope?.tracks?.find(track=>track.id===uiId);
      if(t)paintAcceptedInputGain(t,ch.controls.inputGainDb);
    }
  }
  // The approved GUI may explicitly cancel a drag on focus/capture loss.
  // Native readback is the sole restore authority; never emit an intent here.
  window.__ARMIX_NATIVE_GAIN_RESTORE__=()=>{
    const state=controller?.state();
    if(state?.current&&!state.pending)
      reconcileAcceptedInputGainSnapshot(state.current);
  };
  // A canceled pointer is a compensating native scalar, not a fake JS
  // rollback. It reverts any intermediate realtime gain ACK to the value
  // at pointer-down; failure still leaves honest accepted readback.
  window.__ARMIX_NATIVE_GAIN_CANCEL__=(input,original)=>{
    const t=scope?.findTrack?.(input);
    if(t?.nativeGainAvailable&&Number.isFinite(original))
      requestLiveInputGain(t,original,true);
  };
  function projectNativeGraphWithoutFocusLoss(next) {
    // Only identity, order, processing width, source and edges are structural.
    // Rack/slot bypass is applied in-place from independent readback below.
    const structure=JSON.stringify([
      appearanceRevision, // cosmetic readback invalidates DOM projection
      next.graph.channels.map(ch=>[ch.id,ch.role,ch.name,
        ch.processingFormat,ch.source,ch.primaryOutId,
        ch.inserts?.map(fx=>[fx.id,fx.catalogId,fx.name])]),
      next.graph.edges.map(e=>[e.edgeId,e.from,e.to,e.tap,e.enabled,e.primaryOutput])]);
    if(structure!==lastNativeTopologySignature){
      if(!projection(next))return false;
      paintExternalReadback(next);
      lastNativeTopologySignature=structure;
      return true;
    }
    scope?.syncNativeWireGains?.(nativeWireGainReadback(next));
    // A SEND OFF is a scalar, NOT a disconnected WIRE. Update just the
    // affected selected SOF strip; preserve fader focus and all plugin state.
    for(const edge of next.graph.edges.filter(e=>e.enabled&&e.primaryOutput===false)){
      const uiId=uiByWireId.get(edge.edgeId),tId=uiByNativeId.get(edge.from);
      const local=scope?.wireEdges?.find(e=>e.id===uiId);
      if(!local)continue;
      if(local.sendEnabled!==(edge.sendEnabled!==false)){
        local.sendEnabled=edge.sendEnabled!==false;
        const t=scope?.tracks?.find(x=>x.id===tId);
        if(t&&scope?.sofEnabled)scope.refreshStrip?.(t);
      }
      if(scope?.sofEnabled && local.destId===scope?.sofTargetId){
        const t=scope?.tracks?.find(x=>x.id===tId);
        const root=document.querySelector('.strip[data-id="'+tId+'"]');
        const slider=root?.querySelector('input.fader');
        // Update accepted SEND readback without replacing the captured fader.
        const readout=root?.querySelector('.sof-info small');
        if(readout&&Number.isFinite(local.levelDb)){
          const db=local.levelDb<=-60?'-∞':(local.levelDb>0?'+':'')+local.levelDb.toFixed(1);
          const label=(local.sendEnabled===false?'OFF · saved ':'LEVEL ')+db+' dB';
          if(readout.textContent!==label)readout.textContent=label;
        }
        if(t&&slider&&scope.activeFaderId!==t.id){
          const draft=wireGainVisualTargets.get(edge.edgeId);
          scope.syncFaderVisual?.(t,root,draft?.identity===next.identity?draft.db:undefined);
        }
      }
    }
    syncNativeSofAvailability(next);
    // Immutable graph topology is identical: update actual scalar controls
    // only. Never detach the active HTML select or the captured fader pointer.
    for(const ch of next.graph.channels){
      const id=uiByNativeId.get(ch.id);
      const t=id===scope?.master?.id?scope.master:
        scope?.tracks?.find(x=>x.id===id);
      if(!t||!ch.controls)continue;
      if(!paintNativeFxPowerReadback(ch,t,next.raw?.globalFxBypassed)){
        // A plugin owner or strip DOM changed under us: do not paint an
        // unrelated instance. Rebuild from the accepted native graph.
        if(!projection(next))return false;
        lastNativeTopologySignature=structure;
        return true;
      }
      const c=ch.controls;
      const gainAdmitted=(next.admitted===true||next.logical===true)&&
        ch.role==="INPUT"&&
        next.graph.capabilities.supportedOps.includes("SET_INPUT_GAIN")&&
        Number.isFinite(c.inputGainDb);
      t.nativeGainAvailable=gainAdmitted;
      if(gainAdmitted)paintAcceptedInputGain(t,c.inputGainDb);
      const db=c.faderDb,pan=c.pan*100;
      const faderChanged=Number.isFinite(db)&&Math.abs(t.level-db)>.015;
      const visualDb=Number.isFinite(db)?pendingFaderVisualDb(t,db):null;
      const switchChanged=t.mute!==c.mute||t.solo!==c.solo;
      if(Number.isFinite(db))t.level=db;
      // Independent WIRE sum scalar, never a fader or sender EdgeId.
      const sumDb=c.wireSumGainDb;
      if(Number.isFinite(sumDb))t.wireSumGainDb=sumDb;
      const sumKnob=document.querySelector(
        '.strip[data-id="'+t.id+'"] input[data-field="wireChannelLevel"]');
      if(sumKnob){
        // This updater paints an accepted scalar, NOT device admission.
        // syncReceiverSumLevelAdmission owns enabling/disabling the pot.
        if(!scope?.wireGainDragging&&Number.isFinite(sumDb)&&
           document.activeElement!==sumKnob){
          sumKnob.value=String(Math.max(-60,Math.min(6,sumDb)));
          scope?.paintWireKnob?.(sumKnob,Number(sumKnob.value));
        }
      }
      if(Number.isFinite(pan))t.pan=pan;
      t.mute=c.mute;t.solo=c.solo;
      if(switchChanged && scope.activeFaderId!==t.id){
        scope.refreshStrip?.(t);
        meterElements.delete(t.id);
      }
      // Model equality is not DOM equality (e.g. a newly mounted fader cap).
      if(Number.isFinite(db))paintFaderReadback(t,visualDb,faderChanged);
      const root=document.querySelector('.strip[data-id="'+t.id+'"]');
      const knob=root?.querySelector('.center-pan');
      if(knob&&scope.activeFaderId!==t.id&&
         document.activeElement!==knob && Number.isFinite(pan))
        knob.value=String(pan);
    }
    return true;
  }
  // Native telemetry owns the physical meter. When no measured per-channel
  // samples are published, leave the meter dark; never use Pages sine demo.
  function paintNativeMeters(next) {
    for(const edge of next.graph.edges||[]){
      const ui=uiByWireId.get(edge.edgeId);
      if(ui===undefined)continue;
      for(const input of document.querySelectorAll?.('.wire-gain[data-wire-id="'+ui+'"]')||[]){
        const fixed=edge.primaryOutput===true;
        const disabled=!!next.retained||fixed;
        if(input.disabled!==disabled)input.disabled=disabled;
        const title=fixed?
          "PRIMARY OUT · fixed unity; receiver WIRE SUM LVL is independent":
          next.retained?"WIRE level locked until audio device reconnects":
          "Parallel SEND level · editable via native EdgeId ACK/readback";
        if(input.title!==title)input.title=title;
        const label=input.parentElement?.querySelector(".wire-gain-caption");
        if(label&&label.textContent!==(fixed?"UNITY":"LVL"))
          label.textContent=fixed?"UNITY":"LVL";
        input.parentElement?.classList.toggle("is-primary-unity",fixed);
        input.setAttribute("aria-label",title);
      }
    }
    if (!scope?.displayMeterDbfs)return;
    const data=next.telemetry?.channelMeterRmsPeak;
    if (!data || typeof data!=="object")return;
    const ts=performance.now();
    for (const ch of next.graph.channels) {
      const ui=uiByNativeId.get(ch.id);
      if (ui===undefined)continue;
      let elements=meterElements.get(ui);
      if (!elements?.root?.isConnected ||
          elements.root.dataset.id!==String(ui)) {
        const root=document.querySelector('.strip[data-id="'+ui+'"]');
        if (!root) {meterElements.delete(ui);continue;}
        elements={root,
          left:root.querySelector('.meter-fill.left'),
          right:root.querySelector('.meter-fill.right')};
        meterElements.set(ui,elements);
      }
      const entry=data[ch.id];
      const one=typeof entry==="number"&&Number.isFinite(entry)&&entry>=0?entry:null;
      const l=typeof entry?.peakL==="number"&&Number.isFinite(entry.peakL)&&entry.peakL>=0?
        entry.peakL:one;
      const right=typeof entry?.peakR==="number"&&Number.isFinite(entry.peakR)&&entry.peakR>=0?
        entry.peakR:ch.processingFormat==="MONO"?l:null;
      const paint=(side,peak)=>{
        const fill=elements[side];
        if(!fill)return;
        const db=peak===null || peak===0?-90:20*Math.log10(peak);
        scope.displayMeterDbfs(fill,Math.max(-90,Math.min(6,db)),ts,ui+':'+(side==="left"?"L":"R"));
      };
      paint("left",l);
      paint("right",right);
    }
  }

  // Native-only operator controls in the EXISTING approved More menu:
  // user selects REAL JUCE active pin -> explicitly patches CH1 -> arms output.
  // No auto-arm, demo PCM, guessed physical port or second GUI.
  let hardware=null, inputSelect=null, routeBtn=null, armBtn=null, muteBtn=null;
  let lastM4Performance=null,m4PaintKey="";
  let externalSerial=0;
  const pendingExternal=new Map(); // each ChannelId independent; no mixer-wide lock
  let hardwareNote=null, pendingSourceId=0, sourceSerial=100;
  const safeNativeCounter=x=>{
    if(typeof x!=="string" || !/^[1-9]\d*$/.test(x))return null;
    const number=Number(x);
    return Number.isSafeInteger(number)&&number>0?number:null;
  };
  const legacy=()=>hardware?.raw?.legacyM4;
  // One native SessionSnapshot, not an independently reconstructed M4 model.
  const mixerSession=()=>hardware?.raw?.mixerSession;
  const m4ControlIsOwner=()=>mixerSession()?.authority==="M4";
  // The pure Strip VM admits MAIN only for the CURRENT, installed PCM
  // producer. Old M4 status is never permission to control a retired owner.
  const nativeMasterAuthority=()=>stripVM.masterScalarAuthority(
    controller?.state()?.current,{m4Verified:m4Verified(),
      graphConnected:flags.acceptedGraphConnected});
  // The shipped /preview/ IN dropdown is the sole normal hardware source
  // selector. Build options exclusively from JUCE-reported active pins, never
  // the public browser demo's hardcoded "Input 1 / CABLE / Realtek" strings.
  // Native endpoint choices are projected by a pure, testable ViewModel.
  // Only accepted JUCE pin readback may populate these selectors.
  // BUS is verified engine state, not an available hardware input choice.
  const INTERNAL_BUS="Internal BUS (read-only)";
  const NEEDS_REMAP="Audio connection lost";
  // Only driver-verified even/odd physical pin pairs (not packed ordinals).
  // MAIN and MONITOR retain exclusive ownership of their actual pins.
  const hardwareOutputPairs=()=>deviceVM.outputChoices(hardware);
  const paintExternalReadback=(next)=>{
    const assignments=Array.isArray(next?.raw?.externalOutputs)?
      next.raw.externalOutputs:[];
    // Readback includes already installed logical routes; only new direct
    // assignments use the reservation-filtered inventory.
    const choices=deviceVM.outputChoices(next,true);
    const availableChoices=hardwareOutputPairs();
    const named=stripNamedChoices("output").map(p=>p.label);
    for(const t of scope?.tracks||[]) {
      const id=nativeIdFor(t);
      const row=assignments.find(p=>p.channelId===id);
      // Native PCM/armed status is projected, never asserted by the DOM.
      const projected=deviceVM.externalPresentation(next,row,choices);
      const {state,value}=projected;
      t.externalOutput=value;
      const select=document.querySelector('.strip[data-id="'+t.id+
        '"] select[data-field="external"]');
      if(!select?.dataset)continue; // headless contract mocks may omit DOM dataset
      const labels=[...availableChoices.map(p=>p.label),...named];
      if(!labels.includes(value))labels.push(value);
      const signature=labels.join("|");
      if(select.dataset.choices!==signature){
        select.replaceChildren(...labels.map(s=>{
          const op=document.createElement("option");
          op.value=op.textContent=s;return op;
        }));
        select.dataset.choices=signature;
      }
      if(select.value!==value)select.value=value;
      select.dataset.status=state;
      const marker=select.parentElement?.querySelector(".external-out-state");
      if(marker){
        marker.textContent=projected.marker;
        marker.dataset.status=state;
      }
      select.title=projected.title;
      const pending=pendingExternal.get(id);
      const result=stripVM.externalPending(pending,next?.device?.epoch,state,row);
      if(result==="STALE")pendingExternal.delete(id);
      else if(result==="CONFIRMED"){
        pendingExternal.delete(id);
        status(state==="MUTED"?"EXTERNAL OUT patched · hardware output muted":
          "EXTERNAL OUT verified · "+t.name+" → "+value);
      }
      if(!pendingExternal.has(id))select.removeAttribute("aria-busy");
    }
  };
  const inputChoicesFor=()=>deviceVM.inputChoices(hardware);
  const sourceChoicesFor=()=>{
    const physical=inputChoicesFor();
    // The PLAYER source must be DISCOVERABLE on every native audio strip,
    // including startup with zero physical capture pins. Capability is not
    // admission: M4/offline/retained modes still fail closed on selection.
    // Previously a transient logicalView/admission gate removed the option
    // altogether, which looked like the product never implemented it.
    return physical.some(p=>p?.mode==="PLAYER")?physical:[...physical,
      {label:"Music Player",mode:"PLAYER",left:-1,right:-1,deckId:"1"}];
  };
  const inputLabelFor=row=>deviceVM.inputLabel(hardware,row);
  // ChannelId and the existing M4 use the SAME ten native input ordinals.
  // GUI track reorder must never silently repatch a different physical lane.
  const m4TrackIds=Array.isArray(scope?.tracks)?
    scope.tracks.filter(t=>t.kind==="audio").slice(0,10).map(t=>t.id):[];
  const m4Index=t=>t?.kind==="audio"?m4TrackIds.indexOf(t.id):-1;
  let lastM4UiSignature="",lastM4Controls="",nativeUiSourcePending=null;
  let lastM4PortSignature="",pendingOutputPatch=null,m4RequestSerial=0,outputSerial=7000;
  const pendingSourceQueue=new Map();
  // G-MV29: one M4 routingChannelPatch adapter for OUT and M/ST.
  // Pure strip VM decides admitted producer, safe host tokens, no-op,
  // and exact pending lease. The bridge retains ONE native dispatcher.
  function m4PatchSelect(p){
    const t=scope?.tracks?.find(x=>m4Index(x)===p?.channel);
    return t?document.querySelector(
      '.strip[data-id="'+t.id+'"] select[data-field="output"]'):null;
  }
  function resetM4PatchUi(p,snapshot=null){
    if(!p||p.operation!=="SET_PRIMARY_OUT")return;
    const el=m4PatchSelect(p);
    if(!el)return;
    el.removeAttribute("aria-busy");
    el.title="Primary output · verified native routing";
    const row=snapshot?.raw?.legacyM4?.channels?.[p.channel];
    const t=scope?.tracks?.find(x=>m4Index(x)===p.channel);
    el.value=typeof row?.main==="boolean"?
      (row.main?"Master":"SOURCE ONLY (No Main)"):(t?.output||"Master");
  }
  function reconcileM4ChannelPatchReadback(next){
    const p=pendingOutputPatch;
    if(!p)return;
    const result=stripVM.m4ChannelPatchSettlement(p,next);
    if(result==="WAIT")return;
    pendingOutputPatch=null;
    resetM4PatchUi(p,result==="CONFIRMED"?next:null);
    if(result==="CONFIRMED")
      status("CH "+(p.channel+1)+" "+
        (p.operation==="SET_PROCESSING_FORMAT"?"M/ST ":"OUT ")+
        p.target+" · native PCM readback verified");
    else status("CH "+(p.channel+1)+
      " channel patch expired · restoring independently accepted routing");
  }
  function dispatchM4ChannelPatch(t,operation,target,channel=m4Index(t)){
    const plan=stripVM.m4ChannelPatchPlan(controller?.state()?.current,{
      operation,channel,target,verifiedM4:m4Verified(),deviceReady:deviceReady(),
      pending:!!pendingOutputPatch,sourcePending:!!nativeUiSourcePending
    });
    if(plan.kind==="NO_CHANGE"){
      controller?.subscribe();return true;
    }
    if(plan.kind!=="READY")return false;
    const requestId=++outputSerial;
    pendingOutputPatch={requestId,...plan.pending};
    if(operation==="SET_PRIMARY_OUT"){
      const menu=m4PatchSelect(pendingOutputPatch);
      if(menu){
        // Pending selection is a draft, not accepted PCM.
        menu.value=target==="MAIN"?"Master":"SOURCE ONLY (No Main)";
        menu.setAttribute("aria-busy","true");
        menu.title="Routing pending · awaiting native ACK and PCM readback";
      }
    }
    backend.emitEvent("arMixAction",{action:plan.action,requestId,...plan.payload});
    status("CH "+(channel+1)+" "+
      (operation==="SET_PROCESSING_FORMAT"?"processing ":"OUT → ")+
      target+" · awaiting native PCM ACK/readback");
    window.setTimeout(()=>{
      if(pendingOutputPatch?.requestId!==requestId)return;
      const p=pendingOutputPatch;
      pendingOutputPatch=null;
      resetM4PatchUi(p);
      status("CH "+(channel+1)+" channel patch timed out · prior native state preserved");
      controller?.subscribe();
    },8000);
    return true;
  }

  function requestProcessingFormat(t){
    const snapshot=activeView(),authority=logicalView?"LOGICAL":
      mixerSession()?.authority;
    const plan=stripVM.processingFormatPlan(snapshot,{
      authority,channelId:nativeIdFor(t),channel:m4Index(t),
      hardwareConfiguration:logicalView?undefined:
        protocol.nativeHardwareConfiguration(snapshot?.device),
      verifiedM4:m4Verified(),deviceReady:deviceReady(),
      pending:!!pendingOutputPatch||!!pendingLogical,
      sourcePending:!!nativeUiSourcePending
    });
    if(plan.kind!=="READY"){
      status("M/ST unavailable · refresh accepted native input/format");
      return false;
    }
    if(authority==="LOGICAL")
      return logicalIntent(plan.operation,plan.channelId,plan.payload).ok;
    if(authority==="CHANNEL_ID"){
      // Existing controller alone correlates ACK + accepted PCM readback.
      const request=controller.intent(plan.operation,plan.channelId,plan.payload);
      if(!request.ok){status("M/ST rejected · "+request.error);return false;}
      window.setTimeout(()=>controller?.expire(request.requestId),8000);
      status(t.name+" M/ST → "+plan.target+" · verifying native PCM readback");
      return true;
    }
    if(!dispatchM4ChannelPatch(t,plan.operation,plan.target,plan.channel)){
      status("M/ST unavailable · wait for accepted hardware route");
      return false;
    }
    return true;
  }
  function previewNativePan(t,el,event){
    let value=Math.max(-100,Math.min(100,Number(el.value)));
    if(!Number.isFinite(value))return null;
    // Same V61 center hysteresis as the canonical Pages UI: capture at
    // ±14, hold to ±22, settle within ±18; never fake native DSP state.
    const keyboard=el.dataset.panKeyboard==="true";
    const bypass=!!event.altKey||el.dataset.panAltPointer==="true";
    const latched=el.dataset.panDetent==="true";
    const width=event.type==="change"?18:latched?22:14;
    if(!keyboard&&!bypass&&Math.abs(value)<=width){value=0;el.value="0";}
    el.dataset.panDetent=String(!keyboard&&value===0&&!bypass);
    if(event.type==="change")delete el.dataset.panKeyboard;
    el.setAttribute?.("aria-valuetext",value===0?"Center":
      value<0?"Left "+Math.abs(value):"Right "+value);
    const container=el.closest?.(".pan-control");
    if(container){
      const left=Math.min(50,50+value/2),right=Math.max(50,50+value/2);
      container.style?.setProperty("--pan-start",left+"%");
      container.style?.setProperty("--pan-end",right+"%");
      container.classList?.toggle("is-centered",value===0);
      const readout=container.nextElementSibling;
      if(readout)readout.textContent=value===0?"C":
        value<0?"L"+Math.abs(value):"R"+value;
    }
    return value;
  }
  // One request/ACK/readback path for BOTH the ordinary OUT select and the
  // hardware-like OUT→MAIN WIRE jack. No browser graph mutation on click.
  function requestPrimaryOut(t,target){
    if(logicalView) {
      const from=nativeIdFor(t);
      const source=logicalView.graph.channels.find(ch=>ch.id===from);
      if(!source||!["INPUT","GROUP","FX_RETURN"].includes(source.role))return false;
      const to=target==="NONE"?null:target==="MAIN"?
        logicalView.graph.mainChannelId:
        nativeOutputTargets(t,logicalView.graph.channels).find(x=>x.label===target)?.id;
      if(target!=="NONE"&&!to)return false;
      if((source.primaryOutId||null)===to)return true;
      return logicalIntent("WIRE_TRANSACTION",from,{changes:[to?
        {op:"ROUTE_ONLY",from,to,gainLinear:1}:
        {op:"DISCONNECT",from}]}).ok;
    }
    // Exactly ONE PCM route authority: the admitted ChannelId publisher
    // owns all MAIN/Group WIRE and OUT mutations, including original CH1-10.
    // Calling the old M4 structural patch after native WIRE admission would
    // reject valid routes or attempt to resurrect an incompatible M4 graph.
    const session=mixerSession(),current=controller?.state()?.current;
    if(session?.authority==="CHANNEL_ID"){
      const channelId=nativeIdFor(t);
      // Native readback and physical authority are validated once inside
      // the existing pure Channel Strip VM, not rebuilt by the DOM bridge.
      const plan=stripVM.primaryOutMutation(
        current,channelId,target,nativeOutputTargets(t));
      if(plan.kind==="UNAVAILABLE")return false;
      if(plan.kind==="NO_CHANGE"){
        controller.subscribe();return true;
      }
      const change=plan.change;
      const accepted=controller.intent("APPLY_WIRE_TRANSACTION",channelId,
                                     {changes:[change]});
      if(!accepted.ok){status("Native OUT rejected · "+accepted.error);return false;}
      window.setTimeout(()=>controller?.expire(accepted.requestId),8000);
      status(t.name+" OUT → "+(target==="NONE"?"SOURCE ONLY":target)+
             " · native PCM readback pending");
      return true;
    }
    const channel=m4Index(t);
    return dispatchM4ChannelPatch(t,"SET_PRIMARY_OUT",target,channel);
  }
  const pendingNativeSources=new Map();
  let activeNativeSource=null,nativeSourceSerial=0;
  // MV-14: one INPUT dispatch owner; ChannelId/M4 keep separate identity Maps.
  function verifiedMatrixSourceChoices(choices,requested,snapshot){
    if(!requested||requested.mode!=="STEREO")return choices;
    const candidate=deviceVM.physicalInputChoice(snapshot,requested.mode,requested.left,requested.right);
    if(!candidate||candidate.label!==requested.label||choices.some(x=>x.label===candidate.label))
      return choices;
    return [...choices,candidate];
  }
  function dispatchSourceQueue(authority){
    const channelIdOwner=authority==="CHANNEL_ID";
    const queue=channelIdOwner?pendingNativeSources:pendingSourceQueue;
    if(!queue.size)return;
    const current=controller?.state()?.current;
    while(queue.size){
      const plan=stripVM.sourceQueueHead({authority,queue,
        active:channelIdOwner?activeNativeSource:nativeUiSourcePending,
        hardware,native:current,
        choices:verifiedMatrixSourceChoices(
          channelIdOwner?sourceChoicesFor():inputChoicesFor(),
          queue.values().next().value?.choice,hardware),
        deviceReady:deviceReady()});
      if(plan.kind==="IDLE"||plan.kind==="WAIT")return;
      if(plan.kind==="DROP_ALL"){queue.clear();return;}
      queue.delete(plan.key);
      // G-MV23: after ACK/readback races or timeout, skip a last-choice
      // draft that the accepted native PCM has ALREADY installed. Avoid
      // reissuing the same routing switch and risking needless audio churn.
      // The pure VM allows this only with the exact hardware/PCM lease.
      if(plan.kind==="SKIP_INSTALLED")continue;
      if(plan.kind==="DROP"){
        if(channelIdOwner)
          nativeToast("Source identity or device pins changed · refresh native mixer");
        else status("CH "+(plan.key+1)+" INPUT dropped · device or pin identity changed");
        continue; // stale source cannot starve an unrelated channel
      }
      const requestId=channelIdOwner?
        "source-native-"+(++nativeSourceSerial):++sourceSerial;
      const lease=stripVM.sourceQueueRequestLease({
        authority,requestId,plan,hardware,native:current});
      if(!lease){
        status("SOURCE identity changed · old input choice discarded");
        continue; // no old pin intent is emitted to a replaced device
      }
      if(channelIdOwner)activeNativeSource=lease;
      else nativeUiSourcePending=lease;
      backend.emitEvent("arMixAction",{action:plan.action,requestId,
        ...plan.payload});
      if(channelIdOwner)
        status("Input "+plan.entry.choice.label+" · native PCM readback pending");
      window.setTimeout(()=>handleSourceTimeout(authority,requestId),8000);
      return; // next request needs fresh readback/route revision
    }
  }
  function dispatchNativeSource(){
    dispatchSourceQueue("CHANNEL_ID");
  }

  // MV-17: timeout is not native rejection evidence. Resolve a previously
  // ACKed & independently installed PCM snapshot if it raced the callback;
  // otherwise discard only the transient UI lease and subscribe again.
  // Never send another queued INPUT here using a cached route revision.
  function handleSourceTimeout(authority,requestId){
    const channelIdOwner=authority==="CHANNEL_ID";
    const pending=channelIdOwner?activeNativeSource:nativeUiSourcePending;
    const snapshot=controller?.state()?.current;
    const decision=stripVM.sourceQueueTimeoutDecision({
      authority,pending,requestId,snapshot});
    if(decision==="IGNORE")return;
    if(decision==="SETTLE"){
      settleSourceReadback(authority,snapshot);
      return;
    }
    if(channelIdOwner){
      activeNativeSource=null;
      status("SOURCE timeout · refreshing accepted PCM and input choices");
    }else{
      nativeUiSourcePending=null;
      status("CH "+(pending.channel+1)+" INPUT timeout · native state preserved");
    }
    paintPhysicalOutputs(hardware);
    controller?.subscribe(); // next source waits for authoritative callback
  }
  // MV-15: one UI settlement owner after strict per-authority native PCM
  // verification. A late ACK can check an already independently published
  // snapshot without waiting for another audio callback.
  function settleSourceReadback(authority,next) {
    const channelIdOwner=authority==="CHANNEL_ID";
    const pending=channelIdOwner?activeNativeSource:nativeUiSourcePending;
    const verdict=stripVM.sourceQueueReadback({authority,pending,snapshot:next});
    if(verdict!=="CONFIRMED"&&verdict!=="STALE")return verdict;
    if(channelIdOwner){
      activeNativeSource=null;
      status(verdict==="CONFIRMED"?
        "Input "+pending.choice.label+" · native PCM readback verified":
        "SOURCE changed or disconnected · accepted input refreshed");
      if(pendingNativeSources.size)window.queueMicrotask(dispatchNativeSource);
    }else{
      nativeUiSourcePending=null;
      status(verdict==="CONFIRMED"?
        "CH "+(pending.channel+1)+" INPUT · native PCM readback verified":
        "Native input changed · old M4 source selection discarded");
      // onNativeReadback already drains after an M4 snapshot. Also support
      // a callback readback that arrives BEFORE the matching ACK event.
      if(verdict==="CONFIRMED"&&pendingSourceQueue.size)
        window.queueMicrotask(dispatchQueuedSource);
    }
    paintPhysicalOutputs(hardware);
    return verdict;
  }
  // MV-16: one lease owner handles both SOURCE ACK formats, but neither
  // ACK event can update visible PCM without independent native readback.
  function handleSourceAck(authority,ack){
    const channelIdOwner=authority==="CHANNEL_ID";
    const pending=channelIdOwner?activeNativeSource:nativeUiSourcePending;
    const transition=stripVM.sourceQueueAckTransition({authority,pending,ack});
    if(transition.kind==="IGNORE")return false;
    if(channelIdOwner)activeNativeSource=transition.pending;
    else nativeUiSourcePending=transition.pending;
    if(transition.kind==="ACKED"){
      if(channelIdOwner)
        status("Native input ACKed · verifying accepted PCM source");
      else status("CH "+(pending.channel+1)+
        " INPUT accepted · verifying native PCM source");
      // Callback readback may have published before this correlated ACK.
      // NEVER use ack.state or an ACK graph revision as installed PCM.
      settleSourceReadback(authority,controller?.state()?.current);
    }else if(channelIdOwner){
      nativeToast("Native input rejected · "+(ack?.error||"stale PCM"));
    }else{
      status("NATIVE INPUT REJECTED · "+(ack?.error||"stale routing"));
    }
    paintPhysicalOutputs(hardware);
    controller?.subscribe();
    return true;
  }
  let pendingBusCreate=null,pendingTrackDelete=null,stripRequestSerial=0;
  // G-MV30: delete INPUT / GROUP / FX_RETURN via ONE ChannelId roster
  // authority and ONE native ACK + independently installed graph lease.
  function dispatchNativeRosterDelete(track,operation){
    const current=controller?.state()?.current;
    const plan=stripVM.rosterMutationPlan(current,{
      operation,channelId:nativeIdFor(track),hardware,
      deviceReady:deviceReady(),controllerPending:!!controller?.state()?.pending,
      structuralPending:!!pendingTrackDelete||!!pendingBusCreate
    });
    if(!connected||plan.kind!=="READY"){
      nativeToast("Delete unavailable · wait for an admitted native channel");
      return false;
    }
    const requestId=(operation==="DELETE_CHANNEL"?"track-delete-":"bus-delete-")+
      (++stripRequestSerial);
    pendingTrackDelete={requestId,...plan.pending,name:track.name};
    syncAddTrack(current);
    backend.emitEvent("arMixAction",{action:plan.action,requestId,...plan.payload});
    status("Deleting "+track.name+" · waiting for native PCM ACK/readback");
    window.setTimeout(()=>{
      if(pendingTrackDelete?.requestId!==requestId)return;
      pendingTrackDelete=null;syncAddTrack();
      nativeToast("Delete timed out · accepted graph preserved until readback");
      controller?.subscribe();
    },8000);
    return true;
  }
  function requestNativeTrackDelete(track){
    if(logicalView)return logicalIntent("DELETE_CHANNEL",nativeIdFor(track),{}).ok;
    return dispatchNativeRosterDelete(track,"DELETE_CHANNEL");
  }
  function requestNativeBusDelete(track){
    if(logicalView)return logicalIntent("REMOVE_BUS",nativeIdFor(track),{}).ok;
    return dispatchNativeRosterDelete(track,"REMOVE_BUS");
  }
  function requestNativeStripDelete(track){
    if(track?.kind==="audio")return requestNativeTrackDelete(track);
    if(track?.kind==="bus"||track?.kind==="fx")return requestNativeBusDelete(track);
    nativeToast("MASTER cannot be deleted");
    return false;
  }
  function canDeleteNativeStrip(track){
    if(logicalView){
      const row=logicalView.graph.channels.find(ch=>ch.id===nativeIdFor(track));
      return !logicalView.readOnly&&
        ["INPUT","GROUP","FX_RETURN"].includes(row?.role);
    }
    const operation=track?.kind==="audio"?"DELETE_CHANNEL":
      (track?.kind==="bus"||track?.kind==="fx")?"REMOVE_BUS":null;
    return !!operation&&stripVM.rosterMutationPlan(controller?.state()?.current,{
      operation,channelId:nativeIdFor(track),hardware,deviceReady:deviceReady(),
      controllerPending:!!controller?.state()?.pending,
      structuralPending:!!pendingTrackDelete||!!pendingBusCreate
    }).kind==="READY";
  }
  function finishNativeTrackDelete(next){
    const p=pendingTrackDelete;
    if(!p)return;
    const verdict=stripVM.rosterReadbackSettlement(p,next);
    if(verdict==="WAIT")return;
    pendingTrackDelete=null;syncAddTrack(next);
    if(verdict==="CONFIRMED")
      nativeToast(p.name+" deleted · native PCM and WIRE readback verified");
    else status("Native deletion expired · refreshing accepted mixer roster");
  }
  function nativeOrbitTarget(track,slot){
    if(!track||!Number.isInteger(slot)||slot<0||slot>=8)return null;
    const next=controller?.state()?.current,id=nativeIdFor(track);
    const channel=next?.graph?.channels?.find(ch=>ch.id===id);
    const insert=channel?.inserts?.[slot];
    if(next?.admitted&&!next.retained&&insert?.id&&
       /dearvr\s*pro\s*2/i.test(insert.name||""))
      return {authority:"CHANNEL_ID",channelId:id,insertId:insert.id,slot};
    // Prior to native ChannelId takeover the verified M4 FX lease is the
    // authoritative processor. Never guess by ordinal from a stale M4 list.
    if(mixerSession()?.authority!=="M4"||!m4Verified())return null;
    const picked=verifiedFxFor(track,slot);
    const catalogName=nativeCatalog.get(picked?.live?.catalogId)?.name||
      track.fx?.[slot]||"";
    if(!picked||!/dearvr\s*pro\s*2/i.test(catalogName))return null;
    return {authority:"M4",channelId:"m4-lane-"+picked.lane,
      lane:picked.lane,insertId:picked.live.instanceId,slot};
  }
  function nativeOrbitAction(track,slot,kind,values={}){
    const target=nativeOrbitTarget(track,slot),next=controller?.state()?.current;
    if(!target){nativeToast("Orbit target changed · reopen the dearVR FX menu");return false;}
    if(!["inspect","start","stop"].includes(kind))return false;
    const action=target.authority==="M4"?
      (kind==="inspect"?"routingM4OrbitInspect":
        kind==="start"?"routingM4OrbitStart":"routingM4OrbitStop"):
      (kind==="inspect"?"routingNativeOrbitInspect":
        kind==="start"?"routingNativeOrbitStart":"routingNativeOrbitStop");
    const identity=target.authority==="M4"?
      {channel:target.lane,deviceEpoch:safeNativeCounter(hardware?.device?.epoch),
       revision:safeNativeCounter(legacy()?.routingRevision)}:
      {channelId:target.channelId,deviceEpoch:next.device.epoch,
       graphRevision:next.graph.revision,rosterRevision:next.graph.rosterRevision};
    if(Object.values(identity).some(v=>v===null||v===undefined))return false;
    backend.emitEvent("arMixAction",{
      action,...values,...identity,instanceId:target.insertId,slot:target.slot
    });
    return true;
  }
  function canCreateNativeTrack(next){
    if(logicalView)return !logicalView.readOnly &&
      logicalView.graph.capabilities.supportedOps.includes("CREATE_CHANNEL");
    return stripVM.rosterMutationPlan(next,{
      operation:"CREATE_CHANNEL",hardware,deviceReady:deviceReady(),
      controllerPending:!!controller?.state()?.pending,
      structuralPending:false
    }).kind==="READY";
  }
  function syncAddTrack(next=controller?.state()?.current){
    const button=document.getElementById("addTrack");
    if(!button)return;
    const available=canCreateNativeTrack(next);
    button.disabled=!available || !!pendingBusCreate ||
      !!pendingTrackDelete || !!pendingLogical;
    button.title=available?"Add a stereo audio track routed to MAIN":
      "Connect a verified audio device in Audio I/O to add tracks";
  }
  // Both audio-track and BUS addition retain the structural lease until
  // a strictly newer independently accepted ChannelId roster proves the
  // precise createdId and role. ACK.state can never create a browser strip.
  function finishNativeTrack(next){
    const p=pendingBusCreate;
    if(!p)return;
    const verdict=stripVM.rosterReadbackSettlement(p,next);
    if(verdict==="WAIT")return;
    if(verdict==="STALE"){
      pendingBusCreate=null;syncAddTrack(next);
      status("Native roster changed · add operation expired");return;
    }
    if(p.track){
      const id=uiByNativeId.get(p.createdId);
      const strip=scope?.tracks?.find(t=>t.id===id);
      if(!strip||!scope?.revealTrack)return;
      pendingBusCreate=null;
      syncAddTrack(next);
      scope.revealTrack(strip);
      nativeToast(strip.name+" added · choose its IN to receive audio");
    }else{
      pendingBusCreate=null;syncAddTrack(next);
      status("BUS added · native ChannelId PCM and WIRE readback verified");
    }
  }
  function requestNativeBusCreate(track=false,fxBus=false){
    if(logicalView)
      return logicalIntent(track?"CREATE_CHANNEL":"CREATE_GROUP",null,
        track?{}:{name:(fxBus?"FX Bus ":"Aux Bus ")+(logicalView.graph.channels.length+1)}).ok;
    const accepted=controller?.state()?.current,operation=
      track?"CREATE_CHANNEL":"CREATE_GROUP";
    const plan=stripVM.rosterMutationPlan(accepted,{
      operation,hardware,deviceReady:deviceReady(),
      controllerPending:!!controller?.state()?.pending,
      structuralPending:!!pendingBusCreate||!!pendingTrackDelete
    });
    if(plan.kind!=="READY")return false;
    const requestId=(track?"track-":"bus-")+(++stripRequestSerial);
    pendingBusCreate={requestId,track,...plan.pending};
    syncAddTrack(accepted);
    backend.emitEvent("arMixAction",{action:plan.action,requestId,...plan.payload,purpose:fxBus?"FX":"AUX"});
    const label=track?"Audio Track":"BUS";
    status("Adding "+label+" → MAIN · native PCM pending");
    window.setTimeout(()=>{
      if(pendingBusCreate?.requestId!==requestId)return;
      pendingBusCreate=null;
      syncAddTrack();
      status("Add "+label+" timed out · refreshing mixer");
      controller?.subscribe();
    },8000);
    return true;
  }
  let pendingFxInsert=null,fxRequestSerial=0,nativeCatalog=new Map();
  let pendingFxEditorAck=null,fxEditorSerial=0; // HWND feedback, never PCM owner
  // Intentional rack edits retire affected slot-relative HWND requests.
  // Late ACKs/timeouts for the deliberately closed editor are obsolete, not
  // a new editor failure. Other racks retain their exact ACK/retry contract.
  function retirePendingRackEditor(channelIds=null){
    const pending=pendingFxEditorAck;
    if(!pending)return;
    if(channelIds===null||(pending.authority==="CHANNEL_ID"&&
       channelIds.some(id=>String(id)===String(pending.lease?.channelId))))
      pendingFxEditorAck=null;
  }
  // One pending M4 insert is owned by the bridge until ACK AND independent
  // accepted Console-v6 snapshot. A stale/superseded ACK can never open HWND.
  function reconcileM4FxInsertReadback(snapshot){
    const p=pendingFxInsert;
    if(!p||p.authority!=="M4")return {kind:"NONE"};
    const decision=fxRackVM.m4InsertReadbackSettlement(p,snapshot,
      protocol.nativeHardwareConfiguration(snapshot?.device));
    if(decision.kind==="STALE"){
      pendingFxInsert=null;
      status("FX INSERT expired · native routing changed");
    }else if(decision.kind==="CONFIRMED"){
      pendingFxInsert=null;
      presentFxCatalog();
      scope?.closeNativePicker?.();
      openRoutedFxEditor(decision.channel,decision.slot,decision.instanceId,
        decision.epoch,decision.revision,
        protocol.nativeHardwareConfiguration(snapshot?.device));
      status("CH "+(decision.channel+1)+
        " FX loaded · independent native PCM readback verified");
    }
    return decision;
  }

  // One original native editor transport for M4 and ChannelId vendor FX.
  // The publisher's accepted lease is checked again BEFORE calling this.
  function dispatchPhysicalFxEditor(plan,lease,authority,retried=false){
    const requestId="fx-editor-"+(++fxEditorSerial);
    pendingFxEditorAck={requestId,authority,lease,retried,
      recenter:plan.payload.recenter===true};
    backend.emitEvent("arMixAction",{action:plan.action,requestId,...plan.payload});
    window.setTimeout(()=>{
      if(pendingFxEditorAck?.requestId!==requestId||pendingFxEditorAck.retryScheduled)return;
      if(retryPhysicalFxEditor(pendingFxEditorAck))return;
      pendingFxEditorAck=null;
      nativeToast("FX editor unavailable · request timed out");
    },8000);
  }
  function retryPhysicalFxEditor(pending){
    if(!pending||pending.authority!=="CHANNEL_ID"||pending.retried||pending.retryScheduled)
      return false;
    const accepted=controller?.state()?.current;
    if(logicalView||!fxRackVM.refreshEditorNavigationLease(accepted,pending.lease,{
      hardwareConfiguration:protocol.nativeHardwareConfiguration(accepted?.device)}))
      return false;
    pending.retryScheduled=true;
    controller?.subscribe(); // only independent accepted readback may refresh this click
    window.setTimeout(()=>{
      if(pendingFxEditorAck!==pending)return; // newer click wins
      const current=controller?.state()?.current;
      const fresh=!logicalView&&fxRackVM.refreshEditorNavigationLease(current,pending.lease,{
        hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
      const plan=fresh&&fxRackVM.nativeEditorDispatchPlan(current,fresh,{
        hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
      if(plan?.kind==="READY"){
        dispatchPhysicalFxEditor({...plan,payload:{...plan.payload,recenter:pending.recenter}},
          fresh,"CHANNEL_ID",true);
      }else{
        pendingFxEditorAck=null;
        nativeToast("FX editor unavailable · accepted instance changed");
      }
    },160);
    return true;
  }
  function openChannelIdFxEditor(lease,recenter=false){
    if(lease?.authority!=="CHANNEL_ID"||logicalView)return false;
    const accepted=controller?.state()?.current;
    const plan=fxRackVM.nativeEditorDispatchPlan(accepted,lease,{
      hardwareConfiguration:protocol.nativeHardwareConfiguration(accepted?.device),
      offline:!!logicalView});
    if(plan.kind!=="READY")return false;
    window.queueMicrotask(()=>{
      const current=controller?.state()?.current;
      if(logicalView||fxRackVM.nativeEditorDispatchPlan(current,lease,{
        hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device),
        offline:!!logicalView}).kind!=="READY"){
        const fresh=!logicalView&&fxRackVM.refreshEditorNavigationLease(current,lease,{
          hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
        if(fresh){
          const refreshed=fxRackVM.nativeEditorDispatchPlan(current,fresh,{
            hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
          if(refreshed.kind==="READY"){
            dispatchPhysicalFxEditor({...refreshed,payload:{...refreshed.payload,recenter}},
              fresh,"CHANNEL_ID");
            return;
          }
        }
        nativeToast("FX editor unavailable · accepted instance changed");
        return;
      }
      if(recenter)dispatchPhysicalFxEditor({
        ...plan,payload:{...plan.payload,recenter:true}
      },lease,"CHANNEL_ID");
      else dispatchPhysicalFxEditor(plan,lease,"CHANNEL_ID");
    });
    return true;
  }
  function openRoutedFxEditor(lane,slot,instanceId,deviceEpoch,revision,
                              hardwareConfiguration,recenter=false){
    const accepted=controller?.state()?.current;
    const plan=fxRackVM.m4EditorDispatchPlan(accepted,{
      channel:lane,slot,instanceId,deviceEpoch,revision,
      verifiedM4:m4Verified(accepted),offline:!!logicalView,
      hardwareConfiguration});
    if(plan.kind!=="READY")return false;
    const lease=plan.lease;
    // No stale queued M4 editor HWND may survive an owner/route/pin/FX
    // change between accepted readback and the native IPC dispatch.
    window.queueMicrotask(()=>{
      const current=controller?.state()?.current;
      if(!fxRackVM.m4EditorDispatchLeaseMatches(current,lease,{
        verifiedM4:m4Verified(current),offline:!!logicalView,
        hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)})){
        nativeToast("FX editor unavailable · accepted instance changed");
        return;
      }
      if(recenter)dispatchPhysicalFxEditor({
        ...plan,payload:{...plan.payload,recenter:true}
      },lease,"M4");
      else dispatchPhysicalFxEditor(plan,lease,"M4");
    });
    return true;
  }
  // One verified post-insert navigation: never open an unaccepted slot and
  // never infer an instance from a catalog name. Reuse the installed editor
  // action for both native DSP and vendor VST3s, after exact graph readback.
  function openVerifiedInsertedFx(channelId,slot,catalogId,instanceId) {
    const verified=activeView();
    const plan=fxRackVM.editorNavigationPlan(verified,{channelId,slot,
      hardwareConfiguration:protocol.nativeHardwareConfiguration(verified?.device)});
    if(plan.kind!=="OPEN"||plan.lease.instanceId!==instanceId||
       plan.lease.catalogId!==catalogId)return;
    const lease=plan.lease; // exact workspace, ChannelId, roster and device
    window.queueMicrotask(()=>{
      const current=activeView();
      if(!fxRackVM.editorNavigationLeaseMatches(current,lease,{
        hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)}))
        return; // a replaced session must never inherit the old editor click
      const local=uiByNativeId.get(channelId);
      const track=[...(scope?.tracks||[]),scope?.master]
        .find(item=>item?.id===local);
      if(track&&nativeIdFor(track)===channelId)
        window.__ARMIX_NATIVE_M2__?.consumeAction?.(track,"openPlugin",
          {dataset:{slot:String(slot)}});
    });
  }
  // Native JUCE processors: these are real compiled DSP, not demo effects,
  // scanned VST binaries, a JS renderer or placeholder library entries.
  let nativeBuiltins=Object.freeze([
    Object.freeze({id:"armix.builtin.channel-eq.v1",name:"Parametric EQ",
      vendor:"ArSonMix",category:"EQ",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.graphic-eq31.v1",name:"Graphic EQ 31",
      vendor:"ArSonMix",category:"EQ",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.channel-compressor.v1",name:"Compressor",
      vendor:"ArSonMix",category:"Dynamics",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.vocal-reverb.v1",name:"Reverb-Sis",
      vendor:"ArSonMix",category:"Reverb",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.arson-harmony.v1",name:"Harmonice",
      vendor:"ArSonMix",category:"Utility",format:"Native",source:"builtin"}),
    // Native GuitarRigProcessor is compiled and installed: no WebAudio mock.
    Object.freeze({id:"armix.builtin.guitar-rig.v1",name:"Guitar FX Rig",
      vendor:"ArSonMix",category:"Guitar",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.vocal-delay.v2",name:"Vocal Delay",
      vendor:"ArSonMix",category:"Utility",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.vocal-throw.v1",name:"Vocal Delay",
      vendor:"ArSonMix",category:"Utility",format:"Native",source:"builtin",showInPicker:false}),
    Object.freeze({id:"armix.builtin.8d-sonku.v1",name:"8D-SonKu",
      vendor:"ArSonMix",category:"Spatial",format:"Native",source:"builtin",showInPicker:false})
  ]);
  const presentFxCatalog=()=>scope?.setNativeCatalog?.(
    [...nativeBuiltins.filter(fx=>fx.showInPicker!==false),...nativeCatalog.values()]);
  let nativeFxDiscoveryRequested=false,nativeFxCatalogScanning=false;
  // Lazy USER-triggered discovery: the first Add FX opens immediately and
  // starts real JUCE VST3 scanning on its dedicated non-audio worker. Never
  // scan during cold device startup, on a timer, or on the RT callback.
  function discoverFxOnFirstPickerOpen(){
    if(!connected||nativeCatalog.size||nativeFxDiscoveryRequested||
       nativeFxCatalogScanning)return;
    nativeFxDiscoveryRequested=true;
    backend.emitEvent("arMixAction",{action:"scanVst3"});
    status("FX · scanning installed VST3 on background worker; picker updates automatically");
  }
  const pendingSlotPowers=new Map(); // ACK/readback, never demo FX flags
  // The rack header restores exact prior per-insert state, including
  // deliberately bypassed plugins; it never blindly turns ALL FX on.
  function dmxReverbConversionTarget(t,slot){
    const state=controller?.state(),current=state?.current;
    if(!current?.admitted||current.retained||state.pending||
       !current.graph.capabilities.supportedOps.includes("CONVERT_REVERB_RETURN"))return null;
    const ch=current.graph.channels.find(c=>c.id===nativeIdFor(t));
    const insert=ch?.inserts?.[slot];
    return ch?.role==="INPUT"&&ch.primaryOutId===current.graph.mainChannelId&&
      Number.isInteger(slot)&&slot>=0&&slot+1===ch.inserts.length&&
      insert?.catalogId==="armix.builtin.vocal-reverb.v1"?{channelId:ch.id,instanceId:insert.id}:null;
  }
  function convertDmxReverb(t,slot){
    const target=dmxReverbConversionTarget(t,slot);
    if(!target)return false;
    const result=controller.intent("CONVERT_REVERB_RETURN",target.channelId,
      {slot,instanceId:target.instanceId});
    if(!result.ok){status("FX Return conversion rejected · "+result.error);return false;}
    window.setTimeout(()=>controller?.expire(result.requestId),15000);
    status("Preparing Reverb-Sis Return · outputs must be disarmed");
    return true;
  }
  function editNativeFx(t,slot,target,position,token){
    const current=controller?.state()?.current,id=nativeIdFor(t);
    const decision=fxRackVM.nativeEditPlan(current,{
      channelId:id,slot,instanceId:token,move:!!target,
      destinationId:target?nativeIdFor(target):null,
      position:target?position:null,
      hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
    if(decision.kind!=="READY"){
      if(decision.kind!=="NO_CHANGE")
        status("FX edit unavailable · refresh accepted native instance");
      return false;
    }
    // Close affected slot-bound panels before MOVE/REMOVE invalidates their
    // leases. The source and destination racks can both shift slot indices.
    retirePendingRackEditor(target?[id,nativeIdFor(target)]:[id]);
    inlineFxClip?.closeRackEditors(target?[id,nativeIdFor(target)]:[id]);
    const result=controller.intent(decision.operation,decision.channelId,
                                   decision.payload);
    if(!result.ok){status("FX edit rejected · "+result.error);return false;}
    window.setTimeout(()=>controller?.expire(result.requestId),8000);
    status(target?"Moving native FX · awaiting accepted readback":
      "Removing native FX · awaiting accepted readback");
    return true;
  }
  // G-MV28: one physical M4 rack/slot transaction adapter. The pure FX
  // ViewModel validates the CURRENT accepted M4 session, legacy routing
  // revision, active FX instance and safe host tokens before any native IPC.
  // Reuse existing per-rack/per-slot queues; ACK alone never paints FX.
  function requestM4FxPower(t,action,button=null){
    const channel=m4Index(t),slot=action==="slotPower"?
      Number(button?.dataset?.slot):null;
    const map=action==="fxOn"?pendingM4RackPowers:pendingSlotPowers;
    const key=action==="fxOn"?channel:channel+":"+slot;
    const current=controller?.state()?.current;
    const plan=fxRackVM.m4PowerPlan(current,{
      action,channel,slot,pending:map.has(key),
      verifiedM4:m4Verified(),offline:!!logicalView,
      hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)
    });
    if(plan.kind!=="READY")return false;
    const requestId=action==="fxOn"?
      "rack-m4-"+(++nativeFxPowerSerial):"fxpower-"+(++fxRequestSerial);
    map.set(plan.key,{requestId,...plan.pending});
    backend.emitEvent("arMixAction",{
      action:plan.action,requestId,...plan.payload});
    window.setTimeout(()=>{
      if(map.get(plan.key)?.requestId!==requestId)return;
      map.delete(plan.key);
      status(action==="fxOn"?
        "Native rack gate timed out · restoring accepted readback":
        "FX bypass timed out · restoring real rack state");
      controller?.subscribe();
    },plan.timeoutMs);
    return true;
  }
  function reconcileM4RackReadback(next){
    for(const [lane,p] of pendingM4RackPowers){
      const decision=fxRackVM.m4RackSettlement(p,next,
        protocol.nativeHardwareConfiguration(next?.device));
      if(decision==="STALE"||decision==="CONFIRMED")
        pendingM4RackPowers.delete(lane);
    }
    // An M4 slot remains pending after ACK until its own exact instance
    // lease and requested bypass are present in independently accepted PCM.
    for(const [key,p] of pendingSlotPowers){
      const decision=fxRackVM.m4SlotPowerSettlement(p,next,
        protocol.nativeHardwareConfiguration(next?.device));
      if(decision==="STALE"||decision==="CONFIRMED")
        pendingSlotPowers.delete(key);
    }
  }
  // Global A/B is a SINGLE native atomic override. Individual racks and
  // inserts remain untouched. Never simulate success in WebView while device
  // admission or a matching native ACK + subsequent readback is missing.
  function globalFxContext(){
    const current=controller?.state()?.current;
    return fxRackVM.globalPowerContext(current,m4Verified(),!!logicalView,
      protocol.nativeHardwareConfiguration(current?.device));
  }
  function syncGlobalFxReadback(){
    // Ignore raw incoming events: controller may reject an invalid/stale
    // snapshot and retain its previous accepted native PCM read model.
    const current=controller?.state()?.current;
    const button=document.getElementById?.("globalFxToggle");
    const context=globalFxContext();
    const decision=fxRackVM.globalPowerSettlement(pendingGlobalFx,current,context);
    if(decision==="CONFIRMED"||decision==="STALE")pendingGlobalFx=null;
    const actual=current?.raw?.globalFxBypassed;
    if(context&&typeof actual==="boolean"&&!pendingGlobalFx)
      window.__ARMIX_SET_GLOBAL_FX_VIEW__?.(!actual);
    if(button){
      button.disabled=!context||!!pendingGlobalFx;
      button.setAttribute("aria-busy",String(!!pendingGlobalFx));
      if(!context)button.title="Global FX requires admitted physical audio";
    }
  }
  function toggleGlobalFx(enableOnly=false){
    const current=controller?.state()?.current,ctx=globalFxContext();
    // Editor recovery is an explicit ENABLE, never an accidental toggle
    // back to bypass if accepted global state changed since its last paint.
    if(enableOnly===true&&current?.raw?.globalFxBypassed!==true)return false;
    const plan=fxRackVM.globalPowerPlan(current,ctx,!!pendingGlobalFx);
    if(plan.kind!=="READY")return false;
    const requestId="global-fx-"+(++globalFxSerial);
    pendingGlobalFx={requestId,bypassed:plan.bypassed,acked:false,...ctx};
    const button=document.getElementById?.("globalFxToggle");
    if(button){button.disabled=true;button.setAttribute("aria-busy","true");}
    backend.emitEvent("arMixAction",{action:plan.action,requestId,...plan.payload});
    window.setTimeout(()=>{
      if(pendingGlobalFx?.requestId!==requestId)return;
      pendingGlobalFx=null;
      status("Global FX A/B not confirmed · keeping previous audible state");
      controller?.subscribe(); // Readback is authoritative, never optimistic.
    },8000);
    return true;
  }
  window.__ARMIX_NATIVE_GLOBAL_FX__={toggle:toggleGlobalFx};
  function requestAdmittedFxPower(t,action,button){
    if(mixerSession()?.authority!=="CHANNEL_ID")return false;
    const current=controller?.state()?.current;
    const plan=fxRackVM.nativePowerPlan(current,{
      channelId:nativeIdFor(t),action,
      slot:action==="fxOn"?null:Number(button?.dataset?.slot),
      pending:!!pendingNativeFxPower,
      hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
    if(plan.kind!=="READY")return false;
    const requestId="native-rack-"+(++nativeFxPowerSerial);
    pendingNativeFxPower={requestId,...plan.pending};
    backend.emitEvent("arMixAction",{action:plan.action,requestId,...plan.payload});
    window.setTimeout(()=>{
      if(pendingNativeFxPower?.requestId!==requestId)return;
      pendingNativeFxPower=null;
      status("Native FX power timed out · accepted state preserved");
      controller?.subscribe();
    },8000);
    return true;
  }
  let pendingNativeControl=new Map(),lastNativeControlSent=new Map();
  const m4Meters=new Map();
  // MV-19: read-model admission must use the candidate native snapshot.
  // The installed hardware cache is updated only AFTER this candidate is
  // validated, so old accepted M4 evidence cannot admit a new device.
  const m4Verified=(snapshot=hardware)=>{
    const m=snapshot?.raw?.legacyM4,d=snapshot?.device;
    return m4TrackIds.length===10 &&
      new Set(m4TrackIds).size===10 &&
      m?.active===true && m.routeAccepted===true &&
      m.nativeBusStructural!==true &&
      Array.isArray(m.channelControls) &&
      m.channelControls.length===10 && m.channels?.length===10 &&
      safeNativeCounter(d?.epoch)!==null &&
      safeNativeCounter(m.routingRevision)!==null;
  };

  // Live gestures choose the installed PCM authority. The M4 ordinal is
  // NEVER a native ChannelId: a ChannelId graph uses its exact admitted id
  // and its existing ACK + later readback contract. MAIN is a physical host
  // scalar and retains its guarded routingM4Control gateway.
  const controlKey=(parameter,t)=>parameter+":"+(t?.kind==="master"?"main":t?.id);
  const channelIdScalarReady=(parameter,t)=>{
    const current=activeView();
    if(logicalView)return !!nativeIdFor(t)&&
      logicalView.graph.capabilities.supportedOps.includes(
        parameter==="faderDb"?"SET_FADER":"SET_PAN");
    return t?.kind!=="master" &&
      mixerSession()?.authority==="CHANNEL_ID" &&
      flags.acceptedGraphConnected && current?.admitted===true &&
      !!nativeIdFor(t) &&
      current.graph.capabilities.supportedOps.includes(
        parameter==="faderDb"?"SET_FADER":"SET_PAN");
  };
  function liveScalarReady(parameter,t) {
    if(t?.kind==="master")return logicalView?parameter==="masterGain":
      parameter==="masterGain"&&nativeMasterAuthority()!=="UNAVAILABLE";
    if(m4ControlIsOwner())return m4Verified()&&m4Index(t)>=0;
    return channelIdScalarReady(parameter,t);
  }
  // G-MV29: use one pure ViewModel projection of the current accepted
  // physical generation. Route/roster changes retire queued targets even if
  // epoch, hardware pin mask and local GUI strip number are unchanged.
  const nativeEditIdentity=(t)=>stripVM.scalarNativeIdentity({
    logical:!!logicalView,
    epoch:logicalView?"LOGICAL":String(hardware?.device?.epoch),
    authority:logicalView?"LOGICAL":mixerSession()?.authority,
    channelId:logicalView?nativeIdFor(t):t?.kind==="master"?null:nativeIdFor(t),
    master:t?.kind==="master",
    hardwareConfiguration:logicalView?undefined:hardware?.hardwareConfiguration,
    routingRevision:logicalView?undefined:String(legacy()?.routingRevision),
    rosterRevision:logicalView?undefined:controller?.state()?.current?.graph?.rosterRevision
  });
  function sameNativeEditIdentity(edit) {
    return stripVM.scalarIdentityMatches(edit,nativeEditIdentity(edit.t));
  }
  // Only the last gesture target owns the visual fader cap until the
  // producer independently reads that SAME target back. Earlier ACKs and
  // snapshot ticks must not move the cap backwards after pointer release.
  // The track model always remains native-owned (not optimistically edited).
  const liveFaderVisualTargets=new Map();
  function pendingFaderVisualDb(t,actualDb){
    const key=controlKey(t?.kind==="master"?"masterGain":"faderDb",t);
    const pending=liveFaderVisualTargets.get(key);
    if(!pending)return null;
    const result=stripVM.scalarVisualSettlement(pending,actualDb,
      nativeEditIdentity(t));
    if(result==="STALE"||result==="CONFIRMED"){
      liveFaderVisualTargets.delete(key);return null;
    }
    return result==="WAIT"?pending.value:null;
  }
  // null means an admitted ChannelId intent is already awaiting its ACK /
  // producer readback. Keep ONLY the newest dB target, never drop release.
  function dispatchLiveScalar(edit) {
    const plan=stripVM.scalarDispatchPlan(edit,{
      identity:nativeEditIdentity(edit.t),
      ready:liveScalarReady(edit.parameter,edit.t),
      logical:!!logicalView,logicalPending:!!pendingLogical,
      m4Owner:edit.t?.kind==="master"||m4ControlIsOwner(),
      nativePending:!!controller?.state()?.pending
    });
    if(plan.kind==="REJECT")return false;
    if(plan.kind==="WAIT")return null;
    if(plan.kind==="M4")return sendM4Control(edit.parameter,edit.t,edit.value);
    return intent(plan.operation,edit.t,plan.payload);
  }
  function flushLiveScalars() {
    for(const [key,edit] of pendingNativeControl) {
      const result=dispatchLiveScalar(edit);
      if(result===null)continue;
      pendingNativeControl.delete(key);
      if(result)lastNativeControlSent.set(key,
        stripVM.scalarDispatchLease(edit,performance.now()));
      else {
        if(liveFaderVisualTargets.get(key)===edit)
          liveFaderVisualTargets.delete(key);
        status("LIVE PCM scalar rejected · native identity changed");
      }
    }
  }
  function streamFaderControl(parameter,t,value,final){
    if(!Number.isFinite(value)||!liveScalarReady(parameter,t))return false;
    const key=controlKey(parameter,t),now=performance.now();
    const edit={parameter,t,value,...nativeEditIdentity(t)};
    pendingNativeControl.set(key,edit); // coalesce: last writer always wins
    if(parameter==="faderDb"||parameter==="masterGain")
      liveFaderVisualTargets.set(key,edit);
    // One pure selector preserves last-writer-wins, pointerup release and
    // offline .smix persistence, while ignoring old channel/epoch throttles.
    const flush=stripVM.scalarGestureFlushPlan({
      logical:!!logicalView,final:!!final,now,
      lastSent:lastNativeControlSent.get(key),identity:edit
    });
    if(flush==="FLUSH")flushLiveScalars(); // pointermove BEFORE pointerup
    if(logicalView)return true;
    if(pendingNativeControl.has(key))queueLiveControlFlush();
    return true;
  }
  // A queued ChannelId target is retried only after its real ACK/readback
  // completes (see onStatus), not by a second timer polling the audio engine.
  let nativeControlFlushQueued=false;
  function queueLiveControlFlush(){
    if(nativeControlFlushQueued)return;
    nativeControlFlushQueued=true;
    window.setTimeout(()=>{
      nativeControlFlushQueued=false;
      flushLiveScalars();
    },32);
  }
  function previewFaderWithoutModelMutation(t,db){
    if(!Number.isFinite(db)||!t)return;
    scope?.syncFaderVisual?.({...t,level:db},undefined,db);
  }
  function paintFaderReadback(t,visualDb,changed){
    if(scope?.activeFaderId===t.id)return;
    // SOF uses an EdgeId, not the ChannelId fader visual lease.
    if(scope?.sofEnabled&&nativeSofSource(t))return;
    const view=visualDb===null?t:{...t,level:visualDb};
    // Only repair a changed control or genuinely stale mounted DOM. Never
    // rewrite unrelated strips during a held drag or meter-only update.
    if(changed||visualDb!==null||scope?.faderVisualNeedsSync?.(view))
      scope?.syncFaderVisual?.(view);
  }
  function paintM4FxInstances(m) {
    // Catalog refresh is metadata, never permission to replace a native
    // rack with an inactive compatibility graph's old/empty FX inventory.
    if(mixerSession()?.authority==="CHANNEL_ID")return;
    if(!Array.isArray(m?.fxInstances)||!scope?.tracks)return;
    for(let lane=0;lane<10;lane++){
      const t=scope.tracks.find(x=>x.id===m4TrackIds[lane]);
      const acceptedRack=mixerSession()?.authority==="M4"?
        mixerSession()?.channels?.[lane]?.fxRackBypassed:undefined;
      const projection=fxRackVM.m4Presentation(m.fxInstances[lane],
        nativeCatalog,acceptedRack);
      if(!t||!projection)continue;
      let change=false;
      if(projection.fxOn!==null&&t.fxOn!==projection.fxOn){
        t.fxOn=projection.fxOn;change=true;
      }
      for(const slot of projection.slots){
        const index=slot.index;
        if(t.fx[index]!==slot.name||t.slotBypass[index]!==slot.bypassed||
           (t.fxInsertIds?.[index]?.id||null)!==(slot.instance?.id||null)){
          t.fx[index]=slot.name;t.slotBypass[index]=slot.bypassed;
          if(!Array.isArray(t.fxInsertIds))t.fxInsertIds=Array(8).fill(null);
          t.fxInsertIds[index]=slot.instance;
          change=true;
        }
      }
      if(change && scope.activeFaderId!==t.id)
        scope.refreshStrip?.(t);
    }
  }
  function sendM4Control(parameter,t,value) {
    if((t?.kind==="master"?
          nativeMasterAuthority()==="UNAVAILABLE":
          !m4ControlIsOwner()||!m4Verified()) ||
       (typeof value!=="boolean"&&!Number.isFinite(value)))
      return false;
    const channel=t?.kind==="master"?-1:m4Index(t);
    if(channel<0 && t?.kind!=="master")return false;
    const ep=safeNativeCounter(hardware.device.epoch),
      rev=safeNativeCounter(legacy().routingRevision);
    if(ep===null||rev===null)return false;
    const requestId="m4-"+(++m4RequestSerial);
    const key=m4ControlKey(parameter,channel);
    pendingM4Controls.set(key,{requestId,channel,parameter,value,
      epoch:String(ep),revision:String(rev),acked:false});
    backend.emitEvent("arMixAction",{action:"routingM4Control",
      requestId,channel,parameter,controlValue:value,deviceEpoch:ep,
      revision:rev});
    window.setTimeout(()=>{
      if(pendingM4Controls.get(key)?.requestId!==requestId)return;
      pendingM4Controls.delete(key);
      status("M4 control ACK/readback timed out · restoring native state");
      controller?.subscribe();
    },8000);
    return true;
  }
  // Correlate each scalar edit, including simultaneous different-channel
  // gestures. An ACK is only permission to request an INDEPENDENT readback.
  const pendingM4Controls=new Map();
  const m4ControlKey=(parameter,channel)=>parameter+":"+channel;
  function reconcileM4ControlReadback(next) {
    let awaiting=false;
    for(const [key,p] of pendingM4Controls) {
      const result=stripVM.m4ScalarReadbackSettlement(p,next);
      if(result==="STALE"){
        pendingM4Controls.delete(key);
        status("STALE M4 control · device/route changed; restoring readback");
      }else if(result==="CONFIRMED")pendingM4Controls.delete(key);
      else awaiting=true;
    }
    return awaiting;
  }
  function paintM4Fallback(next) {
    if(!m4Verified()||!Array.isArray(scope?.tracks))return;
    const session=next.raw?.mixerSession,m=next.raw?.legacyM4;
    if(session?.authority!=="M4"||!Array.isArray(session.channels)||session.channels.length!==10)return;
    const ctl=session.channels.map(row=>row.controls);
    paintM4FxInstances(m);
    const controlSignature=JSON.stringify([next.device.epoch,
      session.routeRevision,ctl,session.main]);
    if(controlSignature!==lastM4Controls) {
      lastM4Controls=controlSignature;
      for(let i=0;i<10;i++){
        const t=scope.tracks.find(x=>x.id===m4TrackIds[i]),c=ctl[i];
        const db=c?.faderDb,pan=c?.pan*100;
        if(!t||!c||!Number.isFinite(db)||!Number.isFinite(pan)||
           typeof c.mute!=="boolean"||typeof c.solo!=="boolean")return;
        const changed=Math.abs(t.level-db)>.015;
        const visualDb=pendingFaderVisualDb(t,db);
        const switchChanged=t.mute!==c.mute||t.solo!==c.solo;
        t.level=db;t.pan=pan;t.mute=c.mute;t.solo=c.solo;
        if(switchChanged && scope.activeFaderId!==t.id){
          scope.refreshStrip?.(t);
          m4Meters.delete(t.id);
        }
        paintFaderReadback(t,visualDb,changed);
        const panKnob=document.querySelector(
          '.strip[data-id="'+t.id+'"] .center-pan');
        if(panKnob && document.activeElement!==panKnob)
          panKnob.value=String(pan);
      }
      if(scope.master && Number.isFinite(session.main?.db)){
        const changed=Math.abs(scope.master.level-session.main.db)>.015;
        const visualDb=pendingFaderVisualDb(scope.master,session.main.db);
        scope.master.level=session.main.db;
        paintFaderReadback(scope.master,visualDb,changed);
      }
      if(scope.master && typeof session.main?.mute==="boolean" &&
         scope.master.mute!==session.main.mute){
        scope.master.mute=session.main.mute;
        if(scope.activeFaderId!==scope.master.id)
          scope.refreshStrip?.(scope.master);
      }
    }
    // Only measured JUCE callback peaks are painted. Cache actual live DOM
    // fills to avoid repeated selector searches for every timer/signal tick.
    if(!scope?.displayMeterDbfs)return;
    const now=performance.now();
    for(let i=0;i<10;i++){
      const t=scope.tracks.find(x=>x.id===m4TrackIds[i]);
      if(!t)continue;
      let entry=m4Meters.get(t.id);
      if(!entry?.root?.isConnected){
        const root=document.querySelector('.strip[data-id="'+t.id+'"]');
        if(!root)continue;
        entry={root,left:root.querySelector(".meter-fill.left"),
          right:root.querySelector(".meter-fill.right")};
        m4Meters.set(t.id,entry);
      }
      for(const side of ["left","right"]){
        const peak=side==="left"?session.channels[i].peakL:session.channels[i].peakR;
        if(!Number.isFinite(peak)||peak<0)continue;
        const fill=entry[side];if(!fill)continue;
        scope.displayMeterDbfs(fill,peak===0?-90:
          Math.max(-90,Math.min(6,20*Math.log10(peak))),
          now,"native-m4:"+i+":"+side);
      }
    }
  }
  function refreshRealM4Inputs(next){
    if(!scope?.tracks || next?.admitted || !Array.isArray(scope.tracks) ||
       scope.tracks.length<10) return;
    const session=next?.raw?.mixerSession;
    const m4=next?.raw?.legacyM4;
    const acceptedM4=session?.authority==="M4" &&
      Array.isArray(session.channels)&&session.channels.length===10;
    // An UNPATCHED prepared native M4 renderer has no accepted route yet,
    // so the SessionSnapshot deliberately reports authority NONE. The
    // operator must still SEE its verified physical IN choices to create
    // the first route. Previously an early return left every dropdown at
    // "None" forever; requiring a patched route to display the first pin
    // created a circular bootstrap dependency.
    const preparedFirstSound=session?.authority==="NONE" &&
      m4?.active===true && m4?.routeAccepted===false;
    if(!acceptedM4&&!preparedFirstSound)return;
    const rows=acceptedM4?session.channels.map(ch=>ch.source):null;
    const signature=JSON.stringify([session.authority,next?.device?.epoch,
      next?.device?.enabledInputs,next?.device?.inputEndpoints,
      m4?.active,m4?.routingRevision,rows]);
    if(signature===lastM4UiSignature)return;
    lastM4UiSignature=signature;
    for(let i=0;i<10;++i){
      const t=scope.tracks.find(x=>x.id===m4TrackIds[i]);
      if(t?.kind!=="audio")continue;
      // An unpatched session has NO physical row. Keep the model at None,
      // while rendering the native pin choices for the initial operator
      // selection. Do not borrow source/OUT data from the demo or a prior
      // device session.
      const row=acceptedM4?rows[i]:null;
      t.input=inputLabelFor(row);
      if(row && typeof row.main==="boolean")
        t.output=row.main?"Master":"SOURCE ONLY (No Main)";
      if(row && ["MONO","STEREO"].includes(row.channelFormat)){
        const format=row.channelFormat.toLowerCase();
        if(t.format!==format){
          t.format=format;
          if((scope.activeFaderId??-1)!==t.id &&
             !document.activeElement?.matches?.(".strip select"))
            scope.refreshStrip?.(t);
        }
      }
    }
    // A source ACK changes only affected fields; never replace the whole
    // mixer while another select is open or another fader is being dragged.
    const ports=JSON.stringify([next?.device?.epoch,
      next?.device?.enabledInputs,next?.device?.inputEndpoints]);
    if(ports!==lastM4PortSignature){
      lastM4PortSignature=ports;
      if((scope.activeFaderId??-1)<0 &&
         !document.activeElement?.matches?.('.strip select'))
        scope.render?.(); // refresh all hardware choices ONLY after device setup
    }else{
      for(let i=0;i<10;++i){
        const t=scope.tracks.find(x=>x.id===m4TrackIds[i]);
        if(!t)continue;
        const root=document.querySelector('.strip[data-id="'+t.id+'"]');
        for(const field of ["input","output"]){
          const element=root?.querySelector('select[data-field="'+field+'"]');
          if(field==="output"&&pendingOutputPatch?.channel===i &&
             pendingOutputPatch?.operation==="SET_PRIMARY_OUT")continue;
          if(element && document.activeElement!==element &&
             element.value!==t[field])element.value=t[field];
        }
      }
    }
  }

  const virtualCableFeedbackPair=()=>{
    const capture=String(hardware?.device?.captureDevice||"").toLowerCase();
    const playback=String(hardware?.device?.playbackDevice||"").toLowerCase();
    return (capture.includes("vb-audio")||playback.includes("vb-audio")) &&
      capture.includes("cable output") && playback.includes("cable input");
  };
  // ChannelId requires its installed accepted callback. Before the FIRST
  // source selection there is intentionally no ChannelId or M4 route yet:
  // authoritative SessionSnapshot is NONE, but legacyM4.active means the
  // JUCE callback has independently prepared a real SILENT M4 graph and
  // verified sparse physical pins. Let THAT state accept the first IN patch.
  // Never infer readiness from the Pages/demo tracks or the presence of a
  // Windows playback device alone.
  const deviceReady=()=>!!(hardware?.device?.backend &&
    // ChannelId generation is a full uint64 decimal string and MUST NOT
    // round through JS Number.MAX_SAFE_INTEGER. M4 numeric event tokens
    // remain separately range-checked before their actual dispatch.
    protocol.decimal(hardware.device.epoch)!==null &&
    hardware.device.epoch!=="0" &&
    hardware.device.sampleRate>0 && hardware.device.bufferFrames>0 &&
    Array.isArray(hardware.device.enabledInputs) &&
    // Physical IN is optional; SOURCE NONE and FX/WIRE are native edits.
    Array.isArray(hardware.device.enabledOutputs) &&
    hardware.device.enabledOutputs.length>=2 &&
    (mixerSession()?.authority==="CHANNEL_ID"
      ? controller?.state()?.current?.admitted===true &&
        flags.acceptedGraphConnected
      : (mixerSession()?.authority==="M4" ||
         mixerSession()?.authority==="NONE") &&
        legacy()?.active===true &&
        safeNativeCounter(legacy()?.routingRevision)!==null));
  // Emergency stop is a deliberate safety action in More, never an
  // everyday header toggle. Temporary driver mute is not a panic latch.
  function syncEmergencyOutputControl(d) {
    if(!muteBtn)return;
    const panic=hardware?.raw?.operatorOutputMuted===true;
    muteBtn.textContent=panic?"Resume after emergency stop":"Emergency stop audio";
    muteBtn.disabled=panic?!deviceReady():d?.outputArmed!==true;
    muteBtn.title=panic?"Resume only the verified native route":
      "Emergency stop · normal device recovery is automatic";
  }
  // Output patching is rendered from physical native readback, never a
  // second routing state. Repainting only on changes avoids 12 Hz DOM churn.
  function requestExternalOutput(t,label,el=null){
        const previous=t.externalOutput||"None";
        if(busOutputPending){
          if(el)el.value=previous;
          nativeToast("Wait for bus mapping readback");return true;
        }
        if(typeof label==="string"&&label.startsWith("Preset · ")){
          const alias=stripNamedChoices("output").find(p=>p.label===label);
          if(!alias){if(el)el.value=previous;nativeToast("I/O preset unavailable");return true;}
          label=alias.choice.label;
        }
        const id=nativeIdFor(t);
        // One pure Channel Strip preflight for ASIO port identity and
        // shared-output confirmation; only this bridge dispatches native.
        const plan=stripVM.externalOutputPlan({
          native:controller?.state()?.current,hardware,channelId:id,
          selectedLabel:label,currentLabel:previous,
          choices:hardwareOutputPairs(),pending:pendingExternal.has(id),
          channelName:t.name
        });
        if(plan.kind==="UNAVAILABLE"){
          if(el)el.value=previous;
          nativeToast("EXTERNAL OUT: unavailable · check verified ASIO pins, pending mapping or logical output assignment");
          return true;
        }
        if(plan.kind==="NO_CHANGE"){if(el)el.value=previous;return true;}
        const {left,right,epoch,revision}=plan;
        const confirmed=!plan.requiresConfirmation || window.confirm(
          plan.confirmation);
        if(!confirmed){if(el)el.value=previous;return true;}
        const requestId="out-o-"+(++externalSerial);
        pendingExternal.set(id,{requestId,channelId:id,left,right,
          epoch,revision,acked:false});
        if(el)el.value=previous;el?.setAttribute("aria-busy","true");
        backend.emitEvent("arMixAction",{action:"routingExternalOutput",
          requestId,channelId:id,deviceEpoch:epoch,revision,
          left,right,confirmed:true});
        window.setTimeout(()=>{
          if(pendingExternal.get(id)?.requestId!==requestId)return;
          pendingExternal.delete(id);el?.removeAttribute("aria-busy");
          paintPhysicalOutputs(hardware);
          nativeToast("EXTERNAL OUT timed out · last PCM retained");
          controller?.subscribe();
        },8000);
        status("EXTERNAL OUT pending · native validation and readback");
        return true;
  }
  // Matrix edits use the same queue/lease and native dispatcher as strips.
  // No synthetic PCM, no independent routing graph or optimistic crosspoint.
  let busOutputPending=null,busOutputSerial=0;
  let monitorTimingPending=null,monitorTimingSerial=0;
  function paintMonitorTiming(next){
    const row=devicePanel?.querySelector("[data-monitor-timing]");
    if(!row)return;
    const p=protocol.monitorTimingView(next),busy=Boolean(monitorTimingPending);
    row.hidden=!p;
    if(!p)return;
    const value=row.querySelector("[data-monitor-timing-value]");
    const toggle=row.querySelector("[data-monitor-timing-toggle]");
    const ms=n=>(n*1000/next.device.sampleRate).toFixed(2)+" ms";
    const summary=(p.policy==="INDEPENDENT_MONITOR"?"Singer low latency":"Aligned")+" · MAIN "+
      ms(p.mainSamples)+" · MONITOR "+ms(p.monitorSamples)+(p.observed?"":" · awaiting callback");
    if(value.textContent!==summary)value.textContent=summary;
    toggle.textContent=busy?"Applying…":p.policy==="INDEPENDENT_MONITOR"?"Use aligned":"Use singer low latency";
    toggle.disabled=busy||!p.editable||matrixBusy()||!matrixReady(next);
    toggle.onclick=()=>{
      const live=protocol.monitorTimingView(hardware);
      if(!live?.editable||live.revision!==p.revision||live.epoch!==p.epoch||matrixBusy()||!matrixReady(hardware))return;
      const policy=live.policy==="GLOBAL_ALIGNED"?"INDEPENDENT_MONITOR":"GLOBAL_ALIGNED";
      const warning=policy==="INDEPENDENT_MONITOR"?
        "Use a separate singer monitor only. MAIN and MONITOR may have different timing; do not sum them acoustically or electrically. Removes up to "+ms(live.potentialSaving)+" of graph alignment, not hardware or insert latency.":
        "Restore global MAIN/MONITOR alignment?";
      if(!window.confirm(warning+" Outputs remain disarmed. Session only."))return;
      const requestId="monitor-timing-"+(++monitorTimingSerial);
      monitorTimingPending={requestId,epoch:live.epoch,roster:live.rosterRevision,
        base:live.revision,policy,config:hardware.hardwareConfiguration,main:live.main,monitor:live.monitor,ack:null};
      backend.emitEvent("arMixAction",{action:"routingMonitorTiming",requestId,confirmed:true,
        policy,deviceEpoch:live.epoch,revision:live.revision,rosterRevision:live.rosterRevision,
        main:live.main,monitor:live.monitor});
      paintMonitorTiming(hardware);
      window.setTimeout(()=>{
        if(monitorTimingPending?.requestId!==requestId)return;
        monitorTimingPending=null;nativeToast("MONITOR timing confirmation timed out · keep outputs muted");
        paintMonitorTiming(hardware);
      },5000);
    };
  }
  function settleMonitorTiming(next){
    const q=monitorTimingPending;
    if(!q)return;
    const p=protocol.monitorTimingView(next);
    if(!p||p.epoch!==q.epoch||p.rosterRevision!==q.roster||next.hardwareConfiguration!==q.config||
       JSON.stringify(p.main)!==JSON.stringify(q.main)||JSON.stringify(p.monitor)!==JSON.stringify(q.monitor)||
       (q.ack&&protocol.cmp(p.revision,q.ack)>0)){
      monitorTimingPending=null;nativeToast("MONITOR timing lease changed · verify before START AUDIO");return;
    }
    if(q.ack&&p.revision===q.ack&&p.policy===q.policy&&p.observed){
      monitorTimingPending=null;nativeToast("MONITOR timing verified · outputs remain disarmed");
    }
  }
  if(connected)backend.addEventListener("arMixMonitorTimingAck",ack=>{
    const q=monitorTimingPending;
    if(!q||q.requestId!==ack?.requestId)return;
    if(ack.ok!==true||protocol.decimal(ack.revision)===null||protocol.cmp(ack.revision,q.base)<=0){
      monitorTimingPending=null;nativeToast(ack.error||"MONITOR timing rejected");paintMonitorTiming(hardware);return;
    }
    q.ack=ack.revision;settleMonitorTiming(hardware);controller?.subscribe();paintMonitorTiming(hardware);
  });
  let logicalIoPending=null,logicalIoSerial=0,logicalIoEditorOpen=false;
  let logicalIoPort="",logicalIoChannel="";
  let logicalOutPort="",logicalOutChannel="",logicalOutEditorOpen=false;
  let ioDefaultsPending=null,ioDefaultsSerial=0;
  const matrixBusy=()=>Boolean(ioDefaultsPending||logicalIoPending||busOutputPending||monitorTimingPending||deviceBusy()||activeNativeSource||pendingNativeSources.size||
    nativeUiSourcePending||pendingSourceQueue.size||pendingExternal.size);
  function matrixReady(snapshot){
    return deviceVM.setupRoutingReady(deviceInventory,deviceChoice,deviceState)&&
      deviceVM.setupPinsApplied(deviceInventory,deviceChoice)&&
      deviceVM.setupMatrixDeviceMatches(deviceInventory,snapshot);
  }
  function applyMatrixRow(side,id,label,lease,requested=null){
    const current=controller?.state()?.current;
    const view=deviceVM.patchMatrix(hardware,side);
    const row=view.rows.find(x=>x.id===id&&x.editable);
    if(!row||!view.live||!deviceReady()||!matrixReady(hardware)||matrixBusy()||namedPatchPending||
       !lease||lease.config!==hardware.hardwareConfiguration||
       lease.epoch!==hardware.device.epoch||
       lease.authority!==view.authority||lease.revision!==view.revision||
       lease.roster!==view.roster){
      nativeToast("Routing changed · review current matrix before applying");
      paintPhysicalOutputs(hardware);return;
    }
    if(side==="input"){
      const nativeOwner=view.authority==="CHANNEL_ID";
      const choices=verifiedMatrixSourceChoices(
        nativeOwner?sourceChoicesFor():inputChoicesFor(),requested,hardware);
      const choice=choices.find(x=>x.label===label);
      if(!choice||choice.mode==="PLAYER")return;
      const decision=stripVM.sourceQueueSelection({choice,choices,
        epoch:hardware.device.epoch,acceptedLabel:row.selected,
        hardwareConfiguration:hardware.hardwareConfiguration});
      if(decision.kind!=="QUEUE")return;
      if(nativeOwner){
        pendingNativeSources.set(id,decision.entry);
        dispatchNativeSource();
      }else{
        const channel=Number(id.slice(3));
        if(!Number.isInteger(channel)||channel<0||channel>=10)return;
        pendingSourceQueue.set(channel,decision.entry);
        dispatchQueuedSource();
      }
    }else if(row.bus){
      if(!requested||JSON.stringify(row.pair)!==lease.busPairs?.[id])return;
      const choice=row.choices.find(x=>x.label===label&&x.left===requested.left&&x.right===requested.right);
      if(!choice||choice.label===row.selected||
         !window.confirm("Remap "+row.name+" to "+choice.label+"? Outputs stay muted until START AUDIO."))return;
      const requestId="bus-out-"+(++busOutputSerial);
      busOutputPending={requestId,id,epoch:lease.epoch,config:lease.config,revision:lease.revision,
        left:choice.left,right:choice.right,acked:false};
      backend.emitEvent("arMixAction",{action:"routingPhysicalBusOutput",requestId,
        deviceEpoch:lease.epoch,revision:lease.revision,bus:row.name,confirmed:true,
        previousLeft:row.pair[0],previousRight:row.pair[1],left:choice.left,right:choice.right,
        leftName:view.pins.find(p=>p.port===choice.left)?.label,
        rightName:view.pins.find(p=>p.port===choice.right)?.label});
      window.setTimeout(()=>{
        if(busOutputPending?.requestId!==requestId)return;
        busOutputPending=null;nativeToast("Bus mapping confirmation timed out · keep output muted and reopen Audio I/O");
        paintPhysicalOutputs(hardware);
      },5000);
    }else{
      const track=scope?.tracks?.find(t=>nativeIdFor(t)===id);
      if(!track||!current?.admitted)return;
      requestExternalOutput(track,label);
    }
    paintPhysicalOutputs(hardware);
  }
  function settleBusOutput(next){
    const p=busOutputPending;
    if(!p)return;
    if(next.device?.epoch!==p.epoch||next.hardwareConfiguration!==p.config||
       next.graph?.revision!==p.revision||!next.admitted||next.retained){
      busOutputPending=null;nativeToast("Bus mapping lease expired");return;
    }
    const pair=next.raw?.[p.id];
    if(!p.acked||next.raw?.physicalBusOutputObserved!==true||
       pair?.[0]!==p.left||pair?.[1]!==p.right)return;
    busOutputPending=null;nativeToast("Bus mapping verified · output remains muted");
    backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
  }
  function pollBusOutput(requestId,attempt=0){
    if(busOutputPending?.requestId!==requestId||attempt>=20)return;
    controller?.subscribe();
    window.setTimeout(()=>pollBusOutput(requestId,attempt+1),150);
  }
  if(connected)backend.addEventListener("arMixPhysicalBusOutputAck",ack=>{
    if(busOutputPending?.requestId!==ack?.requestId)return;
    if(ack.ok!==true){
      busOutputPending=null;nativeToast(ack.error||"Bus mapping rejected");paintPhysicalOutputs(hardware);return;
    }
    busOutputPending.acked=true;
    pollBusOutput(ack.requestId);
  });
  let namedPatchSerial=0,namedPatchPending=null;
  let namedProfileFileSerial=0,namedProfileFilePending=null;
  function namedPatchChoices(side,model,next){
    return matrixReady(next)?deviceVM.namedPatchChoices(deviceInventory,next,side):[];
  }
  function stripNamedChoices(side){
    if(namedPatchPending||deviceBusy()||hardware?.retained||logicalView)return [];
    return deviceVM.namedPatchChoices(deviceInventory,hardware,side);
  }
  // Update only options on mounted inputs. NativeHooks covers strips that
  // are mounted later by bank/roster projection. Never replace a fader or
  // an active select; stale selection still fails exact admission on change.
  function syncNamedStripOptions(side){
    const labels=stripNamedChoices(side).map(p=>p.label);
    for(const t of scope?.tracks||[]){
      if(side==="input"?t.kind!=="audio":t.kind==="master")continue;
      const field=side==="input"?"input":"external";
      const el=document.querySelector('.strip[data-id="'+t.id+'"] select[data-field="'+field+'"]');
      if(!el?.options||document.activeElement===el)continue;
      const current=[...el.options].filter(o=>o.value.startsWith("Preset · "));
      if(current.length===labels.length&&current.every((o,i)=>o.value===labels[i]))continue;
      for(const o of current)o.remove();
      for(const label of labels)el.append(new Option(label,label));
    }
  }
  function syncNamedOptions(){
    syncNamedStripOptions("input");syncNamedStripOptions("output");
  }
  let namedInventoryLease="";
  function requestNamedInventory(next){
    // One cached inventory request per installed device lease, not a scan
    // and never a polling operation. Open dialog owns its own inventory.
    if(!connected||next?.retained||!next?.hardwareConfiguration||
       !next.device?.epoch||(!next.admitted&&next.raw?.legacyM4?.active!==true)||
       (devicePanel&&!devicePanel.hidden)||deviceBusy())return;
    const lease=next.hardwareConfiguration+"|"+next.device.epoch;
    if(namedInventoryLease===lease)return;
    namedInventoryLease=lease;
    if(!deviceVM.setupMatrixDeviceMatches(deviceInventory,next))
      backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
  }
  function renderNamedPatchEditor(host,side,model,next,busy){
    const data=deviceInventory?.namedPatches;
    if(!data||!Array.isArray(data.entries))return; // older host, no fake storage
    const entries=data.entries.filter(p=>p.side===side);
    const section=document.createElement("div");section.className="armix-named-patches";
    const title=document.createElement("strong");title.textContent="Named "+side+" presets";
    const existing=document.createElement("select");existing.setAttribute("aria-label","Saved "+side+" preset");
    existing.append(new Option("New preset",""));
    for(const p of entries)existing.append(new Option(p.name+(p.available?"":" · Needs remap"),p.id));
    const name=document.createElement("input");name.type="text";name.maxLength=48;name.placeholder="Preset name";name.setAttribute("aria-label","Preset name");
    const mode=document.createElement("select");mode.setAttribute("aria-label","Preset format");
    if(side==="input")mode.append(new Option("Mono","MONO"));
    mode.append(new Option("Stereo","STEREO"));
    const pin=(label)=>{
      const el=document.createElement("select");el.setAttribute("aria-label","Preset "+label+" pin");
      for(const p of model.pins)el.append(new Option(label+" · "+(p.port+1)+" "+p.label,String(p.port)));
      return el;
    };
    const left=pin("L"),right=pin("R");if(model.pins.length>1)right.selectedIndex=1;
    const save=document.createElement("button");save.type="button";save.textContent="Save preset";
    const remove=document.createElement("button");remove.type="button";remove.textContent="Remove";
    const hint=document.createElement("span");hint.className="armix-device-note";
    hint.textContent=data.readOnly?data.error:"Presets do not change any mixer route until row Apply.";
    const exchange=document.createElement("details");
    const summary=document.createElement("summary");summary.textContent="Import / Export presets";
    const profileText=document.createElement("textarea");
    profileText.setAttribute("aria-label","I/O profile JSON");profileText.rows=3;
    profileText.maxLength=262144;profileText.style.width="100%";
    profileText.placeholder="Paste a compatible I/O profile";
    const exportProfile=document.createElement("button");exportProfile.type="button";exportProfile.textContent="Export profile";
    const importProfile=document.createElement("button");importProfile.type="button";importProfile.textContent="Import profile";
    const exchangeHint=document.createElement("span");exchangeHint.className="armix-device-note";
    exchangeHint.textContent="Copy or paste JSON. Import adds presets only; duplicate names or unavailable pins reject the entire profile.";
    const openFile=document.createElement("button");openFile.type="button";openFile.textContent="Open .ario file";
    const saveFile=document.createElement("button");saveFile.type="button";saveFile.textContent="Save .ario file";
    if(data.fileExchange===true)exchange.append(summary,openFile,saveFile,exportProfile,importProfile,profileText,exchangeHint);
    else exchange.append(summary,exportProfile,importProfile,profileText,exchangeHint);
    const fields=["name","mode","left","right","leftName","rightName"];
    const identity=()=>Object.fromEntries(["backend","captureDevice","playbackDevice"].map(k=>[k,deviceInventory.active[k]]));
    const choice=()=>{
      if(left.value===""||(mode.value==="STEREO"&&right.value===""))return null;
      return side==="input"?deviceVM.physicalInputChoice(next,mode.value,Number(left.value),
        mode.value==="MONO"?-1:Number(right.value)):
        model.choices.find(x=>x.left===Number(left.value)&&x.right===Number(right.value));
    };
    const locked=busy||!!namedPatchPending||data.readOnly===true;
    const update=()=>{
      right.hidden=mode.value==="MONO";
      const editingLocked=locked||!!namedProfileFilePending;
      for(const el of [existing,name,mode,left,right,profileText])el.disabled=editingLocked;
      save.disabled=editingLocked||!name.value.trim()||!choice();
      remove.disabled=editingLocked||!existing.value;
      exportProfile.disabled=editingLocked||!entries.length;
      importProfile.disabled=editingLocked||!profileText.value.trim();
      openFile.disabled=editingLocked;
      saveFile.disabled=editingLocked||!entries.length;
    };
    profileText.oninput=update;
    const chooseFile=operation=>{
      if(locked||namedProfileFilePending||namedPatchPending||matrixBusy()||!matrixReady(hardware)||
         deviceInventory?.namedPatches?.revision!==data.revision||
         hardware.device.epoch!==next.device.epoch||
         hardware.hardwareConfiguration!==next.hardwareConfiguration)return;
      const requestId="io-file-"+(++namedProfileFileSerial);
      const lease={...identity(),side,revision:data.revision,deviceGeneration:deviceInventory.active.deviceGeneration};
      const unlockEditor=()=>{
        update();
        if(!profileText.isConnected){
          for(const grid of devicePanel?.querySelectorAll("[data-audio-input-grid],[data-audio-output-grid]")||[])
            delete grid.dataset.signature;
          paintPhysicalOutputs(hardware);
        }
      };
      namedProfileFilePending={requestId,operation,finish:message=>{
        unlockEditor();
        if(message.cancelled)return;
        if(message.ok!==true){nativeToast(message.error||"I/O profile file operation failed");return;}
        if(operation==="EXPORT"){nativeToast("I/O profile file saved");return;}
        if(!profileText.isConnected||!matrixReady(hardware)||deviceInventory?.namedPatches?.revision!==lease.revision||
           hardware.device.epoch!==next.device.epoch||hardware.hardwareConfiguration!==next.hardwareConfiguration||
           !Object.entries(identity()).every(([k,v])=>v===lease[k])||
           !message.profile||message.profile.side!==side||
           !Object.entries(identity()).every(([k,v])=>message.profile[k]===v)){
          nativeToast("I/O profile draft expired · reopen file");return;
        }
        profileText.value=JSON.stringify(message.profile,null,2);
        update();
        nativeToast("I/O profile loaded · review and Import profile");
      }};
      update();
      backend.emitEvent("arMixAction",{action:"audioNamedProfileFile",requestId,operation,...lease});
      window.setTimeout(()=>{
        if(namedProfileFilePending?.requestId!==requestId)return;
        namedProfileFilePending=null;unlockEditor();
        nativeToast("I/O file dialog confirmation expired");
      },120000);
    };
    openFile.onclick=()=>chooseFile("OPEN");
    saveFile.onclick=()=>chooseFile("EXPORT");
    exportProfile.onclick=()=>{
      if(locked||!matrixReady(hardware))return;
      profileText.value=JSON.stringify({schema:"arsonmix.io-profile.v1",...identity(),side,
        entries:entries.map(p=>Object.fromEntries(fields.map(k=>[k,p[k]])))},null,2);
      profileText.focus();profileText.select();update();
    };
    existing.onchange=()=>{
      const p=entries.find(x=>x.id===existing.value);
      name.value=p?.name||"";
      if(p){mode.value=p.mode;left.value=String(p.left);right.value=String(p.right);}
      update();
    };
    for(const el of [existing,name,mode,left,right])el.disabled=locked;
    name.oninput=mode.onchange=left.onchange=right.onchange=update;
    const send=(operation,profile=null)=>{
      if(locked||namedPatchPending||namedProfileFilePending||matrixBusy()||!matrixReady(hardware)||
         hardware.device.epoch!==next.device.epoch||
         hardware.hardwareConfiguration!==next.hardwareConfiguration||
         deviceInventory?.namedPatches?.revision!==data.revision)return;
      const a=deviceInventory.active,c=choice();
      if(operation==="SAVE"&&!c)return;
      const requestId="io-preset-"+(++namedPatchSerial);
      const payload={operation,revision:data.revision,deviceGeneration:a.deviceGeneration,
        backend:a.backend,captureDevice:a.captureDevice,playbackDevice:a.playbackDevice,
        id:existing.value,side,name:name.value.trim(),mode:mode.value,
        left:c?.left??-1,right:c?.right??-1,
        leftName:model.pins.find(p=>p.port===c?.left)?.label||"",
        rightName:model.pins.find(p=>p.port===c?.right)?.label||""};
      if(operation==="IMPORT"){payload.id="";payload.profile=profile;}
      namedPatchPending={requestId,phase:"APPLYING",revision:data.revision,payload};
      backend.emitEvent("arMixAction",{action:"audioNamedPatchEdit",requestId,...payload});
      paintPhysicalOutputs(hardware);
      window.setTimeout(()=>{
        if(namedPatchPending?.requestId!==requestId)return;
        namedPatchPending=null;
        // Do not continue to offer stale choices after uncertain disk commit.
        if(deviceInventory)deviceInventory={...deviceInventory,namedPatches:null};
        syncNamedOptions();
        nativeToast("I/O preset confirmation timed out · reopen Audio I/O");
        paintPhysicalOutputs(hardware);
      },5000);
    };
    save.onclick=()=>send("SAVE");
    remove.onclick=()=>{if(existing.value&&window.confirm("Remove this routing preset? Installed routes are unchanged."))send("REMOVE");};
    importProfile.onclick=()=>{
      if(locked)return;
      let profile;
      try{profile=JSON.parse(profileText.value);}catch(_){nativeToast("Invalid I/O profile JSON");return;}
      if(!profile||profile.schema!=="arsonmix.io-profile.v1"||profile.side!==side||
         !Array.isArray(profile.entries)||!profile.entries.length||profile.entries.length>128||
         !profile.entries.every(p=>p&&typeof p.name==="string"&&fields.every(k=>Object.hasOwn(p,k)))||
         !Object.entries(identity()).every(([k,v])=>profile[k]===v)){
        nativeToast("Incompatible I/O profile");return;
      }
      send("IMPORT",profile);
    };
    section.append(title,existing,name,mode,left,right,save,remove,hint,exchange);update();host.append(section);
  }
  function settleLogicalIo(next){
    const p=logicalIoPending;
    if(!p)return;
    if(next?.device?.epoch!==p.epoch||next.hardwareConfiguration!==p.config){
      logicalIoPending=null;logicalIoPort="";logicalIoChannel="";logicalOutPort="";logicalOutChannel="";
      nativeToast("Logical I/O device changed");return;
    }
    const output=p.payload.action==="routingLogicalOutputEdit";
    const data=output?next.raw?.logicalOutputRegistry:next.raw?.logicalInputRegistry;
    if(!p.acked||data?.revision!==p.logicalRevision||next.graph?.revision!==p.graphRevision||
       !Array.isArray(data.ports)||!Array.isArray(data.references))return;
    const q=p.payload,port=data.ports.find(x=>x.id===q.portId);
    let proved=false;
    if(q.operation==="ADD"){
      const created=data.ports.filter(x=>!p.previousIds.includes(x.id)&&x.name===q.name&&x.format===q.format);
      proved=created.length===1;
      if(proved){if(output)logicalOutPort=created[0].id;else logicalIoPort=created[0].id;}
    }else if(q.operation==="RENAME")proved=port?.name===q.name;
    else if(q.operation==="REMOVE"){proved=!port;if(proved){if(output)logicalOutPort="";else logicalIoPort="";}}
    else if(q.operation==="BIND")proved=port?.bound===true&&
      ["left","right","leftName","rightName"].every(k=>port[k]===q[k]);
    else if(q.operation==="UNBIND")proved=port?.bound===false;
    else if(q.operation==="ASSIGN")proved=data.references.some(x=>x.channelId===q.channelId&&x.portId===q.portId);
    else if(q.operation==="CLEAR")proved=!data.references.some(x=>x.channelId===q.channelId);
    if(proved){logicalIoPending=null;nativeToast("Logical I/O updated");}
  }
  // Native snapshots may replace the editor in the same task as an
  // activation. Do not use the deferred/coalesced details toggle event:
  // a detached old element could overwrite the operator's current choice.
  function bindLogicalDisclosure(section,summary,open,remember){
    section.open=open;
    summary.onclick=event=>{
      if(event.defaultPrevented)return;
      event.preventDefault();
      section.open=!section.open;
      remember(section.open);
    };
  }
  function renderLogicalInputs(host,model,next,busy){
    const data=next.raw?.logicalInputRegistry;
    if(model.authority!=="CHANNEL_ID"||!Array.isArray(data?.ports)||
       !Array.isArray(data.references)||typeof data.revision!=="string")return;
    const section=document.createElement("details");section.className="armix-named-patches";
    const summary=document.createElement("summary");summary.textContent="Session logical inputs";
    bindLogicalDisclosure(section,summary,logicalIoEditorOpen,open=>{logicalIoEditorOpen=open;});
    const hint=document.createElement("span");hint.className="armix-device-note";
    hint.textContent="Ports saved in .smix. Remapping a port updates every assigned channel.";
    const port=document.createElement("select");port.setAttribute("aria-label","Logical input port");
    port.append(new Option("New logical input",""));
    for(const p of data.ports){
      const refs=data.references.filter(r=>r.portId===p.id);
      port.append(new Option(p.name+" · "+p.format+" · "+refs.length+" used"+
        (refs.some(r=>r.state==="NEEDS_REMAP")?" · Needs remap":""),p.id));
    }
    port.value=data.ports.some(p=>p.id===logicalIoPort)?logicalIoPort:"";
    const name=document.createElement("input");name.maxLength=48;name.placeholder="Logical input name";
    name.setAttribute("aria-label","Logical input name");
    const format=document.createElement("select");format.setAttribute("aria-label","Logical input format");
    format.append(new Option("Mono","MONO"),new Option("Stereo","STEREO"));
    const makePin=label=>{
      const el=document.createElement("select");el.setAttribute("aria-label","Logical "+label+" pin");
      el.append(new Option("Unselected",""));
      for(const p of model.pins)el.append(new Option((p.port+1)+" · "+p.label,String(p.port)));
      return el;
    };
    const left=makePin("L"),right=makePin("R");
    const channel=document.createElement("select");channel.setAttribute("aria-label","Logical input channel");
    channel.append(new Option("Choose mixer channel",""));
    for(const row of model.rows){
      const ref=data.references.find(r=>r.channelId===row.id);
      const assigned=data.ports.find(p=>p.id===ref?.portId);
      channel.append(new Option(row.name+(assigned?" · "+assigned.name:""),row.id));
    }
    channel.value=model.rows.some(row=>row.id===logicalIoChannel)?logicalIoChannel:"";
    const buttons={};
    for(const [key,label] of [["ADD","Add port"],["RENAME","Rename port"],["REMOVE","Remove port"],
      ["BIND","Apply pin binding"],["UNBIND","Unbind port"],["ASSIGN","Assign to channel"],["CLEAR","Clear logical assignment"]]){
      const b=document.createElement("button");b.type="button";b.textContent=label;buttons[key]=b;
    }
    const selected=()=>data.ports.find(p=>p.id===port.value);
    const used=()=>data.references.some(r=>r.portId===port.value);
    const currentPin=el=>model.pins.find(p=>String(p.port)===el.value);
    const update=()=>{
      const p=selected(),locked=busy||!!logicalIoPending;
      for(const el of [port,name,format,left,right,channel])el.disabled=locked;
      format.disabled=locked||!!p;right.hidden=format.value==="MONO";
      buttons.ADD.disabled=locked||!!p||!name.value.trim();
      buttons.RENAME.disabled=locked||!p||!name.value.trim();
      buttons.REMOVE.disabled=locked||!p||used();
      buttons.BIND.disabled=locked||!p||!currentPin(left)||
        (format.value==="STEREO"&&(!currentPin(right)||left.value===right.value));
      buttons.UNBIND.disabled=locked||!p||!p.bound;
      buttons.ASSIGN.disabled=locked||!p||!channel.value;
      buttons.CLEAR.disabled=locked||!data.references.some(r=>r.channelId===channel.value);
    };
    const loadPort=()=>{
      const p=selected();logicalIoPort=port.value;
      name.value=p?.name||"";format.value=p?.format||"MONO";
      const d=next.device;
      const same=p?.bound&&p.backend===d.backend&&p.capture===d.captureDevice&&p.playback===d.playbackDevice;
      left.value=same&&model.pins.some(x=>x.port===p.left&&x.label===p.leftName)?String(p.left):"";
      right.value=same&&model.pins.some(x=>x.port===p.right&&x.label===p.rightName)?String(p.right):"";
      update();
    };
    port.onchange=loadPort;
    name.oninput=format.onchange=left.onchange=right.onchange=update;
    channel.onchange=()=>{logicalIoChannel=channel.value;update();};
    const send=operation=>{
      if(buttons[operation].disabled||busy||logicalIoPending||matrixBusy()||namedPatchPending||!matrixReady(hardware)||
         hardware.device.epoch!==next.device.epoch||hardware.hardwareConfiguration!==next.hardwareConfiguration||
         hardware.graph?.revision!==model.revision||hardware.graph?.rosterRevision!==model.roster||
         hardware.raw?.logicalInputRegistry?.revision!==data.revision)return;
      if((operation==="BIND"||operation==="UNBIND")&&used()&&
         !window.confirm("Update every mixer channel using this logical input?"))return;
      const l=currentPin(left),r=format.value==="STEREO"?currentPin(right):null;
      const requestId="logical-io-"+(++logicalIoSerial);
      const payload={action:"routingLogicalInputEdit",requestId,operation,
        deviceEpoch:next.device.epoch,graphRevision:model.revision,rosterRevision:model.roster,
        logicalRevision:data.revision,portId:port.value,channelId:channel.value,
        name:name.value.trim(),format:format.value,left:l?.port??-1,right:r?.port??-1,
        leftName:l?.label||"",rightName:r?.label||""};
      logicalIoPending={requestId,epoch:next.device.epoch,config:next.hardwareConfiguration,
        payload,previousIds:data.ports.map(x=>x.id),acked:false};
      backend.emitEvent("arMixAction",payload);
      paintPhysicalOutputs(hardware);
      window.setTimeout(()=>{
        if(logicalIoPending?.requestId!==requestId)return;
        logicalIoPending=null;paintPhysicalOutputs(hardware);controller?.subscribe();
        nativeToast("Logical I/O confirmation timed out · verify current routing");
      },8000);
    };
    for(const [key,b] of Object.entries(buttons))b.onclick=()=>send(key);
    section.append(summary,hint,port,name,format,buttons.ADD,buttons.RENAME,buttons.REMOVE,
      left,right,buttons.BIND,buttons.UNBIND,channel,buttons.ASSIGN,buttons.CLEAR);
    loadPort();host.append(section);
  }
  function renderLogicalOutputs(host,model,next,busy){
    const data=next.raw?.logicalOutputRegistry;
    if(model.authority!=="CHANNEL_ID"||next.device?.backend!=="ASIO"||
       !Array.isArray(data?.ports)||!Array.isArray(data.references)||typeof data.revision!=="string")return;
    const section=document.createElement("details");section.className="armix-named-patches";
    const summary=document.createElement("summary");summary.textContent="Session logical outputs";
    bindLogicalDisclosure(section,summary,logicalOutEditorOpen,open=>{logicalOutEditorOpen=open;});
    const hint=document.createElement("span");hint.className="armix-device-note";
    hint.textContent="Mute MAIN and STREAM first. MAIN/MONITOR aliases must match their verified physical pair. External remap updates all assigned channels.";
    const port=document.createElement("select");port.setAttribute("aria-label","Logical output port");
    port.append(new Option("New logical output",""));
    for(const p of data.ports){
      const refs=data.references.filter(r=>r.portId===p.id);
      port.append(new Option(p.name+" · "+refs.length+" used"+(refs.some(r=>r.state==="NEEDS_REMAP")?" · Needs remap":""),p.id));
    }
    port.value=data.ports.some(p=>p.id===logicalOutPort)?logicalOutPort:"";
    const name=document.createElement("input");name.maxLength=48;name.placeholder="Logical output name";
    name.setAttribute("aria-label","Logical output name");
    const pair=document.createElement("select");pair.setAttribute("aria-label","Logical output stereo pair");
    pair.append(new Option("Unselected",""));
    const pairs=model.pins.filter(p=>p.port%2===0&&model.pins.some(q=>q.port===p.port+1))
      .map(p=>({l:p,r:model.pins.find(q=>q.port===p.port+1)}));
    for(const p of pairs)pair.append(new Option((p.l.port+1)+"/"+(p.r.port+1)+" · "+p.l.label+" + "+p.r.label,String(p.l.port)));
    const channel=document.createElement("select");channel.setAttribute("aria-label","Logical output channel");
    channel.append(new Option("Choose mixer channel",""));
    const channels=(next.graph?.channels||[]).filter(c=>["INPUT","GROUP","FX_RETURN","MAIN","MONITOR"].includes(c.role));
    for(const c of channels){
      const ref=data.references.find(r=>r.channelId===c.id),assigned=data.ports.find(p=>p.id===ref?.portId);
      channel.append(new Option(c.name+(assigned?" · "+assigned.name:""),c.id));
    }
    channel.value=channels.some(c=>c.id===logicalOutChannel)?logicalOutChannel:"";
    const buttons={};
    for(const [key,label] of [["ADD","Add port"],["RENAME","Rename port"],["REMOVE","Remove port"],["BIND","Apply output pair"],["UNBIND","Unbind port"],["ASSIGN","Assign to channel"],["CLEAR","Clear logical assignment"]]){
      const b=document.createElement("button");b.type="button";b.textContent=label;buttons[key]=b;
    }
    const selected=()=>data.ports.find(p=>p.id===port.value);
    const used=()=>data.references.some(r=>r.portId===port.value);
    const muted=()=>next.raw?.operatorOutputMuted===true&&next.device?.outputArmed===false&&next.raw?.physicalBusSecondaryArmed===false;
    const update=()=>{
      const p=selected(),locked=busy||!!logicalIoPending||!muted();
      for(const el of [port,name,pair,channel])el.disabled=locked;
      buttons.ADD.disabled=locked||!!p||!name.value.trim();
      buttons.RENAME.disabled=locked||!p||!name.value.trim();
      buttons.REMOVE.disabled=locked||!p||used();
      buttons.BIND.disabled=locked||!p||!pairs.some(q=>String(q.l.port)===pair.value);
      buttons.UNBIND.disabled=locked||!p||!p.bound||data.references.some(r=>r.portId===p.id&&r.role!=="EXTERNAL");
      buttons.ASSIGN.disabled=locked||!p||!channel.value;
      buttons.CLEAR.disabled=locked||!data.references.some(r=>r.channelId===channel.value);
    };
    const loadPort=()=>{
      const p=selected();logicalOutPort=port.value;name.value=p?.name||"";
      pair.value=p?.bound&&p.backend===next.device.backend&&p.playback===next.device.playbackDevice&&
        pairs.some(q=>q.l.port===p.left&&q.r.port===p.right&&q.l.label===p.leftName&&q.r.label===p.rightName)?String(p.left):"";
      update();
    };
    port.onchange=loadPort;name.oninput=pair.onchange=update;
    channel.onchange=()=>{logicalOutChannel=channel.value;update();};
    const send=operation=>{
      if(buttons[operation].disabled||busy||logicalIoPending||matrixBusy()||namedPatchPending||!matrixReady(hardware)||
         hardware.device.epoch!==next.device.epoch||hardware.hardwareConfiguration!==next.hardwareConfiguration||
         hardware.graph?.revision!==model.revision||hardware.graph?.rosterRevision!==model.roster||
         hardware.raw?.logicalOutputRegistry?.revision!==data.revision||hardware.raw?.operatorOutputMuted!==true||
         hardware.device.outputArmed!==false||hardware.raw?.physicalBusSecondaryArmed!==false)return;
      const confirmed=["BIND","UNBIND","ASSIGN","CLEAR"].includes(operation);
      if(confirmed&&!window.confirm("Confirm logical output change? Verify the exact hardware destination. MAIN and STREAM must remain muted."))return;
      const selectedPair=pairs.find(q=>String(q.l.port)===pair.value);
      const requestId="logical-io-"+(++logicalIoSerial);
      const payload={action:"routingLogicalOutputEdit",requestId,operation,confirmed,
        deviceEpoch:next.device.epoch,graphRevision:model.revision,rosterRevision:model.roster,
        logicalRevision:data.revision,portId:port.value,channelId:channel.value,
        name:name.value.trim(),format:"STEREO",left:selectedPair?.l.port??-1,right:selectedPair?.r.port??-1,
        leftName:selectedPair?.l.label||"",rightName:selectedPair?.r.label||""};
      logicalIoPending={requestId,epoch:next.device.epoch,config:next.hardwareConfiguration,
        payload,previousIds:data.ports.map(x=>x.id),acked:false};
      backend.emitEvent("arMixAction",payload);paintPhysicalOutputs(hardware);
      window.setTimeout(()=>{
        if(logicalIoPending?.requestId!==requestId)return;
        logicalIoPending=null;paintPhysicalOutputs(hardware);controller?.subscribe();
        nativeToast("Logical output confirmation timed out · verify current routing");
      },8000);
    };
    for(const [key,b] of Object.entries(buttons))b.onclick=()=>send(key);
    section.append(summary,hint,port,name,buttons.ADD,buttons.RENAME,buttons.REMOVE,
      pair,buttons.BIND,buttons.UNBIND,channel,buttons.ASSIGN,buttons.CLEAR);
    loadPort();host.append(section);
  }
  function settleIoDefaults(next){
    const p=ioDefaultsPending;
    if(!p)return;
    if(next?.device?.epoch!==p.epoch||next.hardwareConfiguration!==p.config){
      ioDefaultsPending=null;nativeToast("I/O default device changed");return;
    }
    const data=next.raw?.ioDefaults,ins=next.raw?.logicalInputRegistry,outs=next.raw?.logicalOutputRegistry;
    if(!p.ack||data?.revision!==p.ack.revision||
       ins?.revision!==p.ack.inputRevision||outs?.revision!==p.ack.outputRevision)return;
    const proved=p.op==="SAVE"?data.inputs===p.inputs&&data.outputs===p.outputs:
      p.op==="CLEAR"?data.inputs===0&&data.outputs===0:
      Array.isArray(ins.ports)&&Array.isArray(outs.ports)&&ins.ports.length===data.inputs&&outs.ports.length===data.outputs&&
      ins.references?.length===0&&outs.references?.length===0&&data.canLoad===false;
    if(proved){ioDefaultsPending=null;nativeToast(p.op==="LOAD"?"Default ports loaded · assign channels explicitly":"I/O default updated");}
  }
  function renderIoDefaults(host,model,next,busy){
    const data=next.raw?.ioDefaults,ins=next.raw?.logicalInputRegistry,outs=next.raw?.logicalOutputRegistry;
    if(model.authority!=="CHANNEL_ID"||typeof data?.revision!=="string"||
       typeof ins?.revision!=="string"||typeof outs?.revision!=="string"||
       !Array.isArray(ins.ports)||!Array.isArray(outs.ports))return;
    const section=document.createElement("div");section.className="armix-named-patches";
    const info=document.createElement("span");info.className="armix-device-note";
    info.textContent=data.readOnly?(data.error||"Default storage read-only"):
      "Default I/O · "+data.inputs+" input / "+data.outputs+" output ports · new empty mixer only";
    section.append(info);
    const buttons=[["SAVE","Make Default",ins.ports.length+outs.ports.length===0],
      ["LOAD","Load Default",data.canLoad!==true],["CLEAR","Clear Default",data.inputs+data.outputs===0]];
    for(const [op,label,unavailable] of buttons){
      const button=document.createElement("button");button.type="button";button.textContent=label;
      button.disabled=busy||!!ioDefaultsPending||data.readOnly||unavailable;
      button.onclick=()=>{
        if(button.disabled||matrixBusy()||namedPatchPending||!matrixReady(hardware)||
           hardware.device.epoch!==next.device.epoch||hardware.hardwareConfiguration!==next.hardwareConfiguration||
           hardware.graph?.revision!==model.revision||hardware.graph?.rosterRevision!==model.roster||
           hardware.raw?.ioDefaults?.revision!==data.revision||
           hardware.raw?.logicalInputRegistry?.revision!==ins.revision||
           hardware.raw?.logicalOutputRegistry?.revision!==outs.revision||
           (op==="LOAD"&&hardware.raw?.ioDefaults?.canLoad!==true))return;
        const message=op==="SAVE"?"Use the current logical input and output port definitions for future empty mixers? Channel assignments, FX and output arm are not saved.":
          op==="LOAD"?"Load default port definitions into this empty muted mixer? No channel will be assigned or armed.":"Clear the saved I/O default? The current mixer will not change.";
        if(!window.confirm(message))return;
        const requestId="io-default-"+(++ioDefaultsSerial);
        ioDefaultsPending={requestId,op,epoch:next.device.epoch,config:next.hardwareConfiguration,
          revision:data.revision,inputRevision:ins.revision,outputRevision:outs.revision,
          inputs:ins.ports.length,outputs:outs.ports.length,ack:null};
        backend.emitEvent("arMixAction",{action:"audioIoDefaults",requestId,operation:op,confirmed:true,
          deviceEpoch:next.device.epoch,graphRevision:model.revision,rosterRevision:model.roster,
          revision:data.revision,inputRevision:ins.revision,outputRevision:outs.revision});
        paintPhysicalOutputs(hardware);
        window.setTimeout(()=>{
          if(ioDefaultsPending?.requestId!==requestId)return;
          ioDefaultsPending=null;paintPhysicalOutputs(hardware);controller?.subscribe();
          nativeToast("I/O default confirmation timed out · verify current state");
        },8000);
      };
      section.append(button);
    }
    host.append(section);
  }
  function paintRouteMatrix(next,side){
    const host=devicePanel?.querySelector("[data-audio-"+side+"-grid]");
    if(!host)return;
    const model=deviceVM.patchMatrix(next,side);
    const ready=matrixReady(next),busy=matrixBusy()||Boolean(namedPatchPending);
    const signature=JSON.stringify([model,busy,ready,next?.hardwareConfiguration,next?.device?.epoch,
      deviceInventory?.namedPatches,namedPatchPending,side==="input"?next?.raw?.logicalInputRegistry:next?.raw?.logicalOutputRegistry,
      next?.raw?.operatorOutputMuted,next?.device?.outputArmed,next?.raw?.physicalBusSecondaryArmed,
      next?.raw?.ioDefaults,next?.raw?.logicalInputRegistry?.revision,next?.raw?.logicalOutputRegistry?.revision]);
    const lease={config:next?.hardwareConfiguration,epoch:next?.device?.epoch,authority:model.authority,
      revision:model.revision,roster:model.roster,
      busPairs:Object.fromEntries(model.rows.filter(r=>r.bus).map(r=>[r.id,JSON.stringify(r.pair)]))};
    if(host.dataset.signature===signature)return;
    host.dataset.signature=signature;
    host.replaceChildren();
    if(!ready||!model.live||!model.pins.length){
      host.textContent=!ready?"Apply device and pin changes first.":
        "Open a verified mixer with enabled "+side+" pins.";
      return;
    }
    if(side==="input")renderLogicalInputs(host,model,next,busy);
    else renderLogicalOutputs(host,model,next,busy);
    renderIoDefaults(host,model,next,busy);
    renderNamedPatchEditor(host,side,model,next,busy);
    const aliases=namedPatchChoices(side,model,next);
    const table=document.createElement("table");table.className="armix-patch-grid";
    const head=table.createTHead().insertRow();
    const th=label=>{const e=document.createElement("th");e.textContent=label;head.append(e);};
    th(side==="input"?"Mixer input":"Mixer output");
    for(const pin of model.pins)th((pin.port+1)+" · "+pin.label);
    th("Assignment");th(busy?"Pending":"");
    const body=table.createTBody();
    for(const row of model.rows){
      const tr=body.insertRow();const name=tr.insertCell();
      name.textContent=row.name;name.title=row.state||row.name;
      const controls=[];
      const select=document.createElement("select");
      select.setAttribute("aria-label",row.name+" assignment");
      const candidates=row.bus?row.choices:model.choices;
      // An installed logical pair may be excluded from new assignments.
      // Read-only rows must retain their installed crosspoints, not a draft.
      const customValue="__custom_stereo__";
      const custom=document.createElement("div");
      custom.className="armix-custom-stereo";custom.hidden=true;
      const customSelect=sideName=>{
        const control=document.createElement("select");
        control.setAttribute("aria-label",row.name+" "+sideName+" pin");
        for(const p of model.pins)control.append(new Option(sideName+" · "+(p.port+1)+" "+p.label,String(p.port)));
        control.disabled=busy;custom.append(control);return control;
      };
      const customLeft=customSelect("L"),customRight=customSelect("R");
      if(model.pins.length>1)customRight.selectedIndex=1;
      if(row.selected===null){const op=new Option(row.state||"Current source", "");op.disabled=true;select.append(op);}
      else if(row.selected&&!candidates.some(c=>c.label===row.selected)){
        const op=new Option(row.selected,row.selected);op.disabled=true;select.append(op);
      }
      for(const choice of candidates)select.append(new Option(choice.label,choice.label));
      if(row.editable&&!row.bus)for(const p of aliases)
        select.append(new Option(p.name+" · "+p.choice.label,"preset:"+p.id));
      if(side==="input"&&row.editable&&model.pins.length>1)
        select.append(new Option("Custom stereo L/R…",customValue));
      select.value=row.selected??"";
      const selectedChoice=()=>select.value===customValue?
        deviceVM.physicalInputChoice(next,"STEREO",Number(customLeft.value),Number(customRight.value)):
        select.value.startsWith("preset:")?aliases.find(p=>"preset:"+p.id===select.value)?.choice:
        candidates.find(x=>x.label===select.value);
      const apply=document.createElement("button");apply.type="button";apply.textContent="Apply";
      const update=()=>{
        const choice=selectedChoice();
        custom.hidden=select.value!==customValue;
        apply.disabled=busy||!row.editable||!choice||choice.label===row.selected;
        controls.forEach(({button,pin,index})=>{
          const mark=row.editable&&choice?(pin.port===choice.left?(choice.mode==="MONO"?"M":"L"):pin.port===choice.right?"R":""):row.cells[index];
          button.textContent=mark||"·";
          button.dataset.mark=mark||"";
          button.dataset.draft=String(Boolean(row.pending&&mark||row.editable&&mark!==row.cells[index]));
        });
      };
      for(const [index,pin] of model.pins.entries()){
        const cell=tr.insertCell(),button=document.createElement("button");
        button.type="button";button.className="armix-patch-cell";
        button.setAttribute("aria-label",row.name+" / "+pin.label);
        button.disabled=busy||!row.editable;
        button.onclick=()=>{
          if(select.value===customValue){
            const pinValue=String(pin.port);
            if(customLeft.value===pinValue)return;
            customRight.value=pinValue;update();return;
          }
          const current=candidates.find(x=>x.label===select.value);
          const stereo=side==="output"||current?.mode==="STEREO"||
            (current?.mode==="NONE"&&row.format==="STEREO");
          const choice=candidates.find(x=>(side==="output"||x.mode===(stereo?"STEREO":"MONO"))&&
            (x.left===pin.port||(stereo&&x.right===pin.port)));
          if(choice){select.value=choice.label;update();}
        };
        cell.append(button);controls.push({button,pin,index});
      }
      const pick=tr.insertCell(),action=tr.insertCell();
      if(row.editable){
        select.disabled=busy;select.onchange=update;pick.append(select);
        if(side==="input"){
          customLeft.onchange=customRight.onchange=update;
          pick.append(custom);
        }
        apply.onclick=()=>{
          const choice=selectedChoice();
          if(choice)applyMatrixRow(side,row.id,choice.label,lease,choice);
        };
        action.append(apply);
      }else pick.textContent=row.state||"Read-only";
      update();
    }
    host.append(table);
  }
  function paintPhysicalOutputs(next) {
    if(devicePanel&&!devicePanel.hidden){
      paintMonitorTiming(next);
      const page=devicePanel.querySelector(".armix-audio-dialog")?.dataset.audioTab;
      if(page==="inputs")paintRouteMatrix(next,"input");
      if(page==="routing")paintRouteMatrix(next,"output");
    }
    paintOutputOverview(next);
  }
  function paintOutputOverview(next) {
    const view=devicePanel?.querySelector?.("[data-audio-output-matrix]");
    if(!view || devicePanel.hidden||
       devicePanel.querySelector(".armix-audio-dialog")?.dataset.audioTab!=="routing")return;
    const patches=protocol.physicalOutputOverview(next).filter(row=>row.pair!=="STREAM");
    const stream=protocol.secondaryOutputRow(next,extraLive);
    if(stream)patches.push(stream);
    const signature=JSON.stringify([patches,String(next?.device?.backend||"")]);
    if(devicePanel.dataset.outputSignature===signature)return;
    devicePanel.dataset.outputSignature=signature;
    const summary=devicePanel.querySelector("[data-audio-output-overview] .armix-output-heading");
    if(summary)summary.textContent="Destinations · "+patches.length;
    view.replaceChildren();
    if(!patches.length){
      const backendName=String(next?.device?.backend||"");
      view.textContent=/ASIO/i.test(backendName)?
        "No assignable ASIO pins. Verify the interface output-pair mapping in Audio Device.":
        "Windows Audio uses the selected stereo playback endpoint. Per-pin output routing is available with a verified multichannel ASIO interface.";
      return;
    }
    for(const patch of patches){
      const line=document.createElement("div");
      line.className="armix-output-row";line.setAttribute("role","listitem");
      const dest=document.createElement("span");
      dest.className="armix-output-pair";dest.textContent=patch.pair;
      const owner=document.createElement("span");
      owner.className="armix-output-owner";
      owner.textContent=patch.role+" · "+patch.pins+" · "+patch.owners;
      const state=document.createElement("span");
      state.className="armix-output-status";
      state.dataset.state=patch.status;
      state.textContent=patch.status.replace("_"," ")+
        (patch.shared>1?" · SHARED "+patch.shared:"");
      line.append(dest,owner,state);
      view.appendChild(line);
    }
  }
  let lastFooterHealth=null;
  // UX-1 visual: create exactly three text-only nodes ONCE for the native
  // footer (not cards, not per-frame markup, no animation). CSS owns icons.
  // A mock DOM without replaceChildren uses the original plain-text path.
  let footerMetricNodes=null;
  function paintFooterMetrics(warning,health) {
    if(!warning || !health)return;
    const entries=[
      ["cpu",health.metrics.cpu],
      ["rt",health.metrics.rt],
      ["ram",health.metrics.ram]
    ];
    if(typeof document.createElement!=="function" ||
       typeof warning.replaceChildren!=="function") {
      if(warning.textContent!==health.performance)
        warning.textContent=health.performance;
      return;
    }
    if(footerMetricNodes?.host!==warning ||
       warning.firstElementChild!==footerMetricNodes.cpu) {
      const nodes={host:warning};
      for(const [key] of entries){
        const item=document.createElement("span");
        item.className="armix-health-stat";
        item.setAttribute("data-metric",key);
        // Text stays accessible; the vector icon is CSS decoration only.
        nodes[key]=item;
      }
      warning.replaceChildren(nodes.cpu,nodes.rt,nodes.ram);
      footerMetricNodes=nodes;
    }
    const help={
      cpu:"ArSonMix process CPU across all logical cores, not whole-system CPU.",
      rt:"Last callback duration versus device period. Not average load or a driver-confirmed XRUN.",
      ram:"ArSonMix process working set, not dedicated audio buffers or total system RAM."
    };
    for(const [key,metric] of entries){
      const node=footerMetricNodes[key];
      if(node.textContent!==metric.label)node.textContent=metric.label;
      if(node.dataset && node.dataset.tone!==metric.tone)
        node.dataset.tone=metric.tone;
      if(node.title!==help[key])node.title=help[key];
    }
  }

  function paintAudioHealthFooter(next) {
    const d=next?.device;
    const deviceOpen=Boolean(d?.backend && d.sampleRate>0 &&
      d.enabledOutputs?.length>=2);
    const playback=String(d?.playbackDevice||"unknown output");
    const virtualOut=/VB-Audio Virtual Cable|CABLE Input/i.test(playback);
    const transport=next?.retained?"Output disconnected · Open Audio I/O":
      !deviceOpen?"Audio device unavailable · Open Audio I/O":
      (d.outputArmed?"Output enabled":"Output muted")+" · "+playback+
      (virtualOut?" · Virtual output · No speakers":"");
    const healthSameEpoch=lastFooterHealth &&
      lastFooterHealth.routingEpoch===String(d?.epoch);
    // The status strip is LIVE only on the exact current hardware epoch.
    // Do not display stale latency after device disconnect or remap.
    const activeOutput=!next?.retained && deviceOpen &&
      d.outputArmed===true && next?.admitted===true;
    const label=document.getElementById("transport");
    // Keep the existing operator wording for muted/disconnected output;
    // show latency hints only for the accepted and armed physical route.
    const operatorTransport=operatorMessage(transport);
    const labelText=activeOutput?
      (healthSameEpoch?lastFooterHealth.latency:"LAT —"):
      operatorTransport;
    if(label && label.textContent!==labelText)label.textContent=labelText;
    const latencyMode=activeOutput?"true":"false";
    // Avoid redundant DOM attribute writes on the 12 Hz snapshot stream.
    // Each write could trigger style invalidation even with the same value.
    if(label?.dataset && label.dataset.telemetry!==latencyMode)
      label.dataset.telemetry=latencyMode;
    const latencyHint="JUCE driver-reported I/O latency + FX/PDC hint, not measured acoustic round-trip.";
    if(label && label.title!==(activeOutput?latencyHint:transport))
      label.title=activeOutput?latencyHint:transport;
    const warning=document.querySelector?.(".status-bar .warning");
    const warningText=next?.retained?"PLAYBACK UNAVAILABLE · MIXER SAVED":
      !deviceOpen?"NO VERIFIED AUDIO DEVICE":
      next?.raw?.waitingAsioInputs>0?"INPUT TEMPORARILY UNAVAILABLE · AUTO-RECONNECT":
      d.outputArmed?(next.admitted?"OUTPUT ENABLED":
        "CHECK AUDIO CONNECTION"):
      !d.enabledInputs?.length?(next.admitted?"NO ACTIVE CAPTURE PINS · AUDIO I/O":
        "CHECK AUDIO I/O SETTINGS"):
      next?.raw?.operatorOutputMuted===true?"EMERGENCY STOP · MORE → RESUME":
      protocol.firstSoundPlan(next)?"SELECT CHANNEL IN":
      legacy()?.routeAccepted||next?.admitted?"OUTPUT RECOVERING · CHECK AUDIO I/O":
      deviceReady()?"SELECT CHANNEL IN · NATIVE MONITOR AFTER ACK":
      "NO VERIFIED INPUT ROUTE · OPEN AUDIO I/O";
    // A warning (missing input / disconnected / muted) always takes
    // priority. Only the redundant healthy OUTPUT ENABLED becomes metrics.
    const telemetry=warningText==="OUTPUT ENABLED";
    const displayWarning=telemetry?
      (healthSameEpoch?lastFooterHealth.performance:"CPU App — · RT — · RAM —"):
      warningText;
    if(telemetry && healthSameEpoch && lastFooterHealth.metrics)
      paintFooterMetrics(warning,lastFooterHealth);
    else {
      if(warning && warning.textContent!==displayWarning)
        warning.textContent=displayWarning;
      // Next valid health must recreate its three static nodes, but every
      // ordinary 12 Hz readback and 1 Hz sample keeps existing nodes.
      if(footerMetricNodes?.host===warning)footerMetricNodes=null;
    }
    const healthMode=telemetry?"true":"false";
    if(warning?.dataset && warning.dataset.telemetry!==healthMode)
      warning.dataset.telemetry=healthMode;
  }
  // M4: presentation-only. Only the native 1 Hz status event calculates new
  // evidence; accepted graph snapshot changes merely invalidate stale display.
  function paintSmartPerformance(force=false) {
    const panel=devicePanel?.querySelector("[data-m4-performance]");
    if(!panel||devicePanel.hidden ||
       devicePanel.querySelector(".armix-audio-dialog")?.dataset.audioTab!=="performance")return;
    const p=lastM4Performance;
    const epoch=hardware?.device?.epoch||"";
    const valid=p?.cursor?.epoch===epoch&&
      p?.cursor?.graphRevision===hardware?.graph?.revision&&
      p?.cursor?.armed===(hardware?.device?.outputArmed===true)&&
      hardware?.admitted===true&&!hardware.retained;
    const key=(valid?p.cursor.epoch+":"+p.cursor.seq:"unavailable:"+epoch+":"+String(hardware?.device?.outputArmed));
    if(!force&&key===m4PaintKey)return;
    m4PaintKey=key;
    const put=(field,value)=>{
      const el=panel.querySelector("[data-m4-"+field+"]");
      if(el&&el.textContent!==value)el.textContent=value;
    };
    const status=valid?p.state:"UNAVAILABLE";
    const stateLabel={UNAVAILABLE:"No verified live evidence",
      COLLECTING:"Collecting 1-second evidence",PRESSURE:"Render budget pressure",
      TIGHT:"Limited render headroom",OBSERVED_MARGIN:"Measured headroom",
      MUTED:"Outputs disarmed"}[status]||"Evidence pending";
    const state=panel.querySelector("[data-m4-state]");
    if(state)state.dataset.state=status;
    put("state",stateLabel);
    put("budget",valid&&p.headroom?
      "P99 "+p.headroom.p99+" of deadline · margin "+p.headroom.margin:
      "P99 — · one-second evidence unavailable");
    put("over",valid&&p.callbacks!==null?
      p.over+" / "+p.callbacks+" host render blocks over budget":
      "Deadline events —");
    put("fx",valid&&p.fxPeak?p.fxPeak.name+" · historical max "+
      p.fxPeak.percent+" of period"+(p.fxPeak.sampled?" · sampled":""):
      "No attributable FX peak");
    put("latency",valid&&p.latency?
      "MAIN "+(p.latency.main||"—")+" · MONITOR "+(p.latency.monitor||"—")+
      " · "+p.latency.policy:"Graph latency —");
    put("correlation",valid&&p.correlation?
      "Last 64 pairs: "+p.correlation.withinLate+" late after in-budget · "+
        p.correlation.overLate+" late after over-budget":
      "Callback pairing not available");
    const rows=panel.querySelector("[data-m4-reviews]");
    const advice=panel.querySelector("[data-m4-advice]");
    const addRows=(host,items)=>{
      if(!host)return;
      // Stable text rows need no DOM allocation or layout invalidation.
      if(host.children.length===items.length&&items.every((v,i)=>host.children[i].textContent===v))return;
      host.replaceChildren(...items.map(value=>{
        const row=document.createElement("div");
        row.className="armix-m4-line";
        row.textContent=value; // Plugin names are data, never markup.
        return row;
      }));
    };
    addRows(rows,valid&&p.reviews.length?p.reviews.map(x=>
      x.name+" · "+(x.channel||"FX")+" · "+x.action+
      (x.window!=="—"?" · last 32 mean "+x.window:"")+
      (x.sampled?" · sampled":"")):["No FX requires review from available evidence"]);
    addRows(advice,valid?p.recommendations:
      ["Open a verified native audio device; no risk rating is inferred from demo or stale data."]);
  }

  function updateHardwarePanel(next) {
    const prev=hardware;
    hardware=next || null;
    if(prev?.device?.epoch!==hardware?.device?.epoch||
       prev?.device?.outputArmed!==hardware?.device?.outputArmed||
       prev?.graph?.revision!==hardware?.graph?.revision||
       prev?.admitted!==hardware?.admitted||prev?.retained!==hardware?.retained)
      lastM4Performance=null;
    refreshRealM4Inputs(next);
    const d=next?.device;
    const deviceOpen=Boolean(d?.backend && d.sampleRate>0 &&
      d.enabledOutputs.length>=2);
    syncEmergencyOutputControl(d);
    paintPhysicalOutputs(next);
    // Keep the SAME verified playback device for mixer master projection.
    // Footer display is independent; device identity must never be dropped.
    const playback=String(d?.playbackDevice||"unknown output");
    const mainOutput=next?.retained?NEEDS_REMAP:next?.admitted&&next.raw?.secondaryMainOnly===true?
      (next.raw.secondaryMainReady===true&&next.raw.secondaryMainEndpoint?
        String(next.raw.secondaryMainEndpoint):"STREAM DISCONNECTED"):deviceOpen?playback:null;
    if(scope?.master && mainOutput && scope.master.output!==mainOutput){
      scope.master.output=mainOutput;
      if(!next?.admitted && !next?.retained)scope.render?.();
    }
    paintAudioHealthFooter(next);
    paintSmartPerformance();
    if(!inputSelect)return;
    const pins=deviceOpen?d.inputEndpoints.filter(p=>
      p.active===true && d.enabledInputs.includes(p.port)):[];
    const key=pins.map(p=>p.id+"|"+p.label).join(";");
    if(inputSelect.dataset.pins!==key) {
      const before=inputSelect.value;
      inputSelect.dataset.pins=key;
      inputSelect.replaceChildren();
      const option=document.createElement("option");
      option.value="";option.textContent="Physical input for CH1";
      inputSelect.appendChild(option);
      for(const pin of pins){
        const item=document.createElement("option");
        item.value=String(pin.port);
        item.textContent=pin.label+" · IN "+(pin.port+1);
        inputSelect.appendChild(item);
      }
      if(pins.some(p=>String(p.port)===before))inputSelect.value=before;
    }
    const ready=deviceReady();
    const selectDisabled=!ready;
    if(inputSelect.disabled!==selectDisabled)inputSelect.disabled=selectDisabled;
    const routeDisabled=!ready || inputSelect.value==="" || pendingSourceId!==0;
    if(routeBtn && routeBtn.disabled!==routeDisabled)routeBtn.disabled=routeDisabled;
    const armDisabled=!ready || legacy()?.routeAccepted!==true || d.outputArmed;
    if(armBtn && armBtn.disabled!==armDisabled)armBtn.disabled=armDisabled;
    syncEmergencyOutputControl(d);
    if(hardwareNote) {
      const evidence=protocol.diagnoseFirstSound?.(next,inputSelect.value);
      // Human-readable PCM stages from the ORIGINAL native callback only.
      // These values are NOT evidence that the DAC or speaker is audible.
      const note=evidence?
        evidence.readout+" · "+evidence.nextAction:
        "No verified hardware PCM readback available.";
      if(hardwareNote.textContent!==note)hardwareNote.textContent=note;
    }
  }
  // Compact DAW-style device preferences INSIDE the approved Windows GUI.
  // Only the existing AudioDeviceManager inventory/APPLY API is used. This
  // replaces the 740x590 default JUCE dialog for normal operator setup.
  let devicePanel=null,deviceInventory=null,deviceChoice=null,deviceRequest=0,
      deviceState=deviceVM.setupInitial(),deviceApplyTimer=0;
  // Snapshot the actual initiating action; never infer it from a later tab.
  let closeAfterVerifiedPinApply=false;
  const deviceBusy=()=>Boolean(deviceState.pending);
  let toastTimer=0;
  function nativeToast(message){
    let element=document.getElementById("armixNativeToast");
    if(!element){
      element=document.createElement("div");
      element.id="armixNativeToast";
      element.setAttribute("role","status");
      element.style.cssText="position:fixed;right:16px;bottom:36px;z-index:99999;"+
        "max-width:min(390px,88vw);padding:9px 13px;border:1px solid #426f7e;"+
        "border-radius:6px;background:#123240;color:#dff8fd;"+
        "font:12px system-ui,sans-serif;box-shadow:0 9px 28px #0008";
      document.body.appendChild(element);
    }
    element.textContent=operatorMessage(message);element.hidden=false;
    window.clearTimeout(toastTimer);
    toastTimer=window.setTimeout(()=>{element.hidden=true;},3400);
  }
  let lastAudioRepairAt=-Infinity;
  function recommendAudioRepair(context) {
    // The user explicitly attempted a live input or FX operation. Avoid
    // disruptive startup popups, silent driver fallback or repeated overlays
    // from the 12 Hz status/readback loop.
    if(deviceBusy()||deviceReady())return false;
    const now=performance.now();
    if(now-lastAudioRepairAt<1600)return true;
    lastAudioRepairAt=now;
    nativeToast(context+" · Check Audio I/O");
    status("AUDIO NOT READY · choose and Apply the correct driver and endpoint");
    window.setTimeout(()=>{
      if(!deviceReady()&&!deviceBusy()&&(!devicePanel||devicePanel.hidden))
        openNativeAudioIO(); // explicit repair prompt, no automatic Apply
    },220);
    return true;
  }
  // Disabled physical inputs cannot emit a change event. Their route field
  // still offers explicit device repair, without opening a modal on startup.
  document.addEventListener("pointerdown",event=>{
    const field=event.target.closest?.(".route-field");
    if(field?.querySelector('select[data-field="input"]')&&!deviceReady())
      recommendAudioRepair("INPUT requires an audio device");
  },true);
  let loopbackSerial=0,loopbackId="",loopbackPending=false;
  const loopbackField=name=>devicePanel?.querySelector("[data-audio-loopback-"+name+"]");
  // U1: approved native bundle owns this workflow. monitor.js belongs to
  // the retired console and is deliberately NOT shipped in this bundle.
  let extraPanel=null,extraControls=null,extraLive=null,extraInventory=[],extraPending=null,extraSerial=0,
      extraError="",extraInventoryKnown=false,extraInventoryPending="",extraPreferred="",extraHasSaved=false,
      extraSelected="",extraInventoryRetryAt=0,extraInventoryId="",extraStatusSerial=0;
  const extraField=name=>extraControls?.querySelector("[data-extra-"+name+"]");
  const extraInlineHost=()=>devicePanel?.querySelector("[data-audio-extra-output]");
  const extraInlineVisible=()=>!devicePanel?.hidden&&
    devicePanel?.querySelector(".armix-audio-dialog")?.dataset.audioTab==="routing";
  const extraSurfaceOpen=()=>Boolean(extraPanel?.open||extraInlineVisible());
  function refreshAdditionalInventory(force=false){
    if(extraInventoryPending)return;
    const requestId="extra-inventory-"+(++extraSerial);
    extraInventoryPending=requestId;extraInventoryKnown=false;extraError="";
    paintAdditionalOutput();
    backend.emitEvent("arMixAction",{action:"secondaryOutputInventory",requestId,force:force===true});
    window.setTimeout(()=>{
      if(extraInventoryPending!==requestId)return;
      extraInventoryPending="";extraInventoryKnown=false;
      extraInventoryRetryAt=performance.now()+5000;
      extraError="Windows playback devices unavailable. Retrying automatically…";
      paintAdditionalOutput();
    },8000);
  }
  function settleAdditionalOutput(live){
    const p=extraPending;
    const result=protocol.secondaryOutputSettlement(p,hardware,live,extraStatusSerial);
    if(result==="SETTLED"){extraPending=null;extraError="";}
    else if(result==="STALE"){
      extraPending=null;extraError="Audio configuration changed · review the current output before retrying.";
    }else if(p?.acked&&extraStatusSerial>p.statusSerial&&
       live?.secondaryOutputFault===true&&live.secondaryOutputRecovering!==true&&
       protocol.secondaryOutputView(hardware,live).fault&&
       p.action!=="secondaryOutputSplit"&&p.action!=="secondaryOutputStop"){
      extraPending=null;extraError="Output fault · disconnect and reconnect the endpoint.";
    }
  }
  // Destination selection is UI navigation only (TotalMix submix / Yamaha SOF).
  // Use the installed MONITOR ChannelId and the existing SOF selector, never
  // synthesize a send, choose a similarly named BUS, or modify MAIN faders.
  function headphoneMixTarget(){
    const current=controller?.state()?.current;
    if(!flags.acceptedGraphConnected||!deviceReady()||
       !protocol.secondaryOutputView(hardware,extraLive).split||
       !wireVM.sofAvailable(current,!!scope?.toggleSof))return null;
    const monitors=current.graph.channels.filter(ch=>ch.role==="MONITOR");
    if(monitors.length!==1)return null;
    const tracks=scope?.tracks?.filter(t=>t.nativeRole==="MONITOR"&&nativeIdFor(t)===monitors[0].id)||[];
    const target=document.getElementById("sofTarget"),toggle=document.getElementById("sofToggle");
    if(tracks.length!==1||!target||!toggle||toggle.disabled||
       !Array.from(target.options).some(o=>o.value===String(tracks[0].id)))return null;
    return {id:tracks[0].id,target,toggle};
  }
  function openHeadphoneMix(){
    if(extraPending||deviceBusy())return;
    const mix=headphoneMixTarget();
    if(!mix){nativeToast("Headphone mix unavailable · verify the current MONITOR route");return;}
    if(extraPanel?.open)extraPanel.close();
    else if(extraInlineVisible())closeNativeAudioIO();
    if(scope.sofTargetId!==mix.id){
      mix.target.value=String(mix.id);
      mix.target.dispatchEvent(new Event("change",{bubbles:true}));
    }
    if(scope.sofTargetId===mix.id&&!scope.sofEnabled)scope.toggleSof(true);
    mix.toggle.focus();
  }
  // MAIN MON is a presentation of the existing native secondary owner.
  const mainMonitorAllowed=()=>extraLive?.directRouteAuthorized===true&&
    extraLive?.adaptiveClockActive!==true&&extraLive?.dryRoutingLab?.hasAcceptedRoute===true&&
    extraLive?.diagnosticRoute==="NORMAL";
  let extraAutoInventoryStarted=false;
  function paintMainMonitor(){
    const select=document.querySelector("[data-main-monitor]");
    if(!select)return;
    const v=protocol.secondaryOutputView(hardware,extraLive);
    const active=v.armed||v.recovering;
    const primary=String(hardware?.device?.playbackDevice||"").toLowerCase();
    const names=extraInventory.filter(n=>n.toLowerCase()!==primary);
    const opts=[["","None"+(extraPending?" · applying":v.fault?" · fault":v.ready&&!active?" · muted":"")]];
    if(active&&v.name)opts.push([v.name,v.name+(v.recovering?" · reconnecting":"")]);
    else if(!v.split&&mainMonitorAllowed()&&extraInventoryKnown&&!extraInventoryPending)
      for(const name of names)opts.push([name,name]);
    if(!active&&!v.split)opts.push(["__armix_refresh__","Refresh devices…"]);
    if(!active&&!v.split&&(v.fault||v.ready||v.name))
      opts.push(["__armix_stop__","Disconnect"]);
    if(v.split)opts.push(["__armix_split__","Separate mix"]);
    opts.push(["__armix_settings__","MON settings…"]);
    const value=v.split?"__armix_split__":active&&v.name?v.name:"";
    const sig=JSON.stringify(opts);
    if(select.dataset.signature!==sig){
      select.replaceChildren(...opts.map(([value,label])=>new Option(label,value)));
      select.dataset.signature=sig;
    }
    if(select.value!==value)select.value=value;
    // Settings remains reachable in Separate mix and before a device is ready.
    // Actual routing still passes the existing native admission checks below.
    select.disabled=Boolean(extraPending)||deviceBusy();
    select.dataset.status=extraPending?"PENDING":v.state;
    const title=extraError||(v.split?"Separate mix · choose MON settings to configure.":
      v.fault?"Secondary output fault · select endpoint to retry or Disconnect.":
      active?"Secondary output "+v.state+" · choose None before switching devices.":
      !mainMonitorAllowed()?"Configure the primary audio device and mixer route first.":
      "Choose a compatible Windows playback endpoint to mirror MAIN. Primary OUT is unchanged.");
    if(select.title!==title)select.title=title;
  }
  document.addEventListener("change",event=>{
    const select=event.target;
    if(!select?.matches?.("[data-main-monitor]"))return;
    const value=select.value;
    const v=protocol.secondaryOutputView(hardware,extraLive);
    paintMainMonitor();
    if(extraPending||deviceBusy())return;
    if(value==="__armix_settings__"){openAdditionalOutput();return;}
    if(v.split)return;
    if(value==="__armix_refresh__"){refreshAdditionalInventory(true);return;}
    if(value===""||value==="__armix_stop__"){
      if(v.ready||v.fault||v.recovering||v.name)
        requestAdditionalOutput("secondaryOutputStop",null,true);
      return;
    }
    if(!mainMonitorAllowed()||v.armed||v.recovering||extraInventoryPending||!extraInventoryKnown||
       !extraInventoryId||!extraInventory.includes(value)||
       value.toLowerCase()===String(hardware?.device?.playbackDevice||"").toLowerCase())return;
    extraSelected=value;
    requestAdditionalOutput("secondaryOutputEnable",value,true);
  });
  function paintAdditionalOutput(){
    paintMainMonitor();
    if(!extraControls||!extraSurfaceOpen())return;
    const live=extraLive, busy=Boolean(extraPending)||deviceBusy(),
      destination=protocol.secondaryOutputView(hardware,live),
      {ready,armed,fault,name,split,recovering}=destination;
    const select=extraField("device");
    const options=extraInventory;
    const sig=JSON.stringify(options);
    if(select.dataset.signature!==sig){
      select.replaceChildren(new Option("Select Windows playback device…",""),
        ...options.map(name=>new Option(name,name)));
      select.dataset.signature=sig;
    }
    const chosen=protocol.secondaryOutputSelection(options,{active:armed||recovering?name:"",
      selected:extraSelected,preferred:extraPreferred,hasSaved:extraHasSaved});
    if(select.value!==chosen)select.value=chosen;
    const allowed=live?.directRouteAuthorized===true&&live?.adaptiveClockActive!==true&&
      live?.dryRoutingLab?.hasAcceptedRoute===true&&live?.diagnosticRoute==="NORMAL";
    select.disabled=busy||armed||recovering||extraInventoryPending;
    extraField("refresh").disabled=busy||extraInventoryPending;
    extraField("connect").disabled=busy||!allowed||armed||recovering||!extraInventoryKnown||!options.includes(select.value);
    extraField("stop").disabled=extraPending?.action==="secondaryOutputStop"||
      (!ready&&!fault&&!name&&!recovering&&extraPending?.action!=="secondaryOutputEnable");
    extraField("repair").hidden=!live||allowed||busy||armed||recovering;
    const mode=split?"Separate headphone mix":"Same mix";
    if(extraField("mode").textContent!==mode)extraField("mode").textContent=mode;
    const mix=protocol.secondaryMixMode(hardware,live);
    const modeButton=extraField("mix");
    modeButton.hidden=!mix.available&&!split;
    modeButton.disabled=busy||!allowed||!mix.available;
    const modeLabel=split?"Use same mix":"Separate headphone mix";
    if(modeButton.textContent!==modeLabel)modeButton.textContent=modeLabel;
    const starter=protocol.secondaryMonitorStarter(hardware,live);
    const starterButton=extraField("starter");
    starterButton.hidden=!starter;
    starterButton.disabled=busy||!allowed||!starter;
    const editButton=extraField("edit-mix");
    const edit=split&&!starter&&headphoneMixTarget();
    editButton.hidden=!edit;
    editButton.disabled=busy||!edit;
    const mixHint=extraField("mix-hint");
    mixHint.hidden=modeButton.hidden&&starterButton.hidden&&editButton.hidden&&!armed;
    const hint=starter?"Headphones have no sends. Create a quiet PRE-fader mix, or edit existing routing in the mixer.":
      split?mix.reason+(edit?" · Edit headphone mix to adjust sends. PRE sends ignore the source channel fader.":""):
      mix.available?"Primary and secondary playback receive the same mix. "+mix.reason:mix.reason;
    if(mixHint.textContent!==hint)mixHint.textContent=hint;
    const surfaceState=extraPending?"PENDING":destination.state;
    if(extraControls.dataset.state!==surfaceState)extraControls.dataset.state=surfaceState;
    const message=extraPending?(extraPending.action==="secondaryOutputSplit"?
      (extraPending.acked?"Restoring audio in the selected mix…":"Changing headphone mix…"):
      (extraPending.acked?"Verifying output…":"Applying output…")):
      busy?"Audio device is applying…":!live?"Reading audio engine status…":
      recovering?"Reconnecting to "+name+" automatically · Disconnect to stop":
      fault?"Output disconnected unexpectedly. Enable the selected device again or disconnect.":
      armed?"Enabled · "+name+" · check audio in receiving app":
      ready?"Connected · "+name+" · Enable to send MAIN":
      !allowed?"Audio device or mixer routing is not ready. Check Audio I/O.":
      extraInventoryPending?"Reading Windows playback devices…":
      !extraInventoryKnown?"Waiting for Windows playback devices…":
      !options.length?"No compatible Windows playback endpoint found. Connect or enable a playback device.":
      extraHasSaved&&!extraPreferred&&!select.value?"Saved output unavailable · choose a destination.":
      "Choose a destination, then Enable output.";
    const status=extraField("status");
    if(status.textContent!==message)status.textContent=message;
    const error=extraField("error");
    error.hidden=!extraError;
    if(error.textContent!==extraError)error.textContent=extraError;
  }
  function requestAdditionalOutput(action,value,quick=false){
    if((!extraSurfaceOpen()&&!(quick&&document.querySelector("[data-main-monitor]")))||(action!=="secondaryOutputStop"&&(extraPending||deviceBusy()))||
       extraPending?.action==="secondaryOutputStop")return;
    const requestId="extra-out-"+(++extraSerial);
    extraError="";
    const before=protocol.secondaryOutputView(hardware,extraLive);
    extraPending={requestId,action,value,quick,acked:false,statusSerial:extraStatusSerial,
      epoch:hardware?.device?.epoch,config:hardware?.hardwareConfiguration,endpoint:before.name,
      workspaceGeneration:hardware?.raw?.workspaceGeneration,
      pair:(before.split?hardware?.raw?.physicalMonitorOutputs:hardware?.raw?.physicalMainOutputs)?.slice()};
    paintAdditionalOutput();
    backend.emitEvent("arMixAction",{action,value,requestId,inventoryId:extraInventoryId});
    window.setTimeout(()=>{
      if(extraPending?.requestId!==requestId)return;
      settleAdditionalOutput(extraLive);
      if(extraPending?.requestId!==requestId){paintAdditionalOutput();return;}
      extraPending=null;
      extraError="Output response timed out · inspect current device state before retrying.";
      paintAdditionalOutput();
    },action==="secondaryOutputSplit"?30000:15000);
  }
  function ensureAdditionalControls(){
    if(extraControls)return;
    const style=document.createElement("style");
    style.textContent=`
      .armix-stream-dialog{width:min(520px,92vw);max-height:90vh;overflow:auto;box-sizing:border-box;background:#1a252d;color:#e3eff4;border:1px solid #506778;border-radius:8px;padding:0;font:12px system-ui,sans-serif;box-shadow:0 16px 56px #0009}
      .armix-stream-dialog::backdrop{background:#020b1290}
      .armix-stream-dialog [data-extra-close]{float:right;margin:10px 12px 0 0}
      .armix-stream-controls{padding:12px 14px;border:1px solid #3c5765;border-radius:5px;background:#10232e;display:grid;gap:7px;font:12px system-ui,sans-serif;color:#dcebf2}
      .armix-stream-dialog .armix-stream-controls{border:0;padding:14px}
      .armix-stream-heading{display:flex;align-items:baseline;justify-content:space-between;gap:8px;flex-wrap:wrap}
      .armix-stream-heading strong{font-size:12px;font-weight:550}
      .armix-stream-mode{font-size:11px;color:#9cb9c8}
      .armix-stream-actions{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
      .armix-stream-controls label{display:grid;gap:4px;color:#a9c5d2}
      .armix-stream-controls button,.armix-stream-controls select,.armix-stream-dialog [data-extra-close]{background:#102631;color:#e1eff6;border:1px solid #527889;border-radius:4px;min-height:30px;padding:4px 9px;font:inherit}
      .armix-stream-controls select{width:100%;min-width:0}
      .armix-stream-controls button[data-extra-connect]{background:#2c5268}
      .armix-stream-controls button:disabled,.armix-stream-controls select:disabled{opacity:.5}
      .armix-stream-controls [hidden]{display:none!important}
      .armix-stream-controls [data-extra-mix]{border-color:#638994}
      .armix-output-status[data-state="RECOVERING"]{color:#e5c17d}
      .armix-stream-controls [data-extra-status]{color:#b6c9d1;overflow-wrap:anywhere}
      .armix-stream-controls[data-state="ENABLED"] [data-extra-status]{color:#9bd3b8}
      .armix-stream-controls[data-state="FAULT"] [data-extra-status],.armix-stream-controls [data-extra-error]{color:#ffb7a5}
      .armix-stream-controls[data-state="PENDING"] [data-extra-status],.armix-stream-controls[data-state="RECOVERING"] [data-extra-status]{color:#e5c17d}
      .armix-stream-hint{font-size:11px;color:#9fb9c8;line-height:1.4}
      .armix-stream-inline{margin-bottom:12px}
      .armix-audio-dialog details[data-audio-hardware-routing]{margin:10px 0;padding:9px 0}
      .armix-output-status[data-state="FAULT"]{color:#ffb7a5}
      .armix-output-status[data-state="RESERVED"]{color:#e5c17d}
    `;
    document.head.appendChild(style);
    const panel=document.createElement("dialog");
    panel.className="armix-stream-dialog";panel.setAttribute("aria-label","Additional Output");
    const close=document.createElement("button");close.type="button";close.setAttribute("data-extra-close","");
    close.setAttribute("aria-label","Close Additional Output");close.textContent="×";close.onclick=()=>panel.close();
    panel.append(close);
    const content=document.createElement("section");content.className="armix-stream-controls";
    content.setAttribute("aria-label","Additional Output controls");
    content.innerHTML=`<div class="armix-stream-heading"><strong>Secondary playback output (MON)</strong><span class="armix-stream-mode" data-extra-mode></span></div>
      <label>Windows playback device<select data-extra-device aria-label="Windows playback device"></select></label>
      <div class="armix-stream-actions"><button type="button" data-extra-connect>Enable output</button><button type="button" data-extra-stop>Disconnect</button><button type="button" data-extra-refresh>Refresh</button><button type="button" data-extra-repair hidden>Set up audio</button></div>
      <div data-extra-status role="status" aria-live="polite"></div><div data-extra-error role="alert" hidden></div>
      <div class="armix-stream-actions"><button type="button" data-extra-mix hidden>Separate headphone mix</button><button type="button" data-extra-starter hidden>Create headphone mix</button><button type="button" data-extra-edit-mix hidden>Edit headphone mix</button><span class="armix-stream-hint" data-extra-mix-hint hidden></span></div>
      <div class="armix-stream-hint">Mirrors MAIN to a separate compatible Windows playback device. Primary OUT stays unchanged in Same mix mode. For virtual cables, select the matching recording endpoint in the receiving application.</div>`;
    panel.append(content);extraPanel=panel;extraControls=content;document.body.appendChild(panel);
    extraField("refresh").onclick=()=>refreshAdditionalInventory(true);
    extraField("repair").onclick=()=>{
      if(extraPanel?.open)extraPanel.close();
      openNativeAudioIO();
      selectAudioSetupPage("device");
    };
    extraField("device").onchange=()=>{extraSelected=extraField("device").value;paintAdditionalOutput();};
    extraField("connect").onclick=()=>requestAdditionalOutput("secondaryOutputEnable",extraField("device").value);
    extraField("stop").onclick=()=>requestAdditionalOutput("secondaryOutputStop",null);
    extraField("mix").onclick=()=>{
      const mode=protocol.secondaryMixMode(hardware,extraLive);
      if(mode.available)requestAdditionalOutput("secondaryOutputSplit",mode.value);
    };
    extraField("edit-mix").onclick=openHeadphoneMix;
    extraField("starter").onclick=()=>{
      const value=protocol.secondaryMonitorStarter(hardware,extraLive);
      if(value)requestAdditionalOutput("secondaryMonitorStarter",value);
    };
  }
  function mountInlineAdditionalOutput(){
    const host=extraInlineHost();
    if(!host)return;
    ensureAdditionalControls();
    if(extraPanel.open)extraPanel.close();
    host.append(extraControls);
    paintAdditionalOutput();
    refreshAdditionalInventory();
  }
  function openAdditionalOutput(){
    if(deviceBusy()){nativeToast("Finish applying the audio device first.");return;}
    if(extraInlineVisible()){
      mountInlineAdditionalOutput();extraField("device").focus();return;
    }
    ensureAdditionalControls();
    extraPanel.append(extraControls);
    if(!extraPanel.open)extraPanel.showModal();
    paintAdditionalOutput();
    refreshAdditionalInventory();
  }
  function closeNativeAudioIO(){
    if(deviceBusy())return;
    if(loopbackPending)backend.emitEvent("arMixAction",{action:"audioLoopbackCancel"});
    loopbackId="";
    loopbackPending=false;
    loopbackField("confirmed").checked=false;
    for(const side of ["input","output"])loopbackField(side).disabled=false;
    devicePanel.hidden=true;
    syncNamedOptions();
    for(const host of devicePanel.querySelectorAll("[data-audio-input-grid],[data-audio-output-grid]"))
      delete host.dataset.signature;
  }
  function requestLoopback(measure){
    const scope=deviceInventory?.active?.physicalLoopback;
    if(!scope||deviceBusy()||loopbackPending)return;
    const input=loopbackField("input"),output=loopbackField("output");
    const wired=loopbackField("confirmed");
    if(input.value===""||output.value===""||(measure&&!wired.checked))return;
    loopbackId="audio-loopback-"+(++loopbackSerial);
    loopbackPending=measure;
    const start=loopbackField("start");
    start.disabled=measure||!wired.checked;
    loopbackField("result").textContent=measure?"Starting wired measurement…":"";
    backend.emitEvent("arMixAction",{
      action:measure?"audioLoopbackMeasure":"audioLoopbackRead",
      requestId:loopbackId,deviceEpoch:scope.deviceEpoch,
      inputPin:Number(input.value),outputPin:Number(output.value),wiredLineLevel:wired.checked
    });
  }
  function renderLoopbackPins(active){
    const details=loopbackField("details");
    if(!details)return;
    const scope=active?.physicalLoopback;
    details.hidden=!scope;
    if(!scope)return;
    for(const [side,values,names] of [
      ["input",scope.inputs,active.inputNames],["output",scope.outputs,active.outputNames]]){
      const select=loopbackField(side),prior=select.value;
      select.replaceChildren();
      for(const pin of values){
        const option=document.createElement("option");
        option.value=String(pin);
        option.textContent=String(names?.[pin]||("Pin "+(pin+1)));
        select.append(option);
      }
      if(values.some(pin=>String(pin)===prior))select.value=prior;
    }
    loopbackField("start").disabled=deviceBusy()||loopbackPending||
      !loopbackField("confirmed").checked;
  }
  function selectAudioSetupPage(page){
    if(!devicePanel)return;
    const dialog=devicePanel.querySelector(".armix-audio-dialog");
    dialog.dataset.audioTab=page;
    for(const item of devicePanel.querySelectorAll("button[data-audio-tab]"))
      item.setAttribute("aria-selected",String(item.dataset.audioTab===page));
    const apply=devicePanel.querySelector("[data-audio-apply]");
    apply.hidden=page==="routing"||page==="inputs"||page==="diagnostics"||page==="performance";
    if(page==="routing")mountInlineAdditionalOutput();
    if(page==="inputs"||page==="routing")paintPhysicalOutputs(hardware);
    if(page==="performance")paintSmartPerformance(true);
    if(!deviceBusy())apply.textContent=page==="pins"?"Apply pins":"Apply device";
  }
  function openNativeAudioIO(){
    if(!devicePanel){
      const style=document.createElement("style");
      style.textContent=`
        .armix-audio-shade{position:fixed;inset:0;background:#020b12b8;z-index:99998;
          display:flex;align-items:center;justify-content:center}
        .armix-audio-shade[hidden]{display:none}
        .armix-audio-dialog{background:#142631;border:1px solid #467083;border-radius:8px;
          box-shadow:0 16px 56px #0009;color:#e3eff4;width:min(480px,94vw);
          padding:17px;display:grid;gap:11px;font:12px system-ui,sans-serif}
        .armix-audio-dialog h2{font-size:14px;font-weight:600;margin:0}
        .armix-audio-top{display:flex;align-items:center;justify-content:space-between;gap:10px}
        /* Floating close belongs to mixer-contextual-buttons.css. */
        .armix-audio-dialog label{display:grid;gap:5px;color:#a9c5d2}
        .armix-audio-dialog select{background:#09202c;border:1px solid #476778;
          border-radius:4px;color:#edf9ff;min-height:30px;width:100%;padding:3px 7px}
        .armix-audio-dialog button{background:#244254;border:1px solid #557b8d;
          border-radius:4px;color:#e9f8ff;min-height:30px;padding:4px 12px}
        .armix-audio-dialog button:disabled{opacity:.45}
        .armix-audio-dialog .armix-device-actions{display:flex;gap:8px;justify-content:flex-end}
        .armix-audio-dialog .armix-device-note{color:#a4c4d0;line-height:1.4}
        .armix-device-progress{display:flex;align-items:center;gap:12px;min-height:48px;
          padding:8px 12px;border:1px solid #9b7440;border-radius:6px;
          background:#30271b;color:#ffd17a}
        .armix-device-progress[hidden]{display:none}
        .armix-device-progress svg{width:26px;height:32px;flex:none}
        .armix-device-progress strong{display:block;font-size:12px;font-weight:600}
        .armix-device-progress span{display:block;margin-top:2px;color:#f2e0bf}
        .armix-audio-dialog details{border-top:1px solid #345666;padding-top:8px}
        .armix-audio-dialog summary{cursor:pointer;color:#c5e1ed}
        .armix-loopback-pins{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:8px 0}
        .armix-audio-dialog .armix-loopback-confirm{display:flex;align-items:flex-start;gap:6px;margin:8px 0}
        .armix-loopback-result{margin-top:6px}
        .armix-audio-dialog{max-height:90vh;overflow:auto}
        .armix-output-overview{border-top:1px solid #345666;padding-top:8px}
        .armix-output-heading{color:#c5e1ed;font-weight:500}
        .armix-output-matrix{max-height:175px;overflow:auto;display:grid;gap:3px;margin:7px 0}
        .armix-output-row{display:grid;grid-template-columns:64px minmax(0,1fr) auto;
          align-items:center;gap:8px;padding:5px 7px;border-radius:4px;background:#0c202b;
          font-size:11px;min-height:28px}
        .armix-output-pair{color:#b9dce8;font-variant-numeric:tabular-nums}
        .armix-output-owner{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
          color:#ccdce3}
        .armix-output-status{font-size:10px;color:#b6c9d1;white-space:nowrap}
        .armix-output-status[data-state="LIVE"],
        .armix-output-status[data-state="ENABLED"]{color:#9bd3b8}
        .armix-output-status[data-state="PENDING"]{color:#e5c17d}
        .armix-output-status[data-state="NEEDS_REMAP"]{color:#e0a18b}
        .armix-audio-pins{display:grid;grid-template-columns:1fr 1fr;gap:8px;
          max-height:135px;overflow:auto;padding:5px 0}
        .armix-audio-pins label{display:flex;align-items:center;gap:5px;font-size:11px}
        /* Audio setup uses one clearly separated Device / Output Routing
           workspace like a DAW preferences panel; no independent audio owner. */
        .armix-audio-dialog{width:min(672px,96vw);padding:0;gap:0;
          background:#1a252d;border-color:#506778;border-radius:8px}
        .armix-audio-top{padding:13px 18px;border-bottom:1px solid #344b59;
          background:#23323d;min-height:44px}
        .armix-audio-dialog h2{font-size:14px;font-weight:550}
        .armix-audio-tabs{display:flex;gap:2px;padding:0 14px;
          border-bottom:1px solid #405866;background:#1b303b}
        .armix-audio-dialog .armix-audio-tab{background:transparent;border:0;
          border-bottom:2px solid transparent;border-radius:0;color:#a6c1cc;
          min-height:35px;padding:7px 15px;font-size:12px;font-weight:500}
        .armix-audio-dialog .armix-audio-tab[aria-selected="true"]{
          background:#335b73;border-color:#79c3e6;color:#f2fcff}
        .armix-endpoint-fields{display:grid;gap:10px;padding:18px 20px 12px}
        .armix-endpoint-fields label{display:grid;align-items:center;
          grid-template-columns:minmax(110px,155px) minmax(0,1fr);
          gap:12px;color:#d0e0e6;min-height:30px}
        .armix-endpoint-fields select{min-width:0;min-height:33px}
        .armix-endpoint-fields .armix-device-note{padding:9px 12px;
          border:1px solid #3c5765;background:#10232e;border-radius:4px}
        .armix-audio-dialog [data-audio-latency]{font-variant-numeric:tabular-nums;
          color:#b8d5e2;line-height:1.55}
        .armix-audio-dialog details{margin:0 20px;padding:11px 0;
          border-top:1px solid #354b59}
        .armix-audio-dialog .armix-output-overview{border:0;padding:15px 20px}
        .armix-output-heading{font-size:12px;color:#d2e9ef;margin-bottom:7px}
        .armix-output-matrix{max-height:min(46vh,360px);gap:4px}
        .armix-output-row{grid-template-columns:92px minmax(0,1fr) auto;
          min-height:34px;padding:8px 10px}
        .armix-audio-dialog[data-audio-tab="inputs"],
        .armix-audio-dialog[data-audio-tab="routing"]{width:min(1050px,96vw)}
        .armix-input-overview{padding:15px 20px}
        .armix-crosspoint-scroll{overflow:auto;max-height:52vh}
        .armix-patch-grid{border-collapse:separate;border-spacing:2px;font-size:11px;width:max-content;min-width:100%}
        .armix-patch-grid th{font-weight:500;max-width:72px;min-width:42px;vertical-align:bottom;overflow-wrap:anywhere}
        .armix-patch-grid th:first-child,.armix-patch-grid td:first-child{position:sticky;left:0;z-index:1;background:#26343d;min-width:95px;max-width:150px;padding:5px}
        .armix-audio-dialog .armix-patch-cell{width:100%;min-width:32px;min-height:26px;padding:2px;border-radius:1px;background:#29343d;color:#8ba2af}
        .armix-audio-dialog .armix-patch-cell[data-mark]:not([data-mark=""]){background:#326b8a;color:#fff}
        .armix-audio-dialog .armix-patch-cell[data-draft="true"]{background:#90672e;color:#fff}
        .armix-patch-grid select{max-width:210px;font-size:11px}
        .armix-custom-stereo{display:grid;gap:3px;margin-top:4px}
        .armix-custom-stereo[hidden]{display:none!important}
        .armix-named-patches{display:flex;flex-wrap:wrap;align-items:center;gap:5px;padding:6px 0 12px}
        .armix-named-patches select,.armix-named-patches input{width:auto;max-width:180px;min-width:72px;font:11px system-ui;color:#e9f8ff;background:#09202c;border:1px solid #476778;border-radius:3px;padding:5px}
        .armix-named-patches .armix-device-note{flex-basis:100%}
        .armix-audio-dialog:not([data-audio-tab="inputs"]) .armix-input-overview,
        .armix-audio-dialog:not([data-audio-tab="routing"]) .armix-output-overview,
        .armix-audio-dialog:not([data-audio-tab="device"]) .armix-endpoint-fields,
        .armix-audio-dialog:not([data-audio-tab="diagnostics"]) .armix-audio-diagnostics,
        .armix-audio-dialog:not([data-audio-tab="performance"]) .armix-m4-performance,
        .armix-audio-dialog:not([data-audio-tab="pins"]) .armix-driver-inventory,
        .armix-audio-dialog:not([data-audio-tab="device"]) [data-audio-remap],
        .armix-audio-dialog [hidden]{display:none!important}
        .armix-audio-diagnostics{padding:12px 20px}
        .armix-m4-performance{padding:14px 20px 18px;font-size:11px;line-height:1.5}
        .armix-m4-head{display:flex;gap:12px;align-items:center;justify-content:space-between;
          border-bottom:1px solid #354c59;padding-bottom:10px;margin-bottom:10px}
        .armix-m4-head strong{font-size:13px;font-weight:550;color:#def2f8}
        .armix-m4-meta{font-size:10px;color:#9db6c3}
        .armix-m4-status{font-size:11px;color:#b7cad4}
        .armix-m4-status[data-state="PRESSURE"]{color:#f3b78d}
        .armix-m4-status[data-state="TIGHT"]{color:#ebd09b}
        .armix-m4-status[data-state="OBSERVED_MARGIN"]{color:#a9dcc7}
        .armix-m4-metrics{display:grid;grid-template-columns:1fr 1fr;gap:7px}
        .armix-m4-metric{padding:7px 10px;border:1px solid #34505e;border-radius:4px;
          min-width:0;background:#122630}
        .armix-m4-metric small{display:block;color:#9fb9c5;font-size:10px}
        .armix-m4-metric span{display:block;color:#e0eef3;font-variant-numeric:tabular-nums}
        .armix-m4-section{border-top:1px solid #344b59;margin-top:10px;padding-top:8px}
        .armix-m4-section strong{font-size:11px;font-weight:550;color:#c6e5ed}
        .armix-m4-line{margin-top:4px;padding-left:9px;border-left:2px solid #486978;
          color:#c6d8e0;overflow-wrap:anywhere}
        .armix-m4-note{color:#9fb6c1;font-size:10px;margin-top:10px}
        @media(max-width:620px){.armix-m4-metrics{grid-template-columns:1fr}}
        .armix-audio-dialog .armix-endpoint-fields .armix-device-note{border:0;background:transparent;padding:3px 0}
        .armix-audio-dialog [data-audio-latency]{padding:8px 0!important;border-top:1px solid #354b59!important}
        .armix-audio-dialog .armix-audio-pins{max-height:min(44vh,340px);gap:16px}
        .armix-audio-dialog .armix-audio-pins label{min-height:24px}
        .armix-audio-dialog [data-audio-tab="diagnostics"]{margin-left:auto}
        .armix-driver-inventory{padding:12px 20px;border-top:1px solid #354b59}
        .armix-endpoint-fields .armix-device-actions{padding:0;border:0;background:transparent}
        .armix-audio-dialog .armix-device-actions{padding:12px 20px;
          border-top:1px solid #344b59;background:#192a33}
        .armix-audio-dialog .armix-device-actions button[data-audio-apply]{
          background:#326881;border-color:#669ab2;min-width:112px}
        .armix-audio-dialog .armix-device-progress{margin:10px 20px}
        @media(max-width:620px){
          .armix-endpoint-fields{padding:14px}
          .armix-endpoint-fields label{grid-template-columns:1fr;gap:4px}
          .armix-audio-dialog .armix-output-overview{padding:12px}
          .armix-output-row{grid-template-columns:75px minmax(0,1fr)}
          .armix-output-status{grid-column:2}
        }
      `;
      document.head.appendChild(style);
      devicePanel=document.createElement("div");
      devicePanel.className="armix-audio-shade";
      devicePanel.innerHTML=`<section class="armix-audio-dialog" role="dialog"
        aria-modal="true" aria-label="Audio hardware setup" data-audio-tab="device">
        <header class="armix-audio-top"><h2>Audio I/O Setup</h2><button class="armix-floating-close" type="button"
          data-audio-close aria-label="Close Audio I/O" title="Close"><svg xmlns="http://www.w3.org/2000/svg"
          viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path
          d="M6.4,19L5,17.6L10.6,12L5,6.4L6.4,5L12,10.6L17.6,5L19,6.4L13.4,12L19,17.6L17.6,19L12,13.4L6.4,19Z"/></svg></button></header>
        <nav class="armix-audio-tabs" aria-label="Audio setup pages" role="tablist">
          <button type="button" class="armix-audio-tab" data-audio-tab="device"
            role="tab" aria-selected="true">Audio Device</button>
          <button type="button" class="armix-audio-tab" data-audio-tab="pins"
            role="tab" aria-selected="false" disabled>Hardware Pins</button>
          <button type="button" class="armix-audio-tab" data-audio-tab="inputs"
            role="tab" aria-selected="false" disabled>Inputs</button>
          <button type="button" class="armix-audio-tab" data-audio-tab="routing"
            role="tab" aria-selected="false" disabled>Outputs</button>
          <button type="button" class="armix-audio-tab" data-audio-tab="performance"
            role="tab" aria-selected="false">Performance</button>
          <button type="button" class="armix-audio-tab" data-audio-tab="diagnostics"
            role="tab" aria-selected="false">Diagnostics</button>
        </nav>
        <div class="armix-endpoint-fields">
          <label>Audio driver<select data-audio-field="backend"></select></label>
          <label>Recording / input<select data-audio-field="capture"></select></label>
          <label>Playback / monitor<select data-audio-field="playback"></select></label>
          <div class="armix-device-note" data-audio-active>Reading current audio device…</div>
          <div class="armix-device-note" data-audio-signal-path role="status"></div>
          <div class="armix-device-actions">
            <button type="button" data-audio-copy-report>Copy Audio Report</button>
            <button type="button" data-audio-review-capture hidden>Review capture pins</button>
          </div>
          <div class="armix-device-note" data-audio-latency aria-label="Driver-reported audio latency">
            Latency · waiting for native device readback
          </div>
          <label>Sample rate<select data-audio-field="rate"></select></label>
          <label>Buffer size<select data-audio-field="buffer"></select></label>
          <div class="armix-device-actions">
            <button type="button" data-audio-driver-panel hidden>ASIO Control Panel</button>
          </div>
          <div class="armix-device-note">Apply device to discover its hardware pins.</div>
        </div>
        <section class="armix-m4-performance" data-m4-performance role="region" aria-label="Smart Performance Assistant">
          <div class="armix-m4-head">
            <div><strong>Smart Performance Assistant</strong>
              <div class="armix-m4-meta">Native DSP evidence · advisory only · 1 Hz</div></div>
            <span class="armix-m4-status" data-m4-state>Waiting for native audio</span>
          </div>
          <div class="armix-m4-metrics">
            <div class="armix-m4-metric"><small>RENDER HEADROOM</small><span data-m4-budget>—</span></div>
            <div class="armix-m4-metric"><small>DEADLINE PRESSURE</small><span data-m4-over>—</span></div>
            <div class="armix-m4-metric"><small>HIGHEST OBSERVED FX PEAK</small><span data-m4-fx>—</span></div>
            <div class="armix-m4-metric"><small>DECLARED GRAPH PDC</small><span data-m4-latency>—</span></div>
          </div>
          <div class="armix-m4-section"><strong>Callback timing context</strong>
            <div class="armix-m4-line" data-m4-correlation>—</div>
          </div>
          <div class="armix-m4-section"><strong>FX to review</strong>
            <div data-m4-reviews></div>
          </div>
          <div class="armix-m4-section"><strong>Recommended next steps</strong>
            <div data-m4-advice></div>
          </div>
          <div class="armix-m4-note">P99 is a bucket ceiling from the latest 1-second host render interval.
            FX peaks are historical; callback lateness is not proven driver XRUN.
            No setting, plugin or audio buffer is changed automatically.</div>
        </section>
        <section class="armix-audio-diagnostics">
          <div class="armix-device-note" data-audio-period></div>
          <div class="armix-device-note" data-audio-health role="status"></div>
          <button type="button" data-audio-read-health>Refresh audio status</button>
        <details data-audio-loopback-details hidden>
          <summary>Wired round-trip calibration · ASIO</summary>
          <div class="armix-loopback-pins">
            <label>Unused line input<select data-audio-loopback-input></select></label>
            <label>MAIN line output<select data-audio-loopback-output></select></label>
          </div>
          <div class="armix-device-note">Pause playback and disconnect speakers. Wire these line-level ports, disable interface direct monitoring and phantom power. The probe briefly replaces mixer output.</div>
          <label class="armix-loopback-confirm"><input type="checkbox" data-audio-loopback-confirmed>
            Cable connected and MAIN already armed
          </label>
          <button type="button" data-audio-loopback-start disabled>Measure round-trip latency</button>
          <div class="armix-loopback-result armix-device-note" data-audio-loopback-result role="status"></div>
        </details>
        </section>
        <div class="armix-device-progress" data-audio-progress role="status" aria-live="polite" hidden>
          <!--By Sam Herbert (@sherb), for everyone. More @ http://goo.gl/7AJzbL-->
          <svg viewBox="0 0 55 80" xmlns="http://www.w3.org/2000/svg" fill="currentColor" aria-hidden="true" focusable="false">
            <g transform="matrix(1 0 0 -1 0 80)">
              <rect width="10" height="20" rx="3">
                <animate attributeName="height" begin="0s" dur="4.3s" values="20;45;57;80;64;32;66;45;64;23;66;13;64;56;34;34;2;23;76;79;20" calcMode="linear" repeatCount="indefinite"/>
              </rect>
              <rect x="15" width="10" height="80" rx="3">
                <animate attributeName="height" begin="0s" dur="2s" values="80;55;33;5;75;23;73;33;12;14;60;80" calcMode="linear" repeatCount="indefinite"/>
              </rect>
              <rect x="30" width="10" height="50" rx="3">
                <animate attributeName="height" begin="0s" dur="1.4s" values="50;34;78;23;56;23;34;76;80;54;21;50" calcMode="linear" repeatCount="indefinite"/>
              </rect>
              <rect x="45" width="10" height="30" rx="3">
                <animate attributeName="height" begin="0s" dur="2s" values="30;45;13;80;56;72;45;76;34;23;67;30" calcMode="linear" repeatCount="indefinite"/>
              </rect>
            </g>
          </svg>
          <div><strong>Applying audio device…</strong>
          <span>Waiting for driver and mixer verification.</span></div>
        </div>
        <section class="armix-input-overview">
          <div class="armix-output-heading">INPUTS · hardware to mixer</div>
          <div class="armix-crosspoint-scroll" data-audio-input-grid></div>
          <div class="armix-device-note">Choose Mono or Stereo, then Apply on the row. Blue = installed; amber = draft.</div>
        </section>
        <section class="armix-output-overview" data-audio-output-overview>
          <div class="armix-output-heading">Destinations</div>
          <div class="armix-output-matrix" data-audio-output-matrix role="list"></div>
          <details data-audio-hardware-routing>
            <summary>Advanced hardware routing</summary>
            <div class="armix-crosspoint-scroll" data-audio-output-grid></div>
            <div class="armix-device-note">Mute MAIN and STREAM before changing an existing MAIN / MONITOR pair. Blue = installed; amber = draft.</div>
          </details>
          <div class="armix-stream-inline" data-audio-extra-output></div>
          <div data-monitor-timing hidden class="armix-device-note">
            <strong>MONITOR timing</strong> · <span data-monitor-timing-value></span>
            <button type="button" data-monitor-timing-toggle disabled>Use singer low latency</button>
            <div>Graph latency only · separate singer monitor, do not combine with MAIN · session only</div>
          </div>
          <div class="armix-device-note">Status from the audio engine · listening in the receiving app is not verified</div>
        </section>
        <section class="armix-driver-inventory" aria-label="Driver channel inventory">
          <div class="armix-output-heading">DRIVER CHANNELS · available / enabled</div>
          <div class="armix-device-note" data-audio-pin-summary></div>
          <div class="armix-device-note" data-audio-asio4all-hint hidden>
            Check ASIO4ALL WDM inputs/outputs before Apply. Different device clocks may drift.
          </div>
          <div class="armix-device-pins" data-audio-pins></div>
          <button type="button" data-audio-main-pins hidden>Apply two selected playback pins as MAIN</button>
          <div class="armix-device-note">Select pins, then Apply. Additional pins are not automatically patched to mixer channels or outputs.</div>
        </section>
        <div class="armix-device-actions">
          <button type="button" data-audio-apply>Apply device</button>
          <button type="button" data-audio-remap hidden>Use same pins on this device</button>
        </div></section>`;
      devicePanel.querySelector("[data-audio-close]").onclick=closeNativeAudioIO;
      // Vendor settings are distinct from choosing a different audio device.
      devicePanel.querySelector("[data-audio-driver-panel]").onclick=()=>{
        if(deviceBusy()||!deviceInventory||!deviceChoice||
           !deviceVM.setupPresentation(deviceInventory,deviceChoice,deviceState).driverPanelAvailable)return;
        const active=deviceInventory.active;
        backend.emitEvent("arMixAction",{action:"audioDriverPanel",driver:active.playbackDevice,
          deviceGeneration:active.deviceGeneration});
      };
      const readHealth=devicePanel.querySelector("[data-audio-read-health]");
      readHealth.title="Read current driver and cumulative timing counters; discard unapplied edits. No driver rescan.";
      readHealth.onclick=()=>{
        if(deviceBusy())return;
        deviceState=deviceVM.setupClearResult(deviceState);
        backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
      };
      devicePanel.addEventListener("click",e=>{
        if(e.target===devicePanel){closeNativeAudioIO();return;}
        const tab=e.target?.closest?.("button[data-audio-tab]");
        const dialog=devicePanel.querySelector(".armix-audio-dialog");
        if(!tab||!dialog||deviceBusy()||tab.disabled||loopbackPending)return;
        selectAudioSetupPage(tab.dataset.audioTab);
      });
      loopbackField("start").onclick=()=>requestLoopback(true);
      loopbackField("confirmed").onchange=e=>{
        if(!e.target.checked&&loopbackPending)
          backend.emitEvent("arMixAction",{action:"audioLoopbackCancel"});
        loopbackField("start").disabled=!e.target.checked||loopbackPending;
      };
      for(const side of ["input","output"])
        loopbackField(side).onchange=()=>{
          loopbackField("confirmed").checked=false;
          loopbackField("start").disabled=true;
          requestLoopback(false);
        };
      devicePanel.addEventListener("change",e=>{
        if(deviceBusy()||!deviceChoice||!deviceInventory)return;
        const side=e.target.dataset.audioPin;
        const field=e.target.dataset.audioField;
        const change=(side==="input"||side==="output")?
          {side,index:Number(e.target.dataset.pinIndex),
            checked:e.target.checked}:
          field?{field,value:e.target.value}:null;
        if(!change)return;
        const next=deviceVM.setupEditDraft(deviceChoice,deviceInventory,change);
        if(next===deviceChoice)return;
        deviceChoice=next;
        deviceState=deviceVM.setupClearResult(deviceState);
        renderDevicePanel();
      });
      devicePanel.querySelector("[data-audio-copy-report]").onclick=()=>{
        backend.emitEvent("arMixAction",{action:"copyDiagnosticReport"});
      };
      devicePanel.querySelector("[data-audio-review-capture]").onclick=()=>{
        if(deviceBusy())return;
        const draft=deviceVM.captureRecoveryDraft(deviceInventory,deviceChoice,deviceState);
        if(!draft)return;
        deviceChoice=draft;
        deviceState=deviceVM.setupClearResult(deviceState);
        selectAudioSetupPage("pins");
        renderDevicePanel(); // separate explicit Apply required
      };
      devicePanel.querySelector("[data-audio-apply]").onclick=()=>{
        if(!deviceBusy())sendDeviceApply();
      };
      devicePanel.querySelector("[data-audio-main-pins]").onclick=()=>{
        if(!deviceBusy())sendDeviceApply(false,true);
      };
      devicePanel.querySelector("[data-audio-remap]").onclick=()=>{
        if(!deviceBusy() && deviceState.remapGeneration!==null)
          sendDeviceApply(true);
      };
       document.body.appendChild(devicePanel);
    }
    devicePanel.hidden=false;
    const dialog=devicePanel.querySelector(".armix-audio-dialog");
    if(dialog)selectAudioSetupPage("device");
    for(const item of devicePanel.querySelectorAll('button[data-audio-tab="pins"],button[data-audio-tab="inputs"],button[data-audio-tab="routing"]'))
      item.disabled=true;
    paintPhysicalOutputs(hardware);
    if(deviceBusy())return;
    const active=devicePanel.querySelector("[data-audio-active]");
    deviceState=deviceVM.setupClearResult(deviceState);
    if(active)active.textContent="Reading verified audio devices…";
    backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
  }
  function fillAudioSelect(name,values,current){
    const el=devicePanel?.querySelector('[data-audio-field="'+name+'"]');
    if(!el)return;
    el.replaceChildren(...values.map(value=>{
      const option=document.createElement("option");
      option.value=value;
      option.textContent=name==="capture"&&value===""
        ?"No hardware input (Player / FX)":value;
      return option;
    }));
    if(values.includes(current))el.value=current;
    el.disabled=deviceBusy();
  }
  function renderDevicePanel(){
    if(!devicePanel||devicePanel.hidden||!deviceInventory)return;
    if(!deviceChoice)deviceChoice=deviceVM.setupDraft(deviceInventory);
    if(!deviceChoice)return;
    const active=deviceInventory.active||{};
    // All draft, selection and eligibility rules live in the ONE pure VM.
    // The bridge only paints the existing approved /preview/ DOM.
    const view=deviceVM.setupPresentation(deviceInventory,deviceChoice,deviceState);
    const routingReady=deviceVM.setupRoutingReady(deviceInventory,deviceChoice,deviceState);
    const masksMatch=routingReady&&deviceVM.setupPinsApplied(deviceInventory,deviceChoice);
    for(const item of devicePanel.querySelectorAll('button[data-audio-tab="pins"],button[data-audio-tab="inputs"],button[data-audio-tab="routing"]'))
      item.disabled=item.dataset.audioTab==="pins"?!routingReady:!masksMatch;
    const page=devicePanel.querySelector(".armix-audio-dialog").dataset.audioTab;
    if(!deviceBusy()&&(!routingReady&&page==="pins"||
       !masksMatch&&(page==="inputs"||page==="routing")))
      selectAudioSetupPage(routingReady?"pins":"device");
    const hint=devicePanel.querySelector("[data-audio-asio4all-hint]");
    if(hint)hint.hidden=!view.asio4allHint;
    fillAudioSelect("backend",view.backendOptions,deviceChoice.backend);
    fillAudioSelect("capture",view.inputOptions,deviceChoice.capture);
    fillAudioSelect("playback",view.outputOptions,deviceChoice.playback);
    fillAudioSelect("rate",view.rateOptions,String(deviceChoice.rate||0));
    fillAudioSelect("buffer",view.bufferOptions,String(deviceChoice.buffer||0));
    const rateSelect=devicePanel.querySelector('[data-audio-field="rate"]');
    const bufferSelect=devicePanel.querySelector('[data-audio-field="buffer"]');
    if(rateSelect){
      rateSelect.options[0].textContent="Driver default (AUTO)";
      for(const option of Array.from(rateSelect.options).slice(1))
        option.textContent=option.value+" Hz"+(Number(option.value)===active.rate?" · current":"");
      rateSelect.disabled=!view.same||deviceBusy();
    }
    if(bufferSelect){
      bufferSelect.options[0].textContent="Driver default (AUTO)";
      for(const option of Array.from(bufferSelect.options).slice(1)){
        const ms=deviceVM.setupBufferPeriod(deviceChoice.rate,Number(option.value));
        option.textContent=option.value+" samples"+(ms===null?"":" · "+ms.toFixed(2)+" ms")+
          (Number(option.value)===active.buffer?" · current":"");
      }
      bufferSelect.disabled=!view.same||deviceBusy();
    }
    const period=devicePanel.querySelector("[data-audio-period]");
    if(period)period.textContent=view.draftPeriodMs===null?
      "Requested period: driver negotiation required":
      "Requested callback period "+view.draftPeriodMs.toFixed(2)+" ms · not round-trip latency";
    const panel=devicePanel.querySelector("[data-audio-driver-panel]");
    if(panel){panel.hidden=!view.driverPanelAvailable;panel.disabled=deviceBusy();}
    const readHealth=devicePanel.querySelector("[data-audio-read-health]");
    if(readHealth)readHealth.disabled=deviceBusy();
    const health=devicePanel.querySelector("[data-audio-health]");
    const h=deviceVM.setupHealthReadback(active);
    const metric=v=>v===null?"N/A":String(v);
    if(health)health.textContent=h?
      (active.bitDepth>0?active.bitDepth+"-bit · ":"")+
      "Cumulative timing snapshot · P99 budget "+h.budget+
      " · deadline misses "+metric(h.overBudget)+" · callback gaps "+metric(h.gaps)+
      " · driver XRUN "+metric(h.xruns)+
      (h.discontinuities===null?"":" · capture discontinuities "+h.discontinuities)+
      " · not a crackle-free guarantee":
      "Audio health unavailable until a driver is verified.";
    const pinSummary=devicePanel.querySelector("[data-audio-pin-summary]");
    if(pinSummary){
      const a=view.inputInventory,b=view.outputInventory;
      pinSummary.textContent=a&&b?
        "IN "+a.active+"/"+a.total+" enabled · OUT "+b.active+"/"+b.total+" enabled"+
          (!a.complete||!b.complete?" · Apply supports up to 64 pins per direction":"")+
          (active.backend==="ASIO"&&a.total===2&&b.total===2?
            " · ASIO driver currently reports 2 × 2; separate Windows endpoints are not ASIO pins.":""):
        "Apply the selected driver first to read its reported channels.";
    }
    const pins=devicePanel.querySelector("[data-audio-pins]");
    if(pins){
      pins.replaceChildren();
      if(routingReady){
        const container=document.createElement("div");
        container.className="armix-audio-pins";
        for(const [side,names,selected] of [
          ["input",active.inputNames,view.inputPins],
          ["output",active.outputNames,view.outputPins]]){
          const group=document.createElement("div");
          const title=document.createElement("strong");
          title.textContent=side==="input"?"Capture pins":"Playback pins";
          group.appendChild(title);
          const inventory=side==="input"?view.inputInventory:view.outputInventory;
          if(active.backend==="ASIO"&&inventory?.complete&&
             inventory.selectable>=(side==="input"?1:2)){
            const all=document.createElement("button");
            all.type="button";
            all.textContent="Select all "+inventory.selectable;
            all.disabled=deviceBusy()||selected.length===inventory.selectable;
            all.onclick=()=>{
              if(deviceBusy())return;
              deviceChoice=deviceVM.setupSelectAllPins(deviceChoice,deviceInventory,side);
              deviceState=deviceVM.setupClearResult(deviceState);
              renderDevicePanel();
            };
            group.appendChild(all);
          }
          (names||[]).forEach((name,index)=>{
            const label=document.createElement("label");
            const control=document.createElement("input");
            control.type="checkbox";
            control.dataset.audioPin=side;
            control.dataset.pinIndex=String(index);
            control.checked=selected.includes(index);
            control.disabled=deviceBusy();
            label.append(control,document.createTextNode(
              (side==="input"?"IN ":"OUT ")+(index+1)+" · "+
              (String(name).trim()?String(name):"Unnamed by driver")));
            group.appendChild(label);
          });
          container.appendChild(group);
        }
        pins.appendChild(container);
      }else pins.textContent="Apply this device first to inspect all reported and enabled pins.";
    }
    const activeText=devicePanel.querySelector("[data-audio-active]");
    if(activeText&&!deviceBusy())activeText.textContent=view.activeText;
    const latencyText=devicePanel.querySelector("[data-audio-latency]");
    if(latencyText){
      const latency=deviceVM.setupLatencyReadback(active);
      const ms=n=>n===null?"—":n.toFixed(2)+" ms";
      latencyText.textContent=latency.ready&&view.same?
        "Round-trip (estimated) "+ms(latency.estimatedTotalMs)+
        " · Device "+ms(latency.driverIoHintMs)+" · FX/PDC "+ms(latency.fxMs):
        "Round-trip latency — Apply device first";
      latencyText.title="Input and output are driver-reported. Buffer period is shown separately. Electrical round-trip requires physical loopback calibration.";
    }
    const pathHint=devicePanel.querySelector("[data-audio-signal-path]");
    if(pathHint)pathHint.textContent=deviceVM.signalPathHint(active);
    const recovery=deviceVM.captureRecoveryDraft(deviceInventory,deviceChoice,deviceState);
    const review=devicePanel.querySelector("[data-audio-review-capture]");
    if(review){
      review.hidden=!recovery;
      review.disabled=deviceBusy();
      if(recovery)review.textContent="Review capture IN "+recovery.inputPins.map(p=>p+1).join(" + ");
    }
    renderLoopbackPins(active);
    devicePanel.querySelector("[data-audio-apply]").disabled=
      deviceBusy()||!view.canApply;
    const assignMain=devicePanel.querySelector("[data-audio-main-pins]");
    assignMain.hidden=!view.canAssignMain;
    assignMain.disabled=deviceBusy();
    const remap=devicePanel.querySelector("[data-audio-remap]");
    remap.hidden=!view.canReusePins;
    remap.disabled=deviceBusy();
    remap.title="Reuse existing input and output pin numbers on the selected device. MONITOR stays off.";
  }
  function showDeviceApplyProgress(pending){
    const progress=devicePanel.querySelector("[data-audio-progress]");
    const svg=progress.querySelector("svg");
    progress.hidden=!pending;
    devicePanel.querySelector("[data-audio-active]").hidden=pending;
    devicePanel.querySelector("[data-audio-close]").disabled=pending;
    const apply=devicePanel.querySelector("[data-audio-apply]");
    apply.textContent=pending?"Applying…":
      devicePanel.querySelector(".armix-audio-dialog").dataset.audioTab==="pins"?"Apply pins":"Apply device";
    apply.setAttribute("aria-busy",String(pending));
    if(pending&&!window.matchMedia?.("(prefers-reduced-motion: reduce)")?.matches){
      svg.setCurrentTime?.(0);
      svg.unpauseAnimations?.();
    }else svg.pauseAnimations?.();
  }
  function sendDeviceApply(confirmRetainedPinMapping=false,assignMain=false){
    if(deviceBusy()||!deviceChoice||!deviceInventory?.active)return;
    const requestId="approved-io-"+(++deviceRequest);
    const begun=deviceVM.setupBegin(deviceState,{
      inventory:deviceInventory,draft:deviceChoice,requestId,
      baselineEpoch:hardware?.device?.epoch,
      confirmRetainedPinMapping,assignMain});
    if(!begun)return;
    deviceState=begun.state;
    closeAfterVerifiedPinApply=!confirmRetainedPinMapping &&
      devicePanel.querySelector(".armix-audio-dialog")?.dataset.audioTab==="pins" &&
      begun.state.pending.expected.requestedPins!==null;
    showDeviceApplyProgress(true);
    renderDevicePanel();
    // Reuse the ONLY native arMixAction dispatcher. UI state cannot touch PCM.
    // Allow WebView to paint before a blocking driver dialog, with one fallback.
    let submitted=false;
    const submit=()=>{
      if(submitted)return;
      submitted=true;
      window.clearTimeout(fallback);
      backend.emitEvent("arMixAction",begun.request);
    };
    const fallback=window.setTimeout(submit,120);
    window.requestAnimationFrame(()=>window.requestAnimationFrame(submit));
    window.clearTimeout(deviceApplyTimer);
    deviceApplyTimer=window.setTimeout(()=>{
      const previous=deviceState;
      deviceState=deviceVM.setupTimeout(deviceState,requestId);
      if(deviceState===previous)return; // expired / unrelated request
      finishDeviceApplyUi();
      controller?.subscribe(); // authoritative current state, not retry Apply
      backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
    },12000);
  }
  if(connected){
    const onLogicalIoAck=ack=>{
      const p=logicalIoPending;
      if(!p||ack?.requestId!==p.requestId)return;
      let expected="";
      try{expected=String(BigInt(p.payload.logicalRevision)+1n);}catch(_){}
      if(ack.ok!==true||!expected||ack.logicalRevision!==expected){
        logicalIoPending=null;nativeToast(ack.error||"Logical I/O rejected");paintPhysicalOutputs(hardware);return;
      }
      logicalIoPending={...p,acked:true,logicalRevision:ack.logicalRevision,graphRevision:ack.graphRevision};
      controller?.subscribe();
    };
    backend.addEventListener("arMixLogicalInputAck",onLogicalIoAck);
    backend.addEventListener("arMixLogicalOutputAck",onLogicalIoAck);
    backend.addEventListener("arMixIoDefaultsAck",ack=>{
      const p=ioDefaultsPending;
      if(!p||ack?.requestId!==p.requestId)return;
      let expected="",inRev="",outRev="";
      try{
        expected=String(BigInt(p.revision)+(p.op==="LOAD"?0n:1n));
        inRev=String(BigInt(p.inputRevision)+(p.op==="LOAD"?1n:0n));
        outRev=String(BigInt(p.outputRevision)+(p.op==="LOAD"?1n:0n));
      }catch(_){}
      if(ack.ok!==true||!expected||ack.revision!==expected||ack.inputRevision!==inRev||ack.outputRevision!==outRev){
        ioDefaultsPending=null;nativeToast(ack.error||"I/O default rejected");paintPhysicalOutputs(hardware);return;
      }
      ioDefaultsPending={...p,ack};controller?.subscribe();
    });
    backend.addEventListener("arMixNamedProfileFileResult",message=>{
      const pending=namedProfileFilePending;
      if(!pending||message?.requestId!==pending.requestId||message.operation!==pending.operation)return;
      namedProfileFilePending=null;
      pending.finish(message);
      // Do not repaint the editor and discard a successfully loaded draft.
    });
    backend.addEventListener("arMixNamedPatchAck",ack=>{
      if(namedPatchPending?.phase!=="APPLYING"||
         ack?.requestId!==namedPatchPending.requestId)return;
      if(ack.ok!==true){
        namedPatchPending=null;
        nativeToast(ack.error||"I/O preset rejected");
        paintPhysicalOutputs(hardware);return;
      }
      namedPatchPending={...namedPatchPending,phase:"VERIFYING"};
      backend.emitEvent("arMixAction",{action:"audioNamedPatchRead",requestId:ack.requestId});
    });
    backend.addEventListener("arMixNamedPatchReadback",message=>{
      if(namedPatchPending?.phase!=="VERIFYING"||
         message?.requestId!==namedPatchPending.requestId)return;
      const a=deviceInventory?.active,b=message.active,data=message.namedPatches;
      const same=a&&b&&["backend","captureDevice","playbackDevice","deviceGeneration","rate","buffer"]
        .every(k=>a[k]===b[k])&&deviceVM.setupMatrixDeviceMatches({active:b},hardware);
      if(!same||data?.revision!==namedPatchPending.revision+1||
         !Array.isArray(data.entries))return;
      const wanted=namedPatchPending.payload;
      const fields=["name","mode","left","right","leftName","rightName"];
      const matches=(p,q)=>p.side===wanted.side&&fields.every(k=>p[k]===q[k]);
      const previous=deviceInventory?.namedPatches?.entries||[];
      const proved=wanted.operation==="IMPORT"?
        data.entries.length===previous.length+wanted.profile.entries.length&&
        previous.every(old=>data.entries.some(p=>p.id===old.id&&p.side===old.side&&fields.every(k=>p[k]===old[k])))&&
        wanted.profile.entries.every(row=>data.entries.filter(p=>matches(p,row)&&!previous.some(old=>old.id===p.id)).length===1):
        wanted.operation==="REMOVE"?!data.entries.some(p=>p.id===wanted.id):
        data.entries.some(p=>(!wanted.id||p.id===wanted.id)&&matches(p,wanted));
      if(!proved)return;
      namedPatchPending=null;
      // Refresh only this independent metadata, not the current device draft.
      deviceInventory={...deviceInventory,namedPatches:data};
      nativeToast(wanted.operation==="IMPORT"?"I/O profile imported":"I/O preset saved");
      syncNamedOptions();
      paintPhysicalOutputs(hardware);
    });
  }
  function finishDeviceApplyUi(verifiedSnapshot=null){
    window.clearTimeout(deviceApplyTimer);
    deviceApplyTimer=0;
    if(!devicePanel)return;
    showDeviceApplyProgress(false);
    const closePins=closeAfterVerifiedPinApply && deviceState.phase==="ACCEPTED" &&
      !!verifiedSnapshot?.device && deviceState.inspectNewAsio!==true;
    closeAfterVerifiedPinApply=false;
    if(deviceState.phase==="ACCEPTED"){
      deviceChoice=null;
      if(closePins){
        // Close only after independent installed readback, never on ACK alone.
        delete devicePanel.dataset.openPinsAfterInventory;
        deviceInventory=null;
        closeNativeAudioIO();
      }else{
        // Device discovery stays in setup to expose the new hardware pins.
        // Never show a candidate or pre-Apply inventory as installed hardware.
        deviceInventory=null;
        devicePanel.dataset.openPinsAfterInventory="true";
        selectAudioSetupPage("device");
        devicePanel.querySelector("[data-audio-active]").textContent=
          "Device applied · reading hardware pins";
        devicePanel.querySelector("[data-audio-pin-summary]").textContent=
          "Waiting for the new driver inventory…";
        devicePanel.querySelector("[data-audio-pins]").textContent="";
        for(const field of devicePanel.querySelectorAll("select, [data-audio-apply], [data-audio-main-pins], [data-audio-remap], [data-audio-driver-panel]"))
          field.disabled=true;
      }
      // The confirmed schema-v6 readback, not the preceding Apply ACK,
      // is the only source for the device-ready status shown to operators.
      const active=verifiedSnapshot?.device;
      status("DEVICE READY · "+String(active?.backend||"Audio")+
        " · "+String(active?.sampleRate||0)+" Hz / "+
        String(active?.bufferFrames||0)+" samples");
      nativeToast(deviceState.message);
    }else{
      deviceChoice=null;
      renderDevicePanel();
      if(deviceState.phase==="RECOVERY_REQUIRED"||deviceState.phase==="STALE")
        nativeToast(deviceState.message);
    }
  }
  if(connected){
    backend.addEventListener("arMixAudioDeviceInventory",inventory=>{
      if(!inventory||!Array.isArray(inventory.types))return;
      if(deviceBusy())return;
      // Inventory is a native observation, not accepted Apply proof.
      deviceInventory=inventory;
      deviceChoice=null;
      renderDevicePanel();
      syncNamedOptions();
      paintPhysicalOutputs(hardware);
      if(devicePanel&&!devicePanel.hidden&&devicePanel.dataset.openPinsAfterInventory==="true"){
        delete devicePanel.dataset.openPinsAfterInventory;
        if(deviceVM.setupRoutingReady(deviceInventory,deviceChoice,deviceState))
          selectAudioSetupPage("pins");
      }
      if(devicePanel&&!devicePanel.hidden&&!loopbackPending)requestLoopback(false);
    });
    backend.addEventListener("arMixPhysicalLoopback",message=>{
      if(!loopbackId||message?.requestId!==loopbackId||!devicePanel||devicePanel.hidden)return;
      loopbackPending=message.state==="CAPTURING";
      const result=message.result;
      const measured=message.state==="MEASURED_LOOPBACK"&&result?.rate>0&&result?.frames>0;
      loopbackField("result").textContent=measured?
        "Measured wired round-trip "+(1000*result.frames/result.rate).toFixed(2)+
        " ms · "+result.frames+" frames · correlation "+result.confidence.toFixed(2)+
        " · "+message.message:message.message||"No calibration saved for these pins";
      if(!loopbackPending)loopbackField("confirmed").checked=false;
      for(const side of ["input","output"])loopbackField(side).disabled=loopbackPending;
      loopbackField("start").disabled=loopbackPending||!loopbackField("confirmed").checked;
    });
    backend.addEventListener("arMixAudioDeviceApplyAck",ack=>{
      if(!devicePanel)return;
      const next=deviceVM.setupAck(deviceState,ack);
      if(next===deviceState)return; // delayed or unrelated ACK
      deviceState=next;
      // ACK.active is unverified hardware evidence. Do not allow either
      // accepted or rejected ACKs to overwrite the installed I/O inventory.
      if(deviceBusy()){
        // ACK alone NEVER closes the dialog or announces LIVE.
        controller?.subscribe();
      }else{
        finishDeviceApplyUi();
        // G-MV21: a negative ACK can still leave a NEW native endpoint in
        // NEEDS_REMAP. Only a freshly queried native inventory may unblock
        // retained-pin recovery or supply the next device generation.
        // Never commit ack.active as installed hardware.
        backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
      }
    });
  }

  function installFirstSoundMenu() {
    const menu=document.querySelector?.("#moreTools .more-tools-panel");
    if(!menu || typeof document.createElement!=="function")return;
    const item=(tag,cls,label)=>{
      const el=document.createElement(tag);
      el.className=cls;el.textContent=label;
      if(tag==="button")el.type="button";
      return el;
    };
    const caption=item("span","more-tools-caption","AUDIO / SAFETY");
    const setup=item("button","tool","Audio I/O");
    setup.addEventListener("click",()=>{
      // Reuse the EXISTING custom driver/endpoint pane. The stock JUCE
      // selector is reserved for explicitly requested advanced recovery.
      // Never create a second Audio Setup workflow from the More menu.
      openNativeAudioIO();
    });
    inputSelect=item("select","toolbar-select","");
    inputSelect.setAttribute("aria-label","JUCE physical input for CH1");
    inputSelect.addEventListener("change",()=>updateHardwarePanel(hardware));
    routeBtn=item("button","tool","Patch CH1");
    routeBtn.addEventListener("click",()=>{
      const d=hardware?.device,l=legacy();
      const epoch=safeNativeCounter(d?.epoch);
      const revision=safeNativeCounter(l?.routingRevision);
      const pin=Number(inputSelect?.value);
      if(!deviceReady()||inputSelect.value==="" ||
         epoch===null||revision===null||
         !Number.isInteger(pin)||!d.enabledInputs.includes(pin)){
        status("NATIVE: invalid input pin, epoch or route revision.");return;
      }
      pendingSourceId=++sourceSerial;
      routeBtn.disabled=true;
      backend.emitEvent("arMixAction",{
        action:"routingSourceSwitch",requestId:pendingSourceId,
        channel:0,mode:"MONO",left:pin,right:-1,
        deviceEpoch:epoch,revision
      });
      status("NATIVE: CH1 physical input patch pending real ACK.");
    });
    armBtn=item("button","tool","Arm output");
    armBtn.addEventListener("click",()=>{
      if(!deviceReady()||legacy()?.routeAccepted!==true)return;
      const epoch=safeNativeCounter(hardware.device.epoch);
      const revision=safeNativeCounter(legacy().routingRevision);
      if(epoch===null||revision===null)return;
      backend.emitEvent("arMixAction",{action:"monitor",value:true,
        expectedEpoch:epoch,expectedRevision:revision});
      status("NATIVE: output ARM requested; awaiting JUCE ACK.");
    });
    muteBtn=item("button","tool","Emergency stop audio");
    muteBtn.dataset.nativeEmergencyOutput="true";
    muteBtn.addEventListener("click",()=>{
      const panic=hardware?.raw?.operatorOutputMuted===true;
      if(!panic){
        backend.emitEvent("arMixAction",{action:"monitor",value:false});
        status("Emergency stop requested");
        return;
      }
      if(!deviceReady()||virtualCableFeedbackPair())return;
      const ep=safeNativeCounter(hardware?.device?.epoch),
        rev=safeNativeCounter(mixerSession()?.authority==="CHANNEL_ID"?
          mixerSession()?.routeRevision:legacy()?.routingRevision);
      if(ep===null||rev===null)return;
      backend.emitEvent("arMixAction",{action:"monitor",value:true,
        requestId:"emergency-resume-"+(++m4RequestSerial),
        expectedEpoch:ep,expectedRevision:rev});
      status("Verifying audio route after emergency stop");
    });
    hardwareNote=item("span","more-tools-caption","Device not initialized");
    hardwareNote.style.whiteSpace="normal";
    // Everyday I/O and driver recovery use the same custom Audio I/O pane.
    // Vendor ASIO dialogs remain available through its explicit driver action.
    inputSelect.hidden=true;routeBtn.hidden=true;armBtn.hidden=true;
    const advanced=document.createElement("details");
    advanced.className="arNativeAdvancedRecovery";
    const summary=item("summary","more-tools-caption","Advanced driver recovery");
    const driver=item("button","tool","Open Audio I/O recovery");
    driver.addEventListener("click",openNativeAudioIO);
    advanced.append(summary,driver);
    // First Sound P0: these are REAL JUCE diagnostic actions, not demo
    // toggles. The marker snapshots the 5-second PRE-event counters and
    // Windows RAM/process paging context from the existing 1 Hz control
    // timer. No sampling or file I/O happens in the ASIO callback.
    const healthTitle=item("span","more-tools-caption","AUDIO HEALTH");
    const tone=item("button","tool","Test MAIN output");
    tone.title="Start or stop a quiet tone on the verified MAIN pair without reopening the driver";
    tone.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"diagnosticRoute",value:"TOGGLE_MAIN_OUTPUT_TONE"});
    });
    const mark=item("button","tool","Mark Crackle");
    mark.title="Capture 5s before an audible crackle, including Windows memory";
    mark.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"diagnosticMark"});
      nativeToast("Audio glitch marked · pre-event counters and RAM saved");
      status("Crackle marker sent to JUCE diagnostics");
    });
    const report=item("button","tool","Copy Audio Report");
    report.title="Copy real JUCE timing / driver / memory evidence to clipboard";
    report.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"copyDiagnosticReport"});
      status("Preparing JUCE audio diagnostic report on control thread");
    });
    backend.addEventListener("arMixDiagnosticReportCopied",ack=>{
      const bytes=Number(ack?.bytes);
      if(Number.isFinite(bytes)&&bytes>0)
        nativeToast("Audio report copied · paste into engineering chat");
    });
    // Native-only access to EXISTING JUCE Show storage, not browser scenes.
    // ShowV2 is 10CH+4AUX; unsupported dynamic WIRE/GROUP is rejected by
    // native capture rather than silently truncated.
    const showHeading=item("span","more-tools-caption","STUDIO / VERIFIED SHOWS");
    const showSave=item("button","tool","Save Show");
    const showUpdate=item("button","tool","Save New Version");
    const showBrowse=item("button","tool","Browse Shows");
    const showSelect=item("select","toolbar-select","");
    const showRecall=item("button","tool","Recall Show");
    const showRecover=item("button","tool","Recover Last Good");
    const showNote=item("span","more-tools-caption",
      "Verified Shows only · full dynamic-session autosave is not yet available");
    showNote.style.whiteSpace="normal";
    showSelect.setAttribute("aria-label","Saved native Show");
    showSelect.style.maxWidth="210px";showSelect.style.fontSize="11px";
    showSelect.hidden=true;showUpdate.hidden=true;
    showRecall.hidden=true;showRecover.hidden=true;
    const showUuid=/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
    let showBusy=false;
    const showEntries=new Map();
    const showOption=(name,value)=>{
      const option=document.createElement("option");
      option.textContent=name;option.value=value;return option;
    };
    function showControls(){
      const entry=showEntries.get(showSelect.value);
      const muted=hardware?.device?.outputArmed===false &&
        hardware?.raw?.monitor!==true;
      showSave.disabled=showBusy;
      showBrowse.disabled=showBusy;
      showUpdate.disabled=showBusy||!muted||!entry||entry.needsRecovery;
      showRecall.disabled=showBusy||!muted||!entry||entry.needsRecovery;
      showRecover.disabled=showBusy||!muted||!entry||!entry.hasPrevious;
    }
    function showRefresh(){
      showBusy=true;showControls();
      showNote.textContent="Browsing verified native Shows…";
      backend.emitEvent("arMixAction",{action:"routingDryShowList"});
    }
    showSave.addEventListener("click",()=>{
      if(showBusy)return;
      if(hardware?.device?.outputArmed!==false||hardware?.raw?.monitor===true){
        status("SHOW SAVE requires MUTED output");return;
      }
      showBusy=true;showControls();
      showNote.textContent="Capturing accepted routing and VST3 state…";
      backend.emitEvent("arMixAction",{action:"routingDryShowSave"});
    });
    showUpdate.addEventListener("click",()=>{
      const id=showSelect.value,entry=showEntries.get(id);
      if(showBusy||!entry||entry.needsRecovery||!showUuid.test(id)||
         hardware?.device?.outputArmed!==false)return;
      showBusy=true;showControls();
      showNote.textContent="Saving verified next generation · previous Show retained…";
      backend.emitEvent("arMixAction",{action:"routingDryShowSave",value:id});
    });
    showBrowse.addEventListener("click",()=>{if(!showBusy)showRefresh();});
    showSelect.addEventListener("change",showControls);
    showRecall.addEventListener("click",()=>{
      const id=showSelect.value,entry=showEntries.get(id);
      if(showBusy||!entry||entry.needsRecovery||!showUuid.test(id)||
         hardware?.device?.outputArmed!==false)return;
      showBusy=true;showControls();
      showNote.textContent="Verifying Show / preparing native FX · output stays MUTED…";
      backend.emitEvent("arMixAction",{action:"routingDryShowRecall",value:id});
    });
    showRecover.addEventListener("click",()=>{
      const id=showSelect.value,entry=showEntries.get(id);
      if(showBusy||!entry?.hasPrevious||!showUuid.test(id)||
         hardware?.device?.outputArmed!==false)return;
      showBusy=true;showControls();
      showNote.textContent="Verifying previous generation · valid latest will NOT be overwritten…";
      backend.emitEvent("arMixAction",{action:"routingDryShowRecover",value:id});
    });
    backend.addEventListener("arMixNativeShowSaveAck",ack=>{
      showBusy=false;showControls();
      if(ack?.ok===true){
        status("SHOW SAVED · native verified state · OUTPUT MUTED");
        showRefresh();
      }else{
        showNote.textContent="Save rejected: "+String(ack?.error||"Native capture rejected");
        status(showNote.textContent);
      }
    });
    backend.addEventListener("arMixNativeShowListAck",ack=>{
      showBusy=false;showEntries.clear();
      showSelect.replaceChildren(showOption("Choose verified Show…",""));
      if(ack?.ok===true&&Array.isArray(ack.items)){
        for(const entry of ack.items.slice(0,64)){
          if(!showUuid.test(entry?.id))continue;
          showEntries.set(entry.id,{
            hasPrevious:entry.hasPrevious===true,
            needsRecovery:entry.needsRecovery===true
          });
          showSelect.append(showOption(
            String(entry.name||"Saved Show")+(entry.needsRecovery?" · RECOVERY":""),
            entry.id));
        }
      }
      showSelect.hidden=false;showUpdate.hidden=false;
      showRecall.hidden=false;showRecover.hidden=false;
      showNote.textContent=ack?.ok===true
        ?showEntries.size+" saved Shows · mute output before recall/recovery"
        :"Browse rejected: "+String(ack?.error||"Native storage unavailable");
      showControls();
    });
    backend.addEventListener("arMixNativeShowRecallAck",ack=>{
      showBusy=false;showControls();
      showNote.textContent=ack?.ok===true
        ?"SHOW RESTORED · OUTPUT MUTED; verify physical pins before MONITOR ON"
        :"Recall rejected: "+String(ack?.error||"Native route unchanged");
      if(ack?.ok===true)controller?.subscribe();
      status(showNote.textContent);
    });
    backend.addEventListener("arMixNativeShowRecoverAck",ack=>{
      showBusy=false;showControls();
      if(ack?.ok===true){
        status("Last verified Show restored on disk · muted; recall explicitly");
        showRefresh();
      }else{
        showNote.textContent="Recovery rejected: "+String(ack?.error||"Previous generation invalid");
        status(showNote.textContent);
      }
    });

    backend.addEventListener("arMixExternalOutputAck",ack=>{
      const found=[...pendingExternal].find(([,p])=>p.requestId===ack?.requestId);
      if(!found)return;
      const [id,transaction]=found;
      if(ack.ok!==true){
        const error=String(ack.error||"Physical output assignment rejected");
        pendingExternal.delete(id);
        const track=(scope?.tracks||[]).find(t=>nativeIdFor(t)===id);
        const select=track&&document.querySelector('.strip[data-id="'+track.id+
          '"] select[data-field="external"]');
        select?.removeAttribute("aria-busy");
        paintPhysicalOutputs(hardware);
        nativeToast(error);status(error);controller?.subscribe();return;
      }
      transaction.acked=true;
      controller?.subscribe(); // ACK alone cannot claim LIVE audio
    });
    backend.addEventListener("arMixNativeSessionAck",ack=>{
      const save=ack?.operation==="SAVE";
      if(!save&&ack?.operation!=="OPEN"&&ack?.operation!=="NEW"&&ack?.operation!=="NEW_LIVE"&&ack?.operation!=="RESET_IO"&&ack?.operation!=="ARCHIVE")return;
      if(ack.ok!==true){
        const error=String(ack.error||"Mixer file operation failed");
        status(error);nativeToast(error);
        return;
      }
      const message=save?"Mixer saved as .smix":ack.operation==="NEW_LIVE"?"New mixer active · previous project archived · output muted":ack.operation==="NEW"?"New offline mixer saved · output remains muted":ack.operation==="RESET_IO"?"I/O defaults restored · mixer and FX retained":ack.operation==="ARCHIVE"?"Archived mixer opened · output remains muted":"Mixer opened";
      status(message);nativeToast(message);
      if(!save)controller?.subscribe();
    });
    const newSessionButtons=[["New Project · empty mixer","EMPTY"],["New from I/O defaults","DEFAULT"],["Reset I/O to defaults","RESET_IO"]].map(([label,mode])=>{
      const button=item("button","tool",label);
      button.title=mode==="EMPTY"?"Start fresh · previous mixer archived, output muted":"Offline only · previous checkpoint archived in Studio/SessionArchive";
      button.addEventListener("click",()=>{
        const current=controller?.state()?.current;
        if(mode==="EMPTY"&&!logicalView&&current?.admitted){
          if(current.raw?.liveNewSessionAvailable!==true){
            nativeToast("Finish current operations before New Project");return;
          }
          if(!window.confirm("Start a new mixer? The current project is archived first. Output will remain muted."))return;
          retirePendingRackEditor();
          inlineFxClip?.closeRackEditors();
          backend.emitEvent("arMixAction",{action:"nativeMixerNewLive",confirmed:true,
            deviceEpoch:current.device.epoch,graphRevision:current.graph.revision,
            rosterRevision:current.graph.rosterRevision});
          status("Archiving current mixer · preparing New Project");
          return;
        }
        if(!logicalView||pendingLogical||hardware?.raw?.offlineNewSessionAvailable!==true){
          nativeToast("Disconnect audio and finish mixer operations before New Session");return;
        }
        if(mode==="RESET_IO"&&hardware?.raw?.offlineResetIoAvailable!==true){
          nativeToast("Reset requires a saved offline session with no logical port assignments");return;
        }
        if(!window.confirm(mode==="RESET_IO"?
          "Replace only logical I/O port definitions? Mixer, FX and shortcuts are retained; a backup is archived.":
          "Create a new offline mixer? The current saved session is archived. No audio output will be armed."))return;
        backend.emitEvent("arMixAction",{action:"nativeMixerNewOffline",mode,confirmed:true,
          workspaceToken:logicalView.workspaceToken,baseRevision:logicalView.graph.revision});
      });
      return button;
    });
    const archiveButton=item("button","tool","Open session archive");
    archiveButton.title="Open an archived mixer offline; current checkpoint is backed up first";
    archiveButton.addEventListener("click",()=>{
      if(!logicalView||pendingLogical||hardware?.raw?.offlineNewSessionAvailable!==true){
        nativeToast("Disconnect audio and finish mixer operations before opening an archive");return;
      }
      if(window.confirm("Open an archived session? Current saved mixer is archived first; output remains muted."))
        backend.emitEvent("arMixAction",{action:"nativeMixerOpenArchive",confirmed:true,
          workspaceToken:logicalView.workspaceToken,baseRevision:logicalView.graph.revision});
    });
    menu.append(caption,setup,muteBtn,hardwareNote,...newSessionButtons,archiveButton,
      healthTitle,tone,mark,report,advanced,
      showHeading,showSave,showBrowse,showSelect,showUpdate,showRecall,showRecover,showNote);
    // Single discoverable AUDIO I/O button next to ordinary mixer tools.
    // The existing More entry remains a shortcut to THIS SAME panel.
    const toolbar=document.querySelector(".command-bar");
    if(toolbar && !toolbar.querySelector("[data-native-io]")){
      const firstTool=toolbar.firstElementChild;
      for(const [caption,action,icon] of [
        ["Save","nativeMixerFileSave","mixer-save.svg"],
        ["Open","nativeMixerFileOpen","mixer-open.svg"]
      ]){
        const button=item("button","tool mixer-file-tool",caption);
        button.type="button";
        button.title=caption+" mixer (.smix)";
        const image=document.createElement("img");
        image.src="assets/"+icon;
        image.alt="";
        image.width=image.height=16;
        button.prepend(image);
        button.addEventListener("click",()=>backend.emitEvent("arMixAction",{action}));
        toolbar.insertBefore(button,firstTool);
      }
      // New leads the file group; reuse the same durable live/offline transaction.
      // Never implement reset by wiping UI state or removing a saved checkpoint.
      const newProject=item("button","tool mixer-file-tool","New");
      newProject.type="button";
      newProject.title="Create a new mixer; archive the previous session";
      newProject.dataset.nativeNewProject="true";
      const newIcon=document.createElement("img");
      newIcon.src="assets/mixer-new.svg";
      newIcon.alt="";
      newIcon.width=newIcon.height=16;
      newProject.prepend(newIcon);
      newProject.addEventListener("click",()=>newSessionButtons[0].click());
      toolbar.prepend(newProject);
      const io=item("button","tool","Audio I/O");
      io.dataset.nativeIo="true";
      io.title="Select ASIO/WASAPI driver and capture/playback device";
      io.addEventListener("click",openNativeAudioIO);
      const more=document.getElementById("moreTools");
      // The three channel/device commands are the last contiguous DOM group.
      toolbar.append(io,more);
      const player=document.getElementById("playerToggle"),plugins=document.getElementById("pluginsButton");
      if(player?.parentElement===toolbar&&plugins?.parentElement===toolbar)
        plugins.after(player);
      const addTrack=document.getElementById("addTrack");
      if(addTrack){
        addTrack.textContent="+ Audio Track";
        toolbar.insertBefore(addTrack,io);
      }
      syncAddTrack();
      // Fixed groups, not extra controls. Placement is DOM-only at startup;
      // no resize listeners, repaint loops or menu reparenting.
      for(const [name,element] of [
        ["files",document.getElementById("pluginsButton")],
        ["routing",document.getElementById("wireNavigatorToggle")],
        ["channels",addTrack]
      ]){
        if(!element || element.parentElement!==toolbar)continue;
        const divider=document.createElement("span");
        divider.className="armix-toolbar-divider";
        divider.dataset.nativeToolbarGroup=name;
        divider.setAttribute("aria-hidden","true");
        toolbar.insertBefore(divider,element);
      }
    }
    updateHardwarePanel(hardware);
  }

  // One control-thread routing transaction at a time, but NEVER lock other
  // dropdowns or lose their choices. Latest choice wins per channel.
  function dispatchQueuedSource(){
    dispatchSourceQueue("M4");
  }

  // This is ONLY an ephemeral visual gesture lease. PCM graph and .smix
  // remain entirely native-authoritative. The original V66 recoil must not
  // begin from a speculative drag, unsupported M4 route or rejected ACK.
  let pendingNativeWireRetract=null;
  function playAcceptedWireImpact(changes) {
    const held=pendingNativeWireRetract;
    if(held && performance.now()-held.createdAt<=8000 &&
       changes?.some(change=>
         String(change.from)===held.nativeSource &&
         ((change.op==="REMOVE" && String(change.edgeId)===held.nativeEdge) ||
          (change.op==="DISCONNECT" && held.primary)))) {
      // Readback has already projected the true graph; only now draw
      // the ORIGINAL 330ms V66 wire until its dot returns to source OUT.
      const stillConnected=scope?.wireEdges?.some(edge=>edge.enabled &&
        edge.sourceId===held.uiSource && edge.destId===held.oldDest);
      if(!stillConnected)
        scope?.animateAcceptedWireRetract?.(held.uiSource,held.point);
      pendingNativeWireRetract=null;
    }
    const plug=changes?.find(change=>["CONNECT","ROUTE_ONLY","RETARGET"].includes(change.op));
    if(!plug)return;
    const from=uiByNativeId.get(plug.from),to=uiByNativeId.get(plug.to);
    if(from===undefined||to===undefined||to===scope?.master?.id)return;
    scope?.kickWireSpring?.(from,to);
  }
  // The schema-v6 controller owns accepted snapshots. This single bridge
  // cleanup drops only outstanding WebView intents from an older device or
  // changed PCM authority; the native session, .smix, FX instances and
  // approved controls remain owned by JUCE and will be repainted by readback.
  let lastNativeLifecycleLease=null,lastWorkspaceGeneration;
  // G-MV24: ONE complete GUI-only discard path for both physical/authority
  // switches and repeated NEEDS_REMAP readbacks. No second PCM ownership.
  // Keep cosmetic readback signatures separate so retained-mixer telemetry
  // does not force pointless full DOM repaint on every callback.
  function discardTransientNativeUi({preserveRetainedIdentity=false}={}) {
    pendingNativeSources.clear();pendingSourceQueue.clear();
    activeNativeSource=null;nativeUiSourcePending=null;pendingSourceId=0;
    pendingOutputPatch=null;pendingNativeFxPower=null;pendingGlobalFx=null;
    pendingBusCreate=null;pendingTrackDelete=null;pendingFxInsert=null;
    pendingFxEditorAck=null; // expire old native HWND on device owner change
    startupWire=null;activeWireAttempt=null;pendingNativeGainIntent=false;
    latestInputGainDrafts.clear();latestLogicalInputGains.clear();
    pendingNativeWireRetract=null; // old output/epoch cannot animate a new cable
    pendingWireGains.clear();wireGainVisualTargets.clear();pendingNativeControl.clear();
    liveFaderVisualTargets.clear();lastNativeControlSent.clear();
    pendingM4Controls.clear();pendingM4RackPowers.clear();
    // MV-18: these two hardware-scoped leases were previously omitted.
    // A same-epoch driver/endpoint replacement or M4 authority change
    // must not strand EXTERNAL OUT selects or slot-bypass edits behind
    // an old ACK/timer. Never touch persisted PCM/FX state here.
    pendingExternal.clear();pendingSlotPowers.clear();busOutputPending=null;
    // Cosmetic ChannelId edits remain legal while native playback is
    // NEEDS_REMAP. They still require their own native ACK/readback proof.
    // Only physical/authority changes must invalidate those old identities.
    if(pendingIdentity&&!preserveRetainedIdentity){
      if(pendingIdentity.migration)legacyDeferred.add(pendingIdentity.channelId);
      rejectIdentity("Channel identity expired after audio connection change");
    }
  }
  function expireTransientNativeUi(reason) {
    discardTransientNativeUi();
    lastPaintedSignature="";lastNativeTopologySignature="";
    status(reason==="DEVICE_CHANGED"?
      "Audio connection changed · old control gestures discarded":
      "Mixer authority changed · old control gestures discarded");
  }
  function onNativeReadback(next,confirmed) {
    // This callback is invoked only by the existing schema-v6 reconciler.
    // No Device Setup, hardware panel or strip state may mutate until the
    // candidate read-model AND native device identity are admitted below.
    // Evaluate M4 against next itself, not previously painted hardware.
    const readModel=protocol.selectNativeReadModel(next,m4Verified(next));
    const session=next?.raw?.mixerSession;
    const offline=next?.raw?.logicalMixer;
    if(readModel.kind==="INVALID"){
      status("NATIVE READBACK INVALID · existing GUI controls unchanged");
      return;
    }
    const lease=deviceVM.nativeLifecycleLease(next,readModel.kind);
    if(!lease){
      status("NATIVE DEVICE IDENTITY INVALID · existing controls unchanged");
      return;
    }
    const lifecycle=deviceVM.nativeLifecycleDecision(lastNativeLifecycleLease,lease);
    if(lifecycle.expirePending){
      expireTransientNativeUi(lifecycle.kind);
      // Old readback confirmations must not animate or complete new PCM work.
      confirmed=null;
    }
    lastNativeLifecycleLease=lease;
    const workspaceGeneration=next.raw.workspaceGeneration;
    if(workspaceGeneration!==undefined && workspaceGeneration!==lastWorkspaceGeneration){
      if(lastWorkspaceGeneration!==undefined){
        scope?.cancelNativeSessionGestures?.();
        inlineFxClip?.closeRackEditors();
        discardTransientNativeUi();pendingLogical=null;logicalRenameId=null;
        // A same-device workspace restore also retires old I/O UI leases.
        // Never send compensating commands; native readback owns the actual output.
        extraPending=null;extraError="";
        pendingRetainedEditor=null;
        lastPaintedSignature="";lastNativeTopologySignature="";lastM4Controls="";
        lastM4UiSignature="";lastM4PortSignature="";
        confirmed=null;
      }
      lastWorkspaceGeneration=workspaceGeneration;
    }
    // MV-19: commit all hardware-scoped UI only after the common identity
    // fence. Invalid/inconsistent candidates must leave the last accepted
    // Device Setup transaction, selectors and physical output untouched.
    scope?.syncNativeWireGains?.([]); // flush accepted paint after pointer release
    const previousSetup=deviceState;
    deviceState=deviceVM.setupReadback(deviceState,next);
    if(deviceState!==previousSetup && !deviceState.pending){
      finishDeviceApplyUi(next);
      // G-MV21: only after installed device verification may the UI
      // request a fresh authoritative native inventory. A late ACK must
      // neither poison the operator's pin choices nor close Audio I/O.
      if(deviceState.phase==="ACCEPTED"&&connected)
        backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
    }
    updateHardwarePanel(next);
    // The MV-18 external-output repaint must stay after the same fence.
    paintExternalReadback(next);
    appearanceFromNative(next);
    settleSourceReadback("CHANNEL_ID",next);
    settleSourceReadback("M4",next);
    settleBusOutput(next);
    settleMonitorTiming(next);
    settleLogicalIo(next);
    settleIoDefaults(next);
    // Reconcile pending leases before the final matrix paint. The graph
    // can be installed while the prior paint still sees a busy SOURCE.
    paintPhysicalOutputs(next);
    requestNamedInventory(next);
    syncNamedStripOptions("input");
    if(readModel.kind==="LOGICAL_ONLY"||readModel.kind==="RETAINED_OFFLINE") {
      const staged=readModel.view;
      let revealCreatedId=null,wireImpact=null;
      if(logicalView&&logicalView.workspaceToken!==offline.workspaceToken)
        latestLogicalInputGains.clear();
      if(pendingLogical && pendingLogical.workspaceToken!==offline.workspaceToken)
        pendingLogical=null;
      let justInsertedLogicalFx=null;
      if(pendingLogical) {
        const p=pendingLogical;
        if(!p.ackRevision)return; // no optimistic pre-ACK projection
        if(protocol.cmp(offline.revision,p.ackRevision)<0)return;
        // A later unrelated revision cannot prove THIS request's exact edit.
        if(!protocol.logicalReadbackProves(p,offline.graph)){
          pendingLogical=null;
          latestLogicalInputGains.clear();
          lastPaintedSignature="";lastNativeTopologySignature="";
          status("Mixer change not verified · refreshing saved session");
          controller?.subscribe();
          return;
        }
        pendingLogical=null;
        if(p.operation==="ADD_FX_REFERENCE"&&p.beforeInsertCatalogs){
          justInsertedLogicalFx={channelId:p.channelId,
            slot:p.beforeInsertCatalogs.length,catalogId:p.payload.catalogId};
        }
        if(p.operation==="WIRE_TRANSACTION"&&offline.revision===p.ackRevision)
          wireImpact=p.payload.changes;
        if((p.operation==="CREATE_CHANNEL"||p.operation==="CREATE_GROUP") &&
           p.createdId)revealCreatedId=p.createdId;
        if(logicalRenameId===p.channelId&&p.operation==="RENAME_CHANNEL") {
          logicalRenameId=null;
          scope?.closeChannelIdentity?.();
        }
        status("Mixer changes saved · audio device not connected");
      }
      logicalView=staged;
      const signature="logical|"+offline.workspaceToken+"|"+offline.revision+"|"+JSON.stringify(
        offline.graph.channels.map(ch=>[ch.id,ch.name,ch.controls,ch.inserts,ch.fxRackBypassed]))+
        "|"+JSON.stringify(offline.graph.edges);
      if(signature!==lastPaintedSignature) {
        if(!projectNativeGraphWithoutFocusLoss(staged)){
          logicalView=null;latestLogicalInputGains.clear();
          status("Saved logical mixer readback invalid · controls unchanged");
          return;
        }
        lastPaintedSignature=signature;
      }
      if(justInsertedLogicalFx){
        const p=justInsertedLogicalFx;
        const inserted=staged.graph.channels.find(ch=>ch.id===p.channelId)
          ?.inserts?.[p.slot];
        if(inserted?.catalogId===p.catalogId&&inserted.id){
          scope?.closeNativePicker?.();
          openVerifiedInsertedFx(p.channelId,p.slot,p.catalogId,inserted.id);
        }
      }
      if(wireImpact)playAcceptedWireImpact(wireImpact);
      if(revealCreatedId) {
        const local=uiByNativeId.get(revealCreatedId);
        const strip=scope?.tracks?.find(t=>t.id===local);
        if(strip)scope?.revealTrack?.(strip);
      }
      document.documentElement.dataset.armixNativeRoster="logical";
      flags.acceptedGraphConnected=false;flags.productionCallbackLive=false;
      stopDemo();syncAddTrack(staged);
      if(pendingRetainedEditor?.acknowledged===true&&
         readModel.kind==="RETAINED_OFFLINE"&&
         pendingRetainedEditor.epoch===(offline.retainedCaptureDeviceEpoch??offline.retainedDeviceEpoch)&&
         pendingRetainedEditor.revision===(offline.retainedCaptureGraphRevision??offline.retainedGraphRevision)) {
        const held=pendingRetainedEditor;
        const fx=staged.graph.channels.find(ch=>ch.id===held.channelId)
          ?.inserts?.[held.slot];
        const row=staged.graph.channels.find(ch=>ch.id===held.channelId);
        if((held.empty&&row?.inserts?.length===held.slot)||(!held.empty&&fx?.catalogId===held.catalogId)) {
          pendingRetainedEditor=null;
          window.queueMicrotask(()=>{
            if(!logicalView||logicalView.workspaceToken!==staged.workspaceToken)return;
            const uiId=uiByNativeId.get(held.channelId);
            const track=[...(scope?.tracks||[]),scope?.master]
              .find(t=>t&&t.id===uiId);
            if(track&&held.action==="insertFx")
              nativeHooks.insertPlugin(held.selectedCatalogId,{channelId:track.id,slot:held.slot});
            else if(track)nativeHooks.consumeAction(track,held.action||"openPlugin",{
              dataset:{slot:String(held.slot),recenter:held.recenter?"true":"false"}
            });
          });
        } else {
          pendingRetainedEditor=null;
          status("Retained FX slot changed before offline editor could open");
        }
      }
      for(const id of ["addBus","addFx"]){
        const addBus=document.getElementById(id);if(!addBus)continue;
        addBus.hidden=false;
        addBus.disabled=!!pendingLogical||staged.readOnly||!staged.graph.capabilities.supportedOps.includes("CREATE_GROUP");
      }
      status(staged.readOnly?
        "Saved mixer recovery required · Open a verified .smix file":
        readModel.kind==="RETAINED_OFFLINE"?
        "FX OFFLINE · saved edits · reconnect and Apply Audio I/O":
        "Mixer ready to edit · connect Audio I/O to enable sound");
      // Drain only AFTER projection of the independently verified revision.
      // Never use the prior revision from the just-settled transaction.
      if(!pendingLogical){
        flushLogicalInputGains();
        if(pendingNativeControl.size)window.queueMicrotask(flushLiveScalars);
      }
      return;
    }
    logicalView=null;latestLogicalInputGains.clear();
    if(pendingRetainedEditor && readModel.kind!=="NEEDS_REMAP"){
      pendingRetainedEditor=null; // never follow a newly admitted PCM owner
    }
    if(pendingLogical) {
      pendingLogical=null;
      status("Audio connection changed · refresh mixer before editing");
    }
    if(readModel.kind==="STALE") {
      flags.acceptedGraphConnected=false;
      flags.productionCallbackLive=false;
      syncReceiverSumLevelAdmission(next);
      lastPaintedSignature="";
      delete document.documentElement.dataset.armixNativeRoster;
      status("AUDIO CONNECTION CHANGED · OPEN AUDIO I/O");
      return;
    }
    // Never paint unacknowledged controls; pending slider caps are kept
    // separately by pendingFaderVisualDb while accepted telemetry continues.
    // A pending scalar ACK is not a global snapshot gate. Only accepted producer
    // data can update controls; independent meters and FX racks keep moving.
    reconcileM4ControlReadback(next);
    reconcileM4RackReadback(next);
    reconcileM4ChannelPatchReadback(next);
    if(readModel.kind==="M4") {
      paintM4Fallback(next);
      // Only an independently admitted M4 producer snapshot can settle FX;
      // the native ACK's attached state is untrusted and never paints a rack.
      reconcileM4FxInsertReadback(next);
      if(scope?.syncVerifiedM4Wires?.(session.channels.map(ch=>ch.source),m4TrackIds))
        document.documentElement.dataset.armixNativeRoster="accepted";
    }
    // A second channel's IN can be selected while the first transaction is
    // in flight. Always dispatch its latest choice with a FRESH native route
    // revision, not stale tokens captured before the previous ACK. An old
    // M4 microtask must never be scheduled from retained hardware state.
    const transientPlan=deviceVM.nativeTransientReadbackPolicy(readModel.kind);
    if(transientPlan.drainM4Sources &&
       !nativeUiSourcePending && pendingSourceQueue.size)
      window.queueMicrotask(()=>dispatchQueuedSource());
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=readModel.kind==="CHANNEL_ID";
    if(pendingBusCreate && pendingBusCreate.epoch!==next.device.epoch)
      pendingBusCreate=null;
    syncAddTrack(next);
    if(pendingTrackDelete && pendingTrackDelete.epoch!==next?.device?.epoch)
      pendingTrackDelete=null;
    for(const id of ["addBus","addFx"]){
      const addBus=document.getElementById(id);if(!addBus)continue;
      addBus.hidden=false;
      addBus.disabled=!nativeAdmitted||!caps?.supportedOps?.includes("CREATE_GROUP")||!!pendingBusCreate||!!pendingTrackDelete;
    }
    if (transientPlan.drainNativeSources &&
         !activeNativeSource && pendingNativeSources.size) {
      // A rejected/timeout source command may leave the graph revision
      // unchanged. Drain the next distinct channel after this authoritative
      // snapshot even if the GUI signature is otherwise identical.
      window.queueMicrotask(dispatchNativeSource);
    }
    if (transientPlan.discardRetained) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      // Also catches stray same-lease transient edits after reconnect. This
      // silently discards ALL source, FX, WIRE and output gesture leases via
      // the same method used for authority/endpoint changes. Unlike expire,
      // it does not reset the accepted retained-graph paint signature.
      discardTransientNativeUi({preserveRetainedIdentity:true});
      const signature="retained|"+next.identity+"|"+appearanceRevision;
      if(lastPaintedSignature!==signature && projectNativeGraphWithoutFocusLoss(next)){
        lastPaintedSignature=signature;
        document.documentElement.dataset.armixNativeRoster="accepted";
      }
      syncReceiverSumLevelAdmission(next);
      stopDemo();
      paintNativeMeters(next);
      status(next.raw?.retainedOfflineFxPending?
        "FX offline edits saved · reconnect and Apply Audio I/O":
        "Playback unavailable · waiting for the audio device");
      return;
    }
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";lastNativeTopologySignature="";
      if(!m4Verified())delete document.documentElement.dataset.armixNativeRoster;
      stopDemo();
      status(m4Verified()?
        "NATIVE M4 · real PCM faders and meters · ChannelId buses not yet admitted":
        deviceReady()?
        "NATIVE G0 · verified physical IN ready · select a channel input":
        hardware?.device?.enabledInputs?.length===0?
        "NATIVE MIXER UNAVAILABLE · verify playback in Audio I/O":
        "NATIVE G0 NOT READY · open Audio I/O and verify device / native route");
      return;
    }
    // Control readback may change at the SAME structural graph revision.
    // Compare the actual accepted controls/edges, never skip native fader updates.
    // GLOBAL A/B is a realtime SCALAR with unchanged graph revision and
    // unchanged saved insert flags. Include its accepted PCM readback so
    // bypassing every FX repaints each mounted rack without rebuilding DOM.
    const signature=next.identity+"|"+String(next.raw?.globalFxBypassed)+"|"+
      JSON.stringify(next.graph.channels.map(ch=>[
      ch.id,ch.controls,ch.inserts,ch.fxRackBypassed,ch.primaryOutId]))+"|"+
      JSON.stringify(next.graph.edges)+"|"+appearanceRevision;
    if (signature===lastPaintedSignature) {
      // A capability/readiness transition may retain the structural
      // signature. Keep the WIRE receiver admission correct without repaint.
      syncReceiverSumLevelAdmission(next);
      // Telemetry-only snapshots should not traverse all channels. But an
      // expired/rejected edit with unchanged graph values must not strand its
      // LCD at a speculative value; error status settles that separately.
      finishNativeTrack(next);finishNativeTrackDelete(next);
      if(extraPending){settleAdditionalOutput(extraLive);paintAdditionalOutput();}
      // Telemetry often advances without a structural graph revision.
      paintNativeMeters(next);
      if(startupWire)window.queueMicrotask(flushStartupWire);
      return;
    }
    if (!projectNativeGraphWithoutFocusLoss(next)) {
      flags.acceptedGraphConnected=false;flags.productionCallbackLive=false;
      paintAdditionalOutput();
      stopDemo();status("NATIVE READBACK INVALID · existing GUI controls unchanged");
      return;
    }
    document.documentElement.dataset.armixNativeRoster="accepted";
    flags.productionCallbackLive=next.productLive;flags.acceptedGraphConnected=true;
    syncReceiverSumLevelAdmission(next);
    // G-MV30: first accepted ChannelId roster is projected only NOW.
    // The earlier pre-projection syncAddTrack() was fail-closed while the
    // native graph was unmounted; enable the button only after successful
    // projection, without requiring a second telemetry callback/click.
    syncAddTrack(next);
    syncEmergencyOutputControl(next.device);
    // Refresh source/output shortcuts only after authoritative projection.
    settleAdditionalOutput(extraLive);
    paintAdditionalOutput();
    lastPaintedSignature=signature;stopDemo();paintNativeMeters(next);
    if(confirmed?.operation==="APPLY_WIRE_TRANSACTION")
      playAcceptedWireImpact(confirmed.payload.changes);
    finishNativeTrack(next);finishNativeTrackDelete(next);
    const fxInsert=fxRackVM.nativeInsertSettlement(pendingFxInsert,next,confirmed,
      protocol.nativeHardwareConfiguration(next?.device));
    if(fxInsert.kind==="STALE")pendingFxInsert=null;
    if(fxInsert.kind==="CONFIRMED"){
      pendingFxInsert=null;
      scope?.closeNativePicker?.();
      openVerifiedInsertedFx(fxInsert.channelId,fxInsert.slot,
        fxInsert.catalogId,fxInsert.instanceId);
      status("VST3 loaded · "+fxInsert.name+" · native PCM readback verified");
    }
    const fxSettlement=fxRackVM.nativePowerSettlement(pendingNativeFxPower,next,
      protocol.nativeHardwareConfiguration(next?.device));
    if(fxSettlement==="CONFIRMED"||fxSettlement==="STALE")
      pendingNativeFxPower=null;
    if(startupWire)window.queueMicrotask(flushStartupWire);
    if(next.raw?.retainedOfflineFxPending)
      status("Saved offline FX changes pending · Apply Audio I/O before output");
    else status("Mixer settings updated");
  }
  if (connected) {
    controller = protocol.createController({
      emit:(name,payload)=>backend.emitEvent(name,payload),
      onAccepted:(next,confirmed)=>{
        settleWireGainVisuals(next,confirmed);
        onNativeReadback(next,confirmed);
        if(confirmed?.operation==="SET_WIRE_GAIN")paintSofGainReadback(next);
        const accepted=controller?.state()?.current;
        if(fxRackVM.nativeEditorAckResult(pendingFxEditorAck,null,accepted,{
          verifiedM4:m4Verified(accepted),offline:!!logicalView,
          hardwareConfiguration:protocol.nativeHardwareConfiguration(accepted?.device)})==="STALE"){
          const p=pendingFxEditorAck;
          const fresh=p?.authority==="CHANNEL_ID"&&!logicalView&&
            fxRackVM.refreshEditorNavigationLease(accepted,p.lease,{
              hardwareConfiguration:protocol.nativeHardwareConfiguration(accepted?.device)});
          if(fresh)p.lease=fresh; // same owner only; HWND feedback is not PCM proof
          else{
            pendingFxEditorAck=null;
            nativeToast("FX editor unavailable · accepted instance changed");
          }
        }
        syncGlobalFxReadback();
        inlineFxClip?.syncBypass(activeView());
      },
      onStatus:state=>{
        const settledGain=pendingNativeGainIntent&&!state.pending;
        if(settledGain){
          pendingNativeGainIntent=false;
          if(state.lastError!=="ACCEPTED_READBACK"){
            latestInputGainDrafts.clear(); // invalid lease: do not replay
            reconcileAcceptedInputGainSnapshot(state.current);
          }
        }
        if(!state.pending&&state.lastError==="ACCEPTED_READBACK"&&
           latestInputGainDrafts.size) scheduleLiveInputGainFlush();
        if(state.lastError==="PENDING_NATIVE_ACK"||state.lastError==="PENDING_ACCEPTED_SNAPSHOT")
          status("NATIVE "+state.lastError+" · awaiting authoritative readback");
        if(!state.pending && state.lastError==="ACCEPTED_READBACK" &&
           pendingNativeControl.size)queueLiveControlFlush();
        if(!state.pending&&state.lastError==="ACCEPTED_READBACK"&&pendingWireGains.size)
          queueWireGainFlush();
        // Pure ViewModel decides retry/rollback; one native controller.
        const recovery=wireVM.nativeRecoveryPlan(
          state,activeWireAttempt,startupWire);
        if(recovery.kind==="ACCEPTED"&&startupWire)
          window.queueMicrotask(flushStartupWire);
        if(recovery.kind==="ACCEPTED")activeWireAttempt=null;
        if(recovery.kind==="FLUSH_STARTUP_RETRY"){
          window.queueMicrotask(flushStartupWire);return;
        }
        if(recovery.kind==="REBASE_CONNECTION"){
          const wire=activeWireAttempt;activeWireAttempt=null;
          // Existing bridge rechecks exact admission before one retry.
          if(deferStartupWire(wire.ops,1))return;
        }
        if(recovery.kind==="ROLLBACK"||
           recovery.kind==="REBASE_CONNECTION"){
          pendingNativeWireRetract=null; // rejected routing must never reel
          activeWireAttempt=null;
          startupWire=null;
          pendingWireGains.clear();wireGainVisualTargets.clear();
          if(state.current)paintSofGainReadback(state.current);
          // The controller deliberately freezes current on a rejected ACK.
          // Repaint only an inspector that still says PENDING from that
          // admitted PCM readback, without a new intent, timer or graph edit.
          if(state.current)
            scope?.rollbackNativeWireInspector?.(nativeWireGainReadback(state.current));
          if(pendingFxInsert?.authority==="CHANNEL_ID"){
            pendingFxInsert=null;
            nativeToast("VST3 insert rejected · "+state.lastError);
          }
          for(const [key,edit] of pendingNativeControl)
            if(edit.authority==="CHANNEL_ID")pendingNativeControl.delete(key);
          for(const [key,edit] of liveFaderVisualTargets)
            if(edit.authority==="CHANNEL_ID")liveFaderVisualTargets.delete(key);
          // A rejected native graph invalidates its old send-time leases.
          // M4 and offline remain readback-owned; next gesture can send now.
          lastNativeControlSent.clear();
          lastNativeError=state.lastError;
          // Never destroy an active pointer capture. Once the user releases,
          // restore only the last independently accepted native controls.
          if(flags.acceptedGraphConnected && (scope.activeFaderId??-1)<0){
            // A rejected scalar GAIN requires only the accepted LCD/needle
            // repaint. Do not rebuild strips (or detach another active drag).
            const activeGain=document.querySelector('input[data-field="trim"][data-gain-dragging="true"]');
            if(!settledGain&&!activeGain&&!scope.wireGainDragging &&
               !document.activeElement?.matches?.(".strip select,.wire-gain,#wireWsGain"))
              scope.render?.();
            controller.subscribe();
            if(state.current)paintNativeMeters(state.current);
          }
        }
      }
    });
    // Reuse existing JUCE 12 Hz status transport; only a single event
    // each second contains the native health sequence/CPU/RAM sample.
    // Rendering changes ONLY two existing footer spans; no new DOM,
    // browser polling, requestAnimationFrame or audio callback work.
    backend.addEventListener("arMixSecondaryOutputInventory",inventory=>{
      if(!extraInventoryPending||inventory?.requestId!==extraInventoryPending||!Array.isArray(inventory.outputs))return;
      extraInventory=[...new Set(inventory.outputs.filter(x=>typeof x==="string"&&x.length>0&&x.length<=240))];
      const oldPreferred=extraPreferred;
      extraPreferred=typeof inventory.preferredOutput==="string"?inventory.preferredOutput:"";
      extraHasSaved=inventory.hasSavedOutput===true;
      if(extraHasSaved&&!extraPreferred&&extraSelected===oldPreferred)extraSelected="";
      if(!extraInventory.includes(extraSelected))extraSelected="";
      extraInventoryId=inventory.requestId;
      extraInventoryPending="";
      extraInventoryKnown=!inventory.error;
      extraInventoryRetryAt=performance.now()+5000;
      if(inventory.error)extraError=String(inventory.error);
      paintAdditionalOutput();
    });
    backend.addEventListener("arMixSecondaryOutputAck",ack=>{
      if(!extraPending||ack?.requestId!==extraPending.requestId||ack.action!==extraPending.action)return;
      if(ack.ok!==true){
        const quick=extraPending.quick;
        extraPending=null;
        extraError=String(ack.error||"Additional output rejected");
        if(quick)nativeToast(extraError);
        if(/list changed|identity changed|endpoint missing/i.test(extraError)){
          extraSelected="";extraInventoryKnown=false;
          refreshAdditionalInventory(true);
        }
      }else {
        extraPending.acked=true;
        if(extraPending.action==="secondaryMonitorStarter"||extraPending.action==="secondaryOutputSplit")
          extraPending.ackRevision=String(ack.graphRevision||"");
        // A fresh status and accepted graph may already have arrived while
        // the native action was in flight; never wait for an extra packet.
        settleAdditionalOutput(extraLive);
      }
      paintAdditionalOutput();
    });
    backend.addEventListener("arMixStatus", live=>{
      const previous=protocol.secondaryOutputView(hardware,extraLive);
      extraLive=live;++extraStatusSerial;
      settleAdditionalOutput(live);
      paintAdditionalOutput();
      const current=protocol.secondaryOutputView(hardware,live);
      if(!extraAutoInventoryStarted&&mainMonitorAllowed()&&document.querySelector("[data-main-monitor]")){
        extraAutoInventoryStarted=true;
        if(!extraInventoryKnown&&!extraInventoryPending)refreshAdditionalInventory();
      }
      // Reuse existing status cadence, no interval or hidden-window scan.
      if(extraSurfaceOpen()&&!extraInventoryPending&&!extraPending&&!current.armed&&!current.recovering&&
         (!extraInventoryKnown||!extraInventory.length||(extraHasSaved&&!extraPreferred))&&
         performance.now()>=extraInventoryRetryAt){
        extraInventoryRetryAt=performance.now()+5000;
        refreshAdditionalInventory();
      }
      if(previous.name!==current.name||previous.state!==current.state||previous.split!==current.split)
        paintOutputOverview(hardware);
      const health=protocol.footerHealthFromLive(live);
      if(!health || health.sequence===lastFooterHealth?.sequence)return;
      lastFooterHealth=health;
      lastM4Performance=protocol.smartPerformanceView(live,hardware,lastM4Performance?.cursor);
      if(hardware){paintAudioHealthFooter(hardware);paintSmartPerformance();}
    });
    backend.addEventListener("arMixConsoleSnapshot", snapshot=>{
      // Only the sole v6 controller may admit a PCM snapshot. Its
      // onAccepted callback updates FX after validation; this listener
      // must not settle an ACK using a cached pre-ACK readback if the
      // incoming payload was rejected as invalid/stale.
      const apply=()=>controller.onSnapshot(snapshot);
      const profiler=window.ArSonMixPerf;
      if(!profiler?.active){apply();return;}
      const started=performance.now();
      try{apply();}
      finally{profiler.sample("nativeSnapshotJsMs",performance.now()-started);}
    });
    backend.addEventListener("arMixConsoleAck", ack=>controller.onAck(ack));
    backend.addEventListener("arMixRetainedFxOfflineAck",ack=>{
      if(!pendingRetainedEditor||ack?.requestId!==pendingRetainedEditor.requestId)return;
      if(ack.ok!==true){
        const error=ack?.error||"Retained FX recovery rejected";
        pendingRetainedEditor=null;
        status("Offline FX unavailable · "+error);
        return;
      }
      pendingRetainedEditor.acknowledged=true;
      controller.subscribe(); // only accepted native logical readback may open UI
    });
    backend.addEventListener("arMixLogicalAck",ack=>{
      const pending=pendingLogical;
      if(!pending||pending.requestId!==ack?.requestId)return;
      if(ack.status!=="ACCEPTED"||ack.errorCode!=="NONE"||
         protocol.decimal(ack.revision)===null ||
         BigInt(ack.revision)<=BigInt(pending.beforeRevision)){
        pendingLogical=null;
        latestLogicalInputGains.clear();
        lastPaintedSignature="";lastNativeTopologySignature="";
        status("Mixer edit rejected · previous saved settings retained");
        controller?.subscribe();
        return;
      }
      pending.ackRevision=ack.revision;
      pending.createdId=ack.createdId||null;
      // An ACK does NOT establish readback, let alone PCM.
      controller?.subscribe();
    });
    backend.addEventListener("arMixChannelIdentityAck", ack=>{
      const decision=stripVM.identityAckResult(pendingIdentity,ack);
      if(decision==="IGNORE")return;
      if(decision==="REJECTED"){
        if(pendingIdentity.migration)legacyDeferred.add(pendingIdentity.channelId);
        rejectIdentity("Native channel identity rejected · "+String(ack.error||"Unknown error"));
      }else pendingIdentity.acked=true;
      // ACK alone cannot change an editor; the next accepted native
      // readback, same graph lease and appearance identity must prove it.
      controller?.subscribe();
    });
    backend.addEventListener("arMixNativeSourceAck",ack=>{
      handleSourceAck("CHANNEL_ID",ack);
    });
    backend.addEventListener("arMixNativeBusAck",ack=>{
      const pending=pendingBusCreate;
      const verdict=stripVM.rosterAckResult(pending,ack);
      if(verdict==="IGNORE")return;
      const label=pending.track?"Audio Track":"BUS";
      if(verdict==="ACKED"){
        pending.acked=true;
        pending.createdId=ack.createdId;
        pending.ackRevision=ack.graphRevision;
        status(label+" ACKed · verifying installed native roster");
        finishNativeTrack(controller?.state()?.current);
      }else{
        pendingBusCreate=null;
        nativeToast("Add "+label+" rejected · "+(ack?.error||"native graph error"));
      }
      syncAddTrack();
      controller?.subscribe();
    });
    backend.addEventListener("arMixNativeTrackDeleteAck",ack=>{
      const pending=pendingTrackDelete;
      const verdict=stripVM.rosterAckResult(pending,ack);
      if(verdict==="IGNORE")return;
      if(verdict==="REJECTED"){
        pendingTrackDelete=null;syncAddTrack();
        nativeToast("Delete rejected · "+(ack?.error||"native graph rejected"));
      }else{
        pending.acked=true;
        pending.ackRevision=ack.graphRevision;
        status(pending.name+" removal ACKed · verifying accepted graph");
        finishNativeTrackDelete(controller?.state()?.current);
      }
      controller?.subscribe();
    });
    backend.addEventListener("arMixGlobalFxAck",ack=>{
      const verdict=fxRackVM.globalPowerAckResult(pendingGlobalFx,ack);
      if(verdict==="IGNORE")return;
      if(verdict==="REJECTED"){
        pendingGlobalFx=null;
        status("Global FX rejected · "+String(ack?.error||"stale device revision"));
      }else pendingGlobalFx.acked=true;
      controller?.subscribe(); // ACK alone never establishes physical bypass.
    });
    backend.addEventListener("arMixNativeFxPowerAck",ack=>{
      const verdict=fxRackVM.nativePowerAckResult(pendingNativeFxPower,ack);
      if(verdict==="IGNORE")return;
      if(verdict==="REJECTED"){
        pendingNativeFxPower=null;
        status("NATIVE FX rejected · "+(ack?.error||"stale epoch or insert"));
      }else pendingNativeFxPower.acked=true;
      controller.subscribe(); // ACK is not independently observed PCM state
    });
    backend.addEventListener("arMixRoutingFxRackAck",ack=>{
      const p=pendingM4RackPowers.get(ack?.channel);
      const verdict=fxRackVM.m4RackAckResult(p,ack);
      if(verdict==="IGNORE")return;
      if(verdict==="REJECTED"){
        pendingM4RackPowers.delete(p.channel);
        status("Native M4 rack gate rejected · "+(ack?.error||"stale route"));
      }else p.acked=true;
      controller.subscribe(); // later authoritative M4 snapshot required
    });
    backend.addEventListener("arMixSourceSwitchAck", ack=>{
      if(handleSourceAck("M4",ack))return;
      if(!pendingSourceId||ack?.requestId!==pendingSourceId)return;
      pendingSourceId=0;
      if(ack.ok===true) {
        status("NATIVE: CH1 PATCH accepted by JUCE; validating later readback.");
        controller.subscribe();
      }else status("NATIVE PATCH REJECTED: "+(ack?.error||"stale physical route"));
      updateHardwarePanel(hardware);
    });
    backend.addEventListener("arMixM4ControlAck",ack=>{
      const item=[...pendingM4Controls].find(([,p])=>p.requestId===ack?.requestId);
      if(!item)return; // late, unrelated and superseded ACKs cannot commit
      const [key,p]=item;
      const verdict=stripVM.m4ScalarAckResult(p,ack);
      if(verdict==="REJECTED") {
        pendingM4Controls.delete(key);
        if(p.parameter==="masterGain")liveFaderVisualTargets.delete("masterGain:main");
        else if(p.parameter==="faderDb")
          liveFaderVisualTargets.delete("faderDb:"+m4TrackIds[p.channel]);
        status("NATIVE M4 REJECTED · "+(ack?.error||"stale control token"));
      } else if(verdict==="ACKED")p.acked=true;
      controller.subscribe(); // request later independent producer readback
    });
    backend.addEventListener("arMixChannelPatchAck",ack=>{
      const verdict=stripVM.m4ChannelPatchAckResult(pendingOutputPatch,ack);
      if(verdict==="IGNORE")return;
      if(verdict==="REJECTED"){
        const p=pendingOutputPatch;
        pendingOutputPatch=null;
        resetM4PatchUi(p);
        status("CH "+(p.channel+1)+" "+
          (p.operation==="SET_PROCESSING_FORMAT"?"M/ST":"OUT")+
          " rejected · "+(ack?.error||"stale route"));
        nativeToast(ack?.error||"Native channel route rejected");
      }else{
        pendingOutputPatch.acked=true;
        // Native ACK may arrive AFTER a separately accepted M4 snapshot.
        reconcileM4ChannelPatchReadback(controller?.state()?.current);
      }
      controller?.subscribe(); // no ACK state is accepted PCM
    });
    backend.addEventListener("arMixRoutingFxBypassAck",ack=>{
      const key=Number(ack?.channel)+":"+Number(ack?.slot);
      // The matched ACK only permits a new independently accepted M4
      // snapshot to settle the exact pending instance and bypass value.
      const pending=pendingSlotPowers.get(key);
      const verdict=fxRackVM.m4SlotPowerAckResult(pending,ack);
      if(verdict==="IGNORE")return;
      if(verdict==="REJECTED"){
        pendingSlotPowers.delete(key);
        nativeToast(ack?.error||"Native VST3 bypass rejected");
      }else pending.acked=true;
      controller?.subscribe();
    });
    backend.addEventListener("arMixRoutingFxEditorAck",ack=>{
      const accepted=controller?.state()?.current;
      const verdict=fxRackVM.nativeEditorAckResult(pendingFxEditorAck,ack,accepted,{
        verifiedM4:m4Verified(accepted),offline:!!logicalView,
        hardwareConfiguration:protocol.nativeHardwareConfiguration(accepted?.device)});
      if(verdict==="IGNORE")return; // obsolete, duplicate or unrelated ACK
      const pending=pendingFxEditorAck;
      if(!pending||ack?.requestId!==pending.requestId||pending.retryScheduled)return;
      if(verdict==="REJECTED"&&ack?.instanceId===pending.lease.instanceId&&
         ack?.retryable===true&&retryPhysicalFxEditor(pending))return;
      pendingFxEditorAck=null; // first ACK wins; no accepted PCM mutation
      if(verdict!=="ACKED"){
        const message="FX editor unavailable · "+String(ack?.error||"accepted instance changed");
        nativeToast("FX editor unavailable · Reopen from FX Rack");
        status(message);
      }
      // Success is HWND presentation, never a parameter edit.
    });
    backend.addEventListener("arMixRoutingFxInsertAck",ack=>{
      const verdict=fxRackVM.m4InsertAckResult(pendingFxInsert,ack);
      if(verdict==="IGNORE")return; // old request cannot settle newer FX
      if(verdict==="REJECTED"){
        pendingFxInsert=null;
        const reason=ack?.error||"native FX insertion rejected";
        status("FX INSERT REJECTED · "+reason);
        nativeToast(reason);
      }else{
        pendingFxInsert.acked=true;
        // ACK may arrive after independent PCM readback. Check the already
        // accepted snapshot now; never use ack.state as the mixer source.
        reconcileM4FxInsertReadback(controller?.state()?.current);
      }
      controller?.subscribe();
    });
    backend.addEventListener("arMixCatalog",catalog=>{
      if(!Array.isArray(catalog?.items))return;
      nativeFxCatalogScanning=catalog.scanning===true;
      nativeCatalog=new Map(catalog.items.filter(p=>
        p&&typeof p.id==="string"&&p.id.length>0&&
        typeof p.name==="string"&&p.name.length>0).map(p=>[p.id,p]));
      if(Array.isArray(catalog.builtIns))
        nativeBuiltins=catalog.builtIns.filter(fx=>fx && typeof fx.id==="string" &&
          typeof fx.name==="string" && fx.id.length && fx.name.length)
          .map(fx=>({...fx,vendor:"ArSonMix",format:"Native",source:"builtin"}));
      presentFxCatalog();
      if(nativeFxDiscoveryRequested&&!nativeFxCatalogScanning&&nativeCatalog.size===0)
        status("Built-in FX ready · import VST3 to add more effects");
      if(hardware?.raw?.legacyM4)paintM4FxInstances(hardware.raw.legacyM4);
      const count=document.getElementById("pluginCount");
      if(count)count.title="Native built-in DSP + verified VST3 catalog";
    });
    backend.addEventListener("arMixScanProgress",progress=>{
      const statusEl=document.getElementById("pluginScanStatus");
      if(statusEl)statusEl.textContent=progress?.active?
        "Scanning real VST3… "+(progress.done||0)+"/"+(progress.total||"…"):
        "Native VST3 scan finished; results from JUCE catalog";
    });
    backend.addEventListener("arMixNotice",notice=>{
      // ConsoleWindow::emitNotice publishes {text,error}, never {message}.
      // The mismatched field hid native VST3 layout/latency, driver and
      // routing failures behind rapid meter/readback status updates.
      const message=notice?.text;
      if(notice?.error===true && typeof message==="string" && message.length){
        nativeToast(message); // physical FX/driver failures remain visible
        status("JUCE · "+message);
      }
    });
    backend.addEventListener("arMixAudioArmAck", ack=>{
      if(ack?.ok===true){
        status(ack.armed?"NATIVE: user ARM accepted; checking real PCM.":
          "NATIVE: output muted.");
        controller.subscribe();
      }else status("NATIVE ARM REJECTED: "+(ack?.error||"device not ready"));
    });
  }
  // G-MV27: UI actions (MUTE/SOLO/unity resets) share one pure native
  // authority plan, separate from MV-10's coalesced pointer gesture lane.
  // IPC, request IDs, ACK/readback and the PCM owner remain unchanged.
  function controlCommandRoute(operation,t) {
    const current=controller?.state()?.current,id=nativeIdFor(t);
    return stripVM.controlCommandPlan({
      operation,trackKind:t?.kind,logical:!!logicalView,
      logicalReadOnly:logicalView?.readOnly===true,
      logicalPending:!!pendingLogical,logicalChannelId:id,
      logicalSupported:logicalView?.graph?.capabilities?.supportedOps?.includes(operation)===true,
      authority:mixerSession()?.authority,admitted:current?.admitted===true &&
        current?.retained!==true,graphConnected:flags.acceptedGraphConnected,
      nativeChannelId:id,
      nativeSupported:current?.graph?.capabilities?.supportedOps?.includes(operation)===true,
      m4Verified:m4Verified(),m4Channel:m4Index(t),
      masterAuthority:t?.kind==="master"?nativeMasterAuthority():"UNAVAILABLE"
    });
  }
  function dispatchControlCommand(operation,t,payload) {
    const route=controlCommandRoute(operation,t);
    if(route==="LOGICAL"||route==="CHANNEL_ID")
      return intent(operation,t,payload);
    if(route==="M4"||route==="MAIN_HOST"){
      const parameter=route==="MAIN_HOST"?
        operation==="SET_FADER"?"masterGain":"masterMute":
        {SET_FADER:"faderDb",SET_PAN:"pan",SET_MUTE:"mute",SET_SOLO:"solo"}[operation];
      const value=operation==="SET_FADER"?payload?.valueDb:
        operation==="SET_PAN"?payload?.value*100:payload?.enabled;
      return sendM4Control(parameter,t,value);
    }
    status("Native "+operation+" unavailable · verify mixer authority/readback");
    return false;
  }
  function rackFileTarget(track){
    const state=controller?.state()?.current,channelId=nativeIdFor(track);
    const channel=state?.graph?.channels?.find(ch=>ch.id===channelId);
    if(!state?.admitted||state?.retained||logicalView||mixerSession()?.authority!=="CHANNEL_ID"||
       !channel||!["INPUT","GROUP","FX_RETURN","MAIN"].includes(channel.role))return null;
    return {channelId,channel,deviceEpoch:state.device?.epoch,
      graphRevision:state.graph?.revision,rosterRevision:state.graph?.rosterRevision};
  }
  function requestRackFile(track,operation,slot,token){
    const target=rackFileTarget(track);
    if(!target||!["SAVE","OPEN","INSERT","DEACTIVATE"].includes(operation)){
      nativeToast("FX rack file unavailable · verify current native rack");return false;
    }
    const {channel,...identity}=target;
    if(operation==="DEACTIVATE"&&(!Number.isInteger(slot)||channel.inserts?.[slot]?.id!==token)){
      nativeToast("FX changed · reopen rack menu");return false;
    }
    // Rack topology may replace or shift slot-relative inline editor leases.
    if(operation!=="SAVE"){
      retirePendingRackEditor([identity.channelId]);
      inlineFxClip?.closeRackEditors([identity.channelId]);
    }
    backend.emitEvent("arMixAction",{action:"fxRackFile",operation,...identity,
      slot:operation==="DEACTIVATE"?slot:-1,instanceId:operation==="DEACTIVATE"?token:""});
    return true;
  }
  const nativeHooks=Object.freeze({
    rackFileTarget,rackFile:requestRackFile,
    paintMainMonitor, // synchronous presentation only; no native command
    enabled:connected,
    requestDeleteTrack:requestNativeTrackDelete,
    requestDeleteStrip:requestNativeStripDelete,
    canDeleteStrip:canDeleteNativeStrip,
    renameStrip:openNativeIdentity,
    orbitTarget:nativeOrbitTarget,
    canConvertReverbReturn:dmxReverbConversionTarget,
    convertReverbReturn:convertDmxReverb,
    orbitAction:nativeOrbitAction,
    inputChoices(t) {
      const labels=stripVM.inputChoices(t,sourceChoicesFor(),
        controller?.state()?.current?.retained,!(!logicalView),INTERNAL_BUS);
      return t?.kind==="audio"?[...labels,...stripNamedChoices("input").map(p=>p.label)]:labels;
    },
    externalChoices(t) {
      const labels=stripVM.externalChoices(t,hardwareOutputPairs());
      return t?.kind!=="master"?[...labels,...stripNamedChoices("output").map(p=>p.label)]:labels;
    },
    outputChoices(t) {
      const choices=stripVM.primaryChoices({track:t,logical:Boolean(logicalView),
        retained:Boolean(hardware?.retained),
        playbackDevice:hardware?.device?.playbackDevice,
        authority:mixerSession()?.authority,
        targets:nativeOutputTargets(t)});
      return t?.kind==="master"?[...choices,"Configure audio output…"]:choices;
    },
    consumeField(event) {
      if (!connected)return false;
      const el=event.target,field=el?.dataset?.field;
      if (!field)return false;
      const t=scope?.findTrack?.(el) || null;
      if (!t)return true;
      if(field==="input"&&event.type==="change"&&!deviceReady()){
        recommendAudioRepair("INPUT requires an audio device");el.value=String(t.input);return true;
      }
      if(controller?.state()?.current?.retained&&!logicalView){
        if(field==="input"&&event.type==="change")
          recommendAudioRepair("INPUT audio device disconnected");
        if(field==="output" && t.kind==="master" && event.type==="change")
          openNativeAudioIO();
        if(field==="level" || field==="pan" || field==="input" || field==="output")
          el.value=String(t[field]);
        return true;
      }
      if(field==="trim"){
        const accepted=t.kind==="audio"&&t.nativeGainAvailable===true&&
          (logicalView||controller?.state()?.current?.admitted===true);
        const raw=Number(el.value);
        if(!accepted||!Number.isFinite(raw)||raw< -24||raw>24){
          el.value=String(Number.isFinite(t.trim)?t.trim:0);
          if(!logicalView&&!deviceReady())recommendAudioRepair("GAIN unavailable");
          status("INPUT GAIN unavailable · native PCM admission required");
          return true;
        }
        // Shared reference/native UI detent. Only accepted readback owns PCM.
        const n=window.__ARMIX_GAIN_UNITY_DETENT__?.(el,event)??raw;
        // Input gain follows the pointer just like a DAW. Only one bounded
        // latest-writer scalar may await the native ACK/readback at a time;
        // the knob preview is never mistaken for confirmed PCM.
        if(event.type!=="change"){
          el.closest(".gain-dial")?.style.setProperty("--gain-rotation",
            (n*5.625).toFixed(2)+"deg");
          const lcd=el.closest(".gain-view-body")?.querySelector(".gain-readout");
          const digits=lcd?.querySelector(".gain-lcd-digits")||lcd;
          if(digits)digits.textContent=(n>=0?"+":"")+n.toFixed(1)+" dB";
          requestLiveInputGain(t,n,false);
          return true;
        }
        if(!requestLiveInputGain(t,n,true)){
          const acceptedDb=Number.isFinite(t.trim)?t.trim:0;
          el.value=String(acceptedDb);
          el.closest(".gain-dial")?.style.setProperty("--gain-rotation",
            (acceptedDb*5.625).toFixed(2)+"deg");
          const lcd=el.closest(".gain-view-body")?.querySelector(".gain-readout");
          const digits=lcd?.querySelector(".gain-lcd-digits")||lcd;
          if(digits)digits.textContent=(acceptedDb>0?"+":"")+acceptedDb.toFixed(1)+" dB";
          status("INPUT GAIN rejected · restoring accepted PCM readback");
          controller?.subscribe?.();
        }
        return true;
      }
      if(field==="level"&&scope?.sofEnabled&&nativeSofSource(t)){
        if(!el.isConnected||el.disabled)return true;
        const edge=selectedNativeSofEdge(t),db=Number(el.value);
        const uiId=edge&&uiByWireId.get(edge.edgeId);
        if(!edge||uiId===undefined||!Number.isFinite(db)||db< -60||db>6){
          status("SENDS ON FADERS · patch a selected WIRE destination first");
          scope?.syncFaderVisual?.(t);return true;
        }
        if(!requestWireGain({sourceId:t.id,edgeId:uiId,levelDb:db})){
          status("SEND LEVEL rejected · accepted WIRE identity changed");
          scope?.syncFaderVisual?.(t);
          return true;
        }
        previewFaderWithoutModelMutation(t,db);
        return true;
      }
      if(field==="wireChannelLevel"){
        const db=Number(el.value);
        if(!requestLiveWireSumGain(t,db,event.type==="change")){
          if(!deviceReady())recommendAudioRepair("WIRE SUM LVL unavailable");
          status("WIRE SUM LVL unavailable · verify receiver and Audio I/O");
          if(Number.isFinite(t.wireSumGainDb)){
            el.value=String(t.wireSumGainDb);scope?.paintWireKnob?.(el,t.wireSumGainDb);
          }
          return true;
        }
        scope?.paintWireKnob?.(el,db);
        return true;
      }
      if(field==="wireGain"){
        const edge=scope?.wireEdges?.find(e=>e.id===Number(el.dataset.wireId)&&e.destId===t.id);
        if(!edge||!requestWireGain({sourceId:edge.sourceId,edgeId:edge.id,levelDb:Number(el.value)}))
          status("WIRE gain unavailable · accepted parallel send required");
        else scope.paintWireKnob?.(el,Number(el.value));
        return true;
      }
      if(field==="input"){
        // Input selection is independent of Record Arm, transport START and
        // ChannelId admission. A physical mixer strip can receive ANY active
        // source, including the very first source in an unpatched session.
        if(event.type!=="change")return true;
        const channel=m4Index(t);
        const nativeOwner=mixerSession()?.authority==="CHANNEL_ID";
        const alias=stripNamedChoices("input").find(p=>p.label===el.value);
        const choices=verifiedMatrixSourceChoices(
          nativeOwner?sourceChoicesFor():inputChoicesFor(),alias?.choice,hardware);
        const choice=choices.find(x=>x.label===(alias?.choice.label||el.value));
        if(choice?.mode==="PLAYER"&&!nativeOwner){
          status("Music Player INPUT requires a live admitted ChannelId mixer; no WebAudio/ASIO-pin fallback");
          el.value=t.input;return true;
        }
        const id=nativeOwner?nativeIdFor(t):null;
        if((nativeOwner&&(!id||!controller?.state()?.current?.admitted))||
           (!nativeOwner&&(channel<0||channel>=10))||!deviceReady()){
          if(!deviceReady())recommendAudioRepair("INPUT unavailable");
          status("INPUT REJECTED · verify native channel and hardware pins");
          el.value=t.input;return true;
        }
        const decision=stripVM.sourceQueueSelection({
          choice,choices,epoch:hardware?.device?.epoch,acceptedLabel:t.input,
          hardwareConfiguration:hardware?.hardwareConfiguration??"",
          activeSameChannel:nativeOwner?activeNativeSource?.id===id:
            nativeUiSourcePending?.channel===channel
        });
        if(decision.kind==="UNAVAILABLE"){
          if(nativeOwner)nativeToast("Select a verified physical input");
          else status("INPUT REJECTED · refresh verified hardware pins");
          el.value=t.input;return true;
        }
        if(nativeOwner){
          if(decision.kind==="CANCEL")pendingNativeSources.delete(id);
          else pendingNativeSources.set(id,decision.entry);
          if(decision.kind==="QUEUE")dispatchNativeSource();
        }else{
          if(decision.kind==="CANCEL")pendingSourceQueue.delete(channel);
          else pendingSourceQueue.set(channel,decision.entry);
          if(decision.kind==="QUEUE"){
            if(nativeUiSourcePending)
              status("CH "+(channel+1)+" input queued behind native route ACK");
            else dispatchQueuedSource();
          }
        }
        el.value=t.input; // installed readback, not an optimistic UI patch
        return true;
      }
      if(field==="external"){
        if(event.type!=="change")return true;
        return requestExternalOutput(t,el.value,el);
      }
      if(field==="output"){
        if(event.type!=="change")return true;
        if(t.kind==="master"){
          el.value=t.output;openNativeAudioIO();return true;
        }
        const target=stripVM.resolvePrimarySelection(el.value,
          nativeOutputTargets(t));
        // No optimistic GUI state. Both this menu and the WIRE jack use
        // the same ID/epoch-guarded, acknowledged native route transaction.
        if(el.value!==t.output && !requestPrimaryOut(t,target)){
          el.value=t.output;
          status("OUT unavailable · verify device, MAIN route and pending edit");
        }
        // No instantaneous snap-back on a valid pending native transaction.
        return true;
      }
      // ONE realtime PCM control for original ten channels and MAIN,
      // regardless of whether the admitted ChannelId callback or M4 renders.
      // <=33 commands/s/control while dragging; always flush on release.
      if((field==="level"||field==="pan") &&
         liveScalarReady(field==="level"?
           (t.kind==="master"?"masterGain":"faderDb"):"pan",t)){
        const numeric=Number(el.value);
        if(!Number.isFinite(numeric))return true;
        const normalized=field==="pan"?
          previewNativePan(t,el,event):numeric;
        if(normalized===null)return true;
        const key=field==="level"?
          (t.kind==="master"?"masterGain":"faderDb"):"pan";
        if(field==="level")previewFaderWithoutModelMutation(t,numeric);
        if(!streamFaderControl(key,t,normalized,event.type==="change"))
          status("LIVE PCM control rejected; checking route identity");
        return true;
      }
      if (!flags.acceptedGraphConnected) {
        if (field==="level")el.value=String(t.level);
        else if (field==="pan")el.value=String(t.pan);
        status("LIVE PCM control unavailable · selected route not admitted");
        return true;
      }
      // During a native pointer drag move the approved fader *cap* only.
      // The authoritative track/PCM state stays unchanged until ACK+readback.
      if (event.type!=="change") {
        if (field==="level") {
          const n=Number(el.value),min=Number(el.min),max=Number(el.max);
          if ([n,min,max].every(Number.isFinite)&&max>min) {
            const p=Math.max(0,Math.min(100,100*(n-min)/(max-min)));
            const box=el.closest?.(".fader-box");
            box?.style?.setProperty("--fader-fill",p+"%");
            box?.style?.setProperty("--fader-lit",String(p/100));
            box?.style?.setProperty("--fader-y",(100-p)+"%");
            box?.style?.setProperty("--fader-frac",String(p/100));
          }
        }
        return true;
      }
      // If the streaming preflight refused a live field, do NOT bypass
      // it by sending an unguarded ChannelId intent on pointer release.
      if(field==="level")el.value=String(t.level);
      else if(field==="pan")el.value=String(t.pan);
      status("NATIVE: "+field+" awaits accepted control/readback");
      return true;
    },
    consumeAction(t,action,btn) {
      if (!connected)return false;
      // PRE/POST is an exact EdgeId structural graph transaction, not a
      // cosmetic browser mode nor a channel-wide batch change.
      if(action==="tapMode"||action==="sofTap"){
        const current=controller?.state()?.current,source=nativeIdFor(t);
        // A SOF edit must own exactly one real SEND to the selected BUS.
        // Ordinary tapMode uses only the selected EdgeId or a unique
        // non-MAIN outgoing SEND; never guesses a sibling channel's route.
        const edge=action==="sofTap"?selectedNativeSofEdge(t):
          wireVM.channelTapEdge(current,{sourceId:source,
            selectedEdgeId:wireByUiId.get(scope?.selectedWireEdgeId)??null});
        if(!edge){
          status("Select one outgoing parallel SEND WIRE to change PRE/POST");
          return true;
        }
        const target=edge.tap==="PRE_FADER"?"POST_FADER":"PRE_FADER";
        requestNativeTap(source,edge.edgeId,target);
        return true;
      }
      if(action==="sofOn"){
        const edge=selectedNativeSofEdge(t),state=controller?.state();
        const plan=wireVM.nativeSendTogglePlan(state?.current,{
          sourceId:nativeIdFor(t),edgeId:edge?.edgeId,
          projected:flags.acceptedGraphConnected,pending:!!state?.pending});
        if(plan.kind!=="DISPATCH"){
          status("SEND ON/OFF unavailable · select an admitted WIRE send");
          return true;
        }
        intent(plan.operation,t,plan.payload);
        return true;
      }
      // Selection, WIRE inspection and section presentation are UI-only.
      if(action==="editIdentity"||action==="rename"){
        // DAW convention: one click only selects/focuses this channel.
        // The existing native consumeDoubleClick opens the identity editor;
        // never open it from the click event preceding a double-click.
        return true;
      }
      if (["wireExpand","wireSource","wireRoute"].includes(action))
        return false;
      if(action==="removeFx") {
        if(logicalView){
          const id=nativeIdFor(t),row=logicalView.graph.channels.find(ch=>ch.id===id);
          const slot=Number(btn?.dataset.slot);
          if(row&&Number.isInteger(slot)&&slot>=0&&slot<row.inserts.length)
            logicalIntent("REMOVE_FX_REFERENCE",id,{slot,beforeCount:row.inserts.length});
          return true;
        }
        const slot=Number(btn?.dataset.slot),id=nativeIdFor(t),current=controller?.state()?.current;
        const token=current?.graph?.channels.find(ch=>ch.id===id)?.inserts?.[slot]?.id;
        if(current?.retained===true){
          // Reuse exact existing offline workspace handoff, then delete only
          // after verified document readback; never change the stale PCM rack.
          nativeHooks.consumeAction(t,"openPlugin",btn);
          if(pendingRetainedEditor?.channelId===id&&pendingRetainedEditor.slot===slot)
            pendingRetainedEditor.action="removeFx";
          return true;
        }
        editNativeFx(t,slot,null,0,token);return true;
      }
      if(action==="openPlugin"){
        // Device-disconnected FX may have a saved retained editor lease.
        // Do not block native offline .smix recovery with a repair modal.
        const editorView=controller?.state()?.current;
        // Navigation uses the accepted owner, not transient UI meter/route
        // projection readiness. The native dispatcher still fences busy I/O.
        if(!logicalView&&!deviceReady()&&editorView?.admitted!==true&&
           editorView?.retained!==true){
          recommendAudioRepair("FX editor unavailable · audio processing unavailable");
          return true;
        }
        const slot=Number(btn?.dataset.slot),current=activeView();
        // Position only; no parameter write or PCM change.
        const recenter=btn?.dataset.recenter==="true";
        if(logicalView||mixerSession()?.authority==="CHANNEL_ID") {
          const id=nativeIdFor(t);
          // A disconnected physical graph cannot authorize editor writes.
          // Native must first capture a quiescent FX state to the .smix
          // logical workspace; the UI opens it only after matching readback.
          if(!logicalView&&current?.retained===true){
            const fx=current.graph?.channels?.find(ch=>ch.id===id)?.inserts?.[slot];
            const row=current.graph?.channels?.find(ch=>ch.id===id);
            const empty=Array.isArray(row?.inserts)&&slot===row.inserts.length;
            if(!id||!Number.isInteger(slot)||slot<0||slot>=8||
               (!empty&&(!fx?.id||!fx.catalogId))){
              nativeToast("Disconnect recovery requires a saved FX insert");
              return true;
            }
            // Empty slot browsing is UI-only; no driver or VST state calls.
            // Capture the retained workspace only when a choice is made.
            if(empty){
              scope?.openFastPicker?.(t,slot,btn);
              return true;
            }
            if(pendingRetainedEditor){
              status("Finishing the previous offline editor request");
              return true;
            }
            const requestId="retained-editor-"+(++retainedEditorSerial);
            pendingRetainedEditor={requestId,channelId:id,slot,
              catalogId:fx?.catalogId||"",instanceId:fx?.id||"",empty,
              epoch:current.device.epoch,revision:current.graph.revision,
              recenter,acknowledged:false};
            backend.emitEvent("arMixAction",{action:"retainedFxOfflineOpen",
              requestId,channelId:id,slot,instanceId:fx?.id||"",
              deviceEpoch:current.device.epoch,graphRevision:current.graph.revision});
            window.setTimeout(()=>{
              if(pendingRetainedEditor?.requestId!==requestId)return;
              pendingRetainedEditor=null;
              status("Offline FX recovery expired · refresh native mixer");
            },10000);
            return true;
          }
          const plan=fxRackVM.editorNavigationPlan(current,{channelId:id,slot,
            hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
          if(plan.kind==="PICKER"){
            discoverFxOnFirstPickerOpen();
            scope?.openFastPicker?.(t,slot,btn);
          }else if(plan.kind==="OPEN"){
            const editor=plan.lease;
            // Pure VM selects an allowlisted browser editor, offline logical
            // vendor reference or the existing correlated native HWND route.
            // No catalog-specific branching, second renderer or JS PCM owner.
            const surface=fxRackVM.editorSurfacePlan(current,editor,{
              hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
            if(surface.kind==="BROWSER"){
              // G-MV approved lease stays native-authoritative; no nested
              // WebView2/child HWND or FX DOM outside this mixer viewport.
              const view=activeView();
              const safe=fxRackVM.editorNavigationLeaseMatches(view,editor,{
                hardwareConfiguration:protocol.nativeHardwareConfiguration(view?.device)});
              const host=window[surface.windowKey];
              if(!safe||!inlineFxClip?.openExact(
                 host,surface.windowKey,t,slot,editor.instanceId,editor))
                nativeToast("Built-in FX editor unavailable · reopen current FX Rack");
              else inlineFxClip.syncBypass(view);
            }else if(surface.kind==="LOGICAL_VENDOR"){
              backend.emitEvent("arMixAction",{action:"logicalVendorFxEditor",
                channelId:editor.channelId,slot:editor.slot,instanceId:editor.instanceId,
                workspaceToken:editor.workspaceToken,
                graphRevision:editor.graphRevision,deviceEpoch:"0",recenter});
            }else if(surface.kind==="NATIVE_HOST"){
              // VST3, ImmerSIB and hosted future JUCE FX retain one original
              // correlated native editor owner and its independent lease.
              const opened=recenter?openChannelIdFxEditor(editor,true):
                openChannelIdFxEditor(editor);
              if(!opened)
                nativeToast("FX editor unavailable · native instance changed");
            }else nativeToast("FX editor unavailable · accepted FX identity changed");
          }else nativeToast("FX editor unavailable · disconnected audio session");
          return true;
        }
        const plan=fxRackVM.m4EditorPlan(current,{
          channel:m4Index(t),slot,verifiedM4:m4Verified(),offline:!!logicalView,
          hardwareConfiguration:protocol.nativeHardwareConfiguration(current?.device)});
        if(plan.kind==="OPEN"){
          if(!openRoutedFxEditor(plan.channel,plan.slot,plan.instanceId,
            plan.deviceEpoch,plan.revision,
            protocol.nativeHardwareConfiguration(current?.device),recenter))
            nativeToast("FX editor unavailable · accepted instance changed");
        }else if(plan.kind==="PICKER"){
          discoverFxOnFirstPickerOpen();
          scope?.openFastPicker?.(t,slot,btn);
        }else nativeToast("VST3 editor · refresh native device and FX instance");
        return true;
      }
      if(action==="globalFxOn"){
        const current=controller?.state()?.current;
        const row=current?.graph?.channels?.find(ch=>ch.id===nativeIdFor(t));
        const slot=Number(btn?.dataset?.slot);
        // Same native Global FX transaction as the toolbar; validate that
        // the requesting embedded editor is still an admitted GEQ31 insert.
        if(!logicalView&&row&&Number.isInteger(slot)&&slot>=0&&
           row.inserts?.[slot]?.catalogId==="armix.builtin.graphic-eq31.v1"&&
           current.raw?.globalFxBypassed===true)toggleGlobalFx(true);
        return true; // never fall through to demo or unsupported action
      }
      if(action==="slotPower"){
        if(!logicalView&&!deviceReady()){
          recommendAudioRepair("FX Power unavailable");
          return true;
        }
        if(logicalView){
          const id=nativeIdFor(t),row=logicalView.graph.channels.find(ch=>ch.id===id);
          const slot=Number(btn?.dataset.slot),insert=row?.inserts?.[slot];
          if(insert)
            logicalIntent("SET_FX_SLOT_BYPASS",id,
                          {slot,enabled:!insert.bypassed});
          return true;
        }
        if(mixerSession()?.authority==="CHANNEL_ID"){
          if(!requestAdmittedFxPower(t,action,btn))
            nativeToast("Native FX slot unavailable · accepted insert/epoch required");
        }else{
          if(!requestM4FxPower(t,"slotPower",btn))
            nativeToast("FX bypass unavailable · select an accepted VST3 slot");
        }
        return true;
      }
      if(action==="fxOn"){
        if(!logicalView&&!deviceReady()){
          recommendAudioRepair("FX Rack unavailable");
          return true;
        }
        if(logicalView){
          const channelId=nativeIdFor(t);
          const row=logicalView.graph.channels.find(ch=>ch.id===channelId);
          if(row)logicalIntent("SET_FX_RACK_BYPASS",channelId,
                               {enabled:!row.fxRackBypassed});
          return true;
        }
        const authority=mixerSession()?.authority;
        const ok=authority==="M4"?requestM4FxPower(t,"fxOn"):
          authority==="CHANNEL_ID"?requestAdmittedFxPower(t,action,btn):false;
        if(!ok){
          nativeToast("FX header requires an accepted native rack gate");
          status("FX rack unchanged · native PCM authority or readback unavailable");
        }
        return true;
      }
      if(action==="format"&&t?.kind==="audio"){
        requestProcessingFormat(t);return true;
      }
      if(action==="cue"){
        // PFL must use a separate physical pre-fader monitor bus; do not
        // make a GUI-only toggle or mix a cue into audience MAIN.
        nativeToast("PFL requires a separate MONITOR L/R pair; MAIN unchanged");
        status("PFL not armed · independent physical MONITOR output required");
        return true;
      }
      if(action==="mute"||action==="solo"){
        dispatchControlCommand(action==="mute"?"SET_MUTE":"SET_SOLO",
          t,{value:!t[action],enabled:!t[action]});
        return true;
      }
      status("NATIVE: "+action+" is not an accepted ChannelId operation");return true;
    },
    canWireChannelLevel(uiChannelId){
      if(logicalView&&!logicalView.readOnly){
        const id=nativeByUiId.get(uiChannelId),g=logicalView.graph;
        return g.capabilities.supportedOps.includes("SET_WIRE_SUM_GAIN")&&g.edges.some(e=>e.to===id&&e.enabled===true);
      }
      const current=controller?.state()?.current;
      const id=nativeByUiId.get(uiChannelId);
      const row=current?.graph?.channels?.find(ch=>ch.id===id);
      return wireVM.channelSumLevelPlan(current,
        flags.acceptedGraphConnected,id,Number(row?.controls?.wireSumGainDb)).kind==="READY";
    },
    canWireGain(uiEdgeId,uiSourceId){
      return !!admittedParallelWire(uiSourceId,uiEdgeId,"SET_WIRE_GAIN");
    },
    canWireTap(uiEdgeId,uiSourceId){
      if(logicalView){
        const from=nativeByUiId.get(uiSourceId),edgeId=wireByUiId.get(uiEdgeId);
        const edge=wireVM.parallelEdge(logicalView.graph,from,edgeId);
        // Offline PRE/POST edits only .smix, never PCM admission.
        return wireVM.canEditTap(edge,{logical:true,
          readOnly:logicalView.readOnly,pending:!!pendingLogical,
          supported:logicalView.graph.capabilities.supportedOps.includes("SET_SEND_TAP")});
      }
      return wireVM.nativeTapPlan(controller?.state()?.current,{
        sourceId:nativeByUiId.get(uiSourceId),edgeId:wireByUiId.get(uiEdgeId),
        projected:flags.acceptedGraphConnected,
        pending:!!controller?.state()?.pending}).kind==="AVAILABLE";
    },
    consumeNativeWireReplug(sourceId,oldDestId,nextDestId,releasePoint,movingEdgeId) {
      // Only the exact wire the operator actually grabbed is admissible.
      // Multiple parallel sends may share FROM/TO; never remove the first
      // matching destination or accidentally alter a different EdgeId.
      const edge=Number.isInteger(movingEdgeId)?
        scope?.wireEdges?.find(e=>e.id===movingEdgeId && e.enabled &&
          e.sourceId===sourceId && e.destId===oldDestId):null;
      if(!connected||!edge||nextDestId===oldDestId)return false;
      const uiSource=Number(sourceId);
      const nativeSource=nativeByUiId.get(uiSource);
      const nativeEdge=wireByUiId.get(edge.id);
      const validPoint=releasePoint&&Number.isFinite(releasePoint.x)&&
        Number.isFinite(releasePoint.y);
      const offline=!!logicalView;
      const snapshot=controller?.state()?.current;
      if(!validPoint||!nativeSource||!nativeEdge ||
         (offline?(logicalView.readOnly||!!pendingLogical):
          (!flags.acceptedGraphConnected||!snapshot?.admitted||
           !!controller?.state()?.pending ||
           !snapshot.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION"))))
        return false; // no demo graph mutation and no speculative recoil
      const removal=nextDestId===null;
      const op=removal?
        {type:"REMOVE",sourceId:uiSource,edgeId:edge.id}:
        {type:"RETARGET",sourceId:uiSource,edgeId:edge.id,destId:nextDestId};
      // Only detached-to-blank receive bay is a reel. A retarget gets the
      // existing accepted plug spring, not a misleading unplug impulse.
      pendingNativeWireRetract=removal?{
        uiSource,oldDest:oldDestId,point:{x:releasePoint.x,y:releasePoint.y},
        nativeSource:String(nativeSource),nativeEdge:String(nativeEdge),
        primary:edge.kind==="primary",createdAt:performance.now()
      }:null;
      nativeHooks.consumeWire([op]);
      return false; // Native ACK+readback, NEVER the browser reducer
    },
    consumeWire(ops,retries=0) {
      if (!connected)return false;
      if(logicalView) {
        if(!Array.isArray(ops)||ops.length!==1){
          nativeToast("One WIRE edit at a time");return true;
        }
        const op=ops[0];
        if(op.type==="GAIN"){
          nativeToast("Send gain editing requires the connected mixer");return true;
        }
        const from=nativeByUiId.get(op.sourceId);
        const to=op.destId===undefined?null:nativeByUiId.get(op.destId);
        const edgeId=wireByUiId.get(op.edgeId);
        if(!from)return true;
        if(op.type==="MODE"){
          // The same WIRE inspector gesture must remain available before
          // binding a physical interface. Exact native EdgeId/readback,
          // not a reference-renderer-only toggle or a fabricated PCM ACK.
          const tap=wireVM.gestureTap(op.mode);
          const edge=wireVM.parallelEdge(logicalView.graph,from,edgeId);
          if(!tap||!edge||!nativeHooks.canWireTap(op.edgeId,op.sourceId)){
            status("Offline PRE/POST unavailable · select a saved parallel SEND");
            return true;
          }
          if((edge.tapStage||edge.tap)===tap)return true;
          const ack=logicalIntent("SET_SEND_TAP",from,{edgeId,tap});
          if(!ack.ok)nativeToast("PRE/POST not saved · "+ack.error);
          return true;
        }
        const primary=(op.type==="ROUTE_ONLY" ||
          (op.type==="CONNECT"&&op.destId===scope?.master?.id&&
           op.mode!=="send"));
        const kind=primary?"ROUTE_ONLY":op.type;
        if(["CONNECT","ROUTE_ONLY","RETARGET"].includes(kind)&&!to)return true;
        if(["REMOVE","RETARGET"].includes(kind)&&!edgeId)return true;
        const change={op:kind,from,...(to?{to}:{}),
          ...(edgeId?{edgeId}:{}),
          ...(kind==="CONNECT"||kind==="ROUTE_ONLY"?
            {gainLinear:kind==="ROUTE_ONLY"?1:
              Math.pow(10,Math.max(-60,Math.min(6,Number(op.levelDb??0)))/20)}:{})};
        if(!logicalIntent("WIRE_TRANSACTION",from,{changes:[change]}).ok)
          nativeToast("WIRE not saved · previous routing retained");
        return true;
      }
      // The inspector's legacy MODE gesture is only a presentation gesture.
      // Map it to one real installed PRE_FADER/POST_FADER EdgeId transaction;
      // never queue/replay it as a startup wire or mutate a demo graph.
      if(Array.isArray(ops)&&ops.length===1&&ops[0].type==="MODE"){
        const op=ops[0];
        const source=nativeByUiId.get(op.sourceId),edgeId=wireByUiId.get(op.edgeId);
        const tap=wireVM.gestureTap(op.mode);
        requestNativeTap(source,edgeId,tap);
        return true;
      }
      const state=controller?.state();
      // A first drag may arrive before SOURCE's later accepted snapshot.
      // Preserve that one operator intent; resolve only against fresh native
      // identities, never require a hidden OUT-dropdown priming operation.
      if((!flags.acceptedGraphConnected||!state?.current?.admitted||state.pending||
          activeNativeSource||nativeUiSourcePending||pendingNativeSources.size||pendingSourceQueue.size)&&
         deferStartupWire(ops,retries))return true;
      if(Array.isArray(ops)&&ops.length===1&&ops[0].type==="GAIN"){
        if(!requestWireGain(ops[0]))status("WIRE gain unavailable · accepted parallel send required");
        return true;
      }
      // Sends-on-faders' draft EdgeId is not a native identifier.
      ops=wireVM.compactSendGesture(ops,wireByUiId);
      const current=controller?.state().current;
      // The WIRE PRIMARY MAIN cable and the ordinary OUT selector share
      // exactly one native patch path. Pure policy resolves ChannelId EdgeId
      // vs M4 ordinal from accepted readback; stale primary cable projection
      // cannot turn into a generic REMOVE of another parallel SEND.
      if(Array.isArray(ops)&&ops.length===1){
        const op=ops[0];
        const t=scope?.tracks?.find(x=>x.id===op.sourceId);
        const plan=wireVM.primaryMainGesturePlan(current,{
          op,mainUiId:scope?.master?.id,
          sourceId:t?nativeByUiId.get(t.id):null,
          edgeId:wireByUiId.get(op.edgeId),uiEdges:scope?.wireEdges,
          m4Verified:m4Verified(),m4Channel:m4Index(t)
        });
        if(plan.kind==="BLOCK"){
          status("WIRE MAIN unchanged · stale PRIMARY cable or native authority");
          controller?.subscribe();
          return true;
        }
        if(plan.kind==="ROUTE"){
          if(t&&requestPrimaryOut(t,plan.target))
            status(t.name+" WIRE → "+
              (plan.target==="MAIN"?"MAIN":"SOURCE ONLY")+
              " · native ACK/readback required");
          else nativeToast("WIRE MAIN unavailable · verify channel and routing state");
          return true; // never simulate native PCM in the Pages reducer
        }
      }
      if (!flags.acceptedGraphConnected || !current?.admitted ||
          !current.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION")) {
        const reason="WIRE BUS/FX return is not admitted by the installed PCM graph";
        status("NATIVE: "+reason+" · no audio reroute");
        nativeToast(reason);
        return true;
      }
      // Pure preflight converts exact UI tokens to native ChannelId/EdgeId.
      // No DSP or UI mutation until the ORIGINAL controller accepts intent.
      const translation=wireVM.nativeTransaction(ops,nativeByUiId,wireByUiId);
      if(!translation.ok){
        if(translation.error==="MAPPED_IDENTITY_MISSING")
          status("NATIVE: WIRE mapped identity missing; no mutation");
        else if(translation.error==="MULTI_SOURCE")
          status("NATIVE: multi-source transaction requires native batch admission");
        else if(translation.error==="INVALID_WIRE_GAIN")
          status("NATIVE: WIRE invalid gain; accepted routing unchanged");
        else status("NATIVE: WIRE "+(translation.operation||"gesture")+
          " needs real engine capability; unchanged");
        return true;
      }
      const {source,changes}=translation;
      const accepted=controller.intent("APPLY_WIRE_TRANSACTION",source,{changes});
      if(accepted.ok&&ops.length===1&&ops[0].type==="CONNECT")
        activeWireAttempt={ops:ops.map(x=>({...x})),epoch:current.device.epoch,retries};
      if (!accepted.ok)status("NATIVE: "+accepted.error+"; WIRE unchanged");
      else window.setTimeout(()=>controller?.expire(accepted.requestId),8000);
      return true;
    },
    consumeUndo() {if (!connected)return false;status("NATIVE: WIRE undo requires accepted native transaction");return true;},
    moveFx(sourceId,slot,destId,position,token){
      const all=[...(scope?.tracks||[]),scope?.master];
      const source=all.find(t=>t?.id===sourceId),dest=all.find(t=>t?.id===destId);
      return !!source&&!!dest&&editNativeFx(source,slot,dest,position,token);
    },
    nativeIdFor(t){return nativeIdFor(t);},
    insertPlugin(id,target){
      if(id==="armix.builtin.vocal-reverb-return.v1"){
        const row=[...(scope?.tracks||[]),scope?.master].find(t=>t?.id===target?.channelId);
        if(row?.nativeRole!=="FX_RETURN"){
          nativeToast("Reverb-Sis Return requires an FX Return strip · or right-click last Reverb-Sis insert");
          return true;
        }
      }
      if(logicalView){
        const t=scope?.tracks?.find(x=>x.id===target?.channelId)||
          (scope?.master?.id===target?.channelId?scope.master:null);
        const channelId=nativeIdFor(t);
        const channel=logicalView.graph.channels.find(ch=>ch.id===channelId);
        const slot=Number(target?.slot);
        if(!channel||!Number.isInteger(slot)||slot!==channel.inserts.length||
           slot<0||slot>=8){
          nativeToast("Choose the next empty FX slot");return true;
        }
        const result=logicalIntent("ADD_FX_REFERENCE",channelId,{catalogId:id});
        if(!result.ok)nativeToast("FX settings not saved · "+result.error);
        return true;
      }
      if(pendingFxInsert){status("FX INSERT · wait for previous native ACK");return true;}
      const plugin=nativeCatalog.get(id) || nativeBuiltins.find(fx=>fx.id===id);
      const track=scope?.tracks?.find(x=>x.id===target?.channelId)||
        (scope?.master?.id===target?.channelId?scope.master:null);
      const channel=m4Index(track),slot=Number(target?.slot);
      const accepted=controller?.state().current;
      const nativeId=nativeIdFor(track);
      if(accepted?.retained===true){
        const row=accepted.graph?.channels?.find(ch=>ch.id===nativeId);
        if(!plugin||!row||!Number.isInteger(slot)||slot<0||slot>=8||slot!==row.inserts?.length){
          nativeToast("Choose the next empty FX slot");return true;
        }
        if(pendingRetainedEditor){status("Finishing the previous offline editor request");return true;}
        const requestId="retained-editor-"+(++retainedEditorSerial);
        pendingRetainedEditor={requestId,channelId:nativeId,slot,empty:true,
          epoch:accepted.device.epoch,revision:accepted.graph.revision,
          acknowledged:false,action:"insertFx",selectedCatalogId:id};
        status("Preparing saved mixer workspace · audio output remains safe");
        backend.emitEvent("arMixAction",{action:"retainedFxOfflineOpen",requestId,
          channelId:nativeId,slot,instanceId:"",deviceEpoch:accepted.device.epoch,
          graphRevision:accepted.graph.revision});
        window.setTimeout(()=>{
          if(pendingRetainedEditor?.requestId!==requestId)return;
          pendingRetainedEditor=null;status("Offline FX recovery expired · refresh native mixer");
        },10000);
        return true;
      }
      if(accepted?.admitted || mixerSession()?.authority==="CHANNEL_ID") {
        const hardwareConfiguration=protocol.nativeHardwareConfiguration(accepted?.device);
        const plan=fxRackVM.nativeInsertPlan(accepted,{
          channelId:nativeId,slot,catalogId:plugin?.id,
          pending:!!pendingFxInsert,hardwareConfiguration});
        if(plan.kind!=="READY"){
          status("VST3 unavailable · select an empty native ChannelId FX slot");
          return true;
        }
        const request=controller.intent("LOAD_INSERT",plan.channelId,plan.payload);
        if(!request.ok){status("VST3 rejected · "+request.error);return true;}
        pendingFxInsert={authority:"CHANNEL_ID",requestId:request.requestId,
                         id,channelId:plan.channelId,slot,
                         epoch:accepted.device.epoch,hardwareConfiguration};
        status(track.name+" · preparing VST3 without stopping MAIN");
        window.setTimeout(()=>{
          if(pendingFxInsert?.requestId!==request.requestId)return;
          pendingFxInsert=null;
          status("Native VST3 timed out · accepted audio left unchanged");
          controller.subscribe();
        },15000);
        return true;
      }
      const plan=fxRackVM.m4InsertPlan(accepted,{
        catalogId:plugin?.id,channel,slot,verifiedM4:m4Verified(),
        offline:!!logicalView,pending:!!pendingFxInsert,
        hardwareConfiguration:protocol.nativeHardwareConfiguration(accepted?.device)});
      if(plan.kind!=="READY"){
        status("FX INSERT REJECTED · choose an empty consecutive slot on a native CH with verified route/catalog");
        return true;
      }
      const requestId="fx-"+(++fxRequestSerial);
      pendingFxInsert={requestId,...plan.pending};
      backend.emitEvent("arMixAction",{action:plan.action,
        requestId,...plan.payload});
      status("CH "+(channel+1)+" · preparing real VST3, awaiting native ACK");
      window.setTimeout(()=>{
        if(pendingFxInsert?.requestId!==requestId)return;
        pendingFxInsert=null;
        status("FX INSERT timeout; accepted routing preserved");
        controller.subscribe();
      },15000);
      return true;
    },
    consumeDoubleClick(event,t) {
      if (!connected)return false;
      // Reset the parameter the fader CURRENTLY owns. A SOF reset must not
      // write the main channel's SET_FADER or M4 faderDb by accident.
      if (event.target.closest?.(".fader-box")||event.target.classList.contains("fader")){
        if(scope?.sofEnabled&&nativeSofSource(t)){
          const edge=selectedNativeSofEdge(t),id=edge&&uiByWireId.get(edge.edgeId);
          if(!edge||id===undefined||
             !requestWireGain({sourceId:t.id,edgeId:id,levelDb:0}))
            status("SOF unity reset unavailable · select one admitted SEND");
          return true;
        }
        if(!streamFaderControl(t.kind==="master"?"masterGain":"faderDb",t,0,true))
          dispatchControlCommand("SET_FADER",t,{valueDb:0});
      }
      else if(event.target.closest?.(".gain-dial")){
        if(t?.kind!=="audio"||t.nativeGainAvailable!==true)
          status("INPUT GAIN unity reset requires native PCM admission");
        else intent("SET_INPUT_GAIN",t,{valueDb:0});
        return true;
      }
      else if(event.target.classList.contains("center-pan"))
        dispatchControlCommand("SET_PAN",t,{value:0});
      else if(event.target.closest?.(".footer-label,.strip-identity-top"))
        openNativeIdentity(t,event.target);
      return true;
    },
    applyIdentity:acceptIdentityPresentation,
    fxEditorState:activeView,
    state:()=>controller?.state() || null
  });
  window.__ARMIX_NATIVE_M2__ = nativeHooks;
  // Shared rack double-click: resolve its exact strip and slot; no last-FX
  // fallback and no duplicate native editor ownership.
  document.addEventListener("arMixFxRackDoubleClick",event=>{
    const trackId=event.detail?.trackId,slot=event.detail?.slot;
    if(!connected||typeof trackId!=="string"||!Number.isInteger(slot)||
       slot<0||slot>=8)return;
    const track=[...(scope?.tracks||[]),scope?.master]
      .find(item=>item&&String(item.id)===trackId);
    if(track&&nativeIdFor(track))
      nativeHooks.consumeAction(track,"openPlugin",{
        dataset:{slot:String(slot),recenter:"true"}});
  });
  window.ArSonFxRackAuthority=Object.freeze({move:(...args)=>nativeHooks.moveFx(...args)});
  const descriptor = Object.freeze({
    source:"/preview/ approved compiled Pages GUI",
    pagesRoute:"/preview/",
    nativeTransport:connected,
    get acceptedGraphConnected(){return flags.acceptedGraphConnected;},
    get productionCallbackLive(){return flags.productionCallbackLive;},
    get status(){return lastNativeError||fallback;},
    m2:nativeHooks
  });
  Object.defineProperty(window,"__ARMIX_APPROVED_NATIVE_GUI__",{
    value:descriptor,configurable:false,writable:false
  });
  if (!connected)return;
  // Disable physical control gestures while G0 has no accepted ChannelId graph.
  // Presentation-only bank/selection remains available.
  document.addEventListener("pointerdown",event=>{
    if (flags.acceptedGraphConnected || logicalView) return;
    const t=scope?.findTrack?.(event.target);
    if(m4Verified() && (t?.kind==="audio"||t?.kind==="master"))return;
    if (!event.target.closest?.(".fader-box,.center-pan")) return;
    event.preventDefault();event.stopImmediatePropagation();
    status("NATIVE G0: physical gain/pan unavailable until engine graph admission");
  },true);
  // Reference-only buttons outside strips must not mutate fake Show/FX state.
  document.addEventListener("click",event=>{
    const target=event.target.closest?.(
      "#preview,#addTrack,#addBus,#addFx,#reset,#saveScene,#recallScene,#sceneSafe,#group,#ungroup,#copy,#paste,#clearCue,#pluginScan,#pluginImport,#pluginAddPath");
    if(!target)return;
    event.preventDefault();event.stopImmediatePropagation();
    const action=target.id==="pluginScan"?"scanVst3":
      target.id==="pluginImport"?"importVst3":
      target.id==="pluginAddPath"?"addScanFolder":null;
    if(action){
      backend.emitEvent("arMixAction",{action});
      status("PLUGIN MANAGER · "+action+" requested from native JUCE");
      return;
    }
    if(target.id==="addBus"||target.id==="addTrack"||target.id==="addFx"){
      requestNativeBusCreate(target.id==="addTrack",target.id==="addFx");return;
    }
    status("NATIVE: "+target.id+" requires admitted engine operation");
  },true);
  // Native fader is committed exactly once INSIDE the original GUI's
  // finishFaderPointer via build_approved_native_gui.py hook. A later
  // window.pointerup listener would run after that handler clears the gesture.

  // Editing is accessible without a mouse. F2 operates on the selected
  // loaded channel; Enter/Space activates the focusable footer name.
  document.addEventListener("keydown",event=>{
    if(!connected||event.altKey||event.ctrlKey||event.metaKey)return;
    const target=event.target;
    const editorHidden=Boolean(document.getElementById("channelIdentity")?.hidden);
    const textEntry=target?.closest?.('input,textarea,select,[contenteditable="true"]');
    // Closing the editor can leave DOM focus on its now-hidden input under
    // WebView2. F2 must still reopen the last selected channel, rather than
    // being swallowed by the generic text-entry shortcut guard.
    if(textEntry && !(event.key==="F2" && editorHidden &&
                      target?.id==="identityName"))return;
    const focused=target?.closest?.('.footer-label .name');
    if(focused && ["Enter"," "].includes(event.key)){
      const track=scope?.tracks?.concat(scope.master||[])?.find(
        item=>item.id===Number(focused.closest(".strip")?.dataset.id));
      event.preventDefault();event.stopImmediatePropagation();
      openNativeIdentity(track,focused);return;
    }
    if(event.key!=="F2")return;
    const tracks=scope?.tracks?.concat(scope.master||[])||[];
    const focusedTrack=target?.closest?.(".strip")?.dataset.id;
    const selected=tracks.find(t=>t.id===Number(focusedTrack))||
      tracks.find(t=>t.selected && identityEligible(t))||
      tracks.find(t=>t.id===lastEditedChannelId && identityEligible(t));
    if(!selected)return;
    event.preventDefault();event.stopImmediatePropagation();
    const anchor=document.querySelector('.strip[data-id="'+selected.id+'"] .footer-label')||
      document.querySelector('.strip[data-id="'+selected.id+'"] .strip-identity-top');
    openNativeIdentity(selected,anchor);
  },true);

  // Preserve keyboard focus when closing the modeless identity window.
  // The original Pages close handlers own visibility; never replace them.
  for(const id of ["identityCancel","identityClose","identitySave"]){
    document.getElementById(id)?.addEventListener("click",()=>{
      requestAnimationFrame(()=>{
        if(!document.getElementById("channelIdentity")?.hidden)return;
        const name=document.querySelector('.strip[data-id="'+lastEditedChannelId+'"] .footer-label .name');
        name?.focus?.({preventScroll:true});
      });
    });
  }

  // Pages' reference-only controls must not masquerade as working native
  // operations. Keep REAL audio/FX/WIRE/device controls fully interactive;
  // until full device-independent Show is implemented, hide unsafe demo
  // shortcuts instead of silently intercepting and leaving broken buttons.
  const nativeReferenceOnly=Object.freeze({
    sceneSelect:"Native full-session Show is not yet available",
    saveScene:"Native full-session Show is not yet available",
    recallScene:"Native full-session Show is not yet available",
    sceneSafe:"Show safe requires native session snapshots",
    group:"Native channel grouping not yet admitted",
    ungroup:"Native channel grouping not yet admitted",
    copy:"Native channel copy not yet admitted",
    paste:"Native channel paste not yet admitted",
    clearCue:"PFL requires dedicated physical monitoring",
    preview:"Demo meters never run inside the native audio host",
    reset:"Demo reset must not erase a live mixer"
  });
  function presentNativeOnlyTools(){
    for(const [id,label] of [["addBus","+ AUX / BUS"],["addFx","+ FX Bus"]]){
      const el=document.getElementById(id);if(el){el.textContent=label;el.hidden=false;el.disabled=true;
        el.title="Create a receiving GROUP with independent routing and insert rack";}
    }
    for(const [id,reason] of Object.entries(nativeReferenceOnly)){
      const el=document.getElementById(id);
      if(!el)continue;
      el.disabled=true;
      el.title=reason;
      if(el.dataset)el.dataset.nativeUnavailable=reason;
      // Do not advertise an interactive click target that native cannot
      // fulfill. Internal IDs survive for the approved Pages DOM contract.
      el.style?.setProperty?.("display","none","important");
      el.setAttribute?.("aria-hidden","true");
    }
    const select=document.getElementById("sceneSelect");
    const label=document.querySelector('label[for="sceneSelect"]');
    if(select&&label)label.style?.setProperty?.("display","none","important");
    // Keyboard shortcuts in Pages call the demo reducer directly, bypassing
    // the click capture guard below. Suppress that route only in JUCE native.
    document.addEventListener("keydown",event=>{
      if(!(event.ctrlKey||event.metaKey))return;
      if(event.target?.closest?.('input,textarea,select,[contenteditable="true"]'))return;
      if(["c","v","g"].includes(event.key.toLowerCase())){
        event.preventDefault();event.stopImmediatePropagation();
        nativeToast("Native channel copy/group requires accepted mixer session support");
      }
    },true);
  }
  const ready=()=>{
    presentNativeOnlyTools();
    syncNativeSofAvailability(); // hidden unless all installed native SEND controls exist
    installFirstSoundMenu();
    presentFxCatalog(); // compiled native FX, never demo or vendor placeholders
    updateHardwarePanel(null);
    paintMainMonitor();
    stopDemo();
    status("NATIVE: subscribed · waiting for true device and accepted ChannelId graph");
    // Only the installed native Pages scope can authenticate host GUI readiness.
    // This handshake says nothing about whether physical PCM is admitted.
    if(scope?.installGraph && scope?.render){
      backend.emitEvent("arMixGuiReady",{
        gui:"APPROVED_PAGES_PREVIEW_BUTTONS",nativeBridgeAttached:true,
        visualReady:true,audioReady:false,
        viewportWidth:Math.round(window.innerWidth),
        viewportHeight:Math.round(window.innerHeight),
        devicePixelRatio:window.devicePixelRatio
      });
    } else status("Native mixer renderer unavailable · repair application");
    controller.subscribe();
  };
  if(document.readyState==="loading")
    document.addEventListener("DOMContentLoaded",ready,{once:true});
  else ready();
})();
