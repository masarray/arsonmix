// SPDX-License-Identifier: AGPL-3.0-only
// ONE approved /preview/ DOM, native authority only. No new UI, no JS PCM.
(() => {
  "use strict";
  const backend = window.__JUCE__?.backend;
  const protocol = window.ArSonApprovedNativeContract;
  // The public GUI owns one lexical model. Native-only bundle exports a narrow
  // reference to that SAME model; never reconstruct a parallel DOM/DSP.
  const scope = window.__ARMIX_NATIVE_SCOPE__ || null;
  const connected = Boolean(backend?.emitEvent && backend?.addEventListener && protocol);
  // The old M1 wording is retained as an explicit boot-time fallback for
  // release-train compatibility, NOT a claim that a demo graph is audio.
  const fallback = "M1_VISUAL_BUNDLE_ONLY__NOT_AUDIO_READY";
  const flags = {acceptedGraphConnected:false,productionCallbackLive:false};
  const nativeByUiId = new Map();       // SMALL UI ordinal -> EXACT decimal-string native ChannelId
  const uiByNativeId = new Map();       // EXACT uint64 string -> local view-only ordinal
  const wireByUiId = new Map();         // view-only EdgeId -> EXACT decimal-string native EdgeId
  const uiByWireId = new Map();
  let lastPaintedSignature = "",lastNativeTopologySignature = "",
      lastNativeError = "NATIVE_NOT_SUBSCRIBED";
  // B-stage: attached-node-only meter caches; a structural render invalidates
  // them atomically. Never retain detached WebView2 strips or stale channel IDs.
  const meterElements = new Map();
  let nativeFxPowerSerial=0,pendingNativeFxPower=null;
  const pendingM4RackPowers=new Map();
  let controller = null;
  const status = message => {
    lastNativeError = message;
    const info = document.getElementById("info");
    if (info && info.textContent !== message) info.textContent = message;
  };
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
      flags.acceptedGraphConnected ? "pcm-accepted" : "not-admitted";
    if (document.documentElement.dataset.armixNativeAuthority !== authority)
      document.documentElement.dataset.armixNativeAuthority = authority;
  };
  const nativeIdFor = t => nativeByUiId.get(t?.id) || null;
  const intent = (operation,t,payload) => {
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
  function projection(native) {
    if (!native?.admitted || !Array.isArray(native.graph.edges) ||
        !scope?.make || !scope?.render || !scope?.wireGraph ||
        !scope?.installGraph || !scope?.allocateWireId) return false;
    const accepted = native.graph;
    // No fake default fader/mute/solo when the producer omitted real readback.
    if (accepted.channels.some(ch => !ch.controls ||
        !["faderDb","pan","mute","solo"].every(k => Object.hasOwn(ch.controls,k)))) return false;
    const previous = new Map(scope.tracks.concat(scope.master).map(t=>[t.id,t]));
    const nextTracks = [], localByNative = new Map(), nextNativeByUi = new Map();
    let nextMaster = null, inputOrdinal=0;
    for (const [index,ch] of accepted.channels.entries()) {
      const kind = ch.role==="MAIN"?"master":ch.role==="GROUP"?"bus":
        ch.role==="FX_RETURN"?"fx":"audio";
      // Preserve approved CH01..CH10 DOM/model identities across M4 G0
      // -> native ChannelId admission -> any later G0 fallback. A new
      // temporary track ID would silently move hardware to another strip.
      const m4Id=ch.role==="INPUT"?m4TrackIds[inputOrdinal++]:undefined;
      const savedId = uiByNativeId.get(ch.id) ?? m4Id;
      const t = savedId !== undefined && previous.has(savedId) ?
        previous.get(savedId) : scope.make(ch.name,kind,scope.colors[index%scope.colors.length]||"#568ead");
      if (savedId !== undefined) t.id = savedId;
      // No Number(ch.id): all native identities stay canonical uint64 strings.
      localByNative.set(ch.id,t.id);
      nextNativeByUi.set(t.id,ch.id);
      t.name=ch.name;t.kind=kind;
      t.format=ch.processingFormat.toLowerCase();
      t.level=ch.controls.faderDb;
      // Native PAN is normalized [-1,+1]; approved fader console uses [-100,+100].
      t.pan=ch.controls.pan*100;
      t.mute=ch.controls.mute;
      t.solo=ch.controls.solo;
      // Admitted ChannelId readback already carries actual physical pins.
      // Do not retain the public demo's "None" when native PCM is active.
      if(ch.role==="INPUT"){
        const src=ch.source||{},l=/^in:(\d+)$/.exec(src.portId||""),
          r=/^in:(\d+)$/.exec(src.portIdRight||"");
        t.input=l?inputLabelFor({mode:src.sourceFormat==="STEREO"?"STEREO":"MONO",
          left:Number(l[1]),right:r?Number(r[1]):-1}):"None";
        const out=accepted.channels.find(dest=>dest.id===ch.primaryOutId);
        t.output=out?.role==="MAIN"?"Master":
          (out?.name||"SOURCE ONLY (No Main)");
      }
      t.selected=previous.get(t.id)?.selected || false;
      if(typeof ch.fxRackBypassed==="boolean")t.fxOn=!ch.fxRackBypassed;
      if (Array.isArray(ch.inserts)) {
        t.fx=Array.from({length:8},(_,i)=>ch.inserts[i]?.name||"None");
        t.slotBypass=Array.from({length:8},(_,i)=>Boolean(ch.inserts[i]?.bypassed));
        t.fxInsertIds=Array.from({length:8},(_,i)=>ch.inserts[i]?
          {name:ch.inserts[i].name,id:ch.inserts[i].id}:null);
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
      const isMain=edge.to===accepted.mainChannelId;
      const isPrimary=source?.primaryOutId===edge.to;
      // An independent SEND to MAIN is not the source's primary OUT.
      const kind=isPrimary?"primary":edge.tap==="SPLIT_PRE"||edge.tap==="SPLIT_POST"?"split":"send";
      const mode=isMain?(isPrimary?"postFader":"send"):kind==="primary"?"routeOnly":
        kind==="split"?(edge.tap==="SPLIT_PRE"?"splitPre":"splitPost"):"send";
      if (isPrimary && edge.gainLinear!==1) return false;
      const gainDb=isPrimary?0:Math.round(Math.max(-60,Math.min(6,
        edge.gainLinear>0?20*Math.log10(edge.gainLinear):-60))*10)/10;
      let uiId=uiByWireId.get(edge.edgeId);
      if (uiId===undefined) uiId=scope.allocateWireId();
      newWireByUi.set(uiId,edge.edgeId);
      newUiByWire.set(edge.edgeId,uiId);
      edges.push({id:uiId,sourceId:src,destId:dst,kind,mode,
        enabled:true,levelDb:gainDb});
    }
    if (!scope.wireGraph.validateCandidateGraph(nextTracks,nextMaster.id,edges).ok) return false;
    const previouslySelected = nextTracks.filter(t=>t.selected);
    if (!previouslySelected.length && nextTracks.length) nextTracks[0].selected=true;
    // One atomic accepted readback projection. Demo wire reducer NEVER runs.
    for (const [id,ui] of localByNative) uiByNativeId.set(id,ui);
    nativeByUiId.clear();for(const [ui,id] of nextNativeByUi)nativeByUiId.set(ui,id);
    wireByUiId.clear();for(const [ui,id] of newWireByUi)wireByUiId.set(ui,id);
    uiByWireId.clear();for(const [id,ui] of newUiByWire)uiByWireId.set(id,ui);
    meterElements.clear(); // installGraph() replaces strip HTML and meter nodes
    scope.installGraph(nextTracks,nextMaster,edges);
    return true;
  }
  // Rack and individual FX power are scalar native DSP controls, NOT a
  // structural graph change. Updating them must not replace DOM strips,
  // steal focus, interrupt a fader drag or reallocate WIRE render objects.
  // The verified individual bypass flag stays distinct from rack bypass.
  function paintNativeFxPowerReadback(ch,t){
    if(typeof ch.fxRackBypassed!=="boolean"||!Array.isArray(ch.inserts))
      return true; // never synthesize missing DSP readback
    const enabled=!ch.fxRackBypassed;
    const strip=document.querySelector('.strip[data-id="'+t.id+'"]');
    const rack=strip?.querySelector(".fx-section");
    // Virtualized mobile banks may have no mounted strip. The model still
    // receives verified state; normal bank render creates the right buttons.
    if(rack){
      const header=rack.querySelector('[data-action="fxOn"]');
      const rows=[...rack.querySelectorAll('.fxslot-row:not(.is-empty)')];
      if(!header||rows.length!==ch.inserts.length)return false;
      for(let slot=0;slot<ch.inserts.length;slot++){
        const insert=ch.inserts[slot],row=rows.find(r=>
          r.dataset.slot===String(slot));
        if(!insert||typeof insert.bypassed!=="boolean"||
           !row||row.dataset.insertId!==insert.id||
           !row.querySelector('[data-action="slotPower"]'))
          return false; // structural re-render, never modify wrong FX owner
      }
    }
    t.fxOn=enabled;
    if(!Array.isArray(t.slotBypass))t.slotBypass=Array(8).fill(false);
    for(let slot=0;slot<ch.inserts.length;slot++)
      t.slotBypass[slot]=ch.inserts[slot].bypassed;
    if(!rack)return true;
    rack.dataset.fxChainEnabled=String(enabled);
    rack.classList.toggle("is-chain-bypassed",!enabled);
    const header=rack.querySelector('[data-action="fxOn"]');
    header.setAttribute("aria-pressed",String(enabled));
    header.title=(enabled?"Bypass":"Enable")+" all channel FX";
    header.setAttribute("aria-label",
      (enabled?"Bypass":"Enable")+" entire FX chain in "+t.name);
    for(let slot=0;slot<ch.inserts.length;slot++){
      const insert=ch.inserts[slot];
      const row=[...rack.querySelectorAll('.fxslot-row:not(.is-empty)')]
        .find(r=>r.dataset.slot===String(slot));
      const effective=enabled&&!insert.bypassed;
      row.dataset.fxSlotEnabled=String(!insert.bypassed);
      row.dataset.fxEffective=String(effective);
      row.classList.toggle("is-active",effective);
      row.classList.toggle("is-bypassed",!effective);
      const button=row.querySelector('[data-action="slotPower"]');
      button.setAttribute("aria-pressed",String(effective));
      button.title=(insert.bypassed?"Enable":"Bypass")+
        " only insert "+(slot+1);
      button.setAttribute("aria-label",
        (insert.bypassed?"Enable":"Bypass")+" only FX insert "+
        (slot+1)+" in "+t.name);
    }
    return true;
  }
  function projectNativeGraphWithoutFocusLoss(next) {
    // Only identity, order, processing width, source and edges are structural.
    // Rack/slot bypass is applied in-place from independent readback below.
    const structure=JSON.stringify([
      next.graph.channels.map(ch=>[ch.id,ch.role,ch.name,
        ch.processingFormat,ch.source,ch.primaryOutId,
        ch.inserts?.map(fx=>[fx.id,fx.catalogId,fx.name])]),
      next.graph.edges]);
    if(structure!==lastNativeTopologySignature){
      if(!projection(next))return false;
      lastNativeTopologySignature=structure;
      return true;
    }
    // Immutable graph topology is identical: update actual scalar controls
    // only. Never detach the active HTML select or the captured fader pointer.
    for(const ch of next.graph.channels){
      const id=uiByNativeId.get(ch.id);
      const t=id===scope?.master?.id?scope.master:
        scope?.tracks?.find(x=>x.id===id);
      if(!t||!ch.controls)continue;
      if(!paintNativeFxPowerReadback(ch,t)){
        // A plugin owner or strip DOM changed under us: do not paint an
        // unrelated instance. Rebuild from the accepted native graph.
        if(!projection(next))return false;
        lastNativeTopologySignature=structure;
        return true;
      }
      const c=ch.controls;
      const db=c.faderDb,pan=c.pan*100;
      const faderChanged=Number.isFinite(db)&&Math.abs(t.level-db)>.015;
      const visualDb=Number.isFinite(db)?pendingFaderVisualDb(t,db):null;
      const switchChanged=t.mute!==c.mute||t.solo!==c.solo;
      if(faderChanged)t.level=db;
      if(Number.isFinite(pan))t.pan=pan;
      t.mute=c.mute;t.solo=c.solo;
      if(switchChanged && scope.activeFaderId!==t.id){
        scope.refreshStrip?.(t);
        if(visualDb!==null)scope.syncFaderVisual?.({...t,level:visualDb});
        meterElements.delete(t.id);
      }else if(faderChanged&&scope.activeFaderId!==t.id&&visualDb===null)
        scope.syncFaderVisual?.(t);
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
  const nativeMasterReady=()=>mixerSession()?.authority==="CHANNEL_ID" &&
    safeNativeCounter(hardware?.device?.epoch)!==null &&
    safeNativeCounter(legacy()?.routingRevision)!==null &&
    Number.isFinite(mixerSession()?.main?.db) &&
    typeof mixerSession()?.main?.mute==="boolean";
  // The shipped /preview/ IN dropdown is the sole normal hardware source
  // selector. Build options exclusively from JUCE-reported active pins, never
  // the public browser demo's hardcoded "Input 1 / CABLE / Realtek" strings.
  const hardwarePins=()=>Array.isArray(hardware?.device?.inputEndpoints)?
    hardware.device.inputEndpoints.filter(p=>p.active===true &&
      Number.isInteger(p.port) && hardware.device.enabledInputs.includes(p.port)):[];
  // BUS is verified engine state, not an available hardware input choice.
  const INTERNAL_BUS="Internal BUS (read-only)";
  const monoLabel=p=>"Mono · "+p.label+" [IN "+(p.port+1)+"]";
  const stereoLabel=(a,b)=>"Stereo · IN "+(a.port+1)+" + "+(b.port+1);
  const inputChoicesFor=()=>{
    const pins=hardwarePins(),entries=[{label:"None",mode:"NONE",left:-1,right:-1}];
    for(const p of pins)entries.push({
      label:monoLabel(p),mode:"MONO",left:p.port,right:-1});
    // Expose real adjacent physical stereo pairs, never guess disabled pins.
    for(let i=0;i+1<pins.length;++i)
      if(pins[i+1].port===pins[i].port+1)
        entries.push({label:stereoLabel(pins[i],pins[i+1]),
          mode:"STEREO",left:pins[i].port,right:pins[i+1].port});
    return entries;
  };
  const inputLabelFor=row=>{
    if(!row||row.mode==="NONE")return "None";
    if(row.mode==="BUS")return INTERNAL_BUS;
    const entries=inputChoicesFor();
    const exact=entries.find(x=>x.mode===row.mode &&
      x.left===row.left && x.right===row.right);
    return exact?.label||"None";
  };
  // ChannelId and the existing M4 use the SAME ten native input ordinals.
  // GUI track reorder must never silently repatch a different physical lane.
  const m4TrackIds=Array.isArray(scope?.tracks)?
    scope.tracks.filter(t=>t.kind==="audio").slice(0,10).map(t=>t.id):[];
  const m4Index=t=>t?.kind==="audio"?m4TrackIds.indexOf(t.id):-1;
  let lastM4UiSignature="",lastM4Controls="",nativeUiSourcePending=null;
  let lastM4PortSignature="",pendingOutputPatch=null,m4RequestSerial=0,outputSerial=7000;
  const pendingSourceQueue=new Map();
  function requestProcessingFormat(t){
    // Once the native ChannelId callback owns PCM, the old M4 routing
    // revision is no longer authoritative. Send M/ST through the SAME
    // admitted epoch/revision/roster transaction as WIRE and SOURCE.
    if(mixerSession()?.authority==="CHANNEL_ID"){
      const current=controller?.state()?.current;
      const channelId=nativeIdFor(t);
      const source=current?.graph?.channels?.find(ch=>ch.id===channelId);
      if(!current?.admitted || !channelId || source?.role!=="INPUT" ||
         !current.graph.capabilities.supportedOps.includes("SET_PROCESSING_FORMAT")){
        status("M/ST unavailable · native processing format not admitted");
        return false;
      }
      const target=source.processingFormat==="MONO"?"STEREO":"MONO";
      const request=controller.intent("SET_PROCESSING_FORMAT",channelId,
                                      {format:target});
      if(!request.ok){
        status("M/ST rejected · "+request.error);
        return false;
      }
      window.setTimeout(()=>controller?.expire(request.requestId),8000);
      status(t.name+" M/ST → "+target+" · verifying native PCM readback");
      return true;
    }
    const channel=m4Index(t);
    const ep=safeNativeCounter(hardware?.device?.epoch),
      rev=safeNativeCounter(legacy()?.routingRevision);
    if(!m4Verified()||channel<0||channel>=10||
       ep===null||rev===null||pendingOutputPatch||nativeUiSourcePending||
       !deviceReady()){
      status("M/ST unavailable · wait for accepted hardware route");
      return false;
    }
    const target=t.format==="mono"?"STEREO":"MONO";
    const requestId=++outputSerial;
    pendingOutputPatch={requestId,channel,target,epoch:ep,
      operation:"SET_PROCESSING_FORMAT"};
    backend.emitEvent("arMixAction",{action:"routingChannelPatch",
      operation:"SET_PROCESSING_FORMAT",requestId,channel,target,
      deviceEpoch:ep,revision:rev});
    status("CH "+(channel+1)+" processing "+target+" · verifying native PCM");
    window.setTimeout(()=>{
      if(pendingOutputPatch?.requestId!==requestId)return;
      pendingOutputPatch=null;
      status("CH "+(channel+1)+" M/ST timed out · prior format preserved");
      controller?.subscribe();
    },8000);
    return true;
  }
  function previewNativePan(t,el,event){
    let value=Math.max(-100,Math.min(100,Number(el.value)));
    if(!Number.isFinite(value))return null;
    // Identical center latch to the approved Pages mixer: capture at 7,
    // hold until 12, snap on release. No fake demo-model DSP mutation.
    const keyboard=el.dataset.panKeyboard==="true";
    const bypass=!!event.altKey||el.dataset.panAltPointer==="true";
    const latched=el.dataset.panDetent==="true";
    const width=event.type==="change"?12:latched?12:7;
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
    // Exactly ONE PCM route authority: the admitted ChannelId publisher
    // owns all MAIN/Group WIRE and OUT mutations, including original CH1-10.
    // Calling the old M4 structural patch after native WIRE admission would
    // reject valid routes or attempt to resurrect an incompatible M4 graph.
    const session=mixerSession(),current=controller?.state()?.current;
    if(session?.authority==="CHANNEL_ID"){
      const channelId=nativeIdFor(t);
      const ch=current?.graph?.channels?.find(row=>row.id===channelId);
      const main=current?.graph?.mainChannelId;
      if(!current?.admitted || !channelId || !ch || !main ||
         !current.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION") ||
         !["INPUT","GROUP","FX_RETURN"].includes(ch.role) ||
         !target)return false;
      const group=current.graph.channels.find(row=>
        row.role==="GROUP"&&row.name===target);
      const to=target==="MAIN"?main:group?.id||null;
      if(target!=="NONE"&&!to)return false;
      const dest=ch.primaryOutId||null;
      if((to&&dest===to)||(target==="NONE"&&!dest)){
        controller.subscribe();return true;
      }
      let change;
      if(to){
        change={op:"ROUTE_ONLY",from:channelId,to,
                tap:"POST_FADER",gainLinear:1};
      }else{
        // Exact EdgeId from the independent native readback; never invent a
        // disconnect ID or remove an unrelated parallel SEND.
        const edge=current.graph.edges.find(e=>e.enabled &&
          e.from===channelId && e.to===dest &&
          e.primaryOutput===true);
        if(!edge)return false;
        change={op:"REMOVE",from:channelId,edgeId:edge.edgeId};
      }
      const accepted=controller.intent("APPLY_WIRE_TRANSACTION",channelId,
                                     {changes:[change]});
      if(!accepted.ok){status("Native OUT rejected · "+accepted.error);return false;}
      window.setTimeout(()=>controller?.expire(accepted.requestId),8000);
      status(t.name+" OUT → "+(target==="NONE"?"SOURCE ONLY":target)+
             " · native PCM readback pending");
      return true;
    }
    const channel=m4Index(t);
    const ep=safeNativeCounter(hardware?.device?.epoch),
      rev=safeNativeCounter(legacy()?.routingRevision);
    if(!m4Verified()||channel<0||channel>=10||
       (target!=="MAIN"&&target!=="NONE")||ep===null||rev===null||
       pendingOutputPatch||!deviceReady())return false;
    const accepted=legacy()?.channels?.[channel];
    if(accepted?.main===(target==="MAIN")){
      status("CH "+(channel+1)+" OUT already "+
        (target==="MAIN"?"MAIN":"SOURCE ONLY")+" · native readback");
      controller?.subscribe();return true; // no need to blink audio
    }
    const requestId=++outputSerial;
    pendingOutputPatch={requestId,channel,target,epoch:ep,operation:"SET_PRIMARY_OUT"};
    const menu=document.querySelector('.strip[data-id="'+t.id+'"] select[data-field="output"]');
    // A draft visibly stays selected while pending, but t.output and
    // audio graph remain exclusively the native accepted readback.
    if(menu){
      menu.value=target==="MAIN"?"Master":"SOURCE ONLY (No Main)";
      menu.setAttribute("aria-busy","true");
      menu.title="Routing pending · awaiting native ACK and PCM readback";
    }
    backend.emitEvent("arMixAction",{action:"routingChannelPatch",
      requestId,channel,target,deviceEpoch:ep,revision:rev});
    window.setTimeout(()=>{
      if(pendingOutputPatch?.requestId!==requestId)return;
      pendingOutputPatch=null;
      status("CH "+(channel+1)+" OUT timed out · restoring verified native route");
      controller?.subscribe();
    },8000);
    return true;
  }
  const pendingNativeSources=new Map();
  let activeNativeSource=null,nativeSourceSerial=0;
  function dispatchNativeSource(){
    if(activeNativeSource||!pendingNativeSources.size)return;
    const current=controller?.state()?.current;
    if(!current?.admitted||mixerSession()?.authority!=="CHANNEL_ID")return;
    const [id,choice]=pendingNativeSources.entries().next().value;
    pendingNativeSources.delete(id);
    if(!current.graph.channels.some(ch=>ch.id===id&&ch.role==="INPUT"))
      return nativeToast("Source identity changed · refresh native mixer");
    const requestId="source-native-"+(++nativeSourceSerial);
    activeNativeSource={requestId,id,choice};
    backend.emitEvent("arMixAction",{
      action:"routingNativeSourcePatch",requestId,channelId:id,
      deviceEpoch:current.device.epoch,graphRevision:current.graph.revision,
      rosterRevision:current.graph.rosterRevision,
      mode:choice.mode,left:choice.left,right:choice.right});
    status("Input "+choice.label+" · native PCM readback pending");
    window.setTimeout(()=>{
      if(activeNativeSource?.requestId!==requestId)return;
      activeNativeSource=null;
      nativeToast("Source ACK timed out · previous PCM preserved");
      controller?.subscribe();
    },8000);
  }
  let pendingBusCreate=null,busRequestSerial=0;
  function requestNativeBusCreate(){
    const accepted=controller?.state()?.current;
    const session=mixerSession();
    const capabilities=accepted?.graph?.capabilities?.supportedOps;
    if(pendingBusCreate||session?.authority!=="CHANNEL_ID"||
       !accepted?.admitted||!Array.isArray(capabilities)||
       !capabilities.includes("CREATE_GROUP")){
      nativeToast("BUS needs the verified native ChannelId graph");
      return false;
    }
    const requestId="bus-"+(++busRequestSerial);
    pendingBusCreate={requestId};
    backend.emitEvent("arMixAction",{
      action:"routingNativeBusCreate",requestId,
      deviceEpoch:accepted.device.epoch,
      graphRevision:accepted.graph.revision,
      rosterRevision:accepted.graph.rosterRevision});
    status("Adding a real stereo BUS → MAIN · native PCM pending");
    window.setTimeout(()=>{
      if(pendingBusCreate?.requestId!==requestId)return;
      pendingBusCreate=null;
      status("Add BUS timed out · old PCM kept");
      controller?.subscribe();
    },8000);
    return true;
  }
  let pendingFxInsert=null,fxRequestSerial=0,nativeCatalog=new Map();
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
  function verifiedFxFor(t,slot){
    const lane=m4Index(t),live=legacy()?.fxInstances?.[lane]?.[slot];
    return lane>=0 && Number.isInteger(slot) && slot>=0&&slot<8 &&
      live && typeof live.instanceId==="string" &&
      live.instanceId && typeof live.catalogId==="string"?
      {lane,slot,live}:null;
  }
  function requestNativeFxPower(t,slot,bypass){
    const picked=verifiedFxFor(t,slot);
    if(!picked||!m4Verified())return false;
    const ep=safeNativeCounter(hardware?.device?.epoch),
      rev=safeNativeCounter(legacy()?.routingRevision);
    const {lane,live}=picked,requestId="fxpower-"+(++fxRequestSerial);
    if(ep===null||rev===null||pendingSlotPowers.has(lane+":"+slot))
      return false;
    pendingSlotPowers.set(lane+":"+slot,{requestId,token:live.instanceId,bypass});
    backend.emitEvent("arMixAction",{action:"routingFxBypass",
      requestId,channel:lane,slot,instanceId:live.instanceId,
      deviceEpoch:ep,revision:rev,value:bypass});
    window.setTimeout(()=>{
      if(pendingSlotPowers.get(lane+":"+slot)?.requestId!==requestId)return;
      pendingSlotPowers.delete(lane+":"+slot);
      status("FX bypass timed out · restoring real rack state");
      controller?.subscribe();
    },5000);
    return true;
  }
  // M4 remains an internal PCM implementation; only native mixerSession
  // readback commits UI state. Rack gate never rewrites saved plugin flags.
  function requestM4RackPower(t){
    const lane=m4Index(t),session=mixerSession(),row=session?.channels?.[lane];
    const ep=safeNativeCounter(session?.deviceEpoch),
      rev=safeNativeCounter(session?.routeRevision);
    if(session?.authority!=="M4"||lane<0||!row ||
       typeof row.fxRackBypassed!=="boolean"||ep===null||rev===null||
       pendingM4RackPowers.has(lane))return false;
    const bypassed=!row.fxRackBypassed,requestId="rack-m4-"+(++nativeFxPowerSerial);
    pendingM4RackPowers.set(lane,{requestId,channel:lane,bypassed,
      epoch:String(ep),revision:String(rev),acked:false});
    backend.emitEvent("arMixAction",{action:"routingFxRackBypass",
      requestId,channel:lane,deviceEpoch:ep,revision:rev,value:bypassed});
    window.setTimeout(()=>{
      if(pendingM4RackPowers.get(lane)?.requestId!==requestId)return;
      pendingM4RackPowers.delete(lane);
      status("Native rack gate timed out · restoring accepted readback");
      controller?.subscribe();
    },8000);
    return true;
  }
  function reconcileM4RackReadback(next){
    const session=next?.raw?.mixerSession;
    for(const [lane,p] of pendingM4RackPowers){
      if(session?.authority!=="M4"||
         session.deviceEpoch!==p.epoch||session.routeRevision!==p.revision){
        pendingM4RackPowers.delete(lane);continue;
      }
      const actual=session.channels?.[lane]?.fxRackBypassed;
      if(p.acked && actual===p.bypassed)pendingM4RackPowers.delete(lane);
    }
  }
  function requestAdmittedFxPower(t,action,button){
    const session=mixerSession(),current=controller?.state()?.current;
    const channelId=nativeIdFor(t);
    const ch=current?.graph?.channels?.find(x=>x.id===channelId);
    if(session?.authority!=="CHANNEL_ID"||!current?.admitted||
       !channelId||!ch||pendingNativeFxPower)return false;
    const rack=action==="fxOn",slot=Number(button?.dataset?.slot);
    const insert=!rack&&Number.isInteger(slot)&&slot>=0&&slot<8?
      ch.inserts?.[slot]:null;
    if((rack&&typeof ch.fxRackBypassed!=="boolean")||
       (!rack&&(!insert||typeof insert.id!=="string"||
                !insert.id||typeof insert.bypassed!=="boolean")))return false;
    const bypassed=rack?!ch.fxRackBypassed:!insert.bypassed;
    const requestId="native-rack-"+(++nativeFxPowerSerial);
    pendingNativeFxPower={requestId,channelId,kind:rack?"rack":"insert",
      insertId:insert?.id,bypassed,acked:false};
    backend.emitEvent("arMixAction",{action:rack?
      "routingNativeFxRackBypass":"routingNativeFxInsertBypass",
      requestId,channelId,deviceEpoch:current.device.epoch,
      revision:current.graph.revision,value:bypassed,
      ...(rack?{}:{slot,insertId:insert.id})});
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
  const m4Verified=()=>{
    const m=legacy(),d=hardware?.device;
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
    const current=controller?.state()?.current;
    return t?.kind!=="master" &&
      mixerSession()?.authority==="CHANNEL_ID" &&
      flags.acceptedGraphConnected && current?.admitted===true &&
      !!nativeIdFor(t) &&
      current.graph.capabilities.supportedOps.includes(
        parameter==="faderDb"?"SET_FADER":"SET_PAN");
  };
  function liveScalarReady(parameter,t) {
    if(t?.kind==="master")return parameter==="masterGain" &&
      (m4Verified()||nativeMasterReady());
    if(m4ControlIsOwner())return m4Verified()&&m4Index(t)>=0;
    return channelIdScalarReady(parameter,t);
  }
  const nativeEditIdentity=(t)=>({
    epoch:String(hardware?.device?.epoch),
    authority:mixerSession()?.authority,
    channelId:t?.kind==="master"?null:nativeIdFor(t)
  });
  function sameNativeEditIdentity(edit) {
    const now=nativeEditIdentity(edit.t);
    return edit.epoch===now.epoch &&
      edit.authority===now.authority &&
      edit.channelId===now.channelId;
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
    if(!sameNativeEditIdentity(pending)){
      liveFaderVisualTargets.delete(key);return null;
    }
    if(Math.abs(actualDb-pending.value)<=.11){
      liveFaderVisualTargets.delete(key);return null;
    }
    return pending.value;
  }
  // null means an admitted ChannelId intent is already awaiting its ACK /
  // producer readback. Keep ONLY the newest dB target, never drop release.
  function dispatchLiveScalar(edit) {
    if(!sameNativeEditIdentity(edit) ||
       !liveScalarReady(edit.parameter,edit.t))return false;
    if(edit.t?.kind==="master" || m4ControlIsOwner())
      return sendM4Control(edit.parameter,edit.t,edit.value);
    if(controller?.state()?.pending)return null;
    return intent(edit.parameter==="faderDb"?"SET_FADER":"SET_PAN",
      edit.t,edit.parameter==="faderDb"?
        {valueDb:edit.value}:{value:edit.value/100});
  }
  function flushLiveScalars() {
    for(const [key,edit] of pendingNativeControl) {
      const result=dispatchLiveScalar(edit);
      if(result===null)continue;
      pendingNativeControl.delete(key);
      if(result)lastNativeControlSent.set(key,performance.now());
      else {
        if(liveFaderVisualTargets.get(key)===edit)
          liveFaderVisualTargets.delete(key);
        status("LIVE PCM scalar rejected · native identity changed");
      }
    }
  }
  function streamFaderControl(parameter,t,value,final){
    if(!liveScalarReady(parameter,t))return false;
    const key=controlKey(parameter,t),now=performance.now();
    const edit={parameter,t,value,...nativeEditIdentity(t)};
    pendingNativeControl.set(key,edit); // coalesce: last writer always wins
    if(parameter==="faderDb"||parameter==="masterGain")
      liveFaderVisualTargets.set(key,edit);
    const last=lastNativeControlSent.get(key);
    if(final||last===undefined||now-last>=30)
      flushLiveScalars(); // pointermove publishes BEFORE pointerup
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
    scope?.syncFaderVisual?.({...t,level:db});
  }
  function paintM4FxInstances(m) {
    if(!Array.isArray(m?.fxInstances)||!scope?.tracks)return;
    for(let lane=0;lane<10;lane++){
      const t=scope.tracks.find(x=>x.id===m4TrackIds[lane]);
      const rows=m.fxInstances[lane];
      if(!t||!Array.isArray(rows))continue;
      let change=false;
      const acceptedRack=mixerSession()?.authority==="M4"?
        mixerSession()?.channels?.[lane]?.fxRackBypassed:undefined;
      if(typeof acceptedRack==="boolean" && t.fxOn===acceptedRack){
        t.fxOn=!acceptedRack;change=true;
      }
      for(let slot=0;slot<8;slot++){
        const live=rows[slot],entry=nativeCatalog.get(live?.catalogId);
        const name=live?(entry?.name||"VST3 Loaded"):"None";
        if(t.fx[slot]!==name || t.slotBypass[slot]!==Boolean(live?.bypassed) ||
           (t.fxInsertIds?.[slot]?.id||null)!==(live?.instanceId||null)){
          t.fx[slot]=name;
          t.slotBypass[slot]=Boolean(live?.bypassed);
          if(!Array.isArray(t.fxInsertIds))t.fxInsertIds=Array(8).fill(null);
          t.fxInsertIds[slot]=live?{id:live.instanceId,name}:null;
          change=true;
        }
      }
      if(change && scope.activeFaderId!==t.id)
        scope.refreshStrip?.(t);
    }
  }
  function sendM4Control(parameter,t,value) {
    if((t?.kind!=="master"&&!m4ControlIsOwner()) ||
       !(m4Verified()||(t?.kind==="master"&&nativeMasterReady())) ||
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
    const session=next?.raw?.mixerSession;
    for(const [key,p] of pendingM4Controls) {
      const revision=p.channel===-1?
        next?.raw?.legacyM4?.routingRevision:session?.routeRevision;
      if(next.device.epoch!==p.epoch || revision!==p.revision ||
         (p.channel!==-1 && session?.authority!=="M4")) {
        pendingM4Controls.delete(key);
        status("STALE M4 control · device/route changed; restoring readback");
        continue;
      }
      const row=p.channel===-1?null:session?.channels?.[p.channel];
      const value=p.channel===-1?
        (p.parameter==="masterGain"?session?.main?.db:session?.main?.mute):
        p.parameter==="faderDb"?row?.controls?.faderDb:
        p.parameter==="pan"?row?.controls?.pan*100:
        row?.controls?.[p.parameter];
      const matches=typeof p.value==="boolean"?value===p.value:
        Number.isFinite(value)&&Math.abs(value-p.value)<=
          (p.parameter==="pan"?.8:.11);
      if(p.acked && matches)pendingM4Controls.delete(key);
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
        const oldDb=t.level;
        const visualDb=pendingFaderVisualDb(t,db);
        const switchChanged=t.mute!==c.mute||t.solo!==c.solo;
        t.level=db;t.pan=pan;t.mute=c.mute;t.solo=c.solo;
        if(switchChanged && scope.activeFaderId!==t.id){
          scope.refreshStrip?.(t);
          if(visualDb!==null)scope.syncFaderVisual?.({...t,level:visualDb});
          m4Meters.delete(t.id);
        }else if(Math.abs(oldDb-db)>.015 &&
                 scope.activeFaderId!==t.id&&visualDb===null)
          scope.syncFaderVisual?.(t);
        const panKnob=document.querySelector(
          '.strip[data-id="'+t.id+'"] .center-pan');
        if(panKnob && document.activeElement!==panKnob)
          panKnob.value=String(pan);
      }
      if(scope.master && Number.isFinite(session.main?.db) &&
         Math.abs(scope.master.level-session.main.db)>.015){
        const visualDb=pendingFaderVisualDb(scope.master,session.main.db);
        scope.master.level=session.main.db;
        if(scope.activeFaderId!==scope.master.id && visualDb===null)
          scope.syncFaderVisual?.(scope.master);
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
    safeNativeCounter(hardware.device.epoch)!==null &&
    hardware.device.sampleRate>0 && hardware.device.bufferFrames>0 &&
    Array.isArray(hardware.device.enabledInputs) &&
    hardware.device.enabledInputs.length>0 &&
    Array.isArray(hardware.device.enabledOutputs) &&
    hardware.device.enabledOutputs.length>=2 &&
    (mixerSession()?.authority==="CHANNEL_ID"
      ? controller?.state()?.current?.admitted===true &&
        flags.acceptedGraphConnected
      : (mixerSession()?.authority==="M4" ||
         mixerSession()?.authority==="NONE") &&
        legacy()?.active===true &&
        safeNativeCounter(legacy()?.routingRevision)!==null));
  function updateHardwarePanel(next) {
    hardware=next || null;
    refreshRealM4Inputs(next);
    const d=next?.device;
    const deviceOpen=Boolean(d?.backend && d.sampleRate>0 &&
      d.enabledOutputs.length>=2);
    const label=document.getElementById("transport");
    const playback=String(d?.playbackDevice||"unknown output");
    const virtualOut=/VB-Audio Virtual Cable|CABLE Input/i.test(playback);
    const monitorButton=document.querySelector?.("[data-native-monitor]");
    if(monitorButton){
      monitorButton.textContent=d?.outputArmed?"MONITOR OFF":"MONITOR ON";
      monitorButton.title=d?.outputArmed?
        "Mute actual MAIN output immediately":"Listen through verified MAIN hardware output";
      monitorButton.disabled=!d?.outputArmed && !deviceReady();
      monitorButton.dataset.armed=String(!!d?.outputArmed);
    }
    if(scope?.master && deviceOpen &&
       scope.master.output!==playback){
      scope.master.output=playback;
      if(!next?.admitted)scope.render?.();
    }
    const transport=!deviceOpen?"Audio device not ready":
      (d.outputArmed?"MONITOR ON":"MONITOR OFF")+" · OUT "+playback+
      (virtualOut?" · VIRTUAL / NO SPEAKERS":"");
    if(label && label.textContent!==transport)label.textContent=transport;
    const warning=document.querySelector?.(".status-bar .warning");
    const warningText=!deviceOpen?"NO VERIFIED AUDIO DEVICE":
      d.outputArmed?(next.admitted?"NATIVE PCM · HARDWARE FAT PENDING":
        "MAIN ARMED · PCM NOT YET ADMITTED"):
      legacy()?.routeAccepted?"INPUT PATCHED · OUTPUT MUTED":
      "NO AUDIBLE OUTPUT · PATCH INPUT + ARM";
    if(warning && warning.textContent!==warningText)warning.textContent=warningText;
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
    const muteDisabled=!deviceOpen || !d.outputArmed;
    if(muteBtn && muteBtn.disabled!==muteDisabled)muteBtn.disabled=muteDisabled;
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
  let devicePanel=null,deviceInventory=null,deviceBusy=false,
      deviceRequest=0,pendingDeviceMute=null,deviceChoice=null,deviceResult="";
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
    element.textContent=message;element.hidden=false;
    window.clearTimeout(toastTimer);
    toastTimer=window.setTimeout(()=>{element.hidden=true;},3400);
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
        .armix-audio-dialog h2{font-size:16px;font-weight:600;margin:0}
        .armix-audio-dialog label{display:grid;gap:5px;color:#a9c5d2}
        .armix-audio-dialog select{background:#09202c;border:1px solid #476778;
          border-radius:4px;color:#edf9ff;min-height:30px;width:100%;padding:3px 7px}
        .armix-audio-dialog button{background:#244254;border:1px solid #557b8d;
          border-radius:4px;color:#e9f8ff;min-height:30px;padding:4px 12px}
        .armix-audio-dialog button:disabled{opacity:.45}
        .armix-audio-dialog .armix-device-actions{display:flex;gap:8px;justify-content:flex-end}
        .armix-audio-dialog .armix-device-note{color:#a4c4d0;line-height:1.4}
        .armix-audio-dialog details{border-top:1px solid #345666;padding-top:8px}
        .armix-audio-dialog summary{cursor:pointer;color:#c5e1ed}
        .armix-audio-pins{display:grid;grid-template-columns:1fr 1fr;gap:8px;
          max-height:135px;overflow:auto;padding:5px 0}
        .armix-audio-pins label{display:flex;align-items:center;gap:5px;font-size:11px}
      `;
      document.head.appendChild(style);
      devicePanel=document.createElement("div");
      devicePanel.className="armix-audio-shade";
      devicePanel.innerHTML=`<section class="armix-audio-dialog" role="dialog"
        aria-modal="true" aria-label="Audio hardware setup">
        <h2>Audio I/O</h2>
        <label>Driver<select data-audio-field="backend"></select></label>
        <label>Input device<select data-audio-field="capture"></select></label>
        <label>Output / monitor device<select data-audio-field="playback"></select></label>
        <div class="armix-device-note" data-audio-active>Reading current audio device…</div>
        <div class="armix-device-note">The output device above is the real destination.
          A virtual cable does not automatically play through headphones.</div>
        <details><summary>Advanced · rate, buffer and channel pins</summary>
          <label>Sample rate<select data-audio-field="rate"></select></label>
          <label>Buffer<select data-audio-field="buffer"></select></label>
          <div class="armix-device-pins" data-audio-pins></div>
        </details>
        <div class="armix-device-actions">
          <button type="button" data-audio-close>Close</button>
          <button type="button" data-audio-apply>Apply device</button>
        </div></section>`;
      devicePanel.querySelector("[data-audio-close]").onclick=()=>{
        devicePanel.hidden=true;
      };
      devicePanel.addEventListener("click",e=>{
        if(e.target===devicePanel)devicePanel.hidden=true;
      });
      devicePanel.addEventListener("change",e=>{
        if(!deviceChoice)return;
        deviceResult="";
        const pinSide=e.target.dataset.audioPin;
        if(pinSide==="input"||pinSide==="output"){
          const index=Number(e.target.dataset.pinIndex);
          const key=pinSide==="input"?"inputPins":"outputPins";
          if(!Number.isInteger(index)||!Array.isArray(deviceChoice[key]))return;
          const next=new Set(deviceChoice[key]);
          if(e.target.checked)next.add(index);else next.delete(index);
          deviceChoice[key]=[...next].sort((a,b)=>a-b);
          renderDevicePanel();return;
        }
        const field=e.target.dataset.audioField;
        if(!field)return;
        if(field==="backend"){
          deviceChoice.backend=e.target.value;
          const t=deviceInventory?.types?.find(x=>x.name===deviceChoice.backend);
          deviceChoice.capture=t?.inputs?.[0]||"";
          deviceChoice.playback=t?.separate?t?.outputs?.[0]||"":deviceChoice.capture;
          deviceChoice.inputPins=null;deviceChoice.outputPins=null;
          deviceChoice.rate=0;deviceChoice.buffer=0;
        }else if(field==="capture"){
          deviceChoice.capture=e.target.value;
          const t=deviceInventory?.types?.find(x=>x.name===deviceChoice.backend);
          if(t&&!t.separate)deviceChoice.playback=e.target.value;
          deviceChoice.inputPins=null;deviceChoice.outputPins=null;
        }else if(field==="playback"){
          deviceChoice.playback=e.target.value;
          deviceChoice.inputPins=null;deviceChoice.outputPins=null;
        }else if(field==="rate")deviceChoice.rate=Number(e.target.value)||0;
        else if(field==="buffer")deviceChoice.buffer=Number(e.target.value)||0;
        renderDevicePanel();
      });
      devicePanel.querySelector("[data-audio-apply]").onclick=()=>{
        if(deviceBusy||pendingDeviceMute||!deviceChoice||!deviceInventory?.active)return;
        const draft=deviceChoice,active=deviceInventory.active;
        if(!draft.backend||!draft.capture||!draft.playback)return;
        if(hardware?.device?.outputArmed){
          // Correlate one deliberate MUTE to this Apply. An unrelated ACK
          // must never authorize a hardware switch.
          const requestId="device-mute-"+(++deviceRequest);
          pendingDeviceMute={requestId,epoch:String(hardware.device.epoch)};
          devicePanel.querySelector("[data-audio-apply]").disabled=true;
          devicePanel.querySelector("[data-audio-active]").textContent=
            "Muting current output before changing driver…";
          backend.emitEvent("arMixAction",{action:"monitor",value:false,requestId});
          window.setTimeout(()=>{
            if(pendingDeviceMute?.requestId!==requestId)return;
            pendingDeviceMute=null;
            deviceResult="Driver change cancelled: no verified MUTE acknowledgement.";
            renderDevicePanel();
          },8000);
        }else sendDeviceApply();
      };
      document.body.appendChild(devicePanel);
    }
    devicePanel.hidden=false;
    const active=devicePanel.querySelector("[data-audio-active]");
    deviceResult="";
    if(active)active.textContent="Reading verified audio devices…";
    backend.emitEvent("arMixAction",{action:"audioDeviceInventory"});
  }
  function fillAudioSelect(name,values,current){
    const el=devicePanel?.querySelector('[data-audio-field="'+name+'"]');
    if(!el)return;
    el.replaceChildren(...values.map(value=>{
      const option=document.createElement("option");
      option.value=value;option.textContent=value;return option;
    }));
    if(values.includes(current))el.value=current;
    el.disabled=deviceBusy;
  }
  function renderDevicePanel(){
    if(!devicePanel||devicePanel.hidden||!deviceInventory)return;
    const active=deviceInventory.active||{},types=deviceInventory.types||[];
    if(!deviceChoice){
      const backendName=active.backend||types[0]?.name||"";
      const selected=types.find(x=>x.name===backendName);
      const capture=active.captureDevice||selected?.inputs?.[0]||"";
      deviceChoice={backend:backendName,capture,
        playback:active.playbackDevice||
          (selected?.separate?selected?.outputs?.[0]:capture)||"",
        inputPins:Array.isArray(active.enabledInputs)?active.enabledInputs.slice():null,
        outputPins:Array.isArray(active.enabledOutputs)?active.enabledOutputs.slice():null,
        rate:0,buffer:0};
    }
    const t=types.find(x=>x.name===deviceChoice.backend);
    fillAudioSelect("backend",types.map(x=>x.name),deviceChoice.backend);
    fillAudioSelect("capture",t?.inputs||[],deviceChoice.capture);
    fillAudioSelect("playback",t?.outputs||[],deviceChoice.playback);
    const same=active.open&&active.backend===deviceChoice.backend&&
      active.captureDevice===deviceChoice.capture&&
      active.playbackDevice===deviceChoice.playback;
    const rates=same?active.rates||[]:[];
    const buffers=same?active.buffers||[]:[];
    fillAudioSelect("rate",["0",...rates.map(String)],String(deviceChoice.rate||0));
    fillAudioSelect("buffer",["0",...buffers.map(String)],String(deviceChoice.buffer||0));
    const rateSelect=devicePanel.querySelector('[data-audio-field="rate"]');
    const bufferSelect=devicePanel.querySelector('[data-audio-field="buffer"]');
    if(rateSelect){rateSelect.options[0].textContent="Driver default";rateSelect.disabled=!same||deviceBusy;}
    if(bufferSelect){bufferSelect.options[0].textContent="Driver default";bufferSelect.disabled=!same||deviceBusy;}
    const pins=document.querySelector(".armix-audio-dialog [data-audio-pins]");
    if(pins){
      pins.replaceChildren();
      if(same){
        const container=document.createElement("div");
        container.className="armix-audio-pins";
        for(const [side,names,key,enabled] of [
          ["input",active.inputNames,"inputPins",active.enabledInputs],
          ["output",active.outputNames,"outputPins",active.enabledOutputs]]){
          const group=document.createElement("div");
          const title=document.createElement("strong");
          title.textContent=side==="input"?"Capture pins":"Playback pins";
          group.appendChild(title);
          (names||[]).forEach((name,index)=>{
            const label=document.createElement("label");
            const control=document.createElement("input");
            control.type="checkbox";control.dataset.audioPin=side;
            control.dataset.pinIndex=String(index);
            control.checked=(deviceChoice[key]||enabled||[]).includes(index);
            control.disabled=deviceBusy;
            label.append(control,document.createTextNode(String(name)));
            group.appendChild(label);
          });
          container.appendChild(group);
        }
        pins.appendChild(container);
      }else pins.textContent="Apply this device first to inspect its real enabled pins.";
    }
    const activeText=devicePanel.querySelector("[data-audio-active]");
    if(activeText && !deviceBusy)activeText.textContent=deviceResult||(active.open?
      "ACTIVE · "+active.backend+" · "+active.captureDevice+
        " → "+active.playbackDevice+" · "+active.rate+" Hz / "+active.buffer+" samples":
      "No verified audio device. Select a supported input and output.");
    devicePanel.querySelector("[data-audio-apply]").disabled=deviceBusy||
      !!pendingDeviceMute||!deviceChoice.backend||!deviceChoice.capture||
      !deviceChoice.playback;
  }
  function sendDeviceApply(){
    if(!deviceChoice||!deviceInventory?.active)return;
    const c=deviceChoice,a=deviceInventory.active;
    const same=a.open&&a.backend===c.backend&&
      a.captureDevice===c.capture&&a.playbackDevice===c.playback;
    deviceBusy=true;
    const requestId="approved-io-"+(++deviceRequest);
    devicePanel.dataset.pendingRequest=requestId;
    devicePanel.querySelector("[data-audio-active]").textContent=
      "Applying audio device; verifying native readback…";
    backend.emitEvent("arMixAction",{action:"audioDeviceApply",
      requestId,expectedDeviceGeneration:Number(a.deviceGeneration),
      config:{backend:c.backend,captureDevice:c.capture,
        playbackDevice:c.playback,sampleRate:same?c.rate:0,
        bufferSize:same?c.buffer:0,
        enabledInputs:same?c.inputPins:[],
        enabledOutputs:same?c.outputPins:[]}});
  }
  if(connected){
    backend.addEventListener("arMixAudioDeviceInventory",inventory=>{
      if(!inventory||!Array.isArray(inventory.types))return;
      deviceInventory=inventory;
      if(!deviceBusy)deviceChoice=null;
      renderDevicePanel();
    });
    backend.addEventListener("arMixAudioDeviceApplyAck",ack=>{
      if(!devicePanel||ack?.requestId!==devicePanel.dataset.pendingRequest)return;
      deviceBusy=false;
      if(ack.active)deviceInventory.active=ack.active;
      const accepted=ack.ok===true&&ack.state==="ACCEPTED_MUTED" &&
        ack.active?.open===true && ack.active?.authorized===true &&
        Array.isArray(ack.active.enabledInputs) &&
        ack.active.enabledInputs.length>0 &&
        Array.isArray(ack.active.enabledOutputs) &&
        ack.active.enabledOutputs.length>=2;
      deviceResult=accepted?
        "Audio device ready · choose an IN on any channel":
        "Device rejected · "+(ack.error||"native hardware readback failed");
      deviceChoice=null;
      if(accepted){
        devicePanel.hidden=true;
        status("AUDIO ACTIVE · "+ack.active.backend+" · "+
          ack.active.rate+" Hz / "+ack.active.buffer+
          " samples · MONITOR muted until a real IN is selected");
        nativeToast("Audio device connected · "+ack.active.backend+
          " · "+ack.active.rate+" Hz · choose channel IN");
        controller?.subscribe();
      }else renderDevicePanel();
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
    muteBtn=item("button","tool","Mute output");
    muteBtn.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"monitor",value:false});
      status("NATIVE: output MUTE requested; awaiting JUCE ACK.");
    });
    hardwareNote=item("span","more-tools-caption","Device not initialized");
    hardwareNote.style.whiteSpace="normal";
    // Everyday I/O remains in the one approved /preview/ mixer. Keep the
    // raw JUCE selector as an explicitly advanced recovery tool only.
    inputSelect.hidden=true;routeBtn.hidden=true;armBtn.hidden=true;
    const advanced=document.createElement("details");
    advanced.className="arNativeAdvancedRecovery";
    const summary=item("summary","more-tools-caption","Advanced driver recovery");
    const driver=item("button","tool","Open JUCE driver recovery");
    driver.addEventListener("click",()=>{
      backend.emitEvent("arMixAction",{action:"audioSetup"});
    });
    advanced.append(summary,driver);
    // First Sound P0: these are REAL JUCE diagnostic actions, not demo
    // toggles. The marker snapshots the 5-second PRE-event counters and
    // Windows RAM/process paging context from the existing 1 Hz control
    // timer. No sampling or file I/O happens in the ASIO callback.
    const healthTitle=item("span","more-tools-caption","AUDIO HEALTH");
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
    menu.append(caption,setup,muteBtn,hardwareNote,
      healthTitle,mark,report,advanced);
    // Single discoverable AUDIO I/O button next to ordinary mixer tools.
    // The existing More entry remains a shortcut to THIS SAME panel.
    const toolbar=document.querySelector(".command-bar");
    if(toolbar && !toolbar.querySelector("[data-native-io]")){
      const io=item("button","tool","Audio I/O");
      io.dataset.nativeIo="true";
      io.title="Select ASIO/WASAPI driver and capture/playback device";
      io.addEventListener("click",openNativeAudioIO);
      toolbar.insertBefore(io,document.getElementById("moreTools"));
      const monitor=item("button","tool","MONITOR ON");
      monitor.dataset.nativeMonitor="true";
      monitor.addEventListener("click",()=>{
        const armed=hardware?.device?.outputArmed===true;
        if(!armed && (!deviceReady()||legacy()?.routeAccepted!==true ||
            virtualCableFeedbackPair())){
          status("MONITOR OFF · select real input and safe playback device");
          return;
        }
        const ep=safeNativeCounter(hardware?.device?.epoch),
          rev=safeNativeCounter(legacy()?.routingRevision);
        if(!armed&&(ep===null||rev===null))return;
        backend.emitEvent("arMixAction",{action:"monitor",value:!armed,
          requestId:"manual-monitor-"+(++m4RequestSerial),
          ...(armed?{}:{expectedEpoch:ep,expectedRevision:rev})});
        status(armed?"Hardware MUTE requested":"MAIN monitor verifying hardware route");
      });
      toolbar.insertBefore(monitor,io);
    }
    updateHardwarePanel(hardware);
  }

  // One control-thread routing transaction at a time, but NEVER lock other
  // dropdowns or lose their choices. Latest choice wins per channel.
  function dispatchQueuedSource(){
    if(nativeUiSourcePending||pendingSourceQueue.size===0)return;
    if(!deviceReady()){pendingSourceQueue.clear();return;}
    const [channel,choice]=pendingSourceQueue.entries().next().value;
    pendingSourceQueue.delete(channel);
    const ep=safeNativeCounter(hardware?.device?.epoch),
      rev=safeNativeCounter(legacy()?.routingRevision);
    if(ep===null||rev===null)return;
    const requestId=++sourceSerial;
    nativeUiSourcePending={requestId,channel,choice,epoch:ep,revision:rev,
      autoListen:legacy()?.routeAccepted!==true &&
        hardware?.device?.outputArmed!==true};
    backend.emitEvent("arMixAction",{action:"routingSourceSwitch",
      requestId,channel,mode:choice.mode,left:choice.left,right:choice.right,
      deviceEpoch:ep,revision:rev});
    window.setTimeout(()=>{
      if(nativeUiSourcePending?.requestId!==requestId)return;
      nativeUiSourcePending=null;
      status("CH "+(channel+1)+" INPUT timeout · native state preserved");
      controller.subscribe();
    },8000);
  }
  function onNativeReadback(next) {
    updateHardwarePanel(next);
    // The installed native SessionSnapshot selects exactly one UI authority.
    const session=next?.raw?.mixerSession;
    if(!session || session.deviceEpoch!==next.device.epoch) {
      flags.acceptedGraphConnected=false;
      status("NATIVE SESSION UNAVAILABLE · controls unchanged");
      return;
    }
    // Never paint unacknowledged controls; pending slider caps are kept
    // separately by pendingFaderVisualDb while accepted telemetry continues.
    // A pending scalar ACK is not a global snapshot gate. Only accepted producer
    // data can update controls; independent meters and FX racks keep moving.
    reconcileM4ControlReadback(next);
    reconcileM4RackReadback(next);
    if(session.authority==="M4" && m4Verified()) {
      paintM4Fallback(next);
      scope?.syncVerifiedM4Wires?.(session.channels.map(ch=>ch.source),m4TrackIds);
    }
    // A second channel's IN can be selected while the first transaction is
    // in flight. Always dispatch its latest choice with a FRESH native route
    // revision, not stale tokens captured before the previous ACK.
    if(!nativeUiSourcePending && pendingSourceQueue.size)
      window.queueMicrotask(()=>dispatchQueuedSource());
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=session.authority==="CHANNEL_ID" &&
      next.admitted && caps?.callbackOwnsChannelIds===true;
    const addBus=document.getElementById("addBus");
    if(addBus){
      addBus.hidden=!nativeAdmitted ||
        !caps?.supportedOps?.includes("CREATE_GROUP");
      addBus.disabled=!!pendingBusCreate;
    }
    if (nativeAdmitted && !activeNativeSource && pendingNativeSources.size) {
      // A rejected/timeout source command may leave the graph revision
      // unchanged. Drain the next distinct channel after this authoritative
      // snapshot even if the GUI signature is otherwise identical.
      window.queueMicrotask(dispatchNativeSource);
    }
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";lastNativeTopologySignature="";
      stopDemo();
      status(m4Verified()?
        "NATIVE M4 · real PCM faders and meters · ChannelId buses not yet admitted":
        deviceReady()?
        "NATIVE G0 · verified physical IN ready · select a channel input":
        hardware?.device?.enabledInputs?.length===0?
        "NO CAPTURE PINS · Audio I/O: enable an actual hardware input":
        "NATIVE G0 NOT READY · open Audio I/O and verify device / native route");
      return;
    }
    // Control readback may change at the SAME structural graph revision.
    // Compare the actual accepted controls/edges, never skip native fader updates.
    const signature=next.identity+"|"+JSON.stringify(next.graph.channels.map(ch=>[
      ch.id,ch.controls,ch.inserts,ch.fxRackBypassed,ch.primaryOutId]))+"|"+JSON.stringify(next.graph.edges);
    if (signature===lastPaintedSignature) {
      // Telemetry often advances without a structural graph revision.
      paintNativeMeters(next);
      return;
    }
    if (!projectNativeGraphWithoutFocusLoss(next)) {
      flags.acceptedGraphConnected=false;flags.productionCallbackLive=false;
      stopDemo();status("NATIVE READBACK INVALID · existing GUI controls unchanged");
      return;
    }
    flags.productionCallbackLive=next.productLive;flags.acceptedGraphConnected=true;
    lastPaintedSignature=signature;stopDemo();paintNativeMeters(next);
    if(pendingFxInsert?.authority==="CHANNEL_ID"){
      const p=pendingFxInsert;
      const inserted=next.graph.channels.find(ch=>ch.id===p.channelId)
        ?.inserts?.[p.slot];
      // The selected catalog key is NOT the live VST3 instance token.
      // Match catalog against catalog, while requiring a real processor ID.
      if(inserted?.catalogId===p.id &&
         typeof inserted.id==="string" && inserted.id.length>0){
        pendingFxInsert=null;
        scope?.closeNativePicker?.();
        status("VST3 loaded · "+inserted.name+" · native PCM readback verified");
      }
    }
    if(pendingNativeFxPower?.acked){
      const p=pendingNativeFxPower;
      const ch=next.graph.channels.find(x=>x.id===p.channelId);
      const actual=p.kind==="rack"?ch?.fxRackBypassed:
        ch?.inserts?.find(x=>x.id===p.insertId)?.bypassed;
      if(actual===p.bypassed)pendingNativeFxPower=null;
    }
    status((next.productLive?"NATIVE PRODUCT LIVE":"NATIVE PCM GRAPH ACCEPTED · FAT PENDING")+
      " · device "+next.device.epoch+
      " · graph "+next.graph.revision+" · roster "+next.graph.rosterRevision);
  }
  if (connected) {
    controller = protocol.createController({
      emit:(name,payload)=>backend.emitEvent(name,payload),
      onAccepted:onNativeReadback,
      onStatus:state=>{
        if(state.lastError==="PENDING_NATIVE_ACK"||state.lastError==="PENDING_ACCEPTED_SNAPSHOT")
          status("NATIVE "+state.lastError+" · awaiting authoritative readback");
        if(!state.pending && state.lastError==="ACCEPTED_READBACK" &&
           pendingNativeControl.size)queueLiveControlFlush();
        // Rejection cleanup and native rollback must BOTH run.
        // Former else-if was unreachable after the cleanup branch matched.
        const failed=!state.pending &&
          state.lastError!=="ACCEPTED_READBACK" &&
          state.lastError!=="NONE" &&
          state.lastError!=="NATIVE_NOT_SUBSCRIBED";
        if(failed){
          if(pendingFxInsert?.authority==="CHANNEL_ID"){
            pendingFxInsert=null;
            nativeToast("VST3 insert rejected · "+state.lastError);
          }
          for(const [key,edit] of pendingNativeControl)
            if(edit.authority==="CHANNEL_ID")pendingNativeControl.delete(key);
          for(const [key,edit] of liveFaderVisualTargets)
            if(edit.authority==="CHANNEL_ID")liveFaderVisualTargets.delete(key);
          lastNativeError=state.lastError;
          // Never destroy an active pointer capture. Once the user releases,
          // restore only the last independently accepted native controls.
          if(flags.acceptedGraphConnected && (scope.activeFaderId??-1)<0){
            if(!document.activeElement?.matches?.(".strip select"))
              scope.render?.();
            controller.subscribe();
            if(state.current)paintNativeMeters(state.current);
          }
        }
      }
    });
    backend.addEventListener("arMixConsoleSnapshot", snapshot=>{
      // Stage A: do not add time-sampling overhead unless explicitly armed.
      const profiler=window.ArSonMixPerf;
      if(!profiler?.active){controller.onSnapshot(snapshot);return;}
      const started=performance.now();
      try{controller.onSnapshot(snapshot);}
      finally{profiler.sample("nativeSnapshotJsMs",performance.now()-started);}
    });
    backend.addEventListener("arMixConsoleAck", ack=>controller.onAck(ack));
    backend.addEventListener("arMixNativeSourceAck",ack=>{
      if(!activeNativeSource||activeNativeSource.requestId!==ack?.requestId)return;
      activeNativeSource=null;
      if(ack.ok!==true)
        nativeToast("Native input rejected · "+(ack?.error||"stale PCM"));
      else status("Native input accepted · checking subsequent PCM readback");
      controller?.subscribe();
    });
    backend.addEventListener("arMixNativeBusAck",ack=>{
      if(!pendingBusCreate||pendingBusCreate.requestId!==ack?.requestId)return;
      pendingBusCreate=null;
      if(ack.ok===true && typeof ack.createdId==="string" &&
         /^[1-9]\d*$/.test(ack.createdId))
        status("BUS accepted by native PCM · verifying later graph readback");
      else nativeToast("Add BUS rejected · "+(ack?.error||"native graph error"));
      controller?.subscribe();
    });
    backend.addEventListener("arMixNativeFxPowerAck",ack=>{
      const p=pendingNativeFxPower;
      if(!p||ack?.requestId!==p.requestId)return;
      if(ack?.ok!==true || ack?.channelId!==p.channelId ||
         ack?.bypassed!==p.bypassed){
        pendingNativeFxPower=null;
        status("NATIVE FX rejected · "+(ack?.error||"stale epoch or insert"));
      }else p.acked=true;
      controller.subscribe(); // ACK is not independently observed PCM state
    });
    backend.addEventListener("arMixRoutingFxRackAck",ack=>{
      const p=pendingM4RackPowers.get(ack?.channel);
      if(!p||ack?.requestId!==p.requestId)return;
      if(ack?.ok!==true || String(ack.deviceEpoch)!==p.epoch ||
         String(ack.routingRevision)!==p.revision ||
         ack.bypassed!==p.bypassed){
        pendingM4RackPowers.delete(p.channel);
        status("Native M4 rack gate rejected · "+(ack?.error||"stale route"));
      }else p.acked=true;
      controller.subscribe(); // later authoritative M4 snapshot required
    });
    backend.addEventListener("arMixSourceSwitchAck", ack=>{
      if(nativeUiSourcePending &&
         ack?.requestId===nativeUiSourcePending.requestId) {
        const p=nativeUiSourcePending;
        nativeUiSourcePending=null;
        const next=ack?.state||{},row=next.acceptedRoute?.channels?.[p.channel];
        const verified=ack.ok===true && next.ready===true &&
          Number(next.epoch)===p.epoch && next.hasAcceptedRoute===true &&
          Number(next.revision)>=p.revision &&
          row?.mode===p.choice.mode && row?.left===p.choice.left &&
          row?.right===p.choice.right;
        if(verified) {
          // Native publisher + engine own the accepted graph; refresh the
          // UI only from a subsequent authoritative snapshot.
          controller.subscribe();
          const firstListen=p.autoListen && row?.main===true &&
             (next.nativeChannelIdPcmActive===true ||
              (Array.isArray(next.channelControls)&&next.channelControls.length===10)) &&
             hardware?.device?.epoch===String(p.epoch);
          // Never arm physical audio when the GUI still lacks admitted PCM
          // controls, or when capture and playback feed the *same* VB Cable.
          // Native C++ enforces the feedback policy a second time.
          if(firstListen && !virtualCableFeedbackPair()) {
            backend.emitEvent("arMixAction",{action:"monitor",value:true,
              expectedEpoch:p.epoch,expectedRevision:Number(next.revision)});
          }
          if(firstListen && virtualCableFeedbackPair())
            status("MONITOR OFF · VB-Cable feedback loop; select physical playback");
          else if(p.autoListen && !firstListen)
            status("CH "+(p.channel+1)+" routed · PCM controls not yet admitted; MONITOR OFF");
          else
            status("CH "+(p.channel+1)+" native input accepted");
        } else {
          status("NATIVE INPUT REJECTED · "+(ack?.error||"stale routing"));
          controller.subscribe();
        }
        return;
      }
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
      const accepted=ack.ok===true &&
        String(ack.deviceEpoch)===p.epoch &&
        String(ack.routingRevision)===p.revision &&
        ack.channel===p.channel && ack.parameter===p.parameter;
      if(!accepted) {
        pendingM4Controls.delete(key);
        if(p.parameter==="masterGain")liveFaderVisualTargets.delete("masterGain:main");
        else if(p.parameter==="faderDb")
          liveFaderVisualTargets.delete("faderDb:"+m4TrackIds[p.channel]);
        status("NATIVE M4 REJECTED · "+(ack?.error||"stale control token"));
      } else p.acked=true;
      controller.subscribe(); // request later independent producer readback
    });
    backend.addEventListener("arMixChannelPatchAck",ack=>{
      if(!pendingOutputPatch||ack?.requestId!==pendingOutputPatch.requestId)return;
      const p=pendingOutputPatch;
      pendingOutputPatch=null;
      const t=scope?.tracks?.find(x=>m4Index(x)===p.channel);
      const el=t?document.querySelector(
        '.strip[data-id="'+t.id+'"] select[data-field="output"]'):null;
      if(el){
        el.removeAttribute("aria-busy");
        el.title="Primary output · verified native routing";
      }
      const row=ack?.state?.acceptedRoute?.channels?.[p.channel];
      const accepted=ack?.ok===true && Number(ack.state?.epoch)===p.epoch &&
        (p.operation==="SET_PROCESSING_FORMAT"?
          row?.channelFormat===p.target:
          row?.main===(p.target==="MAIN"));
      if(accepted) {
        if(el&&p.operation==="SET_PRIMARY_OUT")
          el.value=p.target==="MAIN"?"Master":"SOURCE ONLY (No Main)";
        status("CH "+(p.channel+1)+" "+
          (p.operation==="SET_PROCESSING_FORMAT"?"M/ST ":"OUT ")+p.target+
          " accepted · verifying native PCM");
      } else {
        if(el)el.value=t?.output||"Master";
        status("CH "+(p.channel+1)+" "+
          (p.operation==="SET_PROCESSING_FORMAT"?"M/ST":"OUT")+
          " rejected · "+(ack?.error||"stale route"));
        nativeToast(ack?.error||"Native channel route rejected");
      }
      controller.subscribe();
    });
    backend.addEventListener("arMixRoutingFxBypassAck",ack=>{
      const key=(Number(ack?.channel)||0)+":"+Number(ack?.slot);
      // Legacy ACKs without channel/requestId can never authorize an
      // arbitrary in-flight slot or paint an invented bypass state.
      const pending=pendingSlotPowers.get(key);
      if(!pending||pending.requestId!==ack?.requestId)return;
      pendingSlotPowers.delete(key);
      if(ack?.ok!==true)
        nativeToast(ack?.error||"Native VST3 bypass rejected");
      controller?.subscribe(); // real FxInstance state owns visual power
    });
    backend.addEventListener("arMixRoutingFxEditorAck",ack=>{
      if(ack?.ok!==true)
        nativeToast(ack?.error||"VST3 editor could not open");
      else status("Native VST3 editor opened");
    });
    backend.addEventListener("arMixRoutingFxInsertAck",ack=>{
      if(!pendingFxInsert||pendingFxInsert.authority==="CHANNEL_ID"||
         ack?.requestId!==pendingFxInsert.requestId)return;
      const p=pendingFxInsert;pendingFxInsert=null;
      const actual=ack?.state?.fxInstances?.[p.channel]?.[p.slot];
      if(ack?.ok===true && actual?.catalogId===p.id &&
         typeof actual?.instanceId==="string"&&actual.instanceId.length){
        status("CH "+(p.channel+1)+" FX loaded · verified native VST3");
        scope?.closeNativePicker?.();
        scope?.setNativeCatalog?.([...nativeCatalog.values()]);
        controller.subscribe();
      }else {
        const reason=ack?.error||"native FX instance missing from accepted PCM";
        status("FX INSERT REJECTED · "+reason);
        nativeToast(reason);
        controller.subscribe();
      }
    });
    backend.addEventListener("arMixCatalog",catalog=>{
      if(!Array.isArray(catalog?.items))return;
      nativeFxCatalogScanning=catalog.scanning===true;
      nativeCatalog=new Map(catalog.items.filter(p=>
        p&&typeof p.id==="string"&&p.id.length>0&&
        typeof p.name==="string"&&p.name.length>0).map(p=>[p.id,p]));
      scope?.setNativeCatalog?.([...nativeCatalog.values()]);
      if(nativeFxDiscoveryRequested&&!nativeFxCatalogScanning&&nativeCatalog.size===0)
        status("No installed VST3 discovered · use Import VST3 in Plugin Manager");
      if(hardware?.raw?.legacyM4)paintM4FxInstances(hardware.raw.legacyM4);
      const count=document.getElementById("pluginCount");
      if(count)count.title="Verified JUCE native VST3 catalog";
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
      if(pendingDeviceMute){
        // Reject any stale/unrelated ACK and any device epoch change.
        if(ack?.requestId!==pendingDeviceMute.requestId)return;
        const epochMatches=String(ack?.deviceEpoch)===pendingDeviceMute.epoch;
        pendingDeviceMute=null;
        if(ack?.ok===true && ack?.armed===false && epochMatches)
          sendDeviceApply();
        else if(devicePanel){
          deviceResult="Device change rejected: MUTE or audio epoch not verified.";
          renderDevicePanel();
        }
        return;
      }
      if(ack?.ok===true){
        status(ack.armed?"NATIVE: user ARM accepted; checking real PCM.":
          "NATIVE: output muted.");
        controller.subscribe();
      }else status("NATIVE ARM REJECTED: "+(ack?.error||"device not ready"));
    });
  }
  const nativeHooks=Object.freeze({
    enabled:connected,
    inputChoices(t) {
      if(t?.kind!=="audio")return ["None"];
      const choices=inputChoicesFor().map(x=>x.label);
      // The native BUS lane is selectable only for display: never invent a
      // hardware pin or promote the internal BUS to a writable physical IN.
      if(t.input===INTERNAL_BUS)choices.push(INTERNAL_BUS);
      return choices;
    },
    outputChoices(t) {
      if(t?.kind==="master")
        return [String(hardware?.device?.playbackDevice||t.output||"Choose audio output")];
      if(t?.kind==="audio"){
        // M4 accepts MAIN or source-only. Bus routing is via native WIRE,
        // never pretend the demo's other destinations have native PCM.
        const options=["Master","SOURCE ONLY (No Main)"];
        const graph=controller?.state()?.current?.graph;
        if(mixerSession()?.authority==="CHANNEL_ID"&&graph?.channels)
          for(const bus of graph.channels)
            if(bus.role==="GROUP"&&bus.name &&
               !options.includes(bus.name))options.push(bus.name);
        if(t.output&&!options.includes(t.output))options.push(t.output);
        return options;
      }
      return [t?.output||"None"];
    },
    consumeField(event) {
      if (!connected)return false;
      const el=event.target,field=el?.dataset?.field;
      if (!field)return false;
      const t=scope?.findTrack?.(el) || null;
      if (!t)return true;
      if(field==="input"){
        // Input selection is independent of Record Arm, transport START and
        // ChannelId admission. A physical mixer strip can receive ANY active
        // source, including the very first source in an unpatched session.
        if(event.type!=="change")return true;
        const channel=m4Index(t);
        const choice=inputChoicesFor().find(x=>x.label===el.value);
        // After BUS/WIRE the installed ChannelId graph is sole PCM owner.
        // Original ten channels must not send source edits to stale M4.
        if(mixerSession()?.authority==="CHANNEL_ID"){
          const current=controller?.state()?.current;
          const id=nativeIdFor(t);
          if(!choice||!id||!current?.admitted||!deviceReady()){
            nativeToast("Select a verified physical input");
            el.value=t.input;return true;
          }
          if(choice.label!==t.input){
            pendingNativeSources.set(id,choice);
            dispatchNativeSource();
          }
          el.value=t.input; // no optimistic browser patch
          return true;
        }
        const ep=safeNativeCounter(hardware?.device?.epoch);
        const rev=safeNativeCounter(legacy()?.routingRevision);
        if(channel<0||channel>=10||!choice||!deviceReady()||
           ep===null||rev===null){
          status("INPUT REJECTED · refresh verified hardware pins");
          el.value=t.input;return true;
        }
        if(choice.label===t.input&&!pendingSourceQueue.has(channel)){
          el.value=t.input;return true;
        }
        pendingSourceQueue.set(channel,choice);
        // No browser demo optimistic patch. The selector stays operable for
        // every channel during a previous JUCE routing transaction.
        el.value=t.input;
        if(nativeUiSourcePending)
          status("CH "+(channel+1)+" input queued behind native route ACK");
        else dispatchQueuedSource();
        return true;
      }
      if(field==="output"){
        if(event.type!=="change")return true;
        if(t.kind==="master"){
          el.value=t.output;openNativeAudioIO();return true;
        }
        const graph=controller?.state()?.current?.graph;
        const bus=graph?.channels?.find(ch=>
          ch.role==="GROUP"&&ch.name===el.value);
        const target=el.value==="Master"?"MAIN":
          el.value==="SOURCE ONLY (No Main)"?"NONE":
          bus?bus.name:null;
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
      const numeric=Number(el.value);
      if (field==="level"&&Number.isFinite(numeric))
        intent("SET_FADER",t,{valueDb:numeric});
      else if (field==="pan"&&Number.isFinite(numeric)) {
        // Preserve hardware center detent without publishing demo PAN state.
        const fine=el.dataset.panKeyboard==="true";
        const bypass=el.dataset.panAltPointer==="true";
        const value=!fine&&!bypass&&Math.abs(numeric)<=12?0:numeric;
        intent("SET_PAN",t,{value:value/100});
      } else {
        status("NATIVE: "+field+" awaits native capability; reference graph unchanged");
        scope?.render?.(); // restore SELECT/route control from accepted graph
      }
      return true;
    },
    consumeAction(t,action,btn) {
      if (!connected)return false;
      // Selection, WIRE inspection and section presentation are UI-only.
      if (["wireExpand","wireSource","wireRoute","editIdentity","rename"].includes(action))
        return !["wireExpand","wireSource","wireRoute"].includes(action);
      if(action==="openPlugin"){
        const slot=Number(btn?.dataset.slot),instance=verifiedFxFor(t,slot);
        if(instance){
          const ep=safeNativeCounter(hardware?.device?.epoch),
            rev=safeNativeCounter(legacy()?.routingRevision);
          if(ep!==null&&rev!==null)
            backend.emitEvent("arMixAction",{action:"routingFxEditor",
              channel:instance.lane,slot,
              instanceId:instance.live.instanceId,deviceEpoch:ep,revision:rev});
          else nativeToast("VST3 editor · refresh native device and FX instance");
        }else {
          discoverFxOnFirstPickerOpen();
          scope?.openFastPicker?.(t,slot,btn);
        }
        return true;
      }
      if(action==="slotPower"){
        if(mixerSession()?.authority==="CHANNEL_ID"){
          if(!requestAdmittedFxPower(t,action,btn))
            nativeToast("Native FX slot unavailable · accepted insert/epoch required");
        }else{
          const slot=Number(btn?.dataset.slot),instance=verifiedFxFor(t,slot);
          if(!m4ControlIsOwner()||!instance||
             !requestNativeFxPower(t,slot,!instance.live.bypassed))
            nativeToast("FX bypass unavailable · select an accepted VST3 slot");
        }
        return true;
      }
      if(action==="fxOn"){
        const authority=mixerSession()?.authority;
        const ok=authority==="M4"?requestM4RackPower(t):
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
      if (action==="mute"||action==="solo") {
        if((m4Verified()||nativeMasterReady()) &&
           t.kind==="master" && action==="mute"){
          if(!sendM4Control("masterMute",t,!t.mute))
            status("Native MAIN mute rejected");
          return true;
        }
        if(m4ControlIsOwner()&&m4Verified()&&m4Index(t)>=0){
          if(!sendM4Control(action,t,!t[action]))
            status("Native M4 mute/solo rejected");
          return true;
        }
        const key=action==="mute"?"SET_MUTE":"SET_SOLO";
        intent(key,t,{value:!t[action],enabled:!t[action]});return true;
      }
      status("NATIVE: "+action+" is not an accepted ChannelId operation");return true;
    },
    consumeWire(ops) {
      if (!connected)return false;
      const current=controller?.state().current;
      // P0 first-sound WIRE to MAIN is the very same native M4 PRIMARY_OUT
      // transaction as the ordinary OUT drop-down. Do not reject a real
      // physical CH→MAIN connection merely because P1 dynamic BUS/WIRE
      // transaction capability has not been admitted yet.
      if(Array.isArray(ops)&&ops.length===1){
        const op=ops[0];
        const t=scope?.tracks?.find(x=>x.id===op.sourceId);
        const sourceId=t?nativeByUiId.get(t.id):null;
        // Ordinary MAIN jack is a primary ROUTE_ONLY. An explicitly selected
        // send mode must pass through native CONNECT instead, preserving the
        // old primary BUS (or the absence of a primary in SOURCE ONLY).
        const isMainConnect=(op.type==="ROUTE_ONLY" ||
          (op.type==="CONNECT" && (!op.mode ||
            op.mode==="postFader" || op.mode==="routeOnly"))) &&
          op.destId===scope?.master?.id;
        // WIRE gesture carries a UI edge id. wireByUiId is UI→native;
        // uiByWireId is the inverse. The old inverse lookup always returned
        // undefined, so unplugging an existing MAIN wire was impossible.
        const edgeId=wireByUiId.get(op.edgeId);
        const lane=m4Index(t);
        const graphMainRemove=op.type==="REMOVE" && sourceId &&
          current?.admitted===true &&
          current.graph.edges.some(e=>e.enabled&&e.edgeId===edgeId&&
            e.from===sourceId&&e.to===current.graph.mainChannelId&&
            e.primaryOutput===true);
        // The approved M4 renderer remains the accepted First Sound owner
        // if ChannelId projection was not admitted. Verify BOTH an exact UI
        // PRIMARY edge and the producer's real MAIN readback before removing.
        // When native ChannelId *is* admitted, never replace its edge-token
        // guard with an M4 label. No invented browser-only PCM or routing.
        const m4MainRemove=op.type==="REMOVE" && current?.admitted!==true &&
          lane>=0 && m4Verified() &&
          legacy()?.channels?.[lane]?.main===true &&
          Array.isArray(scope?.wireEdges) &&
          scope.wireEdges.some(e=>e.enabled&&e.kind==="primary"&&
            e.id===op.edgeId&&e.sourceId===op.sourceId&&
            e.destId===scope?.master?.id);
        const isMainRemove=graphMainRemove||m4MainRemove;
        // Admitted new ChannelIds after CH10 have no M4 lane ordinal.
        if(t&&(lane>=0||(current?.admitted===true&&sourceId))&&
           (isMainConnect||isMainRemove)){
          const target=isMainRemove?"NONE":"MAIN";
          if(requestPrimaryOut(t,target))
            status(t.name+" WIRE → "+
              (target==="MAIN"?"MAIN":"SOURCE ONLY")+
              " · native ACK/readback required");
          else nativeToast("WIRE MAIN unavailable · verify channel and routing state");
          return true; // original Pages reducer never simulates native PCM
        }
      }
      if (!flags.acceptedGraphConnected || !current?.admitted ||
          !current.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION")) {
        const reason="WIRE BUS/FX return is not admitted by the installed PCM graph";
        status("NATIVE: "+reason+" · no audio reroute");
        nativeToast(reason);
        return true;
      }
      const changes=[];
      for (const op of ops) {
        const sourceId=nativeByUiId.get(op.sourceId);
        const destId=op.destId===undefined?undefined:nativeByUiId.get(op.destId);
        const edgeId=op.edgeId===undefined?undefined:wireByUiId.get(op.edgeId);
        if (!sourceId || (op.destId!==undefined&&!destId) ||
            (op.edgeId!==undefined&&!edgeId)) {
          status("NATIVE: WIRE mapped identity missing; no mutation");return true;
        }
        // ENGINE #266's real PCM decoder consumes op/from/to/edgeId,
        // NOT the Pages reducer's type/sourceId/destId. Never feed it
        // an unrecognized demo command or an unbound EdgeId.
        if (!["CONNECT","REMOVE","RETARGET","ROUTE_ONLY"].includes(op.type) ||
            (op.mode && !["send","postFader","routeOnly"].includes(op.mode))) {
          status("NATIVE: WIRE "+op.type+" needs real engine capability; unchanged");
          return true;
        }
        const change={op:op.type,from:sourceId};
        if (destId)change.to=destId;
        if (edgeId)change.edgeId=edgeId;
        if (op.type==="CONNECT" || op.type==="ROUTE_ONLY") {
          change.tap="POST_FADER";
          change.gainLinear=op.levelDb===undefined?1:
            Math.pow(10,Math.max(-60,Math.min(6,Number(op.levelDb)))/20);
        }
        changes.push(change);
      }
      const source=changes[0]?.from;
      if (!source || changes.some(change=>change.from!==source)) {
        status("NATIVE: multi-source transaction requires native batch admission");
        return true;
      }
      const accepted=controller.intent("APPLY_WIRE_TRANSACTION",source,{changes});
      if (!accepted.ok)status("NATIVE: "+accepted.error+"; WIRE unchanged");
      else window.setTimeout(()=>controller?.expire(accepted.requestId),8000);
      return true;
    },
    consumeUndo() {if (!connected)return false;status("NATIVE: WIRE undo requires accepted native transaction");return true;},
    insertPlugin(id,target){
      if(pendingFxInsert){status("FX INSERT · wait for previous native ACK");return true;}
      const plugin=nativeCatalog.get(id);
      const track=scope?.tracks?.find(x=>x.id===target?.channelId);
      const channel=m4Index(track),slot=Number(target?.slot);
      const accepted=controller?.state().current;
      const nativeId=nativeIdFor(track);
      const nativeCh=accepted?.graph?.channels?.find(ch=>ch.id===nativeId);
      if(accepted?.admitted && nativeCh &&
         (channel<0 || legacy()?.nativeBusStructural===true)) {
        if(!plugin || !["INPUT","GROUP"].includes(nativeCh.role) ||
           !accepted.graph.capabilities.supportedOps.includes("LOAD_INSERT") ||
           !Number.isInteger(slot)||slot<0||slot>=8 ||
           !Array.isArray(nativeCh.inserts)||nativeCh.inserts.length!==slot){
          status("VST3 unavailable · select an empty native ChannelId FX slot");
          return true;
        }
        const request=controller.intent("LOAD_INSERT",nativeId,
                                        {catalogId:id,slot});
        if(!request.ok){status("VST3 rejected · "+request.error);return true;}
        pendingFxInsert={authority:"CHANNEL_ID",requestId:request.requestId,
                         id,channelId:nativeId,slot};
        status(track.name+" · preparing VST3 without stopping MAIN");
        window.setTimeout(()=>{
          if(pendingFxInsert?.requestId!==request.requestId)return;
          pendingFxInsert=null;
          status("Native VST3 timed out · accepted audio left unchanged");
          controller.subscribe();
        },15000);
        return true;
      }
      const ep=safeNativeCounter(hardware?.device?.epoch);
      const rev=safeNativeCounter(legacy()?.routingRevision);
      const existing=legacy()?.fxInstances?.[channel]||[];
      if(!plugin||!m4Verified()||channel<0||
         !Number.isInteger(slot)||slot<0||slot>=8 ||
         ep===null||rev===null||existing[slot] ||
         (slot>0&&!existing[slot-1])){
        status("FX INSERT REJECTED · choose an empty consecutive slot on a native CH with verified route/catalog");
        return true;
      }
      const requestId="fx-"+(++fxRequestSerial);
      pendingFxInsert={requestId,id,channel,slot};
      backend.emitEvent("arMixAction",{action:"routingFxInsert",
        requestId,id,channel,slot,deviceEpoch:ep,revision:rev});
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
      if (event.target.classList.contains("fader")){
        if((m4ControlIsOwner()&&m4Verified()) ||
           (t?.kind==="master"&&(m4Verified()||nativeMasterReady())))
          sendM4Control(t?.kind==="master"?"masterGain":"faderDb",t,0);
        else intent("SET_FADER",t,{valueDb:0});
      }
      else if(event.target.classList.contains("center-pan")){
        if(m4ControlIsOwner()&&m4Verified())sendM4Control("pan",t,0);
        else intent("SET_PAN",t,{value:0});
      }
      else status("NATIVE: identity editor unavailable until native ACK contract");
      return true;
    },
    state:()=>controller?.state() || null
  });
  window.__ARMIX_NATIVE_M2__ = nativeHooks;
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
    if (flags.acceptedGraphConnected) return;
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
    if(target.id==="addBus"){
      requestNativeBusCreate();return;
    }
    status("NATIVE: "+target.id+" requires admitted engine operation");
  },true);
  // Native fader is committed exactly once INSIDE the original GUI's
  // finishFaderPointer via build_approved_native_gui.py hook. A later
  // window.pointerup listener would run after that handler clears the gesture.
  const ready=()=>{
    installFirstSoundMenu();
    scope?.setNativeCatalog?.([]); // no synthetic/demo VST3 in Windows host
    updateHardwarePanel(null);
    stopDemo();
    status("NATIVE: subscribed · waiting for true device and accepted ChannelId graph");
    backend.emitEvent("arMixGuiReady",{
      gui:"APPROVED_PAGES_PREVIEW_BUTTONS",visualReady:true,audioReady:false
    });
    controller.subscribe();
  };
  if(document.readyState==="loading")
    document.addEventListener("DOMContentLoaded",ready,{once:true});
  else ready();
})();
