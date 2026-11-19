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

  function gainReadback(edges,uiByEdgeId,pendingGains) {
    if(!Array.isArray(edges))return [];
    return edges.filter(e=>e.enabled===true&&e.primaryOutput===false)
      .flatMap(e=>{
        const id=uiByEdgeId.get(e.edgeId),levelDb=gainDb(e.gainLinear);
        if(id===undefined||levelDb===null)return [];
        return [{id,pending:pendingGains.has(e.edgeId),levelDb}];
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
    return snapshot?.admitted===true&&snapshot.retained!==true&&
      canToggle===true&&SOF_OPS.every(op=>
        snapshot.graph?.capabilities?.supportedOps?.includes(op));
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

  // Only one parallel SEND can own a selected SOF destination. It must be
  // an actual GROUP/FX_RETURN ChannelId, never MAIN/primary, an ordinal or
  // an ambiguous duplicate. SOF gain, PRE/POST, and enable share this edge.
  function sofSendEdge(snapshot,{sourceId,destinationId}={}) {
    if(snapshot?.admitted!==true||snapshot.retained===true||
       !nativeId(sourceId)||!nativeId(destinationId))return null;
    const graph=snapshot.graph;
    const source=graph?.channels?.filter(ch=>ch.id===sourceId);
    const target=graph?.channels?.filter(ch=>ch.id===destinationId);
    if(source?.length!==1||source[0].role!=="INPUT"||
       target?.length!==1||!["GROUP","FX_RETURN"].includes(target[0].role))
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

  return Object.freeze({gainDb,edgePresentation,parallelEdge,
    acceptedParallelEdge,gainReadback,canEditTap,gestureTap,sofAvailable,
    compactSendGesture,nativeSendEdge,nativeTapPlan,sofSendEdge,
    channelTapEdge,nativeSendTogglePlan,nativeTransaction,
    gainDispatchPlan,nativeRecoveryPlan});
});
