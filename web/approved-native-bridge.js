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
  let lastPaintedSignature = "", lastNativeError = "NATIVE_NOT_SUBSCRIBED";
  let controller = null;
  const status = message => {
    lastNativeError = message;
    const info = document.getElementById("info");
    if (info) info.textContent = message;
  };
  const stopDemo = () => {
    // The public Pages demo remains unchanged. In native, DEMO meters and
    // reference-only physical operations are never passed off as live audio.
    const demo = document.getElementById("preview");
    if (demo) {demo.disabled = true;demo.title = "Demo meters disabled in native Windows application";}
    scope?.stopDemo?.();
    document.documentElement.dataset.armixNativeAuthority =
      flags.productionCallbackLive ? "product-live" :
      flags.acceptedGraphConnected ? "pcm-accepted" : "not-admitted";
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
    let nextMaster = null;
    for (const [index,ch] of accepted.channels.entries()) {
      const kind = ch.role==="MAIN"?"master":ch.role==="GROUP"?"bus":
        ch.role==="FX_RETURN"?"fx":"audio";
      const savedId = uiByNativeId.get(ch.id);
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
    scope.installGraph(nextTracks,nextMaster,edges);
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
      const root=document.querySelector('.strip[data-id="'+ui+'"]');
      if (!root)continue;
      const entry=data[ch.id];
      const one=typeof entry==="number"&&Number.isFinite(entry)&&entry>=0?entry:null;
      const l=typeof entry?.peakL==="number"&&Number.isFinite(entry.peakL)&&entry.peakL>=0?
        entry.peakL:one;
      const right=typeof entry?.peakR==="number"&&Number.isFinite(entry.peakR)&&entry.peakR>=0?
        entry.peakR:ch.processingFormat==="MONO"?l:null;
      const paint=(side,peak)=>{
        const fill=root.querySelector('.meter-fill.'+side);
        if(!fill)return;
        const db=peak===null || peak===0?-90:20*Math.log10(peak);
        scope.displayMeterDbfs(fill,Math.max(-90,Math.min(6,db)),ts,ui+':'+(side==="left"?"L":"R"));
      };
      paint("left",l);
      paint("right",right);
    }
  }

  // G0's already-installed physical M4 callback has its OWN real ten-strip
  // control authority. The ChannelId graph is intentionally not admitted yet.
  // Use actual arMixStatus control readback, never the Pages demo state, to
  // drive fader/pan/mute/solo. All DSP mutations remain JUCE message-thread
  // calls through the existing arMixParameter -> setDryRoutingLabParameter.
  let m4Status=null, m4LiveStatus=null, m4ControlStamp="";
  const physicalTrackIds=[];
  let sourceRequestSeq=2000, sourcePending=null, g0Announced=false;
  const validM4Integer=x=>Number.isSafeInteger(x)&&x>0;
  // Native /preview/ uses data-id, NOT the legacy standalone data-strip.
  // Bind a stable ten-channel hardware identity once, independent of visual
  // selection/reorder, and never infer pins from track display numbers.
  function bindPhysicalTrackIds(){
    if(physicalTrackIds.length===10)return true;
    const tracks=scope?.tracks;
    if(!Array.isArray(tracks)||tracks.length<10)return false;
    const first=tracks.slice(0,10);
    if(first.some(t=>t?.kind!=='audio'||!Number.isSafeInteger(t.id))||
       new Set(first.map(t=>t.id)).size!==10)return false;
    physicalTrackIds.push(...first.map(t=>String(t.id)));
    return true;
  }
  function verifiedM4ControlLane() {
    const d=m4Status;
    return !flags.acceptedGraphConnected && d?.ready===true &&
      d?.active===true && d?.topologyFault!==true &&
      validM4Integer(Number(d.epoch)) &&
      validM4Integer(Number(d.revision)) &&
      Array.isArray(d.channelControls) && d.channelControls.length===10?d:null;
  }
  function nativePhysicalStrip(el) {
    if(!bindPhysicalTrackIds())return -1;
    const id=el?.closest?.('.strip[data-id]')?.dataset?.id;
    return id===undefined?-1:physicalTrackIds.indexOf(id);
  }
  function emitVerifiedM4Param(source,name,value) {
    const d=verifiedM4ControlLane(),lane=nativePhysicalStrip(source);
    if(!d||lane<0)return false;
    if((name==='mute'||name==='solo') ? typeof value!=='boolean' :
       !(typeof value==='number'&&Number.isFinite(value)&&
         (name==='pan'?value>=-100&&value<=100:
          name==='faderDb'&&value>=-60&&value<=10)))return false;
    // No JS-side mutation or optimism. The next real M4 channelControls
    // readback decides what the approved GUI displays.
    backend.emitEvent('arMixParameter',{
      channel:lane+1,parameter:name,value,
      routeEpoch:Number(d.epoch),routeRevision:Number(d.revision)
    });
    return true;
  }
  function emitVerifiedMasterParam(name,value) {
    if(!verifiedM4ControlLane())return false;
    if(name==='masterGain'&&
       !(typeof value==='number'&&Number.isFinite(value)&&value>=-60&&value<=10))
      return false;
    if(name==='masterMute'&&typeof value!=='boolean')return false;
    if(name!=='masterGain'&&name!=='masterMute')return false;
    backend.emitEvent('arMixParameter',{channel:0,parameter:name,value});
    return true;
  }
  function projectVerifiedM4ControlReadback() {
    const d=verifiedM4ControlLane();
    if(!d||!scope?.findTrack||!scope?.render)return;
    if(!bindPhysicalTrackIds())return;
    const controls=d.channelControls,changes=[];
    for(let lane=0;lane<10;++lane) {
      const model=scope.tracks.find(t=>String(t.id)===physicalTrackIds[lane]);
      const c=controls[lane];
      if(!model||model.kind!=='audio'||!c ||
         !Number.isFinite(c.db)||c.db < -60||c.db > 10 ||
         !Number.isFinite(c.pan)||c.pan < -100||c.pan > 100||
         typeof c.mute!=='boolean'||typeof c.solo!=='boolean')return;
      changes.push({model,c});
    }
    if(changes.length!==10)return;
    const live=m4LiveStatus;
    const prepared=d.acceptedRoute?.channels||d.preparedM4Route?.channels;
    const stamp=Number(d.epoch)+'/'+Number(d.revision)+'/'+
      JSON.stringify(controls.map(c=>[c.db,c.pan,c.mute,c.solo]))+'/' +
      JSON.stringify([prepared?.map(c=>c.main),live?.masterDb,live?.masterMute]);
    if(stamp===m4ControlStamp)return;
    m4ControlStamp=stamp;
    let dirty=false;
    for(const {model,c} of changes) {
      if(Math.abs(model.level-c.db)>0.005||Math.abs(model.pan-c.pan)>0.005||
         model.mute!==c.mute||model.solo!==c.solo) {
        model.level=c.db;model.pan=c.pan;model.mute=c.mute;model.solo=c.solo;
        dirty=true;
      }
    }
    // Reflect the native MAIN assignment, not the Pages preview's
    // SOURCE ONLY placeholder. The physical route remains the authority.
    if(Array.isArray(prepared)&&prepared.length===10)
      for(let lane=0;lane<10;++lane) {
        const main=prepared[lane]?.main;
        if(typeof main!=='boolean')continue;
        const target=main?'Master':'SOURCE ONLY (No Main)';
        if(changes[lane].model.output!==target) {
          changes[lane].model.output=target;dirty=true;
        }
      }
    if(scope.master&&Number.isFinite(live?.masterDb) &&
       live.masterDb>=-60&&live.masterDb<=10 &&
       typeof live?.masterMute==='boolean') {
      if(Math.abs(scope.master.level-live.masterDb)>0.005||
         scope.master.mute!==live.masterMute) {
        scope.master.level=live.masterDb;
        scope.master.mute=live.masterMute;
        dirty=true;
      }
    }
    if(dirty)scope.render();
  }
  // The approved /preview/ HTML embeds its own renderer; legacy
  // web/mixer-io.js is NOT loaded into the Windows executable. Populate that
  // renderer's EXISTING IN select directly with actual JUCE callback pins.
  // No second widget for CH1, no synthetic browser audio graph.
  const m4Rows=d=>d?.acceptedRoute?.channels||d?.preparedM4Route?.channels||null;
  function updateNativeM4Inputs(){
    const d=verifiedM4ControlLane();
    if(!d||!bindPhysicalTrackIds())return;
    const ports=Array.isArray(d.ports)?d.ports.filter(p=>
      Number.isInteger(p?.port)&&p.port>=0&&p.port<64):[];
    const occupied=new Set(ports.map(p=>p.port));
    const choices=[['NONE','No hardware input']];
    for(const port of ports)
      choices.push(['MONO:'+port.port,'Mono · '+String(port.label||('Input '+(port.port+1)))]);
    for(const port of ports)
      if(occupied.has(port.port+1))
        choices.push(['STEREO:'+port.port+':'+(port.port+1),
          'Stereo · IN '+(port.port+1)+' / '+(port.port+2)]);
    const rows=m4Rows(d), key=JSON.stringify([d.epoch,choices]);
    for(let lane=0;lane<10;++lane) {
      const id=physicalTrackIds[lane];
      const sel=document.querySelector('.strip[data-id="'+id+'"] select.route-select[data-field="input"]');
      if(!sel)continue;
      const row=Array.isArray(rows)?rows[lane]:null;
      const current=row?.mode==='MONO'?'MONO:'+row.left:
        row?.mode==='STEREO'?'STEREO:'+row.left+':'+row.right:'NONE';
      if(sel.dataset.nativeM4Choices!==key) {
        sel.replaceChildren(...choices.map(([value,label])=>{
          const option=document.createElement('option');
          option.value=value;option.textContent=label;
          // A currently patched channel is never cleared through an
          // unsupported shortcut. Explicit structural UNPATCH is a separate gate.
          if(value==='NONE'&&current!=='NONE')option.disabled=true;
          return option;
        }));
        sel.dataset.nativeM4Choices=key;
      }
      if(!choices.some(([value])=>value===current)){
        const custom=document.createElement('option');
        custom.value=current;custom.textContent='Previously accepted input · verify ports';
        custom.disabled=true;sel.appendChild(custom);
      }
      if(sel.value!==current)sel.value=current;
      sel.dataset.nativeHardwareChannel=String(lane);
      sel.disabled=!!sourcePending||!ports.length;
      sel.title='Select real hardware input for CH '+(lane+1)+' · native ACK required';
    }
    // Main monitor is a normal visible mixer action (Audition-style I),
    // not a hidden START / ARM wizard. OFF is default until user enables it.
    const master=document.querySelector('#master .strip.master .route-top');
    if(master){
      let button=master.querySelector('[data-native-monitor]');
      if(!button){
        button=document.createElement('button');
        button.type='button';button.className='mx-util-key';
        button.dataset.nativeMonitor='1';
        button.setAttribute('aria-label','Main input monitoring');
        master.appendChild(button);
      }
      button.textContent=m4LiveStatus?.monitor?'I · ON':'I · OFF';
      button.setAttribute('aria-pressed',String(!!m4LiveStatus?.monitor));
      button.title='Monitor live hardware input through MAIN · click to toggle';
      button.disabled=!!sourcePending;
    }
  }
  function sendM4SourcePatch(p){
    if(!p)return;
    p.phase='source';
    backend.emitEvent('arMixAction',{
      action:'routingSourceSwitch',requestId:p.id,channel:p.lane,
      mode:p.mode,left:p.left,right:p.right,
      deviceEpoch:p.epoch,revision:p.revision
    });
  }
  function chooseM4HardwareInput(el){
    const d=verifiedM4ControlLane(),lane=nativePhysicalStrip(el);
    if(!d||lane<0||sourcePending)return;
    const rows=m4Rows(d),old=rows?.[lane],parts=String(el.value).split(':');
    const mode=parts[0],left=Number(parts[1]),right=mode==='STEREO'?Number(parts[2]):-1;
    const ports=new Set((d.ports||[]).map(p=>p.port));
    const valid=(mode==='MONO'||mode==='STEREO')&&
      Number.isInteger(left)&&ports.has(left)&&
      (mode==='MONO'||Number.isInteger(right)&&right!==left&&ports.has(right));
    if(!valid){status('Select an enabled driver input; source unchanged');updateNativeM4Inputs();return;}
    if(old?.mode===mode&&old.left===left&&(mode!=='STEREO'||old.right===right)){
      updateNativeM4Inputs();return;
    }
    sourcePending={id:++sourceRequestSeq,lane,mode,left,right,
      epoch:Number(d.epoch),revision:Number(d.revision),
      resume:!!m4LiveStatus?.monitor,phase:'source'};
    // Only a STRUCTURAL change needs the guarded mute/republish.
    // Same-format changes use existing native 64-sample crossfade live.
    if(sourcePending.resume&&old?.mode!==mode) {
      sourcePending.phase='mute';
      backend.emitEvent('arMixAction',{action:'monitor',value:false,
        expectedEpoch:sourcePending.epoch,expectedRevision:sourcePending.revision});
    }else sendM4SourcePatch(sourcePending);
    status('CH '+(lane+1)+' input change pending native ACK');
    updateNativeM4Inputs();
  }
  function acceptM4SourceAck(ack){
    const p=sourcePending;
    if(!p||p.phase!=='source'||Number(ack?.requestId)!==p.id)return;
    const next=ack?.state, row=next?.acceptedRoute?.channels?.[p.lane];
    const valid=ack?.ok===true&&Number(next?.epoch)===p.epoch&&
      Number(next?.revision)>=p.revision &&
      row?.mode===p.mode&&row.left===p.left&&row.right===p.right;
    if(!valid){
      sourcePending=null;
      status('INPUT REJECTED · '+String(ack?.error||'stale native routing readback'));
      updateNativeM4Inputs();return;
    }
    m4Status=next; // Engine-provided real readback, never optimistic local route.
    if(p.resume&&p.phase==='source'){
      p.phase='resume';
      p.revision=Number(next.revision);
      backend.emitEvent('arMixAction',{action:'monitor',value:true,
        expectedEpoch:p.epoch,expectedRevision:p.revision});
    }else sourcePending=null;
    status('CH '+(p.lane+1)+' input accepted by native engine');
    projectVerifiedM4ControlReadback();
    updateNativeM4Inputs();
  }
  function acceptM4MonitorAck(ack){
    const p=sourcePending;
    if(!p||!['mute','resume'].includes(p.phase))return;
    if(ack?.ok!==true||Number(ack.deviceEpoch)!==p.epoch||
       ack.armed!==(p.phase==='resume')){
      sourcePending=null;
      status('MONITOR TRANSITION REJECTED · output remains under native safety gate');
      updateNativeM4Inputs();return;
    }
    if(p.phase==='mute')sendM4SourcePatch(p);
    else {sourcePending=null;status('CH '+(p.lane+1)+' source accepted · MAIN monitoring restored');}
    updateNativeM4Inputs();
  }
  // Stop Pages' synthetic input select model from seeing any native choice.
  // This listener captures before the embedded Pages handleField bubble.
  document.addEventListener('change',event=>{
    const select=event.target;
    if(!connected||!select?.matches?.('select.route-select[data-native-hardware-channel]'))return;
    event.preventDefault();event.stopImmediatePropagation();
    chooseM4HardwareInput(select);
  },true);
  document.addEventListener('click',event=>{
    const button=event.target.closest?.('[data-native-monitor]');
    if(!connected||!button)return;
    event.preventDefault();event.stopImmediatePropagation();
    const d=verifiedM4ControlLane();
    if(!d||sourcePending)return;
    backend.emitEvent('arMixAction',{action:'monitor',value:!m4LiveStatus?.monitor,
      expectedEpoch:Number(d.epoch),expectedRevision:Number(d.revision)});
  },true);
  // One hardware setup authority: the existing custom Audio I/O WebView
  // (web/mixer-io.js). Never mount a second CH1-only Patch/Arm/Start menu:
  // all ten mixer IN fields now use the same native route controller.
  function updateHardwarePanel(next) {
    const d=next?.device;
    const opened=!!(d?.backend && d.sampleRate>0 &&
      Array.isArray(d.enabledOutputs) && d.enabledOutputs.length>=2);
    const label=document.getElementById("transport");
    if(label)label.textContent=!opened?"Choose audio device":
      d.outputArmed?"Monitor ON":"Monitor OFF";
    const warning=document.querySelector?.(".status-bar .warning");
    if(warning)warning.textContent=!opened?"NO VERIFIED AUDIO DEVICE":
      d.outputArmed?(next.admitted?"NATIVE PCM · HARDWARE FAT PENDING":
        "MONITOR ON · CHECK REAL INPUT/MAIN PCM"):
      "MONITOR OFF · INPUT CHOICES AVAILABLE IN EACH CHANNEL";
  }
  function onNativeReadback(next) {
    updateHardwarePanel(next);
    const caps=next?.graph?.capabilities;
    const nativeAdmitted=next.admitted && caps?.callbackOwnsChannelIds===true;
    if (!nativeAdmitted) {
      flags.productionCallbackLive=false;flags.acceptedGraphConnected=false;
      lastPaintedSignature="";
      stopDemo();
      updateNativeM4Inputs();
      // In G0, physical M4 DSP owns the ten inputs and faders; unrelated
      // ChannelId-only operations remain gated, not silently simulated.
      if(!g0Announced){g0Announced=true;
        status("Select the real IN on any channel · M4 native source is live");
      }
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
    if (!projection(next)) {
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
          if (!state.pending && flags.acceptedGraphConnected && scope?.render) {
            scope.render();
            if(state.current)paintNativeMeters(state.current);
          }
        }
      }
    });
    backend.addEventListener("arMixStatus", state=>{
      m4LiveStatus=state||null;
      m4Status=state?.dryRoutingLab||null;
      if(sourcePending&&Number(m4Status?.epoch)!==sourcePending.epoch) {
        sourcePending=null;
        status('Audio device changed: source transaction cancelled; output stays safe');
      }
      projectVerifiedM4ControlReadback();
      updateNativeM4Inputs();
    });
    backend.addEventListener("arMixConsoleSnapshot", snapshot=>controller.onSnapshot(snapshot));
    backend.addEventListener("arMixConsoleAck", ack=>controller.onAck(ack));
    backend.addEventListener("arMixSourceSwitchAck",acceptM4SourceAck);
    backend.addEventListener("arMixAudioArmAck", ack=>{
      acceptM4MonitorAck(ack);
      if(ack?.ok===true){
        status(ack.armed?"NATIVE: user ARM accepted; checking real PCM.":
          "NATIVE: output muted.");
        controller.subscribe();
      }else status("NATIVE ARM REJECTED: "+(ack?.error||"device not ready"));
    });
  }
  const nativeHooks=Object.freeze({
    enabled:connected,
    consumeField(event) {
      if (!connected)return false;
      const el=event.target,field=el?.dataset?.field;
      if (!field)return false;
      const t=scope?.findTrack?.(el) || null;
      if (!t)return true;
      if (!flags.acceptedGraphConnected) {
        // Genuine M4 live ten-strip controls work BEFORE ChannelId cutover.
        // Every edit still needs the native epoch/revision fence, and no
        // local fader/mute/model changes until the next verified readback.
        if(event.type==="change" && t.kind==='master'&&field==="level") {
          const numeric=Number(el.value);
          if(!emitVerifiedMasterParam('masterGain',numeric))
            el.value=String(t.level);
          return true;
        }
        if(event.type==="change" && t.kind!=='master'&&
           (field==="level"||field==="pan")) {
          const numeric=Number(el.value);
          const value=field==="pan"&&Number.isFinite(numeric)&&
            el.dataset.panKeyboard!=="true"&&
            el.dataset.panAltPointer!=="true"&&Math.abs(numeric)<=12?0:numeric;
          const submitted=emitVerifiedM4Param(el,field==="level"?"faderDb":"pan",value);
          if(!submitted) {
            el.value=String(field==="level"?t.level:t.pan);
            status("M4 INPUT CONTROL unavailable · refresh verified device");
          }
        }
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
      if (action==="mute"||action==="solo") {
        if(!flags.acceptedGraphConnected && t?.kind==='master') {
          if(action==='mute'&&typeof m4LiveStatus?.masterMute==='boolean')
            emitVerifiedMasterParam('masterMute',!m4LiveStatus.masterMute);
          return true;
        }
        if(!flags.acceptedGraphConnected) {
          const lane=nativePhysicalStrip(btn),d=verifiedM4ControlLane();
          const now=lane>=0?d?.channelControls?.[lane]?.[action]:undefined;
          if(typeof now!=="boolean"||
             !emitVerifiedM4Param(btn,action,!now))
            status("M4 "+action+" unavailable · verify native channel");
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
      if (!flags.acceptedGraphConnected || !current?.admitted ||
          !current.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION")) {
        status("NATIVE: WIRE transaction not admitted; graph unchanged");return true;
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
    consumeDoubleClick(event,t) {
      if (!connected)return false;
      if (event.target.classList.contains("fader")) {
        if(!flags.acceptedGraphConnected)emitVerifiedM4Param(event.target,"faderDb",0);
        else intent("SET_FADER",t,{valueDb:0});
      } else if(event.target.classList.contains("center-pan")) {
        if(!flags.acceptedGraphConnected)emitVerifiedM4Param(event.target,"pan",0);
        else intent("SET_PAN",t,{value:0});
      }
      else status("NATIVE: identity editor unavailable until native ACK contract");
      return true;
    },
    state:()=>controller?.state() || null
  });
  window.__ARMIX_NATIVE_M2__ = nativeHooks;
  const descriptor = Object.freeze({
    source:"site/preview-v2 + site/preview-buttons",
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
    if (!event.target.closest?.(".fader-box,.center-pan")) return;
    event.preventDefault();event.stopImmediatePropagation();
    status("NATIVE G0: physical gain/pan unavailable until engine graph admission");
  },true);
  // Reference-only buttons outside strips must not mutate fake Show/FX state.
  document.addEventListener("click",event=>{
    const target=event.target.closest?.(
      "#preview,#addTrack,#addBus,#addFx,#reset,#saveScene,#recallScene,#sceneSafe,#group,#ungroup,#copy,#paste,#clearCue,#pluginsButton,#pluginScan,#pluginImport,#pluginAddPath");
    if(!target)return;
    event.preventDefault();event.stopImmediatePropagation();
    status("NATIVE: "+target.id+" requires real engine admission and readback");
  },true);
  // Native fader is committed exactly once INSIDE the original GUI's
  // finishFaderPointer via build_approved_native_gui.py hook. A later
  // window.pointerup listener would run after that handler clears the gesture.
  const ready=()=>{
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
