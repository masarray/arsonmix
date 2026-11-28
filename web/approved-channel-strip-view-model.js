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
    // Keep virtual player separate from the physical pin inventory. It is
    // offered ONLY when the native ChannelId graph has the real decoder
    // command; retained/M4 never see an imaginary browser PCM source.
    if(Array.isArray(physical)&&physical.some(p=>p.mode==="PLAYER")&&
       !labels.includes("Music Player"))labels.push("Music Player");
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
        src.sourceFormat==="PLAYER"&&src.playerDeckId==="1"?
          "Music Player":
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
    if(((state==="LIVE"||state==="MUTED"||state==="ASSIGNED_SILENT")&&row&&
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
      edge.from===sourceId&&edge.to===previous&&validId(edge.edgeId)&&
      // Legacy admitted v6 can omit primaryOutput. Only a UNIQUE unity
      // edge matching the accepted primaryOutId may stand in for PRIMARY.
      // Explicit false remains a parallel SEND regardless of destination.
      (edge.primaryOutput===true||
       (edge.primaryOutput==null&&edge.gainLinear===1)));
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
    // ChannelId route tokens use exact decimal uint64 strings, not JS
    // Number. The native token parser verifies these again on admission.
    const safeCounter=x=>{
      if(typeof x!=="string"||!/^[1-9][0-9]*$/.test(x))return null;
      try{return BigInt(x)<=18446744073709551615n?x:null;}catch{return null;}
    };
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
    if((typeof graph.revision==="string"&&graph.revision!==revision)||
       hardware.raw.physicalBusOutputObserved===false||
       hardware.raw.logicalOutputRegistry?.references?.some(r=>r.channelId===channelId))
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
      const ports=hardware.raw.logicalOutputRegistry?.ports;
      if(Array.isArray(ports)&&ports.some(p=>p.bound===true&&p.playback===d.playbackDevice&&
        [p.left,p.right].some(pin=>pin===left||pin===right)))return unavailable;
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


  // G-MV30: ONE pure ChannelId roster transaction authority for native
  // CREATE_CHANNEL / CREATE_GROUP / DELETE_CHANNEL / REMOVE_BUS. The bridge
  // owns one pending create and delete lane, native IPC, timers and UI.
  // Neither ACK contents nor an unmodified accepted graph prove PCM edits.
  const ROSTER_OPS=Object.freeze({
    CREATE_CHANNEL:{action:"routingNativeTrackCreate",role:"INPUT"},
    CREATE_GROUP:{action:"routingNativeBusCreate",role:"GROUP"},
    DELETE_CHANNEL:{action:"routingNativeTrackDelete",role:"INPUT"},
    REMOVE_BUS:{action:"routingNativeBusDelete",role:"BUS"}
  });
  function rosterMutationPlan(snapshot,{operation,channelId=null,
    hardware=null,deviceReady=false,controllerPending=false,
    structuralPending=false}={}) {
    const unavailable={kind:"UNAVAILABLE"},spec=ROSTER_OPS[operation];
    const d=snapshot?.device,session=snapshot?.raw?.mixerSession;
    if(!spec||controllerPending||structuralPending||snapshot?.retained===true||
       !decimalId(d?.epoch)||hardware?.device?.epoch!==d.epoch||
       deviceReady!==true)return unavailable;
    const g=snapshot?.graph,creating=operation.startsWith("CREATE_");
    const admitted=snapshot?.admitted===true&&
      session?.authority==="CHANNEL_ID"&&
      session?.deviceEpoch===d.epoch&&
      g?.capabilities?.supportedOps?.includes(operation)===true&&
      decimalId(g?.revision)&&decimalId(g?.rosterRevision)&&
      session?.routeRevision===g.revision&&
      session?.rosterRevision===g.rosterRevision&&
      Array.isArray(g?.channels);
    if(admitted) {
      let row=null;
      if(!creating){
        if(!decimalId(channelId))return unavailable;
        const rows=g.channels.filter(ch=>ch.id===channelId);
        if(rows.length!==1)return unavailable;
        row=rows[0];
        if(operation==="DELETE_CHANNEL"&&row.role!=="INPUT"||
           operation==="REMOVE_BUS"&&
           !["GROUP","FX_RETURN"].includes(row.role))
          return unavailable;
      }
      return {kind:"READY",action:spec.action,
        payload:{...(creating?{}:{channelId}),deviceEpoch:d.epoch,
          graphRevision:g.revision,rosterRevision:g.rosterRevision},
        pending:{operation,epoch:d.epoch,baseGraphRevision:g.revision,
          baseRosterRevision:g.rosterRevision,
          ...(creating?{beforeIds:g.channels.map(ch=>ch.id)}:
            {id:channelId,role:row.role}),
          acked:false}};
    }
    // Only first AUDIO TRACK may bootstrap a verified silent prepared M4
    // graph. A BUS cannot bootstrap. No browser-made graph or auto-arm.
    const m=snapshot?.raw?.legacyM4;
    if(operation!=="CREATE_CHANNEL"||snapshot?.admitted!==false||
       session?.authority!=="NONE"||session.deviceEpoch!==d.epoch||
       m?.active!==true||m?.routeAccepted!==false||
       !positiveCounter(m.routingRevision))return unavailable;
    return {kind:"READY",action:spec.action,
      payload:{deviceEpoch:d.epoch,bootstrapRevision:m.routingRevision},
      pending:{operation,epoch:d.epoch,bootstrap:true,acked:false}};
  }
  function rosterAckResult(pending,ack) {
    if(!pending||ack?.requestId!==pending.requestId)return "IGNORE";
    if(ack.ok!==true)return "REJECTED";
    // Native host emits graphRevision on both create/delete. It is a
    // correlated ACK target, not the independently installed PCM graph.
    if(!decimalId(ack.graphRevision)||
       (!pending.bootstrap&&
        (!decimalId(pending.baseGraphRevision)||
         BigInt(ack.graphRevision)<=BigInt(pending.baseGraphRevision))))
      return "REJECTED";
    if(pending.operation==="CREATE_CHANNEL"||
       pending.operation==="CREATE_GROUP")
      return decimalId(ack.createdId)&&
        ack.createdId!==pending.id&&
        !pending.beforeIds?.includes(ack.createdId)?"ACKED":"REJECTED";
    if(pending.operation==="DELETE_CHANNEL"||
       pending.operation==="REMOVE_BUS")
      return ack.channelId===pending.id?"ACKED":"REJECTED";
    return "REJECTED";
  }
  function rosterReadbackSettlement(pending,snapshot) {
    if(!pending)return "NONE";
    if(snapshot?.device?.epoch!==pending.epoch||
       snapshot?.retained===true)return "STALE";
    const session=snapshot?.raw?.mixerSession,g=snapshot?.graph;
    // Initial bootstrap can be in-flight before ChannelId admission.
    if(snapshot?.admitted!==true)
      return pending.bootstrap&&session?.authority==="NONE"?"WAIT":"STALE";
    if(session?.authority!=="CHANNEL_ID"||
       session.deviceEpoch!==pending.epoch||
       !decimalId(g?.revision)||!decimalId(g?.rosterRevision)||
       session.routeRevision!==g.revision||
       session.rosterRevision!==g.rosterRevision||
       !Array.isArray(g?.channels))return "STALE";
    if(!pending.bootstrap){
      if(!decimalId(pending.baseGraphRevision)||
         !decimalId(pending.baseRosterRevision)||
         BigInt(g.revision)<BigInt(pending.baseGraphRevision)||
         BigInt(g.rosterRevision)<BigInt(pending.baseRosterRevision))
        return "STALE";
    }
    if(pending.acked!==true||!decimalId(pending.ackRevision)||
       BigInt(g.revision)<BigInt(pending.ackRevision)||
       (!pending.bootstrap&&
        (BigInt(g.revision)<=BigInt(pending.baseGraphRevision)||
         BigInt(g.rosterRevision)<=BigInt(pending.baseRosterRevision))))
      return "WAIT";
    const matches=g.channels.filter(ch=>ch.id===(pending.createdId||pending.id));
    if(pending.operation==="CREATE_CHANNEL"||
       pending.operation==="CREATE_GROUP")
      return decimalId(pending.createdId)&&
        !pending.beforeIds?.includes(pending.createdId)&&
        matches.length===1&&
        matches[0].role===(pending.purpose==="FX"?"FX_RETURN":ROSTER_OPS[pending.operation].role)&&
        (pending.purpose!=="FX"||(matches[0].inserts?.length===1&&matches[0].inserts[0].catalogId==="armix.builtin.vocal-reverb-return.v1"&&matches[0].primaryOutId===g.mainChannelId))&&
        (pending.connectMain!==false||!matches[0].primaryOutId)&&
        (!pending.selection?.length||pending.selection.every(id=>g.channels.some(ch=>ch.id===id&&ch.primaryOutId===pending.createdId)))?
        "CONFIRMED":"WAIT";
    return (pending.operation==="DELETE_CHANNEL"||
       pending.operation==="REMOVE_BUS")&&
       decimalId(pending.id)&&matches.length===0?"CONFIRMED":"WAIT";
  }

  // G-MV29: one M4-native physical channel-patch policy for both OUT
  // (MAIN/SOURCE ONLY) and MONO/STEREO. The bridge keeps one dispatcher,
  // request ID and timeout, but no longer decides PCM eligibility from
  // a local strip label, a stale legacy revision, or an ACK payload.
  // The M4 host accepts only JS-safe epoch/revision integer tokens;
  // ChannelId's separate uint64 path MUST NOT be rounded here.
  function m4ChannelPatchPlan(snapshot,{operation,channel,target,
    verifiedM4=false,deviceReady=false,pending=false,
    sourcePending=false}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const session=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4,
      epoch=snapshot?.device?.epoch,revision=m?.routingRevision;
    if(pending||sourcePending||verifiedM4!==true||deviceReady!==true||
       snapshot?.admitted!==false||snapshot?.retained===true||
       session?.authority!=="M4"||m?.active!==true||
       m.routeAccepted!==true||m.nativeBusStructural===true||
       !positiveCounter(epoch)||!positiveCounter(revision)||
       session.deviceEpoch!==epoch||session.routeRevision!==revision||
       !Number.isInteger(channel)||channel<0||channel>=10||
       !Array.isArray(session.channels)||session.channels.length!==10||
       !Array.isArray(m.channels)||m.channels.length!==10)
      return unavailable;
    const row=session.channels[channel],source=m.channels[channel];
    if(row?.role!=="INPUT"||row.ordinal!==channel||!source||
       row.source?.main!==source.main)return unavailable;
    let actual;
    if(operation==="SET_PRIMARY_OUT"){
      if(target!=="MAIN"&&target!=="NONE"||
         typeof source.main!=="boolean"||
         row.primaryOut!==(source.main?"MAIN":"NONE"))
        return unavailable;
      actual=source.main?"MAIN":"NONE";
    }else if(operation==="SET_PROCESSING_FORMAT"){
      if(target!=="MONO"&&target!=="STEREO"||
         !["MONO","STEREO"].includes(source.channelFormat)||
         row.source?.channelFormat!==source.channelFormat)
        return unavailable;
      actual=source.channelFormat;
    }else return unavailable;
    if(actual===target)return {kind:"NO_CHANGE"};
    return {kind:"READY",action:"routingChannelPatch",
      payload:{operation,channel,target,
        deviceEpoch:Number(epoch),revision:Number(revision)},
      pending:{channel,operation,target,epoch,baseRevision:revision,
        acked:false}};
  }
  // ACK is just a matching producer response. The ACK's embedded state
  // is NOT independently installed PCM and must never repaint the strip.
  function m4ChannelPatchAckResult(pending,ack) {
    if(!pending||!ack||ack.requestId!==pending.requestId)return "IGNORE";
    return ack.ok===true&&ack.channel===pending.channel&&
      String(ack.state?.epoch)===pending.epoch?"ACKED":"REJECTED";
  }
  // Only a subsequent accepted M4 native session, with a strictly newer
  // route revision and exact installed channel format/OUT, can unlock the
  // next patch and mark this one successful. One stale ACK cannot settle it.
  function m4ChannelPatchSettlement(pending,snapshot) {
    if(!pending)return "NONE";
    const session=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4;
    if(snapshot?.admitted!==false||snapshot?.retained===true||
       snapshot?.device?.epoch!==pending.epoch||
       session?.authority!=="M4"||session.deviceEpoch!==pending.epoch||
       m?.active!==true||m.routeAccepted!==true||
       m.nativeBusStructural===true||
       !positiveCounter(session.routeRevision)||
       session.routeRevision!==m.routingRevision||
       !Number.isInteger(pending.channel)||pending.channel<0||
       pending.channel>=10||!Array.isArray(session.channels)||
       session.channels.length!==10||
       !Array.isArray(m.channels)||m.channels.length!==10)
      return "STALE";
    const row=session.channels[pending.channel],
      source=m.channels[pending.channel];
    if(row?.role!=="INPUT"||row.ordinal!==pending.channel||
       !source||row.source?.main!==source.main)
      return "STALE";
    // Revision cannot decrease or be reused by a different installed
    // producer, even if its visible output happens to equal the request.
    if(!positiveCounter(pending.baseRevision)||
       Number(session.routeRevision)<Number(pending.baseRevision))
      return "STALE";
    if(pending.acked!==true||
       Number(session.routeRevision)<=Number(pending.baseRevision))
      return "WAIT";
    const installed=pending.operation==="SET_PRIMARY_OUT"?
      (typeof source.main==="boolean"&&
        row.primaryOut===(source.main?"MAIN":"NONE")?
        (source.main?"MAIN":"NONE"):null):
      pending.operation==="SET_PROCESSING_FORMAT"?
        (["MONO","STEREO"].includes(source.channelFormat)&&
         row.source?.channelFormat===source.channelFormat?
           source.channelFormat:null):null;
    return installed!==null&&installed===pending.target?"CONFIRMED":"WAIT";
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
    if(!["NONE","MONO","STEREO","PLAYER"].includes(choice.mode)||
       exact[0].mode!==choice.mode||
       exact[0].left!==choice.left||exact[0].right!==choice.right)return false;
    if(choice.mode==="PLAYER")
      return choice.left===-1&&choice.right===-1&&
        choice.deckId==="1"&&exact[0].deckId==="1";
    if(choice.mode==="NONE")return choice.left===-1&&choice.right===-1;
    if(!Number.isSafeInteger(choice.left)||choice.left<0)return false;
    if(choice.mode==="MONO")return choice.right===-1;
    return Number.isSafeInteger(choice.right)&&choice.right>=0&&
      choice.right!==choice.left;
  }
  function sourceQueueSelection({choice,choices,epoch,acceptedLabel,
                                 activeSameChannel=false,
                                 hardwareConfiguration}={}) {
    // G-MV22: the operator's transient pin choice belongs to the EXACT
    // installed physical pin/format configuration, not merely an epoch.
    // The v6 native controller is the sole owner of this canonical token.
    if(!decimalId(epoch)||!sourceChoiceMatches(choice,choices)||
       (hardwareConfiguration!==undefined&&
        (typeof hardwareConfiguration!=="string"||!hardwareConfiguration)))
      return {kind:"UNAVAILABLE"};
    // The operator can cancel a queued edit by returning to installed PCM.
    // But when THIS channel already has an in-flight edit, queuing the old
    // accepted choice is a legitimate rollback requested after its ACK.
    if(choice.label===acceptedLabel&&!activeSameChannel)
      return {kind:"CANCEL"};
    return {kind:"QUEUE",entry:{epoch,
      ...(hardwareConfiguration!==undefined?{hardwareConfiguration}:{}),
      choice:{label:choice.label,mode:choice.mode,
        left:choice.left,right:choice.right,
        ...(choice.mode==="PLAYER"?{deckId:"1"}:{})}}};
  }
  function sourceDispatchPlan({authority,entry,hardware,native,
                               channelId,channel,choices,deviceReady=false}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const choice=entry?.choice,epoch=hardware?.device?.epoch;
    if(!deviceReady||!decimalId(epoch)||epoch!==entry?.epoch||
       !sourceChoiceMatches(choice,choices)||
       // A live normalized native readback always has a fingerprint.
       // Never let an unscoped old queue entry slip into that device.
       (hardware?.hardwareConfiguration!==undefined?
         !hardware.hardwareConfiguration||
         entry?.hardwareConfiguration!==hardware.hardwareConfiguration:
         entry?.hardwareConfiguration!==undefined)||
       (native?.hardwareConfiguration!==undefined&&
        entry?.hardwareConfiguration!==native.hardwareConfiguration))
      return unavailable;
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
          mode:choice.mode,left:choice.left,right:choice.right,
          ...(choice.mode==="PLAYER"?{deckId:"1"}:{})}};
    }
    if(authority==="M4"){
      if(choice.mode==="PLAYER")return unavailable;
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
  // G-MV23: ONE pure accepted-source comparator reused by the normal
  // correlated ACK/readback settlement and the queue's no-op optimization.
  // No GUI label, ACK payload, or pending request may prove installed PCM.
  function installedSourceMatches(authority,choice,source) {
    if(!choice||!["NONE","MONO","STEREO","PLAYER"].includes(choice.mode))
      return false;
    if(authority==="CHANNEL_ID"){
      if(choice.mode==="NONE")return !source;
      if(choice.mode==="PLAYER")
        return source?.bindingState!=="NEEDS_REMAP"&&
          source?.sourceFormat==="PLAYER"&&source?.playerDeckId==="1";
      if(!source||source.bindingState==="NEEDS_REMAP"||
         source.portId!=="in:"+choice.left)return false;
      if(choice.mode==="MONO")
        return source.sourceFormat==="MONO"&&source.portIdRight===undefined;
      return source.sourceFormat==="STEREO"&&
        source.portIdRight==="in:"+choice.right;
    }
    return authority==="M4"&&!!source&&
      source.mode===choice.mode&&source.left===choice.left&&
      source.right===choice.right;
  }
  // Never skip a native SOURCE patch merely because its pin numbers match.
  // An independently installed, admitted ChannelId graph (or fully
  // accepted M4 producer) with the exact current physical lease is required.
  // A stale/NEEDS_REMAP/unadmitted session must still use normal native
  // preflight rather than be optimistically accepted as already installed.
  function sourceQueueAlreadyInstalled({authority,key,entry,hardware,native}={}) {
    if(!entry?.choice||!decimalId(entry.epoch)||
       hardware?.device?.epoch!==entry.epoch||
       (hardware?.hardwareConfiguration!==undefined&&
         entry.hardwareConfiguration!==hardware.hardwareConfiguration)||
       (native?.hardwareConfiguration!==undefined&&
         entry.hardwareConfiguration!==native.hardwareConfiguration))
      return false;
    if(authority==="CHANNEL_ID"){
      const session=hardware?.raw?.mixerSession;
      if(!decimalId(key)||native?.admitted!==true||
         native?.retained===true||session?.authority!=="CHANNEL_ID"||
         session.deviceEpoch!==entry.epoch||
         native?.device?.epoch!==entry.epoch)return false;
      const row=native.graph?.channels?.find(ch=>ch.id===key);
      return row?.role==="INPUT"&&installedSourceMatches(
        authority,entry.choice,row.source);
    }
    if(authority==="M4"){
      const m=hardware?.raw?.legacyM4,session=hardware?.raw?.mixerSession;
      if(hardware?.admitted!==false||hardware?.retained===true||
         m?.active!==true||m.routeAccepted!==true||
         session?.authority!=="M4"||
         session.deviceEpoch!==entry.epoch||
         !positiveCounter(session.routeRevision)||
         session.routeRevision!==m.routingRevision||
         !Number.isInteger(key)||key<0||key>=10||
         !Array.isArray(session.channels)||session.channels.length!==10)
        return false;
      const row=session.channels[key];
      return row?.role==="INPUT"&&installedSourceMatches(
        authority,entry.choice,row.source);
    }
    return false;
  }

  // MV-14: pure FIFO head shared by ChannelId and legacy M4 INPUT.
  // The bridge alone owns queue mutation, native events, timeouts and
  // correlated ACK/readback. Never replay pin drafts from an old device.
  function sourceQueueHead({authority,queue,active=null,hardware,native,
                            choices,deviceReady=false}={}) {
    // A WebView/test harness can hand in a Map from another JS realm.
    // instanceof Map would wrongly treat this valid queue as EMPTY.
    if(active||!queue||!Number.isSafeInteger(queue.size)||queue.size<1||
       typeof queue.entries!=="function")return {kind:"IDLE"};
    if(authority==="CHANNEL_ID"){
      if(native?.admitted!==true||
         hardware?.raw?.mixerSession?.authority!=="CHANNEL_ID")
        return {kind:"WAIT"};
    }else if(authority==="M4"){
      if(!deviceReady)return {kind:"DROP_ALL"};
    }else return {kind:"WAIT"};
    const [key,entry]=queue.entries().next().value;
    const plan=sourceDispatchPlan({authority,entry,hardware,native,
      channelId:authority==="CHANNEL_ID"?key:undefined,
      channel:authority==="M4"?key:undefined,choices,deviceReady});
    if(plan.kind!=="READY")return {kind:"DROP",key};
    if(sourceQueueAlreadyInstalled({authority,key,entry,hardware,native}))
      return {kind:"SKIP_INSTALLED",key};
    return {kind:"SEND",key,entry,action:plan.action,payload:plan.payload};
  }

  // G-MV22: one pure request-lease factory for both native SOURCE paths.
  // Previously the bridge duplicated ChannelId/M4 pending shape, silently
  // omitting the installed physical pin/format identity from both. The
  // bridge still owns queues, timers and the ONE native command dispatcher.
  function sourceQueueRequestLease({authority,requestId,plan,hardware,native}={}) {
    if(plan?.kind!=="SEND"||!plan.entry?.choice||
       !decimalId(plan.entry.epoch))return null;
    const {entry,payload,key}=plan,config=entry.hardwareConfiguration;
    if(config!==undefined&&
       (typeof config!=="string"||!config||
        config!==hardware?.hardwareConfiguration||
        (native?.hardwareConfiguration!==undefined&&
         config!==native.hardwareConfiguration)))return null;
    const common={requestId,choice:Object.freeze({...entry.choice}),
      ...(config!==undefined?{hardwareConfiguration:config}:{})};
    if(authority==="CHANNEL_ID") {
      if(typeof requestId!=="string"||!requestId||
         plan.action!=="routingNativeSourcePatch"||
         !decimalId(key)||payload?.channelId!==key||
         payload.deviceEpoch!==entry.epoch||
         !decimalId(native?.graph?.revision)||
         !decimalId(native?.graph?.rosterRevision)||
         payload.graphRevision!==native.graph.revision||
         payload.rosterRevision!==native.graph.rosterRevision)
        return null;
      return Object.freeze({...common,id:key,epoch:entry.epoch,
        beforeRevision:native.graph.revision,acked:false});
    }
    if(authority==="M4"){
      if(!Number.isSafeInteger(requestId)||requestId<=0||
         !Number.isInteger(key)||key<0||key>=10||
         plan.action!=="routingSourceSwitch"||
         payload?.channel!==key||!positiveCounter(entry.epoch)||
         payload.deviceEpoch!==Number(entry.epoch)||
         !Number.isSafeInteger(payload.revision)||payload.revision<=0)
        return null;
      return Object.freeze({...common,channel:key,
        epoch:payload.deviceEpoch,revision:payload.revision,acked:false});
    }
    return null;
  }

  function sourceAckResult(pending,ack,authority) {
    if(!pending||ack?.requestId!==pending.requestId)return "IGNORE";
    if(authority==="CHANNEL_ID") {
      if(ack?.ok!==true||ack.channelId!==pending.id||
         !decimalId(pending.beforeRevision)||!decimalId(ack.graphRevision))return "REJECTED";
      if(ack.sourceSwitchTicket!==undefined)
        return decimalId(ack.sourceSwitchTicket)&&ack.sourceSwitchTicket!=="0"&&
          BigInt(ack.sourceSwitchTicket)<=0xffffffffn&&
          ack.graphRevision===pending.beforeRevision&&
          ["MONO","STEREO"].includes(pending.choice?.mode)?"ACKED":"REJECTED";
      return BigInt(ack.graphRevision)>BigInt(pending.beforeRevision)?
        "ACKED":"REJECTED";
    }
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


  // MV-16: immutable ACK transition shared by ChannelId and M4. The
  // first correlated successful ACK wins its lease; contradictory/late
  // duplicates cannot overwrite the revision or discard installed PCM.
  function sourceQueueAckTransition({authority,pending,ack}={}) {
    if(!pending||ack?.requestId!==pending.requestId||
       pending.acked===true)return {kind:"IGNORE"};
    const verdict=sourceAckResult(pending,ack,authority);
    if(verdict==="IGNORE")return {kind:"IGNORE"};
    if(verdict!=="ACKED")return {kind:"REJECTED",pending:null};
    const ackRevision=authority==="CHANNEL_ID"?
      ack.graphRevision:String(ack.state.revision);
    const sourceSwitchTicket=authority==="CHANNEL_ID"?ack.sourceSwitchTicket:undefined;
    return {kind:"ACKED",pending:{...pending,acked:true,ackRevision,
      ...(sourceSwitchTicket!==undefined?{sourceSwitchTicket}:{})}};
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
    if(pending.sourceSwitchTicket!==undefined) {
      if(!decimalId(pending.sourceSwitchTicket)||pending.sourceSwitchTicket==="0"||
         BigInt(pending.sourceSwitchTicket)>0xffffffffn||
         pending.ackRevision!==pending.beforeRevision)return "PENDING";
      if(next.graph.revision!==pending.ackRevision)return "STALE";
      return channel.source?.appliedSwitchTicket===pending.sourceSwitchTicket&&
        installedSourceMatches("CHANNEL_ID",pending.choice,channel.source)?
        "CONFIRMED":"PENDING";
    }
    if(!decimalId(pending.beforeRevision)||
       !decimalId(pending.ackRevision)||
       !decimalId(next.graph.revision)||
       BigInt(pending.ackRevision)<=BigInt(pending.beforeRevision)||
       BigInt(next.graph.revision)<BigInt(pending.ackRevision))
      return "PENDING";
    return installedSourceMatches("CHANNEL_ID",pending.choice,channel.source)?
      "CONFIRMED":"PENDING";
  }


  // MV-13: M4's correlated acceptedRoute in an ACK is not an independently
  // published PCM snapshot. Hold exactly one GUI source transaction until
  // a subsequent M4 readback confirms the requested physical source and
  // the accepted routing revision. This is pure presentation settlement.
  function m4SourceReadbackSettlement(pending,snapshot) {
    if(!pending)return "NONE";
    const d=snapshot?.device,session=snapshot?.raw?.mixerSession,
      m=snapshot?.raw?.legacyM4;
    if(snapshot?.admitted!==false||snapshot?.retained===true||
       !Number.isSafeInteger(pending.epoch)||pending.epoch<=0||
       d?.epoch!==String(pending.epoch)||
       session?.deviceEpoch!==d.epoch)return "STALE";
    // The prepared first-source M4 producer initially publishes NONE, and
    // only acquires M4 authority once its first routing patch is installed.
    // NONE before that ACK/PCM publication is not a device change.
    if(session.authority==="NONE"&&m?.active===true&&
       m.routeAccepted===false)
      return pending.acked===true?"PENDING":"PENDING_ACK";
    if(session.authority!=="M4")return "STALE";
    const channels=session.channels;
    if(!Number.isInteger(pending.channel)||pending.channel<0||
       pending.channel>=10||!Array.isArray(channels)||
       channels.length!==10||channels[pending.channel]?.role!=="INPUT")
      return "STALE";
    if(pending.acked!==true)return "PENDING_ACK";
    const revision=session.routeRevision,ackRevision=pending.ackRevision;
    if(m?.active!==true||m.routeAccepted!==true||
       !positiveCounter(revision)||!positiveCounter(m.routingRevision)||
       revision!==m.routingRevision||!positiveCounter(ackRevision)||
       !Number.isSafeInteger(pending.revision)||
       BigInt(ackRevision)<=BigInt(pending.revision)||
       BigInt(revision)<BigInt(ackRevision))return "PENDING";
    return installedSourceMatches("M4",pending.choice,
      channels[pending.channel].source)?"CONFIRMED":"PENDING";
  }

  // MV-15: a single readback decision for both native SOURCE authorities.
  // Each physical graph retains its own strict format, revision, epoch and
  // pin verifier. A correlated ACK alone cannot confirm accepted PCM.
  function sourceQueueReadback({authority,pending,snapshot}={}) {
    if(pending?.hardwareConfiguration!==undefined&&
       pending.hardwareConfiguration!==snapshot?.hardwareConfiguration)
      return "STALE"; // same-epoch physical replacement cannot settle old ACK
    if(authority==="CHANNEL_ID")
      return sourceReadbackSettlement(pending,snapshot);
    if(authority==="M4")
      return m4SourceReadbackSettlement(pending,snapshot);
    return pending?"STALE":"NONE"; // no fallback to a third source owner
  }

  // MV-17: one fail-closed SOURCE timeout policy. Expiring a timer cannot
  // discard an ACKed request whose independently installed PCM snapshot
  // is already available. There is no implicit replay of an old pin draft.
  // The bridge remains the sole timer, transport and queue owner.
  function sourceQueueTimeoutDecision({authority,pending,requestId,snapshot}={}) {
    if((authority!=="CHANNEL_ID"&&authority!=="M4")||
       !pending||pending.requestId!==requestId)return "IGNORE";
    return sourceQueueReadback({authority,pending,snapshot})==="CONFIRMED"?
      "SETTLE":"REFRESH";
  }

  // G-MV26: one native-authority gate for physical MAIN gain and mute.
  // The host still accepts MAIN via its existing routingM4Control scalar
  // transport, but stale legacy M4 telemetry is NEVER enough to authorize
  // a new command after ChannelId takeover or loss of PCM admission.
  // Keep this pure: no GUI, ACK state, output arming, or graph mutation.
  function masterScalarAuthority(snapshot,{m4Verified=false,
      graphConnected=false}={}) {
    const epoch=snapshot?.device?.epoch,
      session=snapshot?.raw?.mixerSession,
      legacy=snapshot?.raw?.legacyM4;
    // MAIN host IPC uses safe integer M4 epoch/revision tokens, unlike
    // the separate uint64 ChannelId controller. Require real readback.
    if(!positiveCounter(epoch)||!positiveCounter(legacy?.routingRevision)||
       session?.deviceEpoch!==epoch||snapshot?.retained===true||
       !Number.isFinite(session?.main?.db)||
       typeof session?.main?.mute!=="boolean")return "UNAVAILABLE";
    if(session.authority==="CHANNEL_ID")
      return snapshot?.admitted===true && graphConnected===true?
        "CHANNEL_ID":"UNAVAILABLE";
    if(session.authority==="M4")
      return snapshot?.admitted===false && m4Verified===true &&
        legacy.active===true && legacy.routeAccepted===true &&
        session.routeRevision===legacy.routingRevision?
          "M4":"UNAVAILABLE";
    return "UNAVAILABLE";
  }

  // G-MV27: one-shot controls use a pure authority selector, not divergent
  // button/double-click branches. MV-10 gesture scheduling remains independent.
  function controlCommandPlan({operation,trackKind,logical=false,
    logicalReadOnly=false,logicalPending=false,logicalChannelId=null,
    logicalSupported=false,authority="",admitted=false,graphConnected=false,
    nativeChannelId=null,nativeSupported=false,m4Verified=false,
    m4Channel=-1,masterAuthority="UNAVAILABLE"}={}) {
    if(!["SET_FADER","SET_PAN","SET_MUTE","SET_SOLO"].includes(operation))
      return "BLOCK";
    if(logical)return logicalReadOnly!==true && logicalPending!==true &&
      decimalId(logicalChannelId) && logicalSupported===true?"LOGICAL":"BLOCK";
    // The physical MAIN host gateway owns its gain/mute even under ChannelId.
    // Never fall through to ChannelId SET_FADER or SET_MUTE for MAIN.
    if(trackKind==="master")
      return ["SET_FADER","SET_MUTE"].includes(operation) &&
        ["CHANNEL_ID","M4"].includes(masterAuthority)?"MAIN_HOST":"BLOCK";
    if(!trackKind)return "BLOCK";
    if(authority==="M4")
      return trackKind==="audio" && m4Verified===true &&
        Number.isInteger(m4Channel) && m4Channel>=0 && m4Channel<10?
          "M4":"BLOCK";
    if(authority==="CHANNEL_ID")
      return admitted===true && graphConnected===true &&
        decimalId(nativeChannelId) && nativeSupported===true?
          "CHANNEL_ID":"BLOCK";
    return "BLOCK";
  }

  // G-MV29: one pure revision-scoped scalar identity. M4 gestures belong
  // to an exact accepted routing generation. ChannelId gestures belong to an
  // exact roster generation; physical MAIN additionally uses its existing
  // M4 host routing generation. Offline .smix has no physical lease.
  // This only describes UI control lifetime; native still owns all PCM.
  function scalarNativeIdentity({logical=false,epoch,authority,channelId=null,
    hardwareConfiguration,master=false,routingRevision,rosterRevision}={}) {
    if(logical)return Object.freeze({epoch:"LOGICAL",authority:"LOGICAL",channelId});
    if(!decimalId(epoch)||!["CHANNEL_ID","M4"].includes(authority)||
       typeof hardwareConfiguration!=="string"||!hardwareConfiguration||
       (authority==="CHANNEL_ID"&&!master&&!decimalId(channelId)))return null;
    const routeOwned=master||authority==="M4";
    const rosterOwned=authority==="CHANNEL_ID";
    if((routeOwned&&!positiveCounter(routingRevision))||
       (rosterOwned&&rosterRevision!=="0"&&!decimalId(rosterRevision)))
      return null; // no physical command without its installed generation
    return Object.freeze({epoch,authority,channelId,hardwareConfiguration,
      ...(routeOwned?{routeRevision:routingRevision}:{}),
      ...(rosterOwned?{rosterRevision}:{} )});
  }

  // MV-9: pure Channel Strip scalar gesture/readback policy. The only
  // pending Maps, native events, timers and audio transport stay in the
  // approved bridge. Treat exact decimal-string ChannelIds as opaque data.
  function scalarIdentityMatches(edit,identity) {
    return !!edit && !!identity &&
      edit.epoch===identity.epoch &&
      edit.authority===identity.authority &&
      edit.channelId===identity.channelId &&
      edit.hardwareConfiguration===identity.hardwareConfiguration &&
      edit.routeRevision===identity.routeRevision &&
      edit.rosterRevision===identity.rosterRevision;
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
      channelId:edit.channelId,
      hardwareConfiguration:edit.hardwareConfiguration,
      ...(edit.routeRevision!==undefined?{routeRevision:edit.routeRevision}:{}),
      ...(edit.rosterRevision!==undefined?{rosterRevision:edit.rosterRevision}:{})};
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

  // MV-11: identity is ONLY a native-owned cosmetic document, independent
  // of the PCM graph. The bridge owns pending requests and storage cleanup;
  // these pure selectors own the validation, intent and settlement policy.
  function identityReadback(snapshot,valid) {
    const appearance=snapshot?.raw?.channelAppearance;
    const channels=snapshot?.graph?.channels;
    if(!appearance||appearance.schema!=="arsonmix.native.channel-appearance.v1"||
       !Array.isArray(appearance.entries)||!Array.isArray(channels)||
       !valid?.decimal||valid.decimal(appearance.revision)===null||
       typeof appearance.readOnly!=="boolean")return {kind:"INVALID"};
    const live=new Set(channels.map(ch=>ch?.id)),seen=new Set(),entries=[];
    if(live.size!==channels.length||appearance.entries.length>512)return {kind:"INVALID"};
    for(const p of appearance.entries){
      if(!p||!decimalId(p.id)||!live.has(p.id)||seen.has(p.id)||
         !valid.validPresentationName(p.name)||
         !valid.validPresentationColor(p.color)||
         !valid.validPresentationIcon(p.icon))return {kind:"INVALID"};
      seen.add(p.id);
      entries.push({id:p.id,name:p.name,color:p.color,icon:p.icon});
    }
    return {kind:"VALID",revision:appearance.revision,
      writable:!appearance.readOnly,entries};
  }
  function identityEditPlan({native,logical,channelId,rawName,color,icon,
                             appearance,appearanceRevision,appearanceWritable,
                             pending=false,valid}={}) {
    const no={kind:"UNAVAILABLE"};
    const name=String(rawName??"").trim().replace(/\s+/g," ");
    if(!decimalId(channelId))return no;
    if(!valid?.validPresentationName?.(name))return {kind:"INVALID_NAME"};
    const current=logical||native;
    const channels=current?.graph?.channels;
    if(!Array.isArray(channels)||channels.filter(ch=>ch?.id===channelId).length!==1)
      return no;
    const names=channels.filter(ch=>ch.id!==channelId).map(ch=>
      logical?ch.name:(appearance?.get?.(ch.id)?.name||ch.name));
    if(names.some(other=>typeof other==="string"&&
       other.toLowerCase()===name.toLowerCase()))return {kind:"DUPLICATE"};
    if(logical) {
      if(logical.readOnly===true||pending)return no;
      return {kind:"LOGICAL",channelId,name,operation:"RENAME_CHANNEL",
        payload:{name}};
    }
    if(pending)return {kind:"BUSY"};
    if(native?.raw?.mixerSession?.authority!=="CHANNEL_ID"||
       !(native.admitted===true||native.retained===true)||
       appearanceWritable!==true||typeof valid?.decimal!=="function"||
       valid.decimal(appearanceRevision)===null||
       !decimalId(native.device?.epoch)||!decimalId(native.graph?.revision)||
       !decimalId(native.graph?.rosterRevision))return no;
    if(!valid.validPresentationColor?.(color)||
       !valid.validPresentationIcon?.(icon))return {kind:"INVALID_FIELDS"};
    const payload={channelId,name,color,icon,appearanceRevision,
      deviceEpoch:native.device.epoch,graphRevision:native.graph.revision,
      rosterRevision:native.graph.rosterRevision};
    return {kind:"READY",payload,pending:{
      channelId,name,color,icon,acked:false,
      deviceEpoch:payload.deviceEpoch,graphRevision:payload.graphRevision,
      rosterRevision:payload.rosterRevision,
      beforeAppearanceRevision:appearanceRevision}};
  }
  function identityAckResult(pending,ack) {
    if(!pending||ack?.requestId!==pending.requestId)return "IGNORE";
    return ack.ok===true?"ACKED":"REJECTED";
  }
  function identitySettlement(pending,snapshot,projection) {
    if(!pending)return "NONE";
    if(snapshot?.device?.epoch!==pending.deviceEpoch||
       snapshot?.graph?.revision!==pending.graphRevision||
       snapshot?.graph?.rosterRevision!==pending.rosterRevision||
       snapshot?.raw?.mixerSession?.authority!=="CHANNEL_ID"||
       !(snapshot?.admitted===true||snapshot?.retained===true)||
       !snapshot.graph.channels?.some(ch=>ch.id===pending.channelId))
      return "STALE";
    const validRevision=x=>x==="0"||decimalId(x);
    if(projection?.kind!=="VALID"||
       !validRevision(projection.revision)||
       !validRevision(pending.beforeAppearanceRevision))return "WAIT";
    if(BigInt(projection.revision)<BigInt(pending.beforeAppearanceRevision))
      return "STALE";
    if(pending.acked!==true)return "WAIT";
    const matches=projection.entries.filter(p=>p.id===pending.channelId);
    return matches.length===1&&matches[0].name===pending.name&&
       matches[0].color===pending.color&&
       matches[0].icon===pending.icon?"CONFIRMED":"WAIT";
  }
  function legacyIdentityDecision({snapshot,id,identity,appearance,deferred,
                                   writable,pending,valid}={}) {
    if(!writable||pending||!(snapshot?.admitted||snapshot?.retained)||
       snapshot?.raw?.mixerSession?.authority!=="CHANNEL_ID"||
       !snapshot.graph?.channels?.some(ch=>ch.id===id)||
       deferred?.has?.(id))return {kind:"SKIP"};
    const original=appearance?.get?.(id);
    if(original)return original.name===identity?.name&&
      original.color===identity?.color&&original.icon===identity?.icon?
        {kind:"RETIRE"}:{kind:"DEFER"};
    const plan=identityEditPlan({native:snapshot,channelId:id,
      rawName:identity?.name,color:identity?.color,icon:identity?.icon,
      appearance,appearanceRevision:snapshot?.raw?.channelAppearance?.revision,
      appearanceWritable:writable,valid});
    if(plan.kind==="DUPLICATE")return {kind:"CONFLICT"};
    return plan.kind==="READY"?{kind:"SEND",plan}:{kind:"DEFER"};
  }

  // G-MV37: the only M/ST decision across saved, ChannelId and M4.
  // UI proposes a change, never mutates accepted PCM before native readback.
  function processingFormatPlan(snapshot,{authority,channelId,channel,
    hardwareConfiguration,verifiedM4=false,deviceReady=false,
    pending=false,sourcePending=false}={}) {
    const bad={kind:"UNAVAILABLE"},graph=snapshot?.graph;
    if(pending||snapshot?.retained===true)return bad;
    if(authority==="LOGICAL"){
      if(snapshot?.logical!==true||snapshot?.admitted!==false||
         snapshot?.readOnly===true||
         typeof snapshot.workspaceToken!=="string"||!snapshot.workspaceToken||
         !decimalId(channelId)||!Array.isArray(graph?.channels)||
         !graph.capabilities?.supportedOps?.includes("SET_PROCESSING_FORMAT"))
        return bad;
      const rows=graph.channels.filter(ch=>ch?.id===channelId);
      if(rows.length!==1||rows[0].role!=="INPUT"||
         !["MONO","STEREO"].includes(rows[0].processingFormat))return bad;
      const target=rows[0].processingFormat==="MONO"?"STEREO":"MONO";
      return {kind:"READY",authority,operation:"SET_PROCESSING_FORMAT",
        channelId,payload:{format:target},target};
    }
    if(typeof hardwareConfiguration!=="string"||!hardwareConfiguration)
      return bad;
    if(authority==="CHANNEL_ID"){
      const session=snapshot?.raw?.mixerSession;
      if(snapshot?.admitted!==true||snapshot?.logical===true||
         session?.authority!=="CHANNEL_ID"||
         session.deviceEpoch!==snapshot.device?.epoch||
         !decimalId(snapshot.device?.epoch)||
         !decimalId(graph?.revision)||!decimalId(graph?.rosterRevision)||
         graph.capabilities?.callbackOwnsChannelIds!==true||
         !graph.capabilities?.supportedOps?.includes("SET_PROCESSING_FORMAT")||
         !decimalId(channelId)||!Array.isArray(graph?.channels))return bad;
      const rows=graph.channels.filter(ch=>ch?.id===channelId);
      if(rows.length!==1||rows[0].role!=="INPUT"||
         !["MONO","STEREO"].includes(rows[0].processingFormat))return bad;
      const target=rows[0].processingFormat==="MONO"?"STEREO":"MONO";
      return {kind:"READY",authority,operation:"SET_PROCESSING_FORMAT",
        channelId,payload:{format:target},target};
    }
    if(authority==="M4"){
      const current=snapshot?.raw?.legacyM4?.channels?.[channel]?.channelFormat;
      if(!["MONO","STEREO"].includes(current))return bad;
      const target=current==="MONO"?"STEREO":"MONO";
      const plan=m4ChannelPatchPlan(snapshot,{
        operation:"SET_PROCESSING_FORMAT",channel,target,
        verifiedM4,deviceReady,pending,sourcePending});
      if(plan.kind!=="READY")return bad;
      return {kind:"READY",authority,operation:"SET_PROCESSING_FORMAT",
        channel,target,action:plan.action,payload:plan.payload};
    }
    return bad;
  }
  return Object.freeze({processingFormatPlan,outputTargets,inputChoices,externalChoices,
    primaryChoices,resolvePrimarySelection,readbackBindings,externalPending,
    primaryOutMutation,externalOutputPlan,rosterMutationPlan,
    rosterAckResult,rosterReadbackSettlement,m4ChannelPatchPlan,
    m4ChannelPatchAckResult,m4ChannelPatchSettlement,sourceChoiceMatches,sourceQueueSelection,
    sourceDispatchPlan,sourceQueueHead,sourceQueueAlreadyInstalled,
    sourceQueueRequestLease,
    sourceAckResult,sourceReadbackSettlement,
    m4SourceReadbackSettlement,sourceQueueReadback,sourceQueueAckTransition,
    sourceQueueTimeoutDecision,
    masterScalarAuthority,controlCommandPlan,scalarNativeIdentity,scalarIdentityMatches,scalarVisualSettlement,m4ScalarAckResult,
    m4ScalarReadbackSettlement,scalarDispatchLease,
    scalarGestureFlushPlan,scalarDispatchPlan,
    identityReadback,identityEditPlan,
    identityAckResult,identitySettlement,legacyIdentityDecision});
});
