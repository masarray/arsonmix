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
      const kind=isMain||isPrimary?"primary":edge.tap==="SPLIT_PRE"||edge.tap==="SPLIT_POST"?"split":"send";
      const mode=isMain?"postFader":kind==="primary"?"routeOnly":
        kind==="split"?(edge.tap==="SPLIT_PRE"?"splitPre":"splitPost"):"send";
      if (isMain && edge.gainLinear!==1) return false;
      const gainDb=isMain?0:Math.round(Math.max(-60,Math.min(6,
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
  function projectNativeGraphWithoutFocusLoss(next) {
    const structure=JSON.stringify([
      next.graph.channels.map(ch=>[ch.id,ch.role,ch.name,
        ch.processingFormat,ch.source,ch.primaryOutId,ch.inserts]),
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
      const c=ch.controls;
      const db=c.faderDb,pan=c.pan*100;
      const faderChanged=Number.isFinite(db)&&Math.abs(t.level-db)>.015;
      const switchChanged=t.mute!==c.mute||t.solo!==c.solo;
      if(faderChanged)t.level=db;
      if(Number.isFinite(pan))t.pan=pan;
      t.mute=c.mute;t.solo=c.solo;
      if(switchChanged){
        scope.refreshStrip?.(t);
        meterElements.delete(t.id);
      }else if(faderChanged&&scope.activeFaderId!==t.id)
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
  let pendingFxInsert=null,fxRequestSerial=0,nativeCatalog=new Map();
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

  // One verified control path for original 10 channels and MAIN, whether
  // the callback currently renders M4 or its admitted ChannelId mirror.
  // Browser holds only transient gesture feedback; engine remains SSOT.
  const controlKey=(parameter,t)=>parameter+":"+(t?.kind==="master"?"main":m4Index(t));
  function streamFaderControl(parameter,t,value,final){
    if(!m4Verified())return false;
    const key=controlKey(parameter,t),now=performance.now();
    const last=lastNativeControlSent.get(key)||0;
    if(!final&&now-last<30){
      pendingNativeControl.set(key,{parameter,t,value});
      return true;
    }
    pendingNativeControl.delete(key);
    lastNativeControlSent.set(key,now);
    return sendM4Control(parameter,t,value);
  }
  // A quiet trailing flush guarantees final PCM value, even if the pointer
  // stops between two 30ms live-control ticks or a keyboard commits instantly.
  let nativeControlFlushQueued=false;
  function queueLiveControlFlush(){
    if(nativeControlFlushQueued)return;
    nativeControlFlushQueued=true;
    window.setTimeout(()=>{
      nativeControlFlushQueued=false;
      for(const [key,edit] of pendingNativeControl) {
        pendingNativeControl.delete(key);
        lastNativeControlSent.set(key,performance.now());
        sendM4Control(edit.parameter,edit.t,edit.value);
      }
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
    if(!m4Verified() || (typeof value!=="boolean"&&!Number.isFinite(value)))
      return false;
    const channel=t?.kind==="master"?-1:m4Index(t);
    if(channel<0 && t?.kind!=="master")return false;
    const ep=safeNativeCounter(hardware.device.epoch),
      rev=safeNativeCounter(legacy().routingRevision);
    if(ep===null||rev===null)return false;
    const requestId="m4-"+(++m4RequestSerial);
    backend.emitEvent("arMixAction",{action:"routingM4Control",
      requestId,channel,parameter,controlValue:value,deviceEpoch:ep,
      revision:rev});
    window.setTimeout(()=>{
      if(requestId!==lastM4Request)return;
      lastM4Request="";
      status("M4 control response timed out; verifying actual native readback");
      controller?.subscribe();
    },8000);
    lastM4Request=requestId;
    return true;
  }
  let lastM4Request="";
  function paintM4Fallback(next) {
    if(!m4Verified()||!Array.isArray(scope?.tracks))return;
    const m=next.raw.legacyM4,ctl=m.channelControls;
    paintM4FxInstances(m);
    const controlSignature=JSON.stringify([next.device.epoch,
      m.routingRevision,ctl,m.masterDb,m.masterMute]);
    if(controlSignature!==lastM4Controls) {
      lastM4Controls=controlSignature;
      for(let i=0;i<10;i++){
        const t=scope.tracks.find(x=>x.id===m4TrackIds[i]),c=ctl[i];
        if(!t||!c||!Number.isFinite(c.db)||!Number.isFinite(c.pan)||
           typeof c.mute!=="boolean"||typeof c.solo!=="boolean")return;
        const oldDb=t.level;
        const switchChanged=t.mute!==c.mute||t.solo!==c.solo;
        t.level=c.db;t.pan=c.pan;t.mute=c.mute;t.solo=c.solo;
        if(switchChanged && scope.activeFaderId!==t.id){
          scope.refreshStrip?.(t);
          m4Meters.delete(t.id);
        }else if(Math.abs(oldDb-c.db)>.015 &&
                 scope.activeFaderId!==t.id)
          scope.syncFaderVisual?.(t);
        const panKnob=document.querySelector(
          '.strip[data-id="'+t.id+'"] .center-pan');
        if(panKnob && document.activeElement!==panKnob)
          panKnob.value=String(c.pan);
      }
      if(scope.master && Number.isFinite(m.masterDb) &&
         Math.abs(scope.master.level-m.masterDb)>.015){
        scope.master.level=m.masterDb;
        if(scope.activeFaderId!==scope.master.id)
          scope.syncFaderVisual?.(scope.master);
      }
      if(scope.master && typeof m.masterMute==="boolean" &&
         scope.master.mute!==m.masterMute){
        scope.master.mute=m.masterMute;
        if(scope.activeFaderId!==scope.master.id)
          scope.refreshStrip?.(scope.master);
      }
    }
    // Only measured JUCE callback peaks are painted. Cache actual live DOM
    // fills to avoid repeated selector searches for every timer/signal tick.
    if(!Array.isArray(m.channelPeaksL)||!Array.isArray(m.channelPeaksR)||
       !scope?.displayMeterDbfs)return;
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
        const peak=side==="left"?m.channelPeaksL[i]:m.channelPeaksR[i];
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
    const m4=next?.raw?.legacyM4;
    const rows=m4?.channels;
    const signature=JSON.stringify([next?.device?.epoch,
      next?.device?.enabledInputs,next?.device?.inputEndpoints,
      m4?.active,m4?.routingRevision,rows]);
    if(signature===lastM4UiSignature)return;
    lastM4UiSignature=signature;
    for(let i=0;i<10;++i){
      const t=scope.tracks.find(x=>x.id===m4TrackIds[i]);
      if(t?.kind!=="audio")continue;
      const row=Array.isArray(rows)&&rows.length===10?rows[i]:null;
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
  const deviceReady=()=>!!(hardware?.device?.backend &&
    hardware.device.sampleRate>0 &&
    hardware.device.enabledInputs.length>0 &&
    hardware.device.enabledOutputs.length>=2 && legacy()?.active===true);
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
    menu.append(caption,setup,muteBtn,hardwareNote,advanced);
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
    paintM4Fallback(next);
    // A second channel's IN can be selected while the first transaction is
    // in flight. Always dispatch its latest choice with a FRESH native route
    // revision, not stale tokens captured before the previous ACK.
    if(!nativeUiSourcePending && pendingSourceQueue.size)
      window.queueMicrotask(()=>dispatchQueuedSource());
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=next.admitted && caps?.callbackOwnsChannelIds===true;
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";lastNativeTopologySignature="";
      stopDemo();
      status(m4Verified()?
        "NATIVE M4 · real PCM faders and meters · ChannelId buses not yet admitted":
        "NATIVE G0 · choose real hardware IN on any channel");
      return;
    }
    // Control readback may change at the SAME structural graph revision.
    // Compare the actual accepted controls/edges, never skip native fader updates.
    const signature=next.identity+"|"+JSON.stringify(next.graph.channels.map(ch=>[
      ch.id,ch.controls,ch.inserts,ch.primaryOutId]))+"|"+JSON.stringify(next.graph.edges);
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
        else if(state.lastError!=="ACCEPTED_READBACK"&&state.lastError!=="NONE"&&
                state.lastError!=="NATIVE_NOT_SUBSCRIBED") {
          lastNativeError=state.lastError;
          // Native REJECTED / STALE / timeout must roll back a temporary fader
          // cap and every input value; no optimistic GUI state survives.
          if (!state.pending && flags.acceptedGraphConnected &&
              (scope.activeFaderId??-1)<0){
            // A rejected edit MUST restore the visual model immediately,
            // but never unmount an open hardware selector. This also keeps
            // existing native transaction rollback semantics testable.
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
      if(ack?.requestId===lastM4Request)lastM4Request="";
      if(ack?.ok!==true)
        status("NATIVE M4 REJECTED · "+(ack?.error||"stale graph"));
      controller.subscribe(); // accepted producer snapshot is authoritative
    });
    backend.addEventListener("arMixChannelPatchAck",ack=>{
      if(!pendingOutputPatch||ack?.requestId!==pendingOutputPatch.requestId)return;
      const p=pendingOutputPatch;
      pendingOutputPatch=null;
      const row=ack?.state?.acceptedRoute?.channels?.[p.channel];
      const accepted=ack?.ok===true && Number(ack.state?.epoch)===p.epoch &&
        (p.operation==="SET_PROCESSING_FORMAT"?
          row?.channelFormat===p.target:
          row?.main===(p.target==="MAIN"));
      if(accepted)
        status("CH "+(p.channel+1)+" "+
          (p.operation==="SET_PROCESSING_FORMAT"?"M/ST ":"OUT ")+p.target+
          " accepted · verifying native PCM");
      else {
        status("CH "+(p.channel+1)+" "+
          (p.operation==="SET_PROCESSING_FORMAT"?"M/ST":"OUT")+
          " rejected · "+(ack?.error||"stale route"));
        nativeToast(ack?.error||"Native channel route rejected");
      }
      controller.subscribe();
    });
    backend.addEventListener("arMixRoutingFxInsertAck",ack=>{
      if(!pendingFxInsert||ack?.requestId!==pendingFxInsert.requestId)return;
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
      nativeCatalog=new Map(catalog.items.filter(p=>
        p&&typeof p.id==="string"&&p.id.length>0&&
        typeof p.name==="string"&&p.name.length>0).map(p=>[p.id,p]));
      scope?.setNativeCatalog?.([...nativeCatalog.values()]);
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
      if(notice?.error===true && typeof notice.message==="string"){
        nativeToast(notice.message); // native VST3/driver cause survives 12Hz status repaint
        status("JUCE · "+notice.message);
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
        if(t.output && !options.includes(t.output))options.push(t.output);
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
        const target=el.value==="Master"?"MAIN":
          el.value==="SOURCE ONLY (No Main)"?"NONE":null;
        // No optimistic GUI state. Both this menu and the WIRE jack use
        // the same ID/epoch-guarded, acknowledged native route transaction.
        if(el.value!==t.output && !requestPrimaryOut(t,target))
          status("OUT unavailable · verify device, MAIN route and pending edit");
        el.value=t.output;
        return true;
      }
      // ONE realtime PCM control for original ten channels and MAIN,
      // regardless of whether the admitted ChannelId callback or M4 renders.
      // <=33 commands/s/control while dragging; always flush on release.
      if((field==="level"||field==="pan") && m4Verified() &&
         (m4Index(t)>=0||t.kind==="master")){
        const numeric=Number(el.value);
        if(!Number.isFinite(numeric))return true;
        const normalized=field==="pan"?
          previewNativePan(t,el,event):numeric;
        if(normalized===null)return true;
        const key=field==="level"?
          (t.kind==="master"?"masterGain":"faderDb"):"pan";
        if(field==="level")previewFaderWithoutModelMutation(t,numeric);
        if(streamFaderControl(key,t,normalized,event.type==="change"))
          queueLiveControlFlush();
        else status("LIVE PCM control rejected; checking route identity");
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
        scope?.openFastPicker?.(t,Number(btn?.dataset.slot),btn);
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
        if(m4Verified() && t.kind==="master" && action==="mute"){
          if(!sendM4Control("masterMute",t,!t.mute))
            status("Native MAIN mute rejected");
          return true;
        }
        if(m4Verified()&&m4Index(t)>=0){
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
        const isMainConnect=(op.type==="CONNECT"||op.type==="ROUTE_ONLY") &&
          op.destId===scope?.master?.id;
        // WIRE gesture carries a UI edge id. wireByUiId is UI→native;
        // uiByWireId is the inverse. The old inverse lookup always returned
        // undefined, so unplugging an existing MAIN wire was impossible.
        const edgeId=wireByUiId.get(op.edgeId);
        const isMainRemove=op.type==="REMOVE" && sourceId &&
          current?.admitted===true &&
          current.graph.edges.some(e=>e.enabled&&e.edgeId===edgeId&&
            e.from===sourceId&&e.to===current.graph.mainChannelId);
        if(t&&m4Index(t)>=0&&(isMainConnect||isMainRemove)){
          const target=isMainRemove?"NONE":"MAIN";
          if(requestPrimaryOut(t,target))
            status("CH "+(m4Index(t)+1)+" WIRE → "+
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
        if(m4Verified())sendM4Control(t?.kind==="master"?"masterGain":"faderDb",t,0);
        else intent("SET_FADER",t,{valueDb:0});
      }
      else if(event.target.classList.contains("center-pan")){
        if(m4Verified())sendM4Control("pan",t,0);
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
