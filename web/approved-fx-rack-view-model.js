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
  function m4RackSettlement(pending,session) {
    if(!pending)return "NONE";
    if(session?.authority!=="M4"||
       session.deviceEpoch!==pending.epoch||
       session.routeRevision!==pending.revision)return "STALE";
    return pending.acked===true&&
      session.channels?.[pending.channel]?.fxRackBypassed===pending.bypassed?
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
                           destinationId=null,position=null}={}) {
    const graph=snapshot?.graph,move=destinationId!==null;
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

  return Object.freeze({powerReadback,mountedRackMatches,
    m4Presentation,nativePowerSettlement,m4RackSettlement,
    nativeInsertPlan,nativeEditPlan,nativeInsertSettlement,
    nativePowerPlan,nativePowerAckResult});
});
