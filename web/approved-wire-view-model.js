// SPDX-License-Identifier: AGPL-3.0-only
// GUI-3: WIRE/BUS/MAIN presentation for one accepted ChannelId/EdgeId graph.
// Pure selectors, no DOM, DSP, native dispatcher or second routing authority.
(function(root,factory){
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  if(root)root.ArSonApprovedWireViewModel=api;
})(typeof globalThis==="object"?globalThis:undefined,function(){
  "use strict";
  // Only independently editable sends; never turn PRIMARY OUT into SEND.
  // Exact uint64 decimal identities remain strings, never JS Numbers.
  function parallelEdge(edges,source,edgeId){
    if(typeof source!=="string"||!source||typeof edgeId!=="string"||
       !edgeId||!Array.isArray(edges))return null;
    const matches=edges.filter(e=>e?.enabled===true&&
      e.edgeId===edgeId&&e.from===source&&e.primaryOutput===false);
    return matches.length===1?matches[0]:null;
  }
  function gainDraft(levelDb,source,identity){
    const db=Number(levelDb);
    if(!Number.isFinite(db)||db< -60||db>6||
       typeof source!=="string"||!source||
       typeof identity!=="string"||!identity)return null;
    // UI floor is -infinity: actual zero PCM, not 0.001 linear gain.
    return {source,gain:db<=-60?0:Math.pow(10,db/20),identity};
  }
  function readbackGains(edges,uiByEdge,pendingByEdge){
    if(!Array.isArray(edges))return [];
    return edges.filter(e=>e?.enabled===true&&e.primaryOutput===false)
      .map(e=>({id:uiByEdge?.get?.(e.edgeId),
        pending:pendingByEdge?.has?.(e.edgeId)===true,
        levelDb:Math.round(Math.max(-60,Math.min(6,
          e.gainLinear>0?20*Math.log10(e.gainLinear):-60))*10)/10}));
  }
  function flushDecision(current,edgeId,draft){
    if(!current||!draft||current.identity!==draft.identity)return "STALE";
    const edge=parallelEdge(current.graph?.edges,draft.source,edgeId);
    if(!edge)return "UNROUTED";
    return Math.abs(edge.gainLinear-draft.gain)<.00001?"UNCHANGED":"SEND";
  }
  // Inspector legacy modes translate to the existing native EdgeId tap.
  function tapFromGesture(mode){
    return mode==="split"?"PRE_FADER":mode==="send"?"POST_FADER":null;
  }
  return Object.freeze({parallelEdge,gainDraft,readbackGains,
    flushDecision,tapFromGesture});
});
