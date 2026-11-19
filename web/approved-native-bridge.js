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
    const value=["SET_FADER","SET_INPUT_GAIN"].includes(operation)?payload.valueDb:
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
      status("Mixer edit not confirmed · previous saved settings retained");
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
        scope?.rollbackNativeWireInspector?.(nativeWireGainReadback(current));
        continue;
      }
      if(plan.kind==="CONFIRMED"){
        scope?.syncNativeWireGains?.(nativeWireGainReadback(current));
        continue;
      }
      const result=controller.intent("SET_WIRE_GAIN",plan.source,{
        edgeId:plan.edgeId,gainLinear:plan.gainLinear});
      if(result.ok)window.setTimeout(()=>controller?.expire(result.requestId),8000);
      else status("WIRE gain rejected · "+result.error);
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
    if(!Number.isFinite(db)||db< -60||db>6||
       !admittedParallelWire(op.sourceId,op.edgeId,"SET_WIRE_GAIN"))return false;
    const current=controller.state().current;
    const edgeId=wireByUiId.get(op.edgeId),source=nativeByUiId.get(op.sourceId);
    // A physical SEND fader floor is -infinity (zero PCM), NOT -60 dB linear.
    pendingWireGains.set(edgeId,{source,gain:db<=-60?0:Math.pow(10,db/20),identity:current.identity});
    queueWireGainFlush();return true;
  }
  function nativeOutputTargets(t,channels=activeView()?.graph?.channels||[]){
    return stripVM.outputTargets(channels,nativeIdFor(t),displayNameFor);
  }
  let pendingNativeGainIntent=false;
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
    if(!current?.admitted||current.retained||state.pending||
       !current.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION")||
       !source||!edgeId||!["PRE_FADER","POST_FADER"].includes(tap)){
      status("PRE/POST unavailable · wait for an admitted native WIRE");return false;
    }
    const selected=wireVM.parallelEdge(current.graph,source,edgeId);
    if(!wireVM.canEditTap(selected,{supported:true})){
      status("PRE/POST unavailable · selected send identity changed");return false;
    }
    if(selected.tap===tap)return true;
    const result=controller.intent("APPLY_WIRE_TRANSACTION",source,{changes:[{
      op:"SET_TAP",from:source,edgeId,tap
    }]});
    if(!result.ok){status("PRE/POST unchanged · "+result.error);return false;}
    window.setTimeout(()=>controller?.expire(result.requestId),8000);
    return true;
  }
  function selectedNativeSofEdge(t) {
    if(!scope?.sofEnabled||!t||t.kind!=="audio")return null;
    const current=controller?.state()?.current,source=nativeIdFor(t);
    const matches=scope?.tracks?.filter(x=>x.id===scope.sofTargetId&&
      (x.kind==="bus"||x.kind==="fx"))||[];
    if(!current?.admitted||!source||matches.length!==1)return null;
    const dest=nativeIdFor(matches[0]);
    const edges=current.graph.edges.filter(e=>e.enabled===true&&
      e.from===source&&e.to===dest&&e.primaryOutput===false);
    return edges.length===1?wireVM.parallelEdge(current.graph,source,
      edges[0].edgeId,dest):null;
  }
  function syncNativeSofAvailability(next=controller?.state()?.current) {
    const admitted=wireVM.sofAvailable(next,!!scope?.toggleSof);
    const toggle=document.getElementById("sofToggle"),target=document.getElementById("sofTarget");
    for(const el of [toggle,target])if(el){
      el.style?.setProperty?.("display",admitted?"":"none","important");
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
      t.name=displayNameFor(ch);t.kind=kind;
      const appearance=channelPresentation.get(ch.id);
      if(appearance?.color)t.color=appearance.color;
      if(appearance?.icon)t.icon=appearance.icon;
      t.format=ch.processingFormat.toLowerCase();
      t.level=ch.controls.faderDb;
      t.trim=Number.isFinite(ch.controls.inputGainDb)?ch.controls.inputGainDb:0;
      t.nativeGainAvailable=(native.admitted===true||native.logical===true) && kind==="audio" &&
        native.graph.capabilities.supportedOps.includes("SET_INPUT_GAIN") &&
        Number.isFinite(ch.controls.inputGainDb);
      t.nativeTapAvailable=native.admitted===true&&kind==="audio"&&
        native.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION");
      t.nativeSendAvailable=native.admitted===true&&kind==="audio"&&
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
      const pair=direct && hardwareOutputPairs().find(p=>
        p.left===direct.physicalLeft&&p.right===direct.physicalRight);
      t.externalOutput=direct?(direct.status==="LIVE"&&pair?pair.label:
        (direct.status==="PENDING"?"Pending · ":"Needs Remap · ")+
        "OUT "+(direct.physicalLeft+1)+"/"+(direct.physicalRight+1)):"None";
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
    for (const [id,ui] of localByNative) uiByNativeId.set(id,ui);
    nativeByUiId.clear();for(const [ui,id] of nextNativeByUi)nativeByUiId.set(ui,id);
    wireByUiId.clear();for(const [ui,id] of newWireByUi)wireByUiId.set(ui,id);
    uiByWireId.clear();for(const [id,ui] of newUiByWire)uiByWireId.set(id,ui);
    meterElements.clear(); // installGraph() replaces strip HTML and meter nodes
    scope.installGraph(nextTracks,nextMaster,edges);
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
  // One authoritative EdgeId gain projection for normal readback AND failure
  // rollback. No duplicate DSP math or synthetic accepted control state.
  function nativeWireGainReadback(next){
    return wireVM.gainReadback(next?.graph?.edges||[],
      uiByWireId,pendingWireGains);
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
        const slider=document.querySelector('.strip[data-id="'+tId+'"] input.fader');
        if(t&&slider&&document.activeElement!==slider){
          slider.value=String(local.levelDb);
          scope.syncFaderVisual?.(t);
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
      const gainAdmitted=next.admitted===true&&ch.role==="INPUT"&&
        next.graph.capabilities.supportedOps.includes("SET_INPUT_GAIN")&&
        Number.isFinite(c.inputGainDb);
      t.nativeGainAvailable=gainAdmitted;
      if(gainAdmitted)paintAcceptedInputGain(t,c.inputGainDb);
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
    for(const edge of next.graph.edges||[]){
      const ui=uiByWireId.get(edge.edgeId);
      if(ui===undefined)continue;
      for(const input of document.querySelectorAll?.('.wire-gain[data-wire-id="'+ui+'"]')||[]){
        const fixed=edge.primaryOutput===true;
        const disabled=fixed || next.retained;
        if(input.disabled!==disabled)input.disabled=disabled;
        const title="Primary OUT is unity gain · use the destination fader";
        if(fixed&&input.title!==title)input.title=title;
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
  const nativeMasterReady=()=>mixerSession()?.authority==="CHANNEL_ID" &&
    safeNativeCounter(hardware?.device?.epoch)!==null &&
    safeNativeCounter(legacy()?.routingRevision)!==null &&
    Number.isFinite(mixerSession()?.main?.db) &&
    typeof mixerSession()?.main?.mute==="boolean";
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
    const choices=hardwareOutputPairs();
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
      const labels=choices.map(p=>p.label);
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
  const inputLabelFor=row=>deviceVM.inputLabel(hardware,row);
  // ChannelId and the existing M4 use the SAME ten native input ordinals.
  // GUI track reorder must never silently repatch a different physical lane.
  const m4TrackIds=Array.isArray(scope?.tracks)?
    scope.tracks.filter(t=>t.kind==="audio").slice(0,10).map(t=>t.id):[];
  const m4Index=t=>t?.kind==="audio"?m4TrackIds.indexOf(t.id):-1;
  let lastM4UiSignature="",lastM4Controls="",nativeUiSourcePending=null;
  let lastM4PortSignature="",pendingOutputPatch=null,m4RequestSerial=0,outputSerial=7000;
  const pendingSourceQueue=new Map();
  function requestProcessingFormat(t){
    if(logicalView){
      const id=nativeIdFor(t),source=logicalView.graph.channels.find(ch=>ch.id===id);
      if(source?.role!=="INPUT")return false;
      const format=source.processingFormat==="MONO"?"STEREO":"MONO";
      return logicalIntent("SET_PROCESSING_FORMAT",id,{format}).ok;
    }
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
  // MV-14: one INPUT dispatch owner; ChannelId/M4 keep separate identity Maps.
  function dispatchSourceQueue(authority){
    const channelIdOwner=authority==="CHANNEL_ID";
    const queue=channelIdOwner?pendingNativeSources:pendingSourceQueue;
    if(!queue.size)return;
    const current=controller?.state()?.current;
    while(queue.size){
      const plan=stripVM.sourceQueueHead({authority,queue,
        active:channelIdOwner?activeNativeSource:nativeUiSourcePending,
        hardware,native:current,choices:inputChoicesFor(),
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
    controller?.subscribe();
    return true;
  }
  let pendingBusCreate=null,pendingTrackDelete=null,stripRequestSerial=0;
  function requestNativeTrackDelete(track){
    if(logicalView)return logicalIntent("DELETE_CHANNEL",nativeIdFor(track),{}).ok;
    const current=controller?.state()?.current,id=nativeIdFor(track);
    const channel=current?.graph?.channels?.find(ch=>ch.id===id);
    if(!connected||pendingTrackDelete||pendingBusCreate||controller?.state()?.pending||
       !current?.admitted||current.retained||channel?.role!=="INPUT"||
       !current.graph.capabilities.supportedOps.includes("DELETE_CHANNEL")){
      nativeToast("Delete Track unavailable · wait for an admitted native input channel");
      return false;
    }
    const requestId="track-delete-"+(++stripRequestSerial);
    pendingTrackDelete={requestId,id,name:track.name,epoch:current.device.epoch,
      revision:current.graph.revision,acked:false};
    syncAddTrack(current);
    backend.emitEvent("arMixAction",{
      action:"routingNativeTrackDelete",requestId,channelId:id,
      deviceEpoch:current.device.epoch,graphRevision:current.graph.revision,
      rosterRevision:current.graph.rosterRevision
    });
    status("Deleting "+track.name+" · waiting for native PCM ACK/readback");
    window.setTimeout(()=>{
      if(pendingTrackDelete?.requestId!==requestId)return;
      pendingTrackDelete=null;syncAddTrack();
      nativeToast("Delete Track timed out · accepted mixer unchanged until readback");
      controller?.subscribe();
    },8000);
    return true;
  }
  // BUS and FX_RETURN use their actual ChannelId; never remove them from
  // browser state before the native publisher ACK and independent graph readback.
  function requestNativeBusDelete(track){
    if(logicalView)return logicalIntent("REMOVE_BUS",nativeIdFor(track),{}).ok;
    const current=controller?.state()?.current,id=nativeIdFor(track);
    const channel=current?.graph?.channels?.find(ch=>ch.id===id);
    if(!connected||pendingTrackDelete||pendingBusCreate||controller?.state()?.pending||
       !current?.admitted||current.retained||
       !["GROUP","FX_RETURN"].includes(channel?.role)||
       !current.graph.capabilities.supportedOps.includes("REMOVE_BUS")){
      nativeToast("Delete unavailable · wait for an admitted native AUX/BUS");
      return false;
    }
    const requestId="bus-delete-"+(++stripRequestSerial);
    pendingTrackDelete={requestId,id,name:track.name,role:channel.role,
      epoch:current.device.epoch,revision:current.graph.revision,acked:false};
    syncAddTrack(current);
    backend.emitEvent("arMixAction",{
      action:"routingNativeBusDelete",requestId,channelId:id,
      deviceEpoch:current.device.epoch,graphRevision:current.graph.revision,
      rosterRevision:current.graph.rosterRevision
    });
    status("Deleting "+track.name+" · waiting for native PCM ACK/readback");
    window.setTimeout(()=>{
      if(pendingTrackDelete?.requestId!==requestId)return;
      pendingTrackDelete=null;syncAddTrack();
      nativeToast("Delete timed out · accepted graph preserved until readback");
      controller?.subscribe();
    },8000);
    return true;
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
    const current=controller?.state()?.current,id=nativeIdFor(track);
    const ch=current?.graph?.channels?.find(row=>row.id===id);
    return Boolean(current?.admitted&&!current.retained&&ch&&
      ((ch.role==="INPUT"&&current.graph.capabilities.supportedOps.includes("DELETE_CHANNEL"))||
       (["GROUP","FX_RETURN"].includes(ch.role)&&
        current.graph.capabilities.supportedOps.includes("REMOVE_BUS"))));
  }
  function finishNativeTrackDelete(next){
    const pending=pendingTrackDelete;
    if(!pending?.acked||!next?.admitted||next.retained||
       pending.epoch!==next.device.epoch||
       BigInt(next.graph.revision)<BigInt(pending.revision)||
       next.graph.channels.some(ch=>ch.id===pending.id))return;
    pendingTrackDelete=null;
    syncAddTrack(next);
    nativeToast(pending.name+" deleted · native PCM and WIRE readback verified");
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
    if(next?.retained)return false;
    if(next?.admitted)
      return next.graph.capabilities.supportedOps.includes("CREATE_CHANNEL");
    return mixerSession()?.authority==="NONE" && deviceReady();
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
  function finishNativeTrack(next){
    const pending=pendingBusCreate;
    if(!pending?.track || !pending.createdId || !next?.admitted ||
       next.device.epoch!==pending.epoch ||
       BigInt(next.graph.revision)<BigInt(pending.revision))return;
    const channel=next.graph.channels.find(ch=>ch.id===pending.createdId && ch.role==="INPUT");
    const id=channel && uiByNativeId.get(channel.id);
    const strip=scope?.tracks.find(t=>t.id===id);
    if(!strip||!scope.revealTrack)return;
    pendingBusCreate=null;
    syncAddTrack(next);
    scope.revealTrack(strip);
    nativeToast(strip.name+" added · choose its IN to receive audio");
  }
  function requestNativeBusCreate(track=false){
    if(logicalView)
      return logicalIntent(track?"CREATE_CHANNEL":"CREATE_GROUP",null,{}).ok;
    const accepted=controller?.state()?.current;
    const session=mixerSession();
    const capabilities=accepted?.graph?.capabilities?.supportedOps;
    const available=track?canCreateNativeTrack(accepted):
      session?.authority==="CHANNEL_ID" && accepted?.admitted &&
      capabilities?.includes("CREATE_GROUP");
    if(pendingBusCreate||pendingTrackDelete||!available)return false;
    const requestId=(track?"track-":"bus-")+(++stripRequestSerial);
    const epoch=hardware.device.epoch;
    const bootstrap=track && session.authority==="NONE";
    pendingBusCreate={requestId,track,epoch};
    syncAddTrack(accepted);
    backend.emitEvent("arMixAction",{
      action:track?"routingNativeTrackCreate":"routingNativeBusCreate",requestId,
      deviceEpoch:epoch,
      ...(bootstrap?{bootstrapRevision:legacy().routingRevision}:{
        graphRevision:accepted.graph.revision,
        rosterRevision:accepted.graph.rosterRevision})});
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
  function openRoutedFxEditor(lane,slot,instanceId,deviceEpoch,revision){
    backend.emitEvent("arMixAction",{action:"routingFxEditor",
      channel:lane,slot,instanceId,deviceEpoch,revision});
  }
  // One verified post-insert navigation: never open an unaccepted slot and
  // never infer an instance from a catalog name. Reuse the installed editor
  // action for both native DSP and vendor VST3s, after exact graph readback.
  function openVerifiedInsertedFx(channelId,slot,catalogId,instanceId) {
    window.queueMicrotask(()=>{
      const view=activeView();
      const effect=view?.graph?.channels?.find(ch=>ch.id===channelId)
        ?.inserts?.[slot];
      if(!effect||effect.id!==instanceId||effect.catalogId!==catalogId)return;
      const local=uiByNativeId.get(channelId);
      const track=[...(scope?.tracks||[]),scope?.master]
        .find(item=>item?.id===local);
      if(track)
        window.__ARMIX_NATIVE_M2__?.consumeAction?.(track,"openPlugin",
          {dataset:{slot:String(slot)}});
    });
  }
  // Native JUCE processors: these are real compiled DSP, not demo effects,
  // scanned VST binaries, a JS renderer or placeholder library entries.
  let nativeBuiltins=Object.freeze([
    Object.freeze({id:"armix.builtin.channel-eq.v1",name:"Parametric EQ",
      vendor:"ArSonMix",category:"EQ",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.channel-compressor.v1",name:"Compressor",
      vendor:"ArSonMix",category:"Dynamics",format:"Native",source:"builtin"}),
    Object.freeze({id:"armix.builtin.arson-harmony.v1",name:"Harmonice",
      vendor:"ArSonMix",category:"Utility",format:"Native",source:"builtin"}),
    // Native GuitarRigProcessor is compiled and installed: no WebAudio mock.
    Object.freeze({id:"armix.builtin.guitar-rig.v1",name:"Guitar FX Rig",
      vendor:"ArSonMix",category:"Guitar",format:"Native",source:"builtin"}),
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
  function verifiedFxFor(t,slot){
    const lane=m4Index(t),live=legacy()?.fxInstances?.[lane]?.[slot];
    return lane>=0 && Number.isInteger(slot) && slot>=0&&slot<8 &&
      live && typeof live.instanceId==="string" &&
      live.instanceId && typeof live.catalogId==="string"?
      {lane,slot,live}:null;
  }
  function editNativeFx(t,slot,target,position,token){
    const current=controller?.state()?.current,id=nativeIdFor(t);
    const decision=fxRackVM.nativeEditPlan(current,{
      channelId:id,slot,instanceId:token,move:!!target,
      destinationId:target?nativeIdFor(target):null,
      position:target?position:null});
    if(decision.kind!=="READY"){
      if(decision.kind!=="NO_CHANGE")
        status("FX edit unavailable · refresh accepted native instance");
      return false;
    }
    const result=controller.intent(decision.operation,decision.channelId,
                                   decision.payload);
    if(!result.ok){status("FX edit rejected · "+result.error);return false;}
    window.setTimeout(()=>controller?.expire(result.requestId),8000);
    status(target?"Moving native FX · awaiting accepted readback":
      "Removing native FX · awaiting accepted readback");
    return true;
  }
  function requestNativeFxPower(t,slot,bypass){
    const picked=verifiedFxFor(t,slot);
    if(!picked||!m4Verified())return false;
    const ep=safeNativeCounter(hardware?.device?.epoch),
      rev=safeNativeCounter(legacy()?.routingRevision);
    const {lane,live}=picked,requestId="fxpower-"+(++fxRequestSerial);
    if(ep===null||rev===null||pendingSlotPowers.has(lane+":"+slot))
      return false;
    pendingSlotPowers.set(lane+":"+slot,{
      requestId,channel:lane,slot,token:live.instanceId,bypass,
      epoch:String(ep),revision:String(rev),acked:false});
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
      const decision=fxRackVM.m4RackSettlement(p,session);
      if(decision==="STALE"||decision==="CONFIRMED")
        pendingM4RackPowers.delete(lane);
    }
    // An M4 slot remains pending after ACK until its own exact instance
    // lease and requested bypass are present in independently accepted PCM.
    for(const [key,p] of pendingSlotPowers){
      const decision=fxRackVM.m4SlotPowerSettlement(p,next);
      if(decision==="STALE"||decision==="CONFIRMED")
        pendingSlotPowers.delete(key);
    }
  }
  // Global A/B is a SINGLE native atomic override. Individual racks and
  // inserts remain untouched. Never simulate success in WebView while device
  // admission or a matching native ACK + subsequent readback is missing.
  function globalFxContext(){
    return fxRackVM.globalPowerContext(controller?.state()?.current,
      m4Verified(),!!logicalView);
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
  function toggleGlobalFx(){
    const current=controller?.state()?.current,ctx=globalFxContext();
    const plan=fxRackVM.globalPowerPlan(current,ctx,!!pendingGlobalFx);
    if(plan.kind!=="READY")return false;
    const requestId="global-fx-"+(++globalFxSerial);
    pendingGlobalFx={requestId,bypassed:plan.bypassed,acked:false,
      epoch:ctx.epoch,revision:ctx.revision,authority:ctx.authority};
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
      pending:!!pendingNativeFxPower});
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
      parameter==="masterGain"&&(m4Verified()||nativeMasterReady());
    if(m4ControlIsOwner())return m4Verified()&&m4Index(t)>=0;
    return channelIdScalarReady(parameter,t);
  }
  const nativeEditIdentity=(t)=>({
    epoch:logicalView?"LOGICAL":String(hardware?.device?.epoch),
    authority:logicalView?"LOGICAL":mixerSession()?.authority,
    channelId:logicalView?nativeIdFor(t):t?.kind==="master"?null:nativeIdFor(t)
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
    scope?.syncFaderVisual?.({...t,level:db});
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
  function syncMonitorButton(d) {
    const monitorButton=document.querySelector?.("[data-native-monitor]");
    if(monitorButton){
      monitorButton.textContent=d?.outputArmed?"Mute Output":"Unmute Output";
      monitorButton.setAttribute("aria-pressed",String(!!d?.outputArmed));
      monitorButton.title=d?.outputArmed?
        "Mute actual MAIN output immediately":"Listen through verified MAIN hardware output";
      monitorButton.disabled=!d?.outputArmed && !deviceReady();
      monitorButton.dataset.armed=String(!!d?.outputArmed);
    }
  }
  // Output patching is rendered from physical native readback, never a
  // second routing state. Repainting only on changes avoids 12 Hz DOM churn.
  function paintPhysicalOutputs(next) {
    const view=devicePanel?.querySelector?.("[data-audio-output-matrix]");
    if(!view || devicePanel.hidden)return;
    const patches=protocol.physicalOutputOverview(next);
    const signature=JSON.stringify(patches);
    if(devicePanel.dataset.outputSignature===signature)return;
    devicePanel.dataset.outputSignature=signature;
    const summary=devicePanel.querySelector("[data-audio-output-overview] .armix-output-heading");
    if(summary)summary.textContent="OUTPUTS · "+patches.length+" physical patches";
    view.replaceChildren();
    if(!patches.length){
      view.textContent="No verified ASIO output pins · choose an ASIO device.";
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
  function updateHardwarePanel(next) {
    hardware=next || null;
    refreshRealM4Inputs(next);
    const d=next?.device;
    const deviceOpen=Boolean(d?.backend && d.sampleRate>0 &&
      d.enabledOutputs.length>=2);
    const label=document.getElementById("transport");
    const playback=String(d?.playbackDevice||"unknown output");
    const virtualOut=/VB-Audio Virtual Cable|CABLE Input/i.test(playback);
    syncMonitorButton(d);
    paintPhysicalOutputs(next);
    const mainOutput=next?.retained?NEEDS_REMAP:deviceOpen?playback:null;
    if(scope?.master && mainOutput && scope.master.output!==mainOutput){
      scope.master.output=mainOutput;
      if(!next?.admitted && !next?.retained)scope.render?.();
    }
    const transport=next?.retained?"Output disconnected · Open Audio I/O":
      !deviceOpen?"Audio device unavailable · Open Audio I/O":
      (d.outputArmed?"Output enabled":"Output muted")+" · "+playback+
      (virtualOut?" · Virtual output · No speakers":"");
    const operatorTransport=operatorMessage(transport);
    if(label && label.textContent!==operatorTransport)label.textContent=operatorTransport;
    const warning=document.querySelector?.(".status-bar .warning");
    // A silent first-sound session is not an operator failure and does
    // not require Patch CH1 / Arm / Start controls. The real IN dropdown
    // initiates native source publication; only its correlated ACK may
    // request a safe first MONITOR. If the driver exposes no physical IN,
    // guide the operator to the existing Audio I/O panel instead.
    const warningText=next?.retained?"PLAYBACK UNAVAILABLE · MIXER SAVED":
      !deviceOpen?"NO VERIFIED AUDIO DEVICE":
      next?.raw?.waitingAsioInputs>0?"INPUT TEMPORARILY UNAVAILABLE · AUTO-RECONNECT":
      d.outputArmed?(next.admitted?"OUTPUT ENABLED":
        "CHECK AUDIO CONNECTION"):
      !d.enabledInputs?.length?(next.admitted?"CHOOSE AN INPUT":
        "CHECK AUDIO I/O SETTINGS"):
      legacy()?.routeAccepted?"NATIVE INPUT ROUTED · MONITOR OFF":
      deviceReady()?"SELECT CHANNEL IN · NATIVE MONITOR AFTER ACK":
      "NO VERIFIED INPUT ROUTE · OPEN AUDIO I/O";
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
  let devicePanel=null,deviceInventory=null,deviceChoice=null,deviceRequest=0,
      deviceState=deviceVM.setupInitial(),deviceApplyTimer=0;
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
      `;
      document.head.appendChild(style);
      devicePanel=document.createElement("div");
      devicePanel.className="armix-audio-shade";
      devicePanel.innerHTML=`<section class="armix-audio-dialog" role="dialog"
        aria-modal="true" aria-label="Audio hardware setup">
        <header class="armix-audio-top"><h2>Audio I/O</h2><button class="armix-floating-close" type="button"
          data-audio-close aria-label="Close Audio I/O" title="Close"><svg xmlns="http://www.w3.org/2000/svg"
          viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path
          d="M6.4,19L5,17.6L10.6,12L5,6.4L6.4,5L12,10.6L17.6,5L19,6.4L13.4,12L19,17.6L17.6,19L12,13.4L6.4,19Z"/></svg></button></header>
        <label>Driver<select data-audio-field="backend"></select></label>
        <label>Input device<select data-audio-field="capture"></select></label>
        <label>Output / monitor device<select data-audio-field="playback"></select></label>
        <div class="armix-device-note" data-audio-active>Reading current audio device…</div>
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
        <section class="armix-output-overview" data-audio-output-overview>
          <div class="armix-output-heading">OUTPUTS · physical routing</div>
          <div class="armix-output-matrix" data-audio-output-matrix role="list"></div>
          <div class="armix-device-note">Native callback/readback only · speaker listening not verified</div>
        </section>
        <details><summary>Advanced · rate, buffer and channel pins</summary>
          <div class="armix-device-note" data-audio-asio4all-hint hidden>
            Check ASIO4ALL WDM inputs/outputs before Apply. Different device clocks may drift.
          </div>
          <label>Sample rate<select data-audio-field="rate"></select></label>
          <label>Buffer<select data-audio-field="buffer"></select></label>
          <div class="armix-device-pins" data-audio-pins></div>
          <button type="button" data-audio-main-pins hidden>Apply two selected playback pins as MAIN</button>
        </details>
        <div class="armix-device-actions">
          <button type="button" data-audio-apply>Apply device</button>
          <button type="button" data-audio-remap hidden>Use same pins on this device</button>
        </div></section>`;
      devicePanel.querySelector("[data-audio-close]").onclick=()=>{
        if(!deviceBusy())devicePanel.hidden=true;
      };
      devicePanel.addEventListener("click",e=>{
        if(e.target===devicePanel&&!deviceBusy())devicePanel.hidden=true;
      });
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
      rateSelect.options[0].textContent="Driver default";
      rateSelect.disabled=!view.same||deviceBusy();
    }
    if(bufferSelect){
      bufferSelect.options[0].textContent="Driver default";
      bufferSelect.disabled=!view.same||deviceBusy();
    }
    const pins=devicePanel.querySelector("[data-audio-pins]");
    if(pins){
      pins.replaceChildren();
      if(view.same){
        const container=document.createElement("div");
        container.className="armix-audio-pins";
        for(const [side,names,selected] of [
          ["input",active.inputNames,view.inputPins],
          ["output",active.outputNames,view.outputPins]]){
          const group=document.createElement("div");
          const title=document.createElement("strong");
          title.textContent=side==="input"?"Capture pins":"Playback pins";
          group.appendChild(title);
          (names||[]).forEach((name,index)=>{
            const label=document.createElement("label");
            const control=document.createElement("input");
            control.type="checkbox";
            control.dataset.audioPin=side;
            control.dataset.pinIndex=String(index);
            control.checked=selected.includes(index);
            control.disabled=deviceBusy();
            label.append(control,document.createTextNode(String(name)));
            group.appendChild(label);
          });
          container.appendChild(group);
        }
        pins.appendChild(container);
      }else pins.textContent="Apply this device first to inspect its real enabled pins.";
    }
    const activeText=devicePanel.querySelector("[data-audio-active]");
    if(activeText&&!deviceBusy())activeText.textContent=view.activeText;
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
    apply.textContent=pending?"Applying…":"Apply device";
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
  function finishDeviceApplyUi(verifiedSnapshot=null){
    window.clearTimeout(deviceApplyTimer);
    deviceApplyTimer=0;
    if(!devicePanel)return;
    showDeviceApplyProgress(false);
    if(deviceState.phase==="ACCEPTED"){
      deviceChoice=null;
      devicePanel.hidden=true;
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
      if(!inventory||!Array.isArray(inventory.types)||deviceBusy())return;
      // Inventory is a native observation, not accepted Apply proof.
      deviceInventory=inventory;
      deviceChoice=null;
      renderDevicePanel();
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
        nativeToast(error);status(error);controller?.subscribe();return;
      }
      transaction.acked=true;
      controller?.subscribe(); // ACK alone cannot claim LIVE audio
    });
    backend.addEventListener("arMixNativeSessionAck",ack=>{
      const save=ack?.operation==="SAVE";
      if(!save&&ack?.operation!=="OPEN")return;
      if(ack.ok!==true){
        const error=String(ack.error||"Mixer file operation failed");
        status(error);nativeToast(error);
        return;
      }
      const message=save?"Mixer saved as .smix":"Mixer opened";
      status(message);nativeToast(message);
      if(!save)controller?.subscribe();
    });
    menu.append(caption,setup,muteBtn,hardwareNote,
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
      const io=item("button","tool","Audio I/O");
      io.dataset.nativeIo="true";
      io.title="Select ASIO/WASAPI driver and capture/playback device";
      io.addEventListener("click",openNativeAudioIO);
      toolbar.insertBefore(io,document.getElementById("moreTools"));
      const addTrack=document.getElementById("addTrack");
      if(addTrack){
        addTrack.textContent="+ Audio Track";
        toolbar.insertBefore(addTrack,io);
      }
      syncAddTrack();
      const monitor=item("button","tool","Unmute Output");
      monitor.dataset.nativeMonitor="true";
      monitor.addEventListener("click",()=>{
        const armed=hardware?.device?.outputArmed===true;
        if(!armed && (!deviceReady()||
            (mixerSession()?.authority!=="CHANNEL_ID"&&legacy()?.routeAccepted!==true)||
            virtualCableFeedbackPair())){
          status("MONITOR OFF · select real input and safe playback device");
          return;
        }
        const ep=safeNativeCounter(hardware?.device?.epoch),
          rev=safeNativeCounter(mixerSession()?.authority==="CHANNEL_ID"?
            mixerSession()?.routeRevision:legacy()?.routingRevision);
        if(!armed&&(ep===null||rev===null))return;
        backend.emitEvent("arMixAction",{action:"monitor",value:!armed,
          requestId:"manual-monitor-"+(++m4RequestSerial),
          ...(armed?{}:{expectedEpoch:ep,expectedRevision:rev})});
        status(armed?"Hardware MUTE requested":"MAIN monitor verifying hardware route");
      });
      toolbar.insertBefore(monitor,io);
      // Fixed groups, not extra controls. Placement is DOM-only at startup;
      // no resize listeners, repaint loops or menu reparenting.
      for(const [name,element] of [
        ["files",document.getElementById("pluginsButton")],
        ["routing",document.getElementById("wireNavigatorToggle")],
        ["channels",addTrack],
        ["monitor",monitor]
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

  function playAcceptedWireImpact(changes) {
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
  let lastNativeLifecycleLease=null;
  // G-MV24: ONE complete GUI-only discard path for both physical/authority
  // switches and repeated NEEDS_REMAP readbacks. No second PCM ownership.
  // Keep cosmetic readback signatures separate so retained-mixer telemetry
  // does not force pointless full DOM repaint on every callback.
  function discardTransientNativeUi() {
    pendingNativeSources.clear();pendingSourceQueue.clear();
    activeNativeSource=null;nativeUiSourcePending=null;pendingSourceId=0;
    pendingOutputPatch=null;pendingNativeFxPower=null;pendingGlobalFx=null;
    pendingBusCreate=null;pendingTrackDelete=null;pendingFxInsert=null;
    startupWire=null;activeWireAttempt=null;pendingNativeGainIntent=false;
    pendingWireGains.clear();pendingNativeControl.clear();
    liveFaderVisualTargets.clear();lastNativeControlSent.clear();
    pendingM4Controls.clear();pendingM4RackPowers.clear();
    // MV-18: these two hardware-scoped leases were previously omitted.
    // A same-epoch driver/endpoint replacement or M4 authority change
    // must not strand EXTERNAL OUT selects or slot-bypass edits behind
    // an old ACK/timer. Never touch persisted PCM/FX state here.
    pendingExternal.clear();pendingSlotPowers.clear();
    if(pendingIdentity){
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
    if(readModel.kind==="LOGICAL_ONLY") {
      const staged=readModel.view;
      let revealCreatedId=null,wireImpact=null;
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
        window.queueMicrotask(flushLiveScalars);
      }
      logicalView=staged;
      const signature="logical|"+offline.workspaceToken+"|"+offline.revision+"|"+JSON.stringify(
        offline.graph.channels.map(ch=>[ch.id,ch.name,ch.controls,ch.inserts,ch.fxRackBypassed]))+
        "|"+JSON.stringify(offline.graph.edges);
      if(signature!==lastPaintedSignature) {
        if(!projectNativeGraphWithoutFocusLoss(staged)){
          logicalView=null;
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
      const addBus=document.getElementById("addBus");
      if(addBus){addBus.hidden=false;addBus.disabled=!!pendingLogical;}
      status(staged.readOnly?
        "Saved mixer recovery required · Open a verified .smix file":
        "Mixer ready to edit · connect Audio I/O to enable sound");
      return;
    }
    logicalView=null;
    if(pendingLogical) {
      pendingLogical=null;
      status("Audio connection changed · refresh mixer before editing");
    }
    if(readModel.kind==="STALE") {
      flags.acceptedGraphConnected=false;
      flags.productionCallbackLive=false;
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
    if(readModel.kind==="M4") {
      paintM4Fallback(next);
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
    const addBus=document.getElementById("addBus");
    if(addBus){
      addBus.hidden=!nativeAdmitted ||
        !caps?.supportedOps?.includes("CREATE_GROUP");
      addBus.disabled=!!pendingBusCreate||!!pendingTrackDelete;
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
      discardTransientNativeUi();
      const signature="retained|"+next.identity+"|"+appearanceRevision;
      if(lastPaintedSignature!==signature && projectNativeGraphWithoutFocusLoss(next)){
        lastPaintedSignature=signature;
        document.documentElement.dataset.armixNativeRoster="accepted";
      }
      stopDemo();
      paintNativeMeters(next);
      status("Playback unavailable · waiting for the audio device");
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
      // Telemetry-only snapshots should not traverse all channels. But an
      // expired/rejected edit with unchanged graph values must not strand its
      // LCD at a speculative value; error status settles that separately.
      finishNativeTrack(next);finishNativeTrackDelete(next);
      // Telemetry often advances without a structural graph revision.
      paintNativeMeters(next);
      if(startupWire)window.queueMicrotask(flushStartupWire);
      return;
    }
    if (!projectNativeGraphWithoutFocusLoss(next)) {
      flags.acceptedGraphConnected=false;flags.productionCallbackLive=false;
      stopDemo();status("NATIVE READBACK INVALID · existing GUI controls unchanged");
      return;
    }
    document.documentElement.dataset.armixNativeRoster="accepted";
    flags.productionCallbackLive=next.productLive;flags.acceptedGraphConnected=true;
    syncMonitorButton(next.device);
    lastPaintedSignature=signature;stopDemo();paintNativeMeters(next);
    if(confirmed?.operation==="APPLY_WIRE_TRANSACTION")
      playAcceptedWireImpact(confirmed.payload.changes);
    finishNativeTrack(next);finishNativeTrackDelete(next);
    const fxInsert=fxRackVM.nativeInsertSettlement(pendingFxInsert,next,confirmed);
    if(fxInsert.kind==="STALE")pendingFxInsert=null;
    if(fxInsert.kind==="CONFIRMED"){
      pendingFxInsert=null;
      scope?.closeNativePicker?.();
      openVerifiedInsertedFx(fxInsert.channelId,fxInsert.slot,
        fxInsert.catalogId,fxInsert.instanceId);
      status("VST3 loaded · "+fxInsert.name+" · native PCM readback verified");
    }
    const fxSettlement=fxRackVM.nativePowerSettlement(pendingNativeFxPower,next);
    if(fxSettlement==="CONFIRMED"||fxSettlement==="STALE")
      pendingNativeFxPower=null;
    if(startupWire)window.queueMicrotask(flushStartupWire);
    status("Mixer settings updated");
  }
  if (connected) {
    controller = protocol.createController({
      emit:(name,payload)=>backend.emitEvent(name,payload),
      onAccepted:(next,confirmed)=>{
        onNativeReadback(next,confirmed);
        syncGlobalFxReadback();
      },
      onStatus:state=>{
        const settledGain=pendingNativeGainIntent&&!state.pending;
        if(settledGain){
          pendingNativeGainIntent=false;
          if(state.lastError!=="ACCEPTED_READBACK")
            reconcileAcceptedInputGainSnapshot(state.current);
        }
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
          activeWireAttempt=null;
          startupWire=null;
          pendingWireGains.clear();
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
    backend.addEventListener("arMixLogicalAck",ack=>{
      const pending=pendingLogical;
      if(!pending||pending.requestId!==ack?.requestId)return;
      if(ack.status!=="ACCEPTED"||ack.errorCode!=="NONE"||
         protocol.decimal(ack.revision)===null ||
         BigInt(ack.revision)<=BigInt(pending.beforeRevision)){
        pendingLogical=null;
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
      if(!pending||pending.requestId!==ack?.requestId)return;
      const label=pending.track?"Audio Track":"BUS";
      if(ack.ok===true && protocol.decimal(ack.createdId) && ack.createdId!=="0" &&
         (!pending.track || (protocol.decimal(ack.graphRevision) && ack.graphRevision!=="0"))){
        status(label+" accepted by native PCM · verifying graph readback");
        if(pending.track){
          pending.createdId=ack.createdId;
          pending.revision=ack.graphRevision;
          finishNativeTrack(controller?.state()?.current);
        }else pendingBusCreate=null;
      }else{
        pendingBusCreate=null;
        nativeToast("Add "+label+" rejected · "+(ack?.error||"native graph error"));
      }
      syncAddTrack();
      controller?.subscribe();
    });
    backend.addEventListener("arMixNativeTrackDeleteAck",ack=>{
      const pending=pendingTrackDelete;
      if(!pending||pending.requestId!==ack?.requestId)return;
      if(ack.ok!==true||ack.channelId!==pending.id){
        pendingTrackDelete=null;syncAddTrack();
        nativeToast("Delete Track rejected · "+(ack?.error||"native graph rejected"));
      }else{
        pending.acked=true;
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
      if(ack?.ok!==true)
        nativeToast(ack?.error||"FX editor could not open");
      // A successful native HWND is its own feedback. Do not send a
      // generic status("... editor opened") that the UI translates to
      // the misleading "Effect settings updated" toast.
    });
    backend.addEventListener("arMixRoutingFxInsertAck",ack=>{
      if(!pendingFxInsert||pendingFxInsert.authority==="CHANNEL_ID"||
         ack?.requestId!==pendingFxInsert.requestId)return;
      const p=pendingFxInsert;pendingFxInsert=null;
      const state=ack?.state;
      const epoch=safeNativeCounter(state?.epoch);
      const revision=safeNativeCounter(state?.revision);
      const actual=state?.fxInstances?.[p.channel]?.[p.slot];
      if(ack?.ok===true && ack.channel===p.channel && ack.slot===p.slot &&
         !logicalView && mixerSession()?.authority!=="CHANNEL_ID" &&
         state?.active===true && state.hasAcceptedRoute===true &&
         epoch===p.epoch && epoch===safeNativeCounter(hardware?.device?.epoch) &&
         revision!==null && revision>=p.revision &&
         revision>=safeNativeCounter(legacy()?.routingRevision) &&
         actual?.catalogId===p.id &&
         typeof actual?.instanceId==="string"&&actual.instanceId.length){
        status("CH "+(p.channel+1)+" FX loaded · verified native VST3");
        presentFxCatalog();
        paintM4FxInstances(state);
        scope?.closeNativePicker?.();
        openRoutedFxEditor(p.channel,p.slot,actual.instanceId,epoch,revision);
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
  const nativeHooks=Object.freeze({
    enabled:connected,
    requestDeleteTrack:requestNativeTrackDelete,
    requestDeleteStrip:requestNativeStripDelete,
    canDeleteStrip:canDeleteNativeStrip,
    renameStrip:openNativeIdentity,
    orbitTarget:nativeOrbitTarget,
    orbitAction:nativeOrbitAction,
    inputChoices(t) {
      return stripVM.inputChoices(t,inputChoicesFor(),
        controller?.state()?.current?.retained,!(!logicalView),INTERNAL_BUS);
    },
    externalChoices(t) {
      return stripVM.externalChoices(t,hardwareOutputPairs());
    },
    outputChoices(t) {
      return stripVM.primaryChoices({track:t,logical:Boolean(logicalView),
        retained:Boolean(hardware?.retained),
        playbackDevice:hardware?.device?.playbackDevice,
        authority:mixerSession()?.authority,
        targets:nativeOutputTargets(t)});
    },
    consumeField(event) {
      if (!connected)return false;
      const el=event.target,field=el?.dataset?.field;
      if (!field)return false;
      const t=scope?.findTrack?.(el) || null;
      if (!t)return true;
      if(controller?.state()?.current?.retained&&!logicalView){
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
          status("INPUT GAIN unavailable · native PCM admission required");
          return true;
        }
        // Shared reference/native UI detent. Only accepted readback owns PCM.
        const n=window.__ARMIX_GAIN_UNITY_DETENT__?.(el,event)??raw;
        // Gesture preview stays inside the knob; no browser-local t.trim
        // audio authority and no phantom native ACK. Commit once on release.
        if(event.type!=="change"){
          el.closest(".gain-dial")?.style.setProperty("--gain-rotation",
            (n*5.625).toFixed(2)+"deg");
          const lcd=el.closest(".gain-view-body")?.querySelector(".gain-readout");
          const digits=lcd?.querySelector(".gain-lcd-digits")||lcd;
          if(digits)digits.textContent=(n>=0?"+":"")+n.toFixed(1)+" dB";
          return true;
        }
        if(!intent("SET_INPUT_GAIN",t,{valueDb:n})){
          const acceptedDb=Number.isFinite(t.trim)?t.trim:0;
          el.value=String(acceptedDb);
          el.closest(".gain-dial")?.style.setProperty("--gain-rotation",
            (acceptedDb*5.625).toFixed(2)+"deg");
          const lcd=el.closest(".gain-view-body")?.querySelector(".gain-readout");
          const digits=lcd?.querySelector(".gain-lcd-digits")||lcd;
          if(digits)digits.textContent=(acceptedDb>0?"+":"")+acceptedDb.toFixed(1)+" dB";
          status("INPUT GAIN rejected · restoring accepted PCM readback");
          controller?.subscribe?.();
        }else if(!logicalView){
          pendingNativeGainIntent=true;
        }
        return true;
      }
      if(field==="level"&&scope?.sofEnabled&&t.kind==="audio"){
        const edge=selectedNativeSofEdge(t),db=Number(el.value);
        const uiId=edge&&uiByWireId.get(edge.edgeId);
        if(!edge||uiId===undefined||!Number.isFinite(db)||db< -60||db>6){
          status("SENDS ON FADERS · patch a selected WIRE destination first");
          scope?.syncFaderVisual?.(t);return true;
        }
        if(event.type!=="change"){
          previewFaderWithoutModelMutation(t,db);return true;
        }
        if(!requestWireGain({sourceId:t.id,edgeId:uiId,levelDb:db}))
          status("SEND LEVEL rejected · accepted WIRE identity changed");
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
        const choices=inputChoicesFor();
        const choice=choices.find(x=>x.label===el.value);
        const nativeOwner=mixerSession()?.authority==="CHANNEL_ID";
        const id=nativeOwner?nativeIdFor(t):null;
        if((nativeOwner&&(!id||!controller?.state()?.current?.admitted))||
           (!nativeOwner&&(channel<0||channel>=10))||!deviceReady()){
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
        const previous=t.externalOutput||"None";
        const id=nativeIdFor(t);
        // One pure Channel Strip preflight for ASIO port identity and
        // shared-output confirmation; only this bridge dispatches native.
        const plan=stripVM.externalOutputPlan({
          native:controller?.state()?.current,hardware,channelId:id,
          selectedLabel:el.value,currentLabel:previous,
          choices:hardwareOutputPairs(),pending:pendingExternal.has(id),
          channelName:t.name
        });
        if(plan.kind==="UNAVAILABLE"){
          el.value=previous;
          nativeToast("EXTERNAL OUT unavailable · check ASIO, graph and pins");
          return true;
        }
        if(plan.kind==="NO_CHANGE"){el.value=previous;return true;}
        const {left,right,epoch,revision}=plan;
        const confirmed=!plan.requiresConfirmation || window.confirm(
          plan.confirmation);
        if(!confirmed){el.value=previous;return true;}
        const requestId="out-o-"+(++externalSerial);
        pendingExternal.set(id,{requestId,channelId:id,left,right,
          epoch,revision,acked:false});
        el.value=previous;el.setAttribute("aria-busy","true");
        backend.emitEvent("arMixAction",{action:"routingExternalOutput",
          requestId,channelId:id,deviceEpoch:epoch,revision,
          left,right,confirmed:true});
        window.setTimeout(()=>{
          if(pendingExternal.get(id)?.requestId!==requestId)return;
          pendingExternal.delete(id);el.removeAttribute("aria-busy");
          nativeToast("EXTERNAL OUT timed out · last PCM retained");
          controller?.subscribe();
        },8000);
        status("EXTERNAL OUT pending · native validation and readback");
        return true;
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
      // PRE/POST is an exact EdgeId structural graph transaction, not a
      // cosmetic browser mode nor a channel-wide batch change.
      if(action==="tapMode"||action==="sofTap"){
        const current=controller?.state()?.current,id=nativeIdFor(t);
        if(!current?.admitted||!current.graph.capabilities.supportedOps.includes("APPLY_WIRE_TRANSACTION")||!id){
          status("PRE/POST unavailable · native PCM graph not admitted");return true;
        }
        const sends=current.graph.edges.filter(e=>e.enabled&&
          e.from===id&&e.primaryOutput===false&&e.to!==current.graph.mainChannelId);
        let target=action==="sofTap"?selectedNativeSofEdge(t):
          sends.find(e=>uiByWireId.get(e.edgeId)===scope?.selectedWireEdgeId)||
          (sends.length===1?sends[0]:null);
        if(!target||!["PRE_FADER","POST_FADER"].includes(target.tap)){
          status("Select one outgoing parallel SEND WIRE to change PRE/POST");
          return true;
        }
        const next=target.tap==="PRE_FADER"?"POST_FADER":"PRE_FADER";
        requestNativeTap(id,target.edgeId,next);
        return true;
      }
      if(action==="sofOn"){
        const current=controller?.state()?.current,edge=selectedNativeSofEdge(t);
        if(!current?.admitted||!current.graph.capabilities.supportedOps.includes("SET_SEND_ENABLED")||
           !edge||typeof edge.sendEnabled!=="boolean"){
          status("SEND ON/OFF unavailable · select an admitted WIRE send");return true;
        }
        intent("SET_SEND_ENABLED",t,{edgeId:edge.edgeId,enabled:!edge.sendEnabled});
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
        editNativeFx(t,slot,null,0,token);return true;
      }
      if(action==="openPlugin"){
        if(logicalView||mixerSession()?.authority==="CHANNEL_ID") {
          const current=activeView(),id=nativeIdFor(t),slot=Number(btn?.dataset.slot);
          if(!Number.isInteger(slot)||slot<0||slot>=8)return true;
          const insert=current?.graph?.channels.find(ch=>ch.id===id)?.inserts?.[slot];
          if(!insert?.id) {
            discoverFxOnFirstPickerOpen();
            scope?.openFastPicker?.(t,slot,btn);
          } else if(current.admitted||current.logical) {
            if(insert.catalogId==='armix.builtin.channel-eq.v1') {
              window.ArSonMixNativeEq?.open(t,slot);
            } else if(insert.catalogId==='armix.builtin.channel-compressor.v1') {
              window.ArSonMixNativeCompressor?.open(t,slot);
            } else if(insert.catalogId==='armix.builtin.arson-harmony.v1') {
              window.ArSonMixNativeHarmony?.open(t,slot);
            } else if(insert.catalogId==='armix.builtin.vocal-throw.v1') {
              window.ArSonMixNativeVocalThrow?.open(t,slot);
            } else if(insert.catalogId==='armix.builtin.8d-sonku.v1') {
              if(window.ArSonMixNativeSonKu?.open)
                window.ArSonMixNativeSonKu.open(t,slot);
              else nativeToast("8D-SonKu editor unavailable · refresh the approved GUI bundle");
            } else if(insert.catalogId==='armix.builtin.imersip.v1' && current.admitted) {
              // ImmerSIB is a real JUCE AudioProcessorEditor, not a
              // WebView2 SonKu overlay, preset-change action or demo FX.
              // Route the exact accepted ChannelId/lease into the dedicated
              // native HWND, which remains open when a VST3 editor is opened.
              backend.emitEvent("arMixAction",{action:"routingFxEditor",channelId:id,slot,
                instanceId:insert.id,deviceEpoch:current.device.epoch,
                revision:current.graph.revision});
            } else if(current.logical) {
              backend.emitEvent("arMixAction",{action:"logicalVendorFxEditor",channelId:id,
                slot,instanceId:insert.id,workspaceToken:current.workspaceToken,
                graphRevision:current.graph.revision,deviceEpoch:"0"});
            } else backend.emitEvent("arMixAction",{action:"routingFxEditor",channelId:id,slot,
              instanceId:insert.id,deviceEpoch:current.device.epoch,revision:current.graph.revision});
          } else nativeToast("This FX is held with the disconnected audio session");
          return true;
        }
        const slot=Number(btn?.dataset.slot),instance=verifiedFxFor(t,slot);
        if(instance){
          const ep=safeNativeCounter(hardware?.device?.epoch),
            rev=safeNativeCounter(legacy()?.routingRevision);
          if(ep!==null&&rev!==null)
            openRoutedFxEditor(instance.lane,slot,instance.live.instanceId,ep,rev);
          else nativeToast("VST3 editor · refresh native device and FX instance");
        }else {
          discoverFxOnFirstPickerOpen();
          scope?.openFastPicker?.(t,slot,btn);
        }
        return true;
      }
      if(action==="slotPower"){
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
          const slot=Number(btn?.dataset.slot),instance=verifiedFxFor(t,slot);
          if(!m4ControlIsOwner()||!instance||
             !requestNativeFxPower(t,slot,!instance.live.bypassed))
            nativeToast("FX bypass unavailable · select an accepted VST3 slot");
        }
        return true;
      }
      if(action==="fxOn"){
        if(logicalView){
          const channelId=nativeIdFor(t);
          const row=logicalView.graph.channels.find(ch=>ch.id===channelId);
          if(row)logicalIntent("SET_FX_RACK_BYPASS",channelId,
                               {enabled:!row.fxRackBypassed});
          return true;
        }
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
      const edge=admittedParallelWire(uiSourceId,uiEdgeId,"APPLY_WIRE_TRANSACTION");
      return wireVM.canEditTap(edge,{pending:!!controller?.state()?.pending,
        supported:true});
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
        if(!logicalIntent("ADD_FX_REFERENCE",channelId,{catalogId:id}).ok)
          nativeToast("FX selection not saved");
        return true;
      }
      if(pendingFxInsert){status("FX INSERT · wait for previous native ACK");return true;}
      const plugin=nativeCatalog.get(id) || nativeBuiltins.find(fx=>fx.id===id);
      const track=scope?.tracks?.find(x=>x.id===target?.channelId)||
        (scope?.master?.id===target?.channelId?scope.master:null);
      const channel=m4Index(track),slot=Number(target?.slot);
      const accepted=controller?.state().current;
      const nativeId=nativeIdFor(track);
      if(accepted?.admitted || mixerSession()?.authority==="CHANNEL_ID") {
        const plan=fxRackVM.nativeInsertPlan(accepted,{
          channelId:nativeId,slot,catalogId:plugin?.id,
          pending:!!pendingFxInsert});
        if(plan.kind!=="READY"){
          status("VST3 unavailable · select an empty native ChannelId FX slot");
          return true;
        }
        const request=controller.intent("LOAD_INSERT",plan.channelId,plan.payload);
        if(!request.ok){status("VST3 rejected · "+request.error);return true;}
        pendingFxInsert={authority:"CHANNEL_ID",requestId:request.requestId,
                         id,channelId:plan.channelId,slot,epoch:accepted.device.epoch};
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
      pendingFxInsert={requestId,id,channel,slot,epoch:ep,revision:rev};
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
      // Reset the parameter the fader CURRENTLY owns. A SOF reset must not
      // write the main channel's SET_FADER or M4 faderDb by accident.
      if (event.target.classList.contains("fader")){
        if(scope?.sofEnabled&&t?.kind==="audio"){
          const edge=selectedNativeSofEdge(t),id=edge&&uiByWireId.get(edge.edgeId);
          if(!edge||id===undefined||
             !requestWireGain({sourceId:t.id,edgeId:id,levelDb:0}))
            status("SOF unity reset unavailable · select one admitted SEND");
          return true;
        }
        if((m4ControlIsOwner()&&m4Verified()) ||
           (t?.kind==="master"&&(m4Verified()||nativeMasterReady())))
          sendM4Control(t?.kind==="master"?"masterGain":"faderDb",t,0);
        else intent("SET_FADER",t,{valueDb:0});
      }
      else if(event.target.closest?.(".gain-dial")){
        if(t?.kind!=="audio"||t.nativeGainAvailable!==true)
          status("INPUT GAIN unity reset requires native PCM admission");
        else intent("SET_INPUT_GAIN",t,{valueDb:0});
        return true;
      }
      else if(event.target.classList.contains("center-pan")){
        if(m4ControlIsOwner()&&m4Verified())sendM4Control("pan",t,0);
        else intent("SET_PAN",t,{value:0});
      }
      else if(event.target.closest?.(".footer-label,.strip-identity-top"))
        openNativeIdentity(t,event.target);
      return true;
    },
    applyIdentity:acceptIdentityPresentation,
    fxEditorState:activeView,
    state:()=>controller?.state() || null
  });
  window.__ARMIX_NATIVE_M2__ = nativeHooks;
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
    if(target.id==="addBus"||target.id==="addTrack"){
      requestNativeBusCreate(target.id==="addTrack");return;
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
    reset:"Demo reset must not erase a live mixer",
    addFx:"Use a verified FX insert slot on an admitted channel"
  });
  function presentNativeOnlyTools(){
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
