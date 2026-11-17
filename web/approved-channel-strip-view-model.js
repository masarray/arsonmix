// SPDX-License-Identifier: AGPL-3.0-only
// GUI-1: pure Channel Strip SOURCE/OUTPUT presentation for the ONE native
// /preview/ mixer. No DOM, dispatch, installed mixer state, or PCM authority.
(function (root, factory) {
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  if(root)root.ArSonApprovedChannelStripViewModel=api;
})(typeof globalThis==="object"?globalThis:undefined,function () {
  "use strict";
  const ONLY="SOURCE ONLY (No Main)";
  const RESERVED=new Set(["Master",ONLY,"MAIN","NONE"]);
  const REMAP="Audio connection lost";
  // Preserve decimal-string native ChannelIds, even beyond JS safe integers.
  // Reserved and duplicate operator names cannot shadow OUT commands.
  function outputTargets(channels,sourceId,nameFor) {
    if(!Array.isArray(channels))return [];
    const rows=channels.map(ch=>({ch,name:typeof nameFor==="function"?
      nameFor(ch):ch?.name}));
    const counts=new Map();
    for(const row of rows)counts.set(row.name,(counts.get(row.name)||0)+1);
    return rows.filter(({ch})=>ch&&ch.id!==sourceId&&
      ["INPUT","GROUP","FX_RETURN"].includes(ch.role)).map(({ch,name})=>({
        id:ch.id,label:RESERVED.has(name)||counts.get(name)>1?
          name+" ["+ch.id+"]":name}));
  }
  function inputChoices(track,physical,retained,logical,internalBus) {
    if(track?.kind!=="audio")return ["None"];
    if(retained&&!logical)return [track.input||"None"];
    const labels=Array.isArray(physical)?physical.map(p=>p.label):["None"];
    // Internal BUS is a readback-only label, never offered as a physical pin.
    if(track.input===internalBus&&!labels.includes(internalBus))
      labels.push(internalBus);
    return labels;
  }
  function externalChoices(track,pairs) {
    if(track?.kind==="master")return ["None"];
    const labels=Array.isArray(pairs)?pairs.map(p=>p.label):[];
    if(track?.externalOutput&&!labels.includes(track.externalOutput))
      labels.push(track.externalOutput);
    return labels;
  }
  function primaryChoices({track,logical=false,retained=false,
                           playbackDevice="",authority="",targets=[]}) {
    if(track?.kind==="master"){
      if(logical)return ["Choose audio output"];
      return [retained?REMAP:
        String(playbackDevice||track.output||"Choose audio output")];
    }
    const labels=["Master",ONLY];
    if(logical||authority==="CHANNEL_ID")
      for(const target of targets)labels.push(target.label);
    if(track?.output&&!labels.includes(track.output))labels.push(track.output);
    return labels;
  }
  function resolvePrimarySelection(value,targets) {
    if(value==="Master")return "MAIN";
    if(value===ONLY)return "NONE";
    return Array.isArray(targets)&&targets.some(x=>x.label===value)?value:null;
  }
  function readbackBindings({native,channel,channels,targets,inputLabel}) {
    const out={};
    if(channel?.role==="INPUT"){
      const src=channel.source||{};
      const left=/^in:([0-9]+)$/.exec(src.portId||"");
      const right=/^in:([0-9]+)$/.exec(src.portIdRight||"");
      const l=left?Number(left[1]):-1,r=right?Number(right[1]):-1;
      const verified=left&&Number.isSafeInteger(l)&&
        (src.sourceFormat!=="STEREO"||right&&Number.isSafeInteger(r));
      out.input=native?.retained&&src.bindingState==="NEEDS_REMAP"?REMAP:
        verified?inputLabel({mode:src.sourceFormat==="STEREO"?"STEREO":"MONO",
          left:l,right:r}):"None";
    }
    if(channel?.role!=="MAIN"){
      const dest=Array.isArray(channels)?
        channels.find(x=>x.id===channel?.primaryOutId):null;
      const target=dest&&targets.find(t=>t.id===dest.id);
      out.output=dest?.role==="MAIN"?"Master":
        target?.label||ONLY;
    }
    return out;
  }
  // Same single bridge-side pending Map: ACK without independent native
  // readback must never clear pending or label a physical output LIVE.
  function externalPending(pending,epoch,state,row) {
    if(!pending)return "NONE";
    if(epoch && epoch!==pending.epoch)return "STALE";
    if(pending.acked!==true)return "PENDING";
    if(((state==="LIVE"||state==="MUTED")&&row&&
        row.physicalLeft===pending.left&&row.physicalRight===pending.right)||
       (pending.left===-1&&!row))return "CONFIRMED";
    return "PENDING";
  }
  return Object.freeze({outputTargets,inputChoices,externalChoices,
    primaryChoices,resolvePrimarySelection,readbackBindings,externalPending});
});
