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

  // GUI-4: derive exactly one Channel Strip PRIMARY OUT change from an
  // installed native snapshot. Both the OUT picker and WIRE jack continue to
  // dispatch via the original controller; this selector has no side effects.
  // SOURCE ONLY removes the exact PRIMARY EdgeId, never a parallel SEND.
  function primaryOutMutation(snapshot,sourceId,target,targets=[]) {
    const unavailable={kind:"UNAVAILABLE"};
    const g=snapshot?.graph;
    const validId=value=>{
      if(typeof value!=="string"||!/^[1-9][0-9]*$/.test(value))return false;
      try{return BigInt(value)<=18446744073709551615n;}catch{return false;}
    };
    if(snapshot?.admitted!==true||snapshot.retained===true||
       !Array.isArray(g?.channels)||!Array.isArray(g?.edges)||
       !g.capabilities?.supportedOps?.includes("APPLY_WIRE_TRANSACTION")||
       !validId(sourceId)||!validId(g.mainChannelId))return unavailable;
    const source=g.channels.find(ch=>ch.id===sourceId);
    if(!source||!["INPUT","GROUP","FX_RETURN"].includes(source.role))
      return unavailable;
    let to=null;
    if(target==="MAIN")to=g.mainChannelId;
    else if(target!=="NONE") {
      const choices=Array.isArray(targets)?
        targets.filter(row=>row?.label===target):[];
      if(choices.length!==1||!validId(choices[0].id))return unavailable;
      to=choices[0].id;
      if(!g.channels.some(ch=>ch.id===to&&
         ["INPUT","GROUP","FX_RETURN"].includes(ch.role)))return unavailable;
    }
    if(to===sourceId|| (target!=="NONE"&&!validId(to))||
       (target!=="NONE"&&target!=="MAIN"&&typeof target!=="string"))
      return unavailable;
    const previous=source.primaryOutId||null;
    if(previous!==null&&!validId(previous))return unavailable;
    if(previous===to)return {kind:"NO_CHANGE"};
    if(to!==null)
      return {kind:"MUTATION",change:{op:"ROUTE_ONLY",from:sourceId,to,
        tap:"POST_FADER",gainLinear:1}};
    const matching=g.edges.filter(edge=>edge?.enabled===true&&
      edge.primaryOutput===true&&edge.from===sourceId&&edge.to===previous&&
      validId(edge.edgeId));
    if(matching.length!==1)return unavailable;
    return {kind:"MUTATION",change:{op:"REMOVE",from:sourceId,
      edgeId:matching[0].edgeId}};
  }


  // MV-5 pure EXTERNAL OUT preflight. Native readback alone owns physical
  // PCM, routing, devices and permission. This never edits an accepted graph.
  function externalOutputPlan({native,hardware,channelId,selectedLabel,
                               currentLabel,choices=[],pending=false,
                               channelName=""}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const safeCounter=x=>typeof x==="string"&&/^[1-9][0-9]*$/.test(x)&&
      Number.isSafeInteger(Number(x))?x:null;
    const nativeId=x=>{
      if(typeof x!=="string"||!/^[1-9][0-9]*$/.test(x))return false;
      try{return BigInt(x)<=18446744073709551615n;}catch{return false;}
    };
    const graph=native?.graph,d=hardware?.device;
    const epoch=d?.epoch,revision=hardware?.raw?.externalRevision;
    if(pending||native?.admitted!==true||native.retained===true||
       !nativeId(channelId)||!Array.isArray(graph?.channels)||
       d?.backend!=="ASIO"||!safeCounter(epoch)||!safeCounter(revision)||
       (typeof native?.device?.epoch==="string"&&
        native.device.epoch!==epoch))return unavailable;
    const channel=graph.channels.find(ch=>ch?.id===channelId);
    if(!channel||!["INPUT","GROUP","FX_RETURN"].includes(channel.role))
      return unavailable;
    const matches=Array.isArray(choices)?
      choices.filter(p=>p?.label===selectedLabel):[];
    if(matches.length!==1)return unavailable;
    const pair=matches[0],left=pair.left,right=pair.right;
    const none=left===-1&&right===-1;
    if(!none){
      if(!Number.isSafeInteger(left)||left<0||left%2!==0||
         !Number.isSafeInteger(right)||right!==left+1||
         !Array.isArray(d.enabledOutputs)||
         !d.enabledOutputs.includes(left)||!d.enabledOutputs.includes(right)||
         !Array.isArray(d.outputEndpoints)||![left,right].every(port=>
           d.outputEndpoints.some(p=>p?.port===port&&p.active===true)))
        return unavailable;
      const main=hardware?.raw?.physicalMainOutputs||[];
      const monitor=hardware?.raw?.physicalMonitorOutputs||[];
      if((Array.isArray(main)&&[left,right].some(p=>main.includes(p)))||
         (Array.isArray(monitor)&&[left,right].some(p=>monitor.includes(p))))
        return unavailable;
    }
    if(currentLabel===selectedLabel)return {kind:"NO_CHANGE"};
    const occupied=Array.isArray(hardware?.raw?.externalOutputs)?
      hardware.raw.externalOutputs.filter(row=>row?.physicalLeft===left&&
        row.physicalRight===right&&row.channelId!==channelId).length:0;
    const sharedNote=occupied?
      " SHARED with "+occupied+" other source(s); signals SUM. Check gains.":"";
    const confirmation=none?"":"Enable ASIO OUT "+(left+1)+"/"+(right+1)+
      " for "+channelName+"?"+sharedNote+" Check amplifier/speakers. "+
      "INTERNAL OUT stays unchanged.";
    return {kind:"READY",left,right,epoch,revision,
      requiresConfirmation:!none,confirmation,sharedCount:occupied};
  }


  // MV-6: SOURCE selection/queue and native/M4 command preflight are pure
  // projections of the one installed device inventory. The bridge owns the
  // only pending queues and requestIds; neither VM nor UI owns accepted PCM.
  const positiveCounter=v=>typeof v==="string"&&/^[1-9][0-9]*$/.test(v)&&
    Number.isSafeInteger(Number(v));
  const decimalId=v=>{
    if(typeof v!=="string"||!/^[1-9][0-9]*$/.test(v))return false;
    try{return BigInt(v)<=18446744073709551615n;}catch{return false;}
  };
  function sourceChoiceMatches(choice,options) {
    if(!choice||typeof choice.label!=="string"||!Array.isArray(options))return false;
    const exact=options.filter(p=>p?.label===choice.label);
    if(exact.length!==1)return false;
    if(!["NONE","MONO","STEREO"].includes(choice.mode)||
       exact[0].mode!==choice.mode||
       exact[0].left!==choice.left||exact[0].right!==choice.right)return false;
    if(choice.mode==="NONE")return choice.left===-1&&choice.right===-1;
    if(!Number.isSafeInteger(choice.left)||choice.left<0)return false;
    if(choice.mode==="MONO")return choice.right===-1;
    return Number.isSafeInteger(choice.right)&&
      choice.right===choice.left+1;
  }
  function sourceQueueSelection({choice,choices,epoch,acceptedLabel,
                                 activeSameChannel=false}={}) {
    if(!decimalId(epoch)||!sourceChoiceMatches(choice,choices))
      return {kind:"UNAVAILABLE"};
    // The operator can cancel a queued edit by returning to installed PCM.
    // But when THIS channel already has an in-flight edit, queuing the old
    // accepted choice is a legitimate rollback requested after its ACK.
    if(choice.label===acceptedLabel&&!activeSameChannel)
      return {kind:"CANCEL"};
    return {kind:"QUEUE",entry:{epoch,choice:{
      label:choice.label,mode:choice.mode,left:choice.left,right:choice.right
    }}};
  }
  function sourceDispatchPlan({authority,entry,hardware,native,
                               channelId,channel,choices,deviceReady=false}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const choice=entry?.choice,epoch=hardware?.device?.epoch;
    if(!deviceReady||!decimalId(epoch)||epoch!==entry?.epoch||
       !sourceChoiceMatches(choice,choices))return unavailable;
    if(authority==="CHANNEL_ID"){
      const graph=native?.graph,session=hardware?.raw?.mixerSession;
      if(!decimalId(channelId)||native?.admitted!==true||
         native?.retained===true||session?.authority!=="CHANNEL_ID"||
         native?.device?.epoch!==epoch||
         !Array.isArray(graph?.channels)||
         !graph.channels.some(ch=>ch.id===channelId&&ch.role==="INPUT")||
         !decimalId(graph?.revision)||
         !decimalId(graph?.rosterRevision))return unavailable;
      return {kind:"READY",action:"routingNativeSourcePatch",
        payload:{channelId,deviceEpoch:epoch,
          graphRevision:graph.revision,rosterRevision:graph.rosterRevision,
          mode:choice.mode,left:choice.left,right:choice.right}};
    }
    if(authority==="M4"){
      if(hardware?.raw?.mixerSession?.authority==="CHANNEL_ID"||
         !positiveCounter(epoch)||
         !Number.isInteger(channel)||channel<0||channel>=10||
         !positiveCounter(hardware?.raw?.legacyM4?.routingRevision))
        return unavailable;
      return {kind:"READY",action:"routingSourceSwitch",
        payload:{channel,mode:choice.mode,left:choice.left,right:choice.right,
          deviceEpoch:Number(epoch),
          revision:Number(hardware.raw.legacyM4.routingRevision)}};
    }
    return unavailable;
  }
  function sourceAckResult(pending,ack,authority) {
    if(!pending||ack?.requestId!==pending.requestId)return "IGNORE";
    if(authority==="CHANNEL_ID")
      return ack?.ok===true?"ACKED":"REJECTED";
    if(authority!=="M4")return "REJECTED";
    const next=ack?.state||{},row=next.acceptedRoute?.channels?.[pending.channel];
    return ack?.ok===true&&next.ready===true&&
      Number(next.epoch)===Number(pending.epoch)&&
      next.hasAcceptedRoute===true&&
      Number(next.revision)>=Number(pending.revision)&&
      row?.mode===pending.choice.mode&&
      row?.left===pending.choice.left&&row?.right===pending.choice.right?
        "ACKED":"REJECTED";
  }

  return Object.freeze({outputTargets,inputChoices,externalChoices,
    primaryChoices,resolvePrimarySelection,readbackBindings,externalPending,
    primaryOutMutation,externalOutputPlan,sourceQueueSelection,
    sourceDispatchPlan,sourceAckResult});
});
