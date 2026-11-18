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
      return ack?.ok===true && ack?.channelId===pending.id &&
        decimalId(pending.beforeRevision) && decimalId(ack.graphRevision) &&
        BigInt(ack.graphRevision)>BigInt(pending.beforeRevision)?
          "ACKED":"REJECTED";
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


  // MV-7: ACK is only a correlated native acceptance, never proof of PCM.
  // Compare the installed ChannelId SOURCE row, physical pins and exact
  // revision without converting 64-bit graph identities through Number.
  // The bridge remains the sole holder of the active transient request.
  function sourceReadbackSettlement(pending,next) {
    if(!pending)return "NONE";
    if(!decimalId(pending.id)||!decimalId(pending.epoch)||
       next?.device?.epoch!==pending.epoch||
       next?.retained===true||next?.admitted!==true||
       (next?.raw && (next.raw.mixerSession?.authority!=="CHANNEL_ID"||
         next.raw.mixerSession.deviceEpoch!==pending.epoch)))return "STALE";
    const rows=next?.graph?.channels;
    if(!Array.isArray(rows))return "PENDING";
    const channel=rows.find(row=>row?.id===pending.id);
    if(!channel||channel.role!=="INPUT")return "STALE";
    if(pending.acked!==true)return "PENDING_ACK";
    if(!decimalId(pending.beforeRevision)||
       !decimalId(pending.ackRevision)||
       !decimalId(next.graph.revision)||
       BigInt(pending.ackRevision)<=BigInt(pending.beforeRevision)||
       BigInt(next.graph.revision)<BigInt(pending.ackRevision))
      return "PENDING";
    const choice=pending.choice;
    if(!choice||!["NONE","MONO","STEREO"].includes(choice.mode))
      return "PENDING";
    const source=channel.source;
    if(choice.mode==="NONE")return !source?"CONFIRMED":"PENDING";
    if(!source||source.bindingState==="NEEDS_REMAP"||
       source.portId!=="in:"+choice.left)return "PENDING";
    if(choice.mode==="MONO")
      return source.sourceFormat==="MONO" &&
        source.portIdRight===undefined?"CONFIRMED":"PENDING";
    return source.sourceFormat==="STEREO" &&
      source.portIdRight==="in:"+choice.right?"CONFIRMED":"PENDING";
  }

  // MV-9: pure Channel Strip scalar gesture/readback policy. The only
  // pending Maps, native events, timers and audio transport stay in the
  // approved bridge. Treat exact decimal-string ChannelIds as opaque data.
  function scalarIdentityMatches(edit,identity) {
    return !!edit && !!identity &&
      edit.epoch===identity.epoch &&
      edit.authority===identity.authority &&
      edit.channelId===identity.channelId;
  }
  // Last pointer target is DISPLAY-ONLY until independently installed
  // fader PCM readback matches it; never rewrite a native track model.
  function scalarVisualSettlement(pending,acceptedDb,identity) {
    if(!pending)return "NONE";
    if(!scalarIdentityMatches(pending,identity) ||
       !Number.isFinite(pending.value))return "STALE";
    return Number.isFinite(acceptedDb) &&
      Math.abs(acceptedDb-pending.value)<=.11?"CONFIRMED":"WAIT";
  }
  function m4ScalarAckResult(pending,ack) {
    if(!pending || ack?.requestId!==pending.requestId)return "IGNORE";
    return ack.ok===true &&
      String(ack.deviceEpoch)===pending.epoch &&
      String(ack.routingRevision)===pending.revision &&
      ack.channel===pending.channel &&
      ack.parameter===pending.parameter?"ACKED":"REJECTED";
  }
  // One M4 scalar can be confirmed by only its corresponding accepted
  // producer readback; an ACK never installs fader, PAN, MUTE, or MAIN gain.
  // The full routing revision must still belong to the original M4 request.
  function m4ScalarReadbackSettlement(pending,snapshot) {
    if(!pending)return "NONE";
    const session=snapshot?.raw?.mixerSession;
    const revision=pending.channel===-1?
      snapshot?.raw?.legacyM4?.routingRevision:session?.routeRevision;
    if(snapshot?.device?.epoch!==pending.epoch ||
       revision!==pending.revision ||
       (pending.channel!==-1&&session?.authority!=="M4"))
      return "STALE";
    const row=pending.channel===-1?null:session?.channels?.[pending.channel];
    const value=pending.channel===-1?
      (pending.parameter==="masterGain"?session?.main?.db:session?.main?.mute):
      pending.parameter==="faderDb"?row?.controls?.faderDb:
      pending.parameter==="pan"?row?.controls?.pan*100:
      row?.controls?.[pending.parameter];
    const matches=typeof pending.value==="boolean"?value===pending.value:
      Number.isFinite(pending.value)&&Number.isFinite(value)&&
      Math.abs(value-pending.value)<=(pending.parameter==="pan"?.8:.11);
    return pending.acked===true&&matches?"CONFIRMED":"WAIT";
  }

  // MV-10: pure scalar dispatch/coalescing policy. The bridge alone owns
  // timers, one pending Map, native transport and the accepted PCM model.
  // The throttle lease belongs to the EXACT native identity, not a recycled
  // local strip key, so reconnect/roster replacement can send immediately.
  function scalarDispatchLease(edit,at) {
    return {at,epoch:edit.epoch,authority:edit.authority,
      channelId:edit.channelId};
  }
  function scalarGestureFlushPlan({logical=false,final=false,now,
                                    lastSent=null,identity=null}={}) {
    if(final)return "FLUSH"; // never defer pointerup/release
    if(logical)return "QUEUE"; // offline .smix: one edit on release
    if(!lastSent || !scalarIdentityMatches(lastSent,identity) ||
       !Number.isFinite(lastSent.at) || !Number.isFinite(now) ||
       now<lastSent.at || now-lastSent.at>=30)return "FLUSH";
    return "QUEUE"; // keep latest target; existing one-shot timer owns retry
  }
  function scalarDispatchPlan(edit,{identity=null,ready=false,
    logical=false,logicalPending=false,m4Owner=false,nativePending=false}={}) {
    if(!scalarIdentityMatches(edit,identity) || ready!==true ||
       !Number.isFinite(edit.value) ||
       !["faderDb","masterGain","pan"].includes(edit.parameter))
      return {kind:"REJECT"};
    if(logical && logicalPending)return {kind:"WAIT"};
    if(!logical && m4Owner)return {kind:"M4"};
    if(!logical && nativePending)return {kind:"WAIT"};
    return edit.parameter==="pan"?
      {kind:"INTENT",operation:"SET_PAN",payload:{value:edit.value/100}}:
      {kind:"INTENT",operation:"SET_FADER",payload:{valueDb:edit.value}};
  }

  return Object.freeze({outputTargets,inputChoices,externalChoices,
    primaryChoices,resolvePrimarySelection,readbackBindings,externalPending,
    primaryOutMutation,externalOutputPlan,sourceQueueSelection,
    sourceDispatchPlan,sourceAckResult,sourceReadbackSettlement,
    scalarIdentityMatches,scalarVisualSettlement,m4ScalarAckResult,
    m4ScalarReadbackSettlement,scalarDispatchLease,
    scalarGestureFlushPlan,scalarDispatchPlan});
});
