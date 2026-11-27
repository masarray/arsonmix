// SPDX-License-Identifier: AGPL-3.0-only
// GUI-3: pure WIRE/EdgeId presentation selectors for the ONE /preview/ GUI.
// Native graph, .smix, ACK and PCM render authority remain external.
(function (root, factory) {
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  if(root)root.ArSonApprovedWireViewModel=api;
})(typeof globalThis==="object"?globalThis:undefined,function () {
  "use strict";
  const TAPS=new Set(["PRE_FADER","POST_FADER"]);
  const SOF_OPS=["SET_WIRE_GAIN","SET_SEND_ENABLED","APPLY_WIRE_TRANSACTION"];

  function gainDb(linear) {
    if(!Number.isFinite(linear)||linear<0)return null;
    return Math.round(Math.max(-60,Math.min(6,
      linear>0?20*Math.log10(linear):-60))*10)/10;
  }

  // Explicit native primaryOutput is decisive. Only old snapshots without
  // that field use the primaryOutId fallback. A parallel SEND to MAIN is not
  // promoted to PRIMARY just because it shares the same destination.
  function edgePresentation(edge,sourcePrimaryOutId,mainChannelId) {
    if(!edge||edge.enabled!==true||
       typeof edge.edgeId!=="string"||!edge.edgeId||
       typeof edge.from!=="string"||typeof edge.to!=="string")return null;
    const primary=typeof edge.primaryOutput==="boolean"?
      edge.primaryOutput:sourcePrimaryOutId===edge.to;
    if(primary&&sourcePrimaryOutId!==edge.to)return null;
    if(primary&&edge.gainLinear!==1)return null; // exact native unity OUT
    const levelDb=primary?0:gainDb(edge.gainLinear);
    if(levelDb===null)return null;
    const isMain=edge.to===mainChannelId;
    const tap=TAPS.has(edge.tapStage)?edge.tapStage:edge.tap;
    const legacySplit=tap==="SPLIT_PRE"||tap==="SPLIT_POST";
    const kind=primary?"primary":legacySplit?"split":"send";
    const mode=isMain?(primary?"postFader":"send"):primary?"routeOnly":
      legacySplit?(tap==="SPLIT_PRE"?"splitPre":"splitPost"):"send";
    return {kind,mode,nativeTap:tap,sendEnabled:edge.sendEnabled!==false,
      enabled:true,levelDb,primary};
  }

  // Exact decimal-string native identities only. Reject collisions: a
  // UI ordinal or destination label is never a substitute for an EdgeId.
  function parallelEdge(graph,sourceId,edgeId,destinationId) {
    if(!sourceId||!edgeId||!Array.isArray(graph?.edges))return null;
    const matches=graph.edges.filter(e=>e.enabled===true&&
      e.from===sourceId&&e.edgeId===edgeId&&e.primaryOutput===false&&
      (destinationId===undefined||e.to===destinationId));
    return matches.length===1?matches[0]:null;
  }

  function acceptedParallelEdge(snapshot,projected,sourceId,edgeId,operation) {
    if(projected!==true||snapshot?.admitted!==true||snapshot.retained||
       !snapshot.graph?.capabilities?.supportedOps?.includes(operation))
      return null;
    return parallelEdge(snapshot.graph,sourceId,edgeId);
  }

  // PRIMARY OUT remains a unity serial wire. Its CH LEVEL control is an
  // honest alias for the source's native post-insert fader, never a second
  // EdgeId gain or an implicit parallel MAIN SEND (which would double audio).
  function primaryChannelLevelPlan(snapshot,projected,sourceId,edgeId) {
    const graph=snapshot?.graph;
    if(projected!==true||snapshot?.admitted!==true||snapshot.retained===true||
       !Array.isArray(graph?.capabilities?.supportedOps)||
       !graph.capabilities.supportedOps.includes("SET_FADER")||
       !Array.isArray(graph.channels)||!Array.isArray(graph.edges)||
       typeof sourceId!=="string"||typeof edgeId!=="string")return {kind:"UNAVAILABLE"};
    const rows=graph.channels.filter(ch=>ch.id===sourceId);
    const edges=graph.edges.filter(e=>e.edgeId===edgeId&&e.enabled===true);
    if(rows.length!==1||edges.length!==1)return {kind:"UNAVAILABLE"};
    const ch=rows[0],edge=edges[0];
    if(!["INPUT","GROUP","FX_RETURN"].includes(ch.role)||
       !Number.isFinite(ch.controls?.faderDb)||
       edge.primaryOutput!==true||edge.gainLinear!==1||
       edge.from!==sourceId||edge.to!==ch.primaryOutId)
      return {kind:"UNAVAILABLE"};
    return {kind:"READY",source:sourceId,levelDb:ch.controls.faderDb};
  }

  // An incoming WIRE pot represents the receiver's independent incoming WIRE sum gain,
  // not a randomly selected sender's EdgeId. One or many incoming routes
  // must behave identically; each send remains separately adjustable in
  // WIRE Inspector / Sends on Faders.
  function channelSumLevelPlan(snapshot,projected,channelId,levelDb) {
    const graph=snapshot?.graph;
    if(projected!==true||snapshot?.admitted!==true||snapshot.retained===true||
       !Array.isArray(graph?.capabilities?.supportedOps)||
       !graph.capabilities.supportedOps.includes("SET_WIRE_SUM_GAIN")||
       !Array.isArray(graph.channels)||!Array.isArray(graph.edges)||
       typeof channelId!=="string"||!/^\d+$/.test(channelId)||
       !Number.isFinite(levelDb)||levelDb< -60||levelDb>6)
      return {kind:"UNAVAILABLE"};
    const rows=graph.channels.filter(row=>row.id===channelId);
    if(rows.length!==1||!["INPUT","GROUP","FX_RETURN"].includes(rows[0].role)||
       !Number.isFinite(rows[0].controls?.wireSumGainDb))return {kind:"UNAVAILABLE"};
    const inbound=graph.edges.filter(e=>e.to===channelId&&e.enabled===true);
    if(inbound.length<1)return {kind:"UNAVAILABLE"};
    return {kind:"READY",source:channelId,levelDb,wireCount:inbound.length};
  }

  function gainReadback(edges,uiByEdgeId,pendingGains,channels=[]) {
    if(!Array.isArray(edges))return [];
    return edges.filter(e=>e.enabled===true)
      .flatMap(e=>{
        const id=uiByEdgeId.get(e.edgeId);
        if(id===undefined)return [];
        if(e.primaryOutput===true) {
          const rows=channels.filter(ch=>ch.id===e.from&&
            ch.primaryOutId===e.to);
          if(rows.length!==1||e.gainLinear!==1||
             !Number.isFinite(rows[0].controls?.faderDb))return [];
          return [{id,pending:false,levelDb:0}];
        }
        const levelDb=gainDb(e.gainLinear);
        return levelDb===null?[]:
          [{id,pending:pendingGains.has(e.edgeId),levelDb}];
      });
  }

  function canEditTap(edge,{pending=false,readOnly=false,
                            supported=false,logical=false}={}) {
    if(!edge||pending||readOnly||!supported)return false;
    const tap=logical?(edge.tapStage||edge.tap):edge.tap;
    return TAPS.has(tap);
  }

  function gestureTap(mode) {
    return mode==="split"?"PRE_FADER":mode==="send"?"POST_FADER":null;
  }

  function sofAvailable(snapshot,canToggle) {
    const operations=snapshot?.graph?.capabilities?.supportedOps;
    const logical=snapshot?.logical===true&&snapshot.readOnly!==true;
    return snapshot?.retained!==true&&canToggle===true&&
      (logical?["SET_WIRE_GAIN","SET_SEND_ENABLED","SET_SEND_TAP","WIRE_TRANSACTION"]:SOF_OPS)
        .every(op=>operations?.includes(op))&&
      (logical||snapshot?.admitted===true);
  }

  // An uncommitted SOF CONNECT+GAIN UI pair is one atomic real SEND
  // creation. Its ephemeral UI edge ID MUST NOT be translated to a native
  // EdgeId, and the presentation graph remains entirely untouched.
  function compactSendGesture(ops,uiEdgeIds) {
    if(!Array.isArray(ops)||ops.length!==2||
       ops[0]?.type!=="CONNECT"||ops[1]?.type!=="GAIN"||
       ops[0].sourceId!==ops[1].sourceId||
       uiEdgeIds.has(ops[1].edgeId)||
       typeof ops[1].levelDb!=="number"||
       !Number.isFinite(ops[1].levelDb)||
       ops[1].levelDb< -60||ops[1].levelDb>6)return ops;
    return [{...ops[0],mode:"send",levelDb:ops[1].levelDb}];
  }

  // Single canonical uint64 native identity. UI ordinals, numbers, and
  // rounded large integers may not cross the native command boundary.
  function nativeId(value) {
    if(typeof value!=="string"||!/^[1-9][0-9]*$/.test(value))return false;
    try{return BigInt(value)<=18446744073709551615n;}catch{return false;}
  }

  // G-MV31: the one admitted ChannelId / EdgeId send-edit authority.
  // These pure selectors have NO browser, transport or PCM ownership. A
  // projected/native readback, not a Pages edge or an ACK, admits a gesture.
  function nativeSendEdge(snapshot,{sourceId,edgeId,projected=false,
    pending=false,operation}={}) {
    if(projected!==true||pending===true||
       snapshot?.admitted!==true||snapshot.retained===true||
       !nativeId(sourceId)||!nativeId(edgeId)||
       !snapshot.graph?.capabilities?.supportedOps?.includes(operation))
      return null;
    const matches=snapshot.graph?.channels?.filter(ch=>ch.id===sourceId);
    if(matches?.length!==1)return null;
    const edge=parallelEdge(snapshot.graph,sourceId,edgeId);
    return edge&&nativeId(edge.to)?edge:null;
  }

  // PRE/POST must be a typed SET_TAP on an exact installed parallel SEND.
  // A missing targetTap is a read-only eligibility query for both operator
  // surfaces; no click-only mutation or native gesture is ever synthesized.
  function nativeTapPlan(snapshot,{sourceId,edgeId,targetTap=null,
    projected=false,pending=false}={}) {
    const edge=nativeSendEdge(snapshot,{sourceId,edgeId,projected,pending,
      operation:"APPLY_WIRE_TRANSACTION"});
    if(!canEditTap(edge,{supported:true}))return {kind:"UNAVAILABLE"};
    if(targetTap===null)return {kind:"AVAILABLE",tap:edge.tap};
    if(!TAPS.has(targetTap))return {kind:"UNAVAILABLE"};
    if(edge.tap===targetTap)return {kind:"NO_CHANGE"};
    return {kind:"DISPATCH",operation:"APPLY_WIRE_TRANSACTION",source:sourceId,
      payload:{changes:[{op:"SET_TAP",from:sourceId,edgeId,tap:targetTap}]}};
  }

  // DMX: a return or group can feed a terminal MONITOR without rerendering
  // its DSP. All gestures still resolve one installed parallel EdgeId.
  function sofSourceRole(role) {
    return ["INPUT","GROUP","FX_RETURN"].includes(role);
  }
  function sofTargetRole(role) {
    return ["GROUP","FX_RETURN","MONITOR"].includes(role);
  }
  // Never MAIN/primary, self, ordinal, stale or ambiguous duplicate.
  function sofSendEdge(snapshot,{sourceId,destinationId}={}) {
    if((snapshot?.admitted!==true&&!(snapshot?.logical===true&&snapshot.readOnly!==true))||snapshot.retained===true||
       !nativeId(sourceId)||!nativeId(destinationId))return null;
    const graph=snapshot.graph;
    const source=graph?.channels?.filter(ch=>ch.id===sourceId);
    const target=graph?.channels?.filter(ch=>ch.id===destinationId);
    if(sourceId===destinationId||source?.length!==1||!sofSourceRole(source[0].role)||
       target?.length!==1||!sofTargetRole(target[0].role))
      return null;
    const matches=graph.edges?.filter(e=>e.enabled===true&&
      e.from===sourceId&&e.to===destinationId&&
      e.primaryOutput===false&&nativeId(e.edgeId));
    return matches?.length===1?
      parallelEdge(graph,sourceId,matches[0].edgeId,destinationId):null;
  }

  // Ordinary channel PRE/POST considers only non-MAIN sends. A selected
  // EdgeId must belong to this channel; a stale selection is never silently
  // changed into an edit of another send. With no selection, exactly one
  // eligible send may be used.
  function channelTapEdge(snapshot,{sourceId,selectedEdgeId=null}={}) {
    if(snapshot?.admitted!==true||snapshot.retained===true||
       !nativeId(sourceId))return null;
    const graph=snapshot.graph,main=graph?.mainChannelId;
    const eligible=graph?.edges?.filter(e=>e.enabled===true&&
      e.from===sourceId&&e.primaryOutput===false&&e.to!==main&&
      nativeId(e.edgeId)&&nativeId(e.to)&&TAPS.has(e.tap))||[];
    if(selectedEdgeId!==null&&selectedEdgeId!==undefined)
      return nativeId(selectedEdgeId)?
        (eligible.some(e=>e.edgeId===selectedEdgeId)?
          parallelEdge(graph,sourceId,selectedEdgeId):null):null;
    return eligible.length===1?eligible[0]:null;
  }

  function nativeSendTogglePlan(snapshot,{sourceId,edgeId,projected=false,
    pending=false}={}) {
    const edge=nativeSendEdge(snapshot,{sourceId,edgeId,projected,pending,
      operation:"SET_SEND_ENABLED"});
    if(!edge||typeof edge.sendEnabled!=="boolean")
      return {kind:"UNAVAILABLE"};
    return {kind:"DISPATCH",operation:"SET_SEND_ENABLED",source:sourceId,
      payload:{edgeId,enabled:!edge.sendEnabled}};
  }

  // G-MV32: one pure PRIMARY MAIN gesture owner for both verified ChannelId
  // and the legacy M4 first-sound patch. A Pages PRIMARY cable must match
  // its own accepted physical route before unplugging; a parallel SEND to
  // MAIN remains a different edge and falls through to ordinary WIRE edits.
  // No command is emitted here. The existing OUT dispatcher still performs
  // final admission, ACK, later native readback and timeout handling.
  function primaryMainGesturePlan(snapshot,{op,mainUiId,sourceId,edgeId,
    uiEdges=[],m4Verified=false,m4Channel=-1}={}) {
    const pass={kind:"PASS"};
    if(!op||typeof op!=="object")return pass;
    const isMain=mainUiId!==null&&mainUiId!==undefined&&
      op.destId===mainUiId;
    const mainConnect=isMain&&(op.type==="ROUTE_ONLY"||
      (op.type==="CONNECT"&&
        (!op.mode||op.mode==="postFader"||op.mode==="routeOnly")));
    if(mainConnect)return {kind:"ROUTE",target:"MAIN"};
    if(op.type!=="REMOVE")return pass;

    // UI IDs are cosmetic; they are never a substitute for the exact
    // producer EdgeId. Check for a *unique* visible PRIMARY cable first.
    const uiPrimary=Array.isArray(uiEdges)?uiEdges.filter(e=>
      e?.id===op.edgeId&&e.sourceId===op.sourceId&&
      e.kind==="primary"&&e.enabled===true):[];
    const visible=uiPrimary.filter(e=>e.destId===mainUiId);
    if(visible.length>1)return {kind:"BLOCK"};
    // M4 has no native EdgeId. A PRIMARY shown against any other target
    // cannot be interpreted as a verified MAIN unplug.
    if(snapshot?.admitted===false&&uiPrimary.length>0&&visible.length!==1)
      return {kind:"BLOCK"};
    const graph=snapshot?.graph;
    const sources=Array.isArray(graph?.channels)?
      graph.channels.filter(ch=>ch?.id===sourceId):[];
    const globalEdges=Array.isArray(graph?.edges)&&nativeId(edgeId)?
      graph.edges.filter(e=>e?.edgeId===edgeId):[];
    // Older v6 producers omit primaryOutput: use the SAME explicit-field
    // precedence + accepted primaryOutId fallback as edgePresentation.
    // A native false is always decisive and can NEVER become PRIMARY.
    const nativePrimary=globalEdges.length===1&&sources.length===1&&
      globalEdges[0].from===sourceId&&
      globalEdges[0].to===graph?.mainChannelId&&
      edgePresentation(globalEdges[0],sources[0].primaryOutId,
        graph.mainChannelId)?.primary===true;
    // A mapped primary native EdgeId cannot bypass this policy through
    // the generic REMOVE path just because the UI projection went stale.
    if(visible.length!==1)
      return nativePrimary?{kind:"BLOCK"}:pass;
    if(snapshot?.retained===true)return {kind:"BLOCK"};

    if(snapshot?.admitted===true) {
      if(nativeId(sourceId)&&nativeId(edgeId)&&
         nativeId(graph?.mainChannelId)&&
         sources.length===1&&
         ["INPUT","GROUP","FX_RETURN"].includes(sources[0].role)&&
         sources[0].primaryOutId===graph.mainChannelId&&
         nativePrimary)return {kind:"ROUTE",target:"NONE"};
      return {kind:"BLOCK"};
    }
    // M4 is ordinal-only: NEVER infer its identity from a stale ChannelId
    // graph or from a cosmetic cable if the producer switched owners.
    const session=snapshot?.raw?.mixerSession;
    const m4=snapshot?.raw?.legacyM4;
    if(snapshot?.admitted===false&&session?.authority==="M4"&&
       m4Verified===true&&Number.isInteger(m4Channel)&&
       m4Channel>=0&&m4Channel<10&&
       session.deviceEpoch===snapshot.device?.epoch&&
       session.routeRevision===m4?.routingRevision&&
       session.channels?.[m4Channel]?.role==="INPUT"&&
       session.channels[m4Channel].ordinal===m4Channel&&
       session.channels[m4Channel].source?.main===true&&
       m4?.channels?.[m4Channel]?.main===true)
      return {kind:"ROUTE",target:"NONE"};
    return {kind:"BLOCK"};
  }

  // UI gesture translation only: caller still owns one native transaction,
  // ACK timeout, installed graph readback, and UI rollback. This helper does
  // not decide that an operation is admitted or mutate any accepted graph.
  function nativeTransaction(ops,channelIds,edgeIds) {
    if(!Array.isArray(ops)||ops.length===0||
       typeof channelIds?.get!=="function"||
       typeof edgeIds?.get!=="function")
      return {ok:false,error:"INVALID_WIRE_OPERATION"};
    const changes=[];
    for(const op of ops) {
      if(!op||typeof op!=="object")
        return {ok:false,error:"INVALID_WIRE_OPERATION"};
      const from=channelIds.get(op.sourceId);
      const to=op.destId===undefined?undefined:channelIds.get(op.destId);
      const edgeId=op.edgeId===undefined?undefined:edgeIds.get(op.edgeId);
      if(!nativeId(from)||
         (op.destId!==undefined&&!nativeId(to))||
         (op.edgeId!==undefined&&!nativeId(edgeId)))
        return {ok:false,error:"MAPPED_IDENTITY_MISSING"};
      if(!["CONNECT","REMOVE","RETARGET","ROUTE_ONLY"].includes(op.type)||
         (op.mode!==undefined&&op.mode!==null&&op.mode!==""&&
          !["send","postFader","routeOnly"].includes(op.mode)))
        return {ok:false,error:"UNSUPPORTED_WIRE_OPERATION",operation:op.type};
      if((op.type==="CONNECT"||op.type==="ROUTE_ONLY"||
          op.type==="RETARGET")&&!nativeId(to))
        return {ok:false,error:"MAPPED_IDENTITY_MISSING"};
      if((op.type==="REMOVE"||op.type==="RETARGET")&&!nativeId(edgeId))
        return {ok:false,error:"MAPPED_IDENTITY_MISSING"};
      const change={op:op.type,from};
      if(to!==undefined)change.to=to;
      if(edgeId!==undefined)change.edgeId=edgeId;
      if(op.type==="CONNECT"||op.type==="ROUTE_ONLY"){
        if(op.levelDb!==undefined&&
           (typeof op.levelDb!=="number"||!Number.isFinite(op.levelDb)))
          return {ok:false,error:"INVALID_WIRE_GAIN"};
        // Preserve the existing approved -60…+6 dB gesture clamp.
        // A malformed value must never turn into a NaN native payload.
        const db=op.levelDb===undefined?0:Math.max(-60,Math.min(6,op.levelDb));
        change.tap="POST_FADER";
        change.gainLinear=Math.pow(10,db/20);
      }
      changes.push(change);
    }
    const source=changes[0].from;
    if(changes.some(change=>change.from!==source))
      return {ok:false,error:"MULTI_SOURCE"};
    return {ok:true,source,changes};
  }


  // MV-7: pure deferred WIRE gain projection. Installed PCM alone decides.
  function gainDispatchPlan(snapshot,edgeId,edit) {
    if(snapshot?.admitted!==true||snapshot.retained===true||
       !edit||snapshot.identity!==edit.identity||
       !nativeId(edgeId)||!nativeId(edit.source)||
       typeof edit.gain!=="number"||!Number.isFinite(edit.gain)||
       edit.gain<0||edit.gain>4)return {kind:"ROLLBACK"};
    const edge=parallelEdge(snapshot.graph,edit.source,edgeId);
    if(!edge||!Number.isFinite(edge.gainLinear))return {kind:"ROLLBACK"};
    if(Math.abs(edge.gainLinear-edit.gain)<0.00001)
      return {kind:"CONFIRMED"};
    return {kind:"DISPATCH",source:edit.source,edgeId,
      gainLinear:edit.gain};
  }

  // MV-7: only a same-device initial CONNECT may rebase once.
  // The original controller remains the sole native ACK/readback authority.
  const STALE_WIRE_ERRORS=new Set([
    "STALE_NATIVE_CONTROL_REVISION","STALE_NATIVE_WIRE_GRAPH"]);
  function nativeRecoveryPlan(state,activeAttempt,startupAttempt) {
    if(state?.pending)return {kind:"PENDING"};
    const error=state?.lastError;
    if(error==="ACCEPTED_READBACK")return {kind:"ACCEPTED"};
    if(error==="NONE"||error==="NATIVE_NOT_SUBSCRIBED"||!error)
      return {kind:"IDLE"};
    if(startupAttempt?.retries===1&&STALE_WIRE_ERRORS.has(error))
      return {kind:"FLUSH_STARTUP_RETRY"};
    if(activeAttempt?.retries===0&&nativeId(activeAttempt.epoch)&&
       state?.current?.device?.epoch===activeAttempt.epoch&&
       STALE_WIRE_ERRORS.has(error)&&
       Array.isArray(activeAttempt.ops)&&activeAttempt.ops.length===1&&
       activeAttempt.ops[0]?.type==="CONNECT")
      return {kind:"REBASE_CONNECTION"};
    return {kind:"ROLLBACK"};
  }

  function routeDiagnostic(snapshot,sourceId){
    if(!snapshot?.admitted||snapshot.retained)return "Audio unavailable · check Audio I/O";
    const graph=snapshot.graph,source=graph.channels.find(ch=>ch.id===sourceId);
    if(!source)return "Channel unavailable · select an admitted channel";
    if(source.controls?.mute===true)return source.name+" · channel muted";
    const main=graph.mainChannelId, rows=new Map(graph.channels.map(ch=>[ch.id,ch]));
    const outgoing=new Map();
    for(const edge of graph.edges){
      if(!edge.enabled||edge.sendEnabled===false||!(edge.gainLinear>0))continue;
      if(!outgoing.has(edge.from))outgoing.set(edge.from,[]);
      outgoing.get(edge.from).push(edge);
    }
    const visited=new Set([sourceId]),queue=[{id:sourceId,path:[sourceId]}];
    let path=null,monitor=false,atMinimum=false;
    for(let i=0;i<queue.length;i++){
      const {id,path:route}=queue[i],row=rows.get(id);
      if(id===main){path=route;break;}
      if(row?.role==="MONITOR")monitor=true;
      for(const edge of outgoing.get(id)||[]){
        const target=rows.get(edge.to);
        if(!target||target.controls?.mute===true||visited.has(edge.to))continue;
        // Native PRE is post-insert/pre-fader; do not suppress it on a
        // minimum fader. The -60 display is not proof of exactly zero PCM.
        const pre=!edge.primaryOutput&&(edge.tapStage||edge.tap)==="PRE_FADER";
        if(!pre&&row?.controls?.faderDb<=-60)atMinimum=true;
        visited.add(edge.to);queue.push({id:edge.to,path:[...route,edge.to]});
      }
    }
    const t=snapshot.telemetry,meters=t?.channelMeterRmsPeak;
    const measured=typeof graph.revision==="string"&&t?.channelMeterGraphRevision===graph.revision;
    const peak=id=>{
      const m=meters?.[id];
      if(!measured||!m||![m.peakL,m.peakR].every(v=>Number.isFinite(v)&&v>=0))return "N/A";
      const value=Math.max(m.peakL,m.peakR);
      return value===0?"silent":(20*Math.log10(value)).toFixed(1)+" dBFS";
    };
    const capture=()=>{
      const pins=[source.source?.portId,source.source?.portIdRight].filter(Boolean);
      if(!pins.length||t?.captureObserved!==true)return "N/A";
      const values=pins.map(id=>{
        const endpoint=snapshot.device?.inputEndpoints?.find(p=>p.id===id&&p.active);
        return endpoint?t.captureCallbackPeaks?.[endpoint.callback]:null;
      });
      if(!values.every(v=>Number.isFinite(v)&&v>=0))return "N/A";
      const value=Math.max(...values);return value===0?"silent":(20*Math.log10(value)).toFixed(1)+" dBFS";
    };
    const evidence=(source.role==="INPUT"?" · IN "+capture():"")+
      " · POST "+peak(sourceId)+" · MAIN "+peak(main);
    if(!path)return source.name+" · no enabled unmuted route to MAIN"+
      (monitor?"; MONITOR route available":"; inspect OUT, WIRE and send levels")+evidence;
    const chain=path.map(id=>rows.get(id)?.name||id).join(" → ");
    return chain+" · active route to MAIN"+(atMinimum?" · check minimum fader (PRE may remain active)":"")+
      evidence+(snapshot.device?.outputArmed===false?" · outputs disarmed":"")+
      (graph.channels.some(ch=>ch.controls?.solo===true)?" · SOLO active":"")+
      " · physical sound unverified";
  }
  return Object.freeze({routeDiagnostic,gainDb,edgePresentation,parallelEdge,
    acceptedParallelEdge,primaryChannelLevelPlan,channelSumLevelPlan,gainReadback,canEditTap,gestureTap,sofAvailable,
    compactSendGesture,nativeSendEdge,nativeTapPlan,sofSendEdge,sofSourceRole,sofTargetRole,
    channelTapEdge,nativeSendTogglePlan,primaryMainGesturePlan,nativeTransaction,
    gainDispatchPlan,nativeRecoveryPlan});
});
