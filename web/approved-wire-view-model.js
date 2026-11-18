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

  return Object.freeze({gainDb,edgePresentation,parallelEdge,
    acceptedParallelEdge,gainReadback,canEditTap,gestureTap,sofAvailable});
});
