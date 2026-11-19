// SPDX-License-Identifier: AGPL-3.0-only
// GUI-2: pure FX Rack readback projection for the ONE approved /preview/ GUI.
// No DOM, timer, native dispatch, plugin lifecycle or accepted audio authority.
// Only the already-validated native snapshot may supply these values.
(function (root, factory) {
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  if(root)root.ArSonApprovedFxRackViewModel=api;
})(typeof globalThis==="object"?globalThis:undefined,function () {
  "use strict";

  // null means missing or incomplete readback, never a guess based on CSS or
  // an optimistic click. Channel-local, global and insert bypass are distinct.
  function powerReadback(channel,globalFxBypassed,channelName) {
    if(typeof channel?.fxRackBypassed!=="boolean" ||
       !Array.isArray(channel.inserts) ||
       typeof globalFxBypassed!=="boolean")return null;
    const enabled=!channel.fxRackBypassed,globalEnabled=!globalFxBypassed;
    const name=String(channelName??"");
    const slots=[];
    for(let index=0;index<channel.inserts.length;index++) {
      const insert=channel.inserts[index];
      if(!insert || typeof insert.id!=="string" || !insert.id ||
         typeof insert.bypassed!=="boolean")return null;
      const effective=enabled&&globalEnabled&&!insert.bypassed;
      const action=insert.bypassed?"Enable":"Bypass";
      const latencyTitle=insert.latencyChanged===true?
        "Plugin delay changed · aligned dry signal retained; reload the plugin before using its new delay":
        Number.isInteger(insert.latencySamples)?
          "Plugin delay: "+insert.latencySamples+" samples · compensated":null;
      slots.push({
        index,insertId:insert.id,bypassed:insert.bypassed,effective,
        latencyTitle,
        pressed:String(effective),
        title:action+" only insert "+(index+1),
        label:action+" only FX insert "+(index+1)+" in "+name
      });
    }
    return {
      enabled,globalEnabled,chainBypassed:!enabled||!globalEnabled,
      headerPressed:String(enabled&&globalEnabled),
      headerTitle:(enabled?"Bypass":"Enable")+" all channel FX",
      headerLabel:(enabled?"Bypass":"Enable")+" entire FX chain in "+name,
      slots
    };
  }

  // A mounted strip belongs to a specific native FX instance and order.
  // Check the FULL roster before the bridge writes any slot or button.
  // An absent/duplicate/wrong slot or missing power control forces the
  // existing authoritative structural re-projection instead of wrong-FX UI.
  function mountedRackMatches(readback,mountedSlots,hasHeader) {
    if(!readback || !hasHeader || !Array.isArray(mountedSlots) ||
       mountedSlots.length!==readback.slots.length)return false;
    const byIndex=new Map();
    for(const mounted of mountedSlots) {
      if(!Number.isInteger(mounted?.index) ||
         byIndex.has(mounted.index))return false;
      byIndex.set(mounted.index,mounted);
    }
    return readback.slots.every(slot=>{
      const mounted=byIndex.get(slot.index);
      return mounted?.insertId===slot.insertId && mounted?.hasPower===true;
    });
  }


  // M4 inventory and catalog names are UI metadata, never an installed
  // ChannelId processor graph or permission to resurrect a retired instance.
  function m4Presentation(instances,catalog,acceptedRack) {
    if(!Array.isArray(instances))return null;
    const slots=[];
    for(let index=0;index<8;index++){
      const live=instances[index];
      if(live!=null &&
         (typeof live.instanceId!=="string"||!live.instanceId||
          typeof live.catalogId!=="string"||!live.catalogId||
          typeof live.bypassed!=="boolean"))return null;
      const name=live?(catalog?.get?.(live.catalogId)?.name||"VST3 Loaded"):"None";
      slots.push({index,name,bypassed:live?.bypassed===true,
        instance:live?{id:live.instanceId,name}:null});
    }
    return {fxOn:typeof acceptedRack==="boolean"?!acceptedRack:null,slots};
  }
  // ACK alone is not installed PCM proof. Correlate channel, insert lease,
  // physical device epoch and subsequent accepted readback.
  function nativePowerSettlement(pending,snapshot) {
    if(!pending)return "NONE";
    if(pending.epoch!==undefined &&
       snapshot?.device?.epoch!==pending.epoch)return "STALE";
    if(pending.acked!==true)return "WAIT";
    const channels=snapshot?.graph?.channels;
    if(!Array.isArray(channels))return "WAIT";
    const channel=channels.find(ch=>ch.id===pending.channelId);
    if(!channel)return "WAIT";
    const value=pending.kind==="rack"?channel.fxRackBypassed:
      pending.kind==="insert"?
        channel.inserts?.find(i=>i.id===pending.insertId)?.bypassed:
        undefined;
    return typeof value==="boolean"&&value===pending.bypassed?
      "CONFIRMED":"WAIT";
  }
  // G-MV28: the two M4 FX power buttons share a *single* pure
  // admission/transaction plan. Legacy FX inventory is not permission to
  // control a retired driver, M4 route or ChannelId-owned PCM.
  // M4 host IPC uses JS-safe integer epoch/revision numbers. Never round
  // the v6 uint64 identity in this compatibility adapter.
  function safeM4Token(x) {
    if(typeof x!=="string"||!/^[1-9][0-9]*$/.test(x))return null;
    const n=Number(x);
    return Number.isSafeInteger(n)&&n>0? n:null;
  }
  function m4PowerPlan(snapshot,{action,channel,slot=null,
    verifiedM4=false,offline=false,pending=false}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const s=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4,
      epoch=safeM4Token(snapshot?.device?.epoch),
      revision=safeM4Token(m?.routingRevision);
    if(pending||offline||verifiedM4!==true||
       snapshot?.admitted!==false||snapshot?.retained===true||
       s?.authority!=="M4"||m?.active!==true||m?.routeAccepted!==true||
       m.nativeBusStructural===true||
       epoch===null||revision===null||
       s.deviceEpoch!==snapshot.device.epoch||
       s.routeRevision!==m.routingRevision||
       !Array.isArray(s.channels)||s.channels.length!==10||
       !Number.isInteger(channel)||channel<0||channel>=10||
       !s.channels[channel])return unavailable;
    const lease={channel,epoch:s.deviceEpoch,
      revision:s.routeRevision,acked:false};
    if(action==="fxOn") {
      const row=s.channels[channel];
      if(typeof row.fxRackBypassed!=="boolean"||slot!==null)
        return unavailable;
      const bypassed=!row.fxRackBypassed;
      return {kind:"READY",action:"routingFxRackBypass",key:channel,
        timeoutMs:8000,
        pending:{...lease,bypassed},
        payload:{channel,deviceEpoch:epoch,revision,value:bypassed}};
    }
    if(action!=="slotPower"||!Number.isInteger(slot)||
       slot<0||slot>=8)return unavailable;
    const instances=m.fxInstances?.[channel],live=instances?.[slot];
    if(!Array.isArray(instances)||
       typeof live?.instanceId!=="string"||!live.instanceId||
       typeof live.catalogId!=="string"||!live.catalogId||
       typeof live.bypassed!=="boolean")return unavailable;
    const bypass=!live.bypassed;
    return {kind:"READY",action:"routingFxBypass",
      key:channel+":"+slot,timeoutMs:5000,
      pending:{...lease,slot,token:live.instanceId,bypass},
      payload:{channel,slot,instanceId:live.instanceId,
        deviceEpoch:epoch,revision,value:bypass}};
  }
  function m4RackAckResult(pending,ack) {
    if(!pending||!ack||ack.requestId!==pending.requestId)return "IGNORE";
    return ack.ok===true&&ack.channel===pending.channel&&
      String(ack.deviceEpoch)===pending.epoch&&
      String(ack.routingRevision)===pending.revision&&
      ack.bypassed===pending.bypassed?"ACKED":"REJECTED";
  }
  function m4RackSettlement(pending,snapshot) {
    if(!pending)return "NONE";
    const s=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4;
    if(snapshot?.admitted!==false||snapshot?.retained===true||
       s?.authority!=="M4"||m?.active!==true||
       m.routeAccepted!==true||
       snapshot?.device?.epoch!==pending.epoch||
       s.deviceEpoch!==pending.epoch||
       s.routeRevision!==pending.revision||
       m.routingRevision!==pending.revision||
       !Number.isInteger(pending.channel)||
       pending.channel<0||pending.channel>=10||
       !Array.isArray(s.channels)||s.channels.length!==10)
      return "STALE";
    return pending.acked===true&&
      s.channels[pending.channel]?.fxRackBypassed===pending.bypassed?
        "CONFIRMED":"WAIT";
  }

  // MV-8: do not invent JS-owned FX identities or interpret a catalog key
  // as an instance lease. These plans may only target admitted native PCM.
  const VALID_ROLES=new Set(["INPUT","GROUP","FX_RETURN","MAIN"]);
  function channelId(value) {
    if(typeof value!=="string"||!/^[1-9][0-9]*$/.test(value))return false;
    try{return BigInt(value)<=18446744073709551615n;}catch{return false;}
  }
  function rosterUnique(graph) {
    if(!Array.isArray(graph?.channels))return false;
    const seen=new Set();
    for(const ch of graph.channels) {
      if(!Array.isArray(ch?.inserts))return false;
      for(const fx of ch.inserts) {
        if(typeof fx?.id!=="string"||!fx.id||seen.has(fx.id))return false;
        seen.add(fx.id);
      }
    }
    return true;
  }
  function nativeInsertPlan(snapshot,{channelId:id,slot,catalogId,pending=false}={}) {
    const graph=snapshot?.graph;
    if(pending||snapshot?.admitted!==true||snapshot?.retained===true||
       !channelId(id)||typeof catalogId!=="string"||!catalogId||
       !Number.isInteger(slot)||slot<0||slot>=8||
       !graph?.capabilities?.supportedOps?.includes("LOAD_INSERT")||
       !rosterUnique(graph))return {kind:"UNAVAILABLE"};
    const matches=graph.channels.filter(ch=>ch.id===id);
    if(matches.length!==1||!VALID_ROLES.has(matches[0].role)||
       matches[0].inserts.length!==slot)return {kind:"UNAVAILABLE"};
    return {kind:"READY",channelId:id,payload:{catalogId,slot}};
  }
  function nativeEditPlan(snapshot,{channelId:id,slot,instanceId,
                           move=false,destinationId=null,position=null}={}) {
    const graph=snapshot?.graph;
    // A MOVE with a lost destination must NEVER become a REMOVE.
    if(typeof move!=="boolean"||
       (!move&&(destinationId!==null||position!==null)))
      return {kind:"UNAVAILABLE"};
    const operation=move?"MOVE_INSERT":"REMOVE_INSERT";
    if(snapshot?.admitted!==true||snapshot?.retained===true||
       !channelId(id)||!Number.isInteger(slot)||slot<0||slot>=8||
       typeof instanceId!=="string"||!instanceId||
       !graph?.capabilities?.supportedOps?.includes(operation)||
       !rosterUnique(graph))return {kind:"UNAVAILABLE"};
    const sources=graph.channels.filter(ch=>ch.id===id);
    if(sources.length!==1||!VALID_ROLES.has(sources[0].role)||
       sources[0].inserts[slot]?.id!==instanceId)
      return {kind:"UNAVAILABLE"};
    const payload={slot,instanceId};
    if(move){
      if(!channelId(destinationId)||!Number.isInteger(position))return {kind:"UNAVAILABLE"};
      const targets=graph.channels.filter(ch=>ch.id===destinationId);
      if(targets.length!==1||!VALID_ROLES.has(targets[0].role))
        return {kind:"UNAVAILABLE"};
      const same=destinationId===id,max=targets[0].inserts.length-(same?1:0);
      if(position<0||position>max||(!same&&max>=8))
        return {kind:"UNAVAILABLE"};
      if(same&&slot===position)return {kind:"NO_CHANGE"};
      payload.toChannelId=destinationId;
      payload.toSlot=position;
    }
    return {kind:"READY",operation,channelId:id,payload};
  }
  // createController is the ONLY source of matched ACK + independent
  // readback. We additionally check exact editor lease before navigation.
  function nativeInsertSettlement(pending,snapshot,confirmed){
    if(!pending||pending.authority!=="CHANNEL_ID")return {kind:"NONE"};
    if(snapshot?.device?.epoch!==pending.epoch)return {kind:"STALE"};
    if(!confirmed||confirmed.operation!=="LOAD_INSERT"||
       confirmed.requestId!==pending.requestId)return {kind:"WAIT"};
    if(snapshot?.admitted!==true||snapshot.retained===true||
       confirmed.targetChannelId!==pending.channelId||
       confirmed.payload?.catalogId!==pending.id||
       confirmed.payload?.slot!==pending.slot||
       !channelId(pending.channelId)||!rosterUnique(snapshot.graph))
      return {kind:"WAIT"};
    const ch=snapshot.graph.channels.filter(c=>c.id===pending.channelId);
    const fx=ch.length===1?ch[0].inserts[pending.slot]:null;
    if(!fx||fx.catalogId!==pending.id||
       typeof fx.id!=="string"||!fx.id)return {kind:"WAIT"};
    return {kind:"CONFIRMED",channelId:pending.channelId,
      slot:pending.slot,catalogId:fx.catalogId,instanceId:fx.id,name:fx.name};
  }
  function nativePowerPlan(snapshot,{channelId:id,action,slot,pending=false}={}){
    const graph=snapshot?.graph;
    if(pending||snapshot?.admitted!==true||snapshot?.retained===true||
       !channelId(id)||!Array.isArray(graph?.channels)||!rosterUnique(graph))
      return {kind:"UNAVAILABLE"};
    const matches=graph.channels.filter(ch=>ch.id===id);
    if(matches.length!==1)return {kind:"UNAVAILABLE"};
    const ch=matches[0],rack=action==="fxOn";
    if(!rack&&action!=="slotPower")return {kind:"UNAVAILABLE"};
    const insert=rack?null:ch.inserts?.[slot];
    if(rack?typeof ch.fxRackBypassed!=="boolean":
       !Number.isInteger(slot)||slot<0||slot>=8||
       typeof insert?.id!=="string"||!insert.id||
       typeof insert.bypassed!=="boolean")return {kind:"UNAVAILABLE"};
    const bypassed=rack?!ch.fxRackBypassed:!insert.bypassed;
    return {kind:"READY",action:rack?"routingNativeFxRackBypass":
      "routingNativeFxInsertBypass",
      pending:{channelId:id,kind:rack?"rack":"insert",
        insertId:insert?.id,bypassed,epoch:snapshot.device?.epoch,acked:false},
      payload:{channelId:id,deviceEpoch:snapshot.device?.epoch,
        revision:graph.revision,value:bypassed,
        ...(rack?{}:{slot,insertId:insert.id})}};
  }
  function nativePowerAckResult(pending,ack){
    if(!pending||!ack||ack.requestId!==pending.requestId)return "IGNORE";
    if(ack.ok!==true||ack.channelId!==pending.channelId||
       ack.bypassed!==pending.bypassed||
       (ack.insertId!==undefined&&
        pending.kind==="insert"&&ack.insertId!==pending.insertId))
      return "REJECTED";
    return "ACKED";
  }


  // MV-9B: global FX is a single native atomic scalar. This ViewModel
  // decides eligibility and ACK/readback settlement but never owns DSP state.
  // ALWAYS use the original controller's normalized accepted snapshot.
  function positiveId(value){
    if(typeof value!=="string"||!/^[1-9][0-9]*$/.test(value))return false;
    try{return BigInt(value)<=18446744073709551615n;}catch{return false;}
  }
  function globalPowerContext(snapshot,verifiedM4=false,offline=false){
    const session=snapshot?.raw?.mixerSession,device=snapshot?.device;
    if(offline||snapshot?.retained===true||!positiveId(device?.epoch)||
       session?.deviceEpoch!==device.epoch)return null;
    if(snapshot.admitted===true&&session.authority==="CHANNEL_ID"&&
       snapshot.graph?.capabilities?.callbackOwnsChannelIds===true&&
       positiveId(snapshot.graph.revision))
      return {authority:"CHANNEL_ID",epoch:device.epoch,
        revision:snapshot.graph.revision};
    if(snapshot.admitted===false&&verifiedM4===true&&
       session.authority==="M4"&&positiveId(session.routeRevision)&&
       session.routeRevision===snapshot.raw?.legacyM4?.routingRevision)
      return {authority:"M4",epoch:device.epoch,revision:session.routeRevision};
    return null;
  }
  function globalPowerPlan(snapshot,context,pending=false){
    const actual=snapshot?.raw?.globalFxBypassed;
    if(pending||!context||typeof actual!=="boolean")return {kind:"UNAVAILABLE"};
    return {kind:"READY",bypassed:!actual,
      action:"routingGlobalFxBypass",
      payload:{deviceEpoch:context.epoch,revision:context.revision,value:!actual}};
  }
  function globalPowerAckResult(pending,ack){
    if(!pending||!ack||pending.requestId!==ack.requestId)return "IGNORE";
    return ack.ok===true&&ack.bypassed===pending.bypassed?"ACKED":"REJECTED";
  }
  function globalPowerSettlement(pending,snapshot,context){
    if(!pending)return "NONE";
    if(!context||pending.epoch!==context.epoch||
       pending.revision!==context.revision||
       pending.authority!==context.authority)return "STALE";
    if(pending.acked!==true)return "WAIT";
    return typeof snapshot?.raw?.globalFxBypassed==="boolean"&&
      snapshot.raw.globalFxBypassed===pending.bypassed?"CONFIRMED":"WAIT";
  }
  // M4 compatibility FX slot ACK is NOT readback. Keep the per-slot lease
  // locked until independent accepted M4 snapshot proves its exact token.
  function m4SlotPowerAckResult(pending,ack){
    if(!pending||!ack||pending.requestId!==ack.requestId)return "IGNORE";
    if(ack.channel!==pending.channel||ack.slot!==pending.slot||
       ack.ok!==true)return "REJECTED";
    return "ACKED";
  }
  function m4SlotPowerSettlement(pending,snapshot){
    if(!pending)return "NONE";
    const session=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4;
    if(snapshot?.retained===true||snapshot?.admitted===true||
       session?.authority!=="M4"||
       snapshot?.device?.epoch!==pending.epoch||
       session.deviceEpoch!==pending.epoch||
       session.routeRevision!==pending.revision||
       m?.routingRevision!==pending.revision)return "STALE";
    const slots=m.fxInstances?.[pending.channel];
    const actual=Array.isArray(slots)?slots[pending.slot]:null;
    if(actual?.instanceId!==pending.token)return "STALE";
    if(pending.acked!==true)return "WAIT";
    return actual?.bypassed===pending.bypass?"CONFIRMED":"WAIT";
  }

  return Object.freeze({powerReadback,mountedRackMatches,
    m4Presentation,nativePowerSettlement,m4PowerPlan,m4RackAckResult,
    m4RackSettlement,
    nativeInsertPlan,nativeEditPlan,nativeInsertSettlement,
    nativePowerPlan,nativePowerAckResult,
    globalPowerContext,globalPowerPlan,globalPowerAckResult,
    globalPowerSettlement,m4SlotPowerAckResult,m4SlotPowerSettlement});
});
