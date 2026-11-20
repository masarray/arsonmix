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
  function nativePowerSettlement(pending,snapshot,hardwareConfiguration) {
    if(!pending)return "NONE";
    if(pending.epoch!==undefined &&
       snapshot?.device?.epoch!==pending.epoch)return "STALE";
    // A same-epoch hardware remap or changed ChannelId roster retires this
    // gesture before ACK or even a matching bypass value may be accepted.
    if(!physicalFxHardwareMatches(pending,hardwareConfiguration)||
       (Object.prototype.hasOwnProperty.call(pending,"hardwareConfiguration")&&
        (!nativePhysicalFxReady(snapshot,hardwareConfiguration)||
         snapshot.graph.rosterRevision!==pending.rosterRevision)))
      return "STALE";
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
  // G-MV33: ONE immutable-readback physical M4 FX admission core.
  // The bridge provides the existing protocol's canonical hardware fingerprint;
  // no second device model, FX host, WebView controller or IPC dispatcher.
  function m4FxHost(snapshot,{channel,slot=null,verifiedM4=false,
    offline=false,pending=false,hardwareConfiguration}={}) {
    const s=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4;
    const epoch=safeM4Token(snapshot?.device?.epoch),
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
       !s.channels[channel]||
       (slot!==null&&(!Number.isInteger(slot)||slot<0||slot>=8))||
       (hardwareConfiguration!==undefined&&
        (typeof hardwareConfiguration!=="string"||!hardwareConfiguration)))
      return null;
    return {s,m,epoch,revision,hardwareConfiguration:
      hardwareConfiguration===undefined?null:hardwareConfiguration};
  }
  function m4FxHardwareLease(host){
    return host.hardwareConfiguration===null?{}:
      {hardwareConfiguration:host.hardwareConfiguration};
  }
  function physicalFxHardwareMatches(pending,hardwareConfiguration){
    return !Object.prototype.hasOwnProperty.call(pending,"hardwareConfiguration")||
      (typeof hardwareConfiguration==="string"&&
       pending.hardwareConfiguration===hardwareConfiguration);
  }
  function m4PowerPlan(snapshot,{action,channel,slot=null,
    verifiedM4=false,offline=false,pending=false,hardwareConfiguration}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const host=m4FxHost(snapshot,{channel,slot,verifiedM4,offline,pending,
      hardwareConfiguration});
    if(!host)return unavailable;
    const {s,m,epoch,revision}=host;
    const lease={channel,epoch:s.deviceEpoch,revision:s.routeRevision,
      acked:false,...m4FxHardwareLease(host)};
    if(action==="fxOn"){
      const row=s.channels[channel];
      if(typeof row.fxRackBypassed!=="boolean"||slot!==null)
        return unavailable;
      const bypassed=!row.fxRackBypassed;
      return {kind:"READY",action:"routingFxRackBypass",key:channel,
        timeoutMs:8000,pending:{...lease,bypassed},
        payload:{channel,deviceEpoch:epoch,revision,value:bypassed}};
    }
    if(action!=="slotPower"||!Number.isInteger(slot))return unavailable;
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
    if(!pending||!ack||ack.requestId!==pending.requestId||
       pending.acked===true)return "IGNORE";
    return ack.ok===true&&ack.channel===pending.channel&&
      String(ack.deviceEpoch)===pending.epoch&&
      String(ack.routingRevision)===pending.revision&&
      ack.bypassed===pending.bypassed?"ACKED":"REJECTED";
  }
  function m4RackSettlement(pending,snapshot,hardwareConfiguration) {
    if(!pending)return "NONE";
    const s=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4;
    if(snapshot?.admitted!==false||snapshot?.retained===true||
       s?.authority!=="M4"||m?.active!==true||
       m.routeAccepted!==true||
       snapshot?.device?.epoch!==pending.epoch||
       s.deviceEpoch!==pending.epoch||
       s.routeRevision!==pending.revision||
       m.routingRevision!==pending.revision||
       !physicalFxHardwareMatches(pending,hardwareConfiguration)||
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
  // G-MV30: M4 VST3 creation has ONE pure transaction policy. The host
  // ACK can report a staged dryRoutingLab state, but that event is NEVER the
  // independent, validated Console-v6 PCM readback. Editors open only when
  // the accepted M4 snapshot proves the exact new catalog/instance/route.
  function m4InsertPlan(snapshot,{channel,slot,catalogId,
    verifiedM4=false,offline=false,pending=false,hardwareConfiguration}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const host=m4FxHost(snapshot,{channel,slot,verifiedM4,offline,pending,
      hardwareConfiguration});
    if(!host||!Number.isInteger(slot)||
       typeof catalogId!=="string"||!catalogId)
      return unavailable;
    const {m,epoch,revision}=host;
    const instances=m.fxInstances?.[channel];
    if(!Array.isArray(instances)||instances[slot]!=null||
       (slot>0&&instances[slot-1]==null))return unavailable;
    return {kind:"READY",action:"routingFxInsert",
      payload:{id:catalogId,channel,slot,deviceEpoch:epoch,revision},
      pending:{authority:"M4",id:catalogId,channel,slot,
        epoch:snapshot.device.epoch,revision:m.routingRevision,acked:false,
        ...m4FxHardwareLease(host)}};
  }
  function m4InsertAckResult(pending,ack){
    if(!pending||pending.authority!=="M4"||
       !ack||ack.requestId!==pending.requestId||
       pending.acked===true)return "IGNORE";
    return ack.ok===true&&ack.channel===pending.channel&&
      ack.slot===pending.slot?"ACKED":"REJECTED";
    // Deliberately ignore ack.state, including FX inventory and revision.
  }
  function m4InsertReadbackSettlement(pending,snapshot,hardwareConfiguration){
    if(!pending||pending.authority!=="M4")return {kind:"NONE"};
    const s=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4,
      revision=safeM4Token(m?.routingRevision),
      previous=safeM4Token(pending.revision);
    if(snapshot?.admitted!==false||snapshot?.retained===true||
       s?.authority!=="M4"||m?.active!==true||m.routeAccepted!==true||
       m.nativeBusStructural===true||
       snapshot?.device?.epoch!==pending.epoch||
       s.deviceEpoch!==pending.epoch||
       s.routeRevision!==m.routingRevision||
       previous===null||revision===null||revision<previous||
       !physicalFxHardwareMatches(pending,hardwareConfiguration)||
       !Number.isInteger(pending.channel)||pending.channel<0||pending.channel>=10||
       !Number.isInteger(pending.slot)||pending.slot<0||pending.slot>=8)
      return {kind:"STALE"};
    // A same-revision cached snapshot is never proof of a structural insert.
    if(revision===previous)return {kind:"WAIT"};
    const instances=m.fxInstances?.[pending.channel],
      installed=Array.isArray(instances)?instances[pending.slot]:null;
    if(installed&&installed.catalogId!==pending.id)return {kind:"STALE"};
    if(!installed||typeof installed.instanceId!=="string"||
       !installed.instanceId||pending.acked!==true)return {kind:"WAIT"};
    return {kind:"CONFIRMED",channel:pending.channel,slot:pending.slot,
      instanceId:installed.instanceId,epoch:safeM4Token(pending.epoch),
      revision};
  }


  // G-MV31: one exact, immutable navigation policy for the verified /preview/
  // FX editor. No HWND, IPC, DOM or duplicated editor state lives in this VM.
  // Capture the accepted graph and device generation before a deferred click;
  // the same generation must still own the instance when it is consumed.
  function editorNavigationPlan(view,{channelId:id,slot,
    hardwareConfiguration}={}) {
    const unavailable={kind:"UNAVAILABLE"},graph=view?.graph;
    if(!channelId(id)||!Number.isInteger(slot)||slot<0||slot>=8||
       !rosterUnique(graph)||!Array.isArray(graph?.channels)||
       !channelId(graph.revision)||!channelId(graph.rosterRevision)||
       new Set(graph.channels.map(ch=>ch.id)).size!==graph.channels.length)
      return unavailable;
    const offline=view?.logical===true&&view?.admitted===false&&
      view?.retained===false&&
      typeof view.workspaceToken==="string"&&view.workspaceToken.length>0;
    const physical=view?.logical!==true&&view?.admitted===true&&
      view?.retained===false&&
      view.graph.capabilities?.callbackOwnsChannelIds===true&&
      view.raw?.mixerSession?.authority==="CHANNEL_ID"&&
      view.raw.mixerSession.deviceEpoch===view.device?.epoch&&
      channelId(view.device.epoch)&&
      typeof hardwareConfiguration==="string"&&hardwareConfiguration.length>0;
    if(!offline&&!physical)return unavailable;
    const rows=graph.channels.filter(ch=>ch.id===id);
    if(rows.length!==1||!VALID_ROLES.has(rows[0].role)||
       !Array.isArray(rows[0].inserts))return unavailable;
    const effect=rows[0].inserts[slot];
    if(effect==null)return {kind:"PICKER"};
    if(typeof effect.id!=="string"||!effect.id||
       typeof effect.catalogId!=="string"||!effect.catalogId)
      return unavailable;
    const lease=Object.freeze({
      authority:offline?"LOGICAL":"CHANNEL_ID",
      channelId:id,slot,instanceId:effect.id,catalogId:effect.catalogId,
      graphRevision:graph.revision,rosterRevision:graph.rosterRevision,
      workspaceToken:offline?view.workspaceToken:null,
      deviceEpoch:physical?view.device.epoch:null,
      hardwareConfiguration:physical?hardwareConfiguration:null
    });
    return {kind:"OPEN",lease};
  }
  function editorNavigationLeaseMatches(view,lease,{hardwareConfiguration}={}) {
    if(!lease||typeof lease!=="object")return false;
    const now=editorNavigationPlan(view,{
      channelId:lease.channelId,slot:lease.slot,hardwareConfiguration});
    if(now.kind!=="OPEN")return false;
    return Object.keys(now.lease).every(key=>now.lease[key]===lease[key])&&
      Object.keys(lease).length===Object.keys(now.lease).length;
  }

  // G-MV38: a single allowlisted editor surface policy. The native FX catalog
  // is *not* permission to create a browser HWND; only these five approved
  // built-in controls have a dedicated /preview/ WebView editor. Other installed
  // FX (including future built-ins hosted in JUCE) use the existing exact
  // native HWND path; offline vendor references keep their logical route.
  // Adding a new approved browser editor changes this registry, not the bridge.
  const BROWSER_EDITOR_HANDLES=Object.freeze({
    "armix.builtin.channel-eq.v1":"ArSonMixNativeEq",
    "armix.builtin.channel-compressor.v1":"ArSonMixNativeCompressor",
    "armix.builtin.arson-harmony.v1":"ArSonMixNativeHarmony",
    "armix.builtin.vocal-throw.v1":"ArSonMixNativeVocalThrow",
    "armix.builtin.8d-sonku.v1":"ArSonMixNativeSonKu"
  });
  function editorSurfacePlan(snapshot,lease,{hardwareConfiguration}={}){
    if(!editorNavigationLeaseMatches(snapshot,lease,{hardwareConfiguration}))
      return {kind:"UNAVAILABLE"};
    const key=lease.catalogId;
    if(Object.prototype.hasOwnProperty.call(BROWSER_EDITOR_HANDLES,key))
      return {kind:"BROWSER",windowKey:BROWSER_EDITOR_HANDLES[key]};
    if(lease.authority==="LOGICAL")return {kind:"LOGICAL_VENDOR"};
    if(lease.authority==="CHANNEL_ID")return {kind:"NATIVE_HOST"};
    return {kind:"UNAVAILABLE"};
  }

  // M4 uses host ordinals, never ChannelIds. An old accepted FX inventory
  // cannot be permission to open an editor after device/route owner changes.
  function m4EditorPlan(snapshot,{channel,slot,verifiedM4=false,
    offline=false,hardwareConfiguration}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    const host=m4FxHost(snapshot,{channel,slot,verifiedM4,offline,
      hardwareConfiguration});
    if(!host||!Number.isInteger(slot))return unavailable;
    const {m,epoch,revision}=host;
    const slots=m.fxInstances?.[channel];
    if(!Array.isArray(slots))return unavailable;
    const effect=slots[slot];
    if(effect==null)return {kind:"PICKER"};
    if(typeof effect.instanceId!=="string"||!effect.instanceId||
       typeof effect.catalogId!=="string"||!effect.catalogId)
      return unavailable;
    return {kind:"OPEN",channel,slot,instanceId:effect.instanceId,
      deviceEpoch:epoch,revision};
  }

  // G-MV35: exact M4 editor dispatch is an independent, short-lived
  // navigation lease, not an ACK and not a second accepted FX state.
  // Keep the original M4 plan as the single host admission authority.
  function m4EditorDispatchPlan(snapshot,{channel,slot,instanceId,
    deviceEpoch,revision,verifiedM4=false,offline=false,
    hardwareConfiguration}={}) {
    const unavailable={kind:"UNAVAILABLE"};
    if(typeof hardwareConfiguration!=="string"||!hardwareConfiguration)
      return unavailable;
    const plan=m4EditorPlan(snapshot,{channel,slot,verifiedM4,offline,
      hardwareConfiguration});
    if(plan.kind!=="OPEN"||plan.instanceId!==instanceId||
       plan.deviceEpoch!==deviceEpoch||plan.revision!==revision)
      return unavailable;
    const catalogId=snapshot.raw.legacyM4.fxInstances[channel][slot].catalogId;
    const lease=Object.freeze({channel,slot,instanceId,catalogId,
      deviceEpoch,revision,hardwareConfiguration});
    return {kind:"READY",lease,action:"routingFxEditor",
      payload:{channel,slot,instanceId,deviceEpoch,revision}};
  }
  function m4EditorDispatchLeaseMatches(snapshot,lease,{
    verifiedM4=false,offline=false,hardwareConfiguration}={}) {
    if(!lease||typeof lease!=="object")return false;
    const current=m4EditorDispatchPlan(snapshot,{...lease,
      verifiedM4,offline,hardwareConfiguration});
    return current.kind==="READY"&&
      Object.keys(current.lease).length===Object.keys(lease).length&&
      Object.keys(current.lease).every(key=>current.lease[key]===lease[key]);
  }

  // G-MV36: physical ChannelId VST3 / ImmerSIB editors use the original
  // JUCE HWND only when the EXACT G-MV31 accepted instance lease survives.
  // Logical .smix and native built-in /preview/ editor owners stay separate.
  function nativeEditorDispatchPlan(snapshot,lease,{
    hardwareConfiguration,offline=false}={}){
    if(offline||lease?.authority!=="CHANNEL_ID"||
       !editorNavigationLeaseMatches(snapshot,lease,{hardwareConfiguration}))
      return {kind:"UNAVAILABLE"};
    return {kind:"READY",action:"routingFxEditor",
      payload:{channelId:lease.channelId,slot:lease.slot,
        instanceId:lease.instanceId,deviceEpoch:lease.deviceEpoch,
        revision:lease.graphRevision}};
  }
  // One pure HWND ACK policy for M4 and ChannelId: transport requestId AND
  // installed FX instance must match, and the current accepted hardware,
  // route, roster, catalog and owner must still hold the exact click lease.
  // Called with ack=null by the controller on every accepted PCM snapshot;
  // this retires stale HWND requests even if the native host never responds.
  function nativeEditorAckResult(pending,ack,snapshot,{
    verifiedM4=false,offline=false,hardwareConfiguration}={}){
    if(!pending)return "IGNORE";
    const lease=pending.lease;
    const valid=pending.authority==="CHANNEL_ID"?
      !offline&&lease?.authority==="CHANNEL_ID"&&
        editorNavigationLeaseMatches(snapshot,lease,{hardwareConfiguration}):
      pending.authority==="M4"&&
        m4EditorDispatchLeaseMatches(snapshot,lease,{
          verifiedM4,offline,hardwareConfiguration});
    if(!valid)return "STALE";
    if(!ack||ack.requestId!==pending.requestId)return "IGNORE";
    if(ack.instanceId!==lease.instanceId||ack.ok!==true)return "REJECTED";
    return "ACKED";
  }

  // G-MV34: one physically admitted ChannelId FX owner for bypass/insert.
  // The bridge supplies the canonical existing native device fingerprint.
  // Historical pure/partial fixtures without the optional fingerprint stay
  // valid; a real physical request with a missing fingerprint fails closed.
  function nativePhysicalFxReady(snapshot,hardwareConfiguration){
    return typeof hardwareConfiguration==="string"&&
      hardwareConfiguration.length>0&&
      snapshot?.admitted===true&&snapshot?.retained!==true&&
      snapshot?.raw?.mixerSession?.authority==="CHANNEL_ID"&&
      snapshot.raw.mixerSession.deviceEpoch===snapshot.device?.epoch&&
      snapshot?.graph?.capabilities?.callbackOwnsChannelIds===true&&
      channelId(snapshot.graph.revision)&&
      channelId(snapshot.graph.rosterRevision);
  }
  function nativeInsertPlan(snapshot,{channelId:id,slot,catalogId,
    pending=false,hardwareConfiguration}={}) {
    const graph=snapshot?.graph;
    if(pending||snapshot?.admitted!==true||snapshot?.retained===true||
       !channelId(id)||typeof catalogId!=="string"||!catalogId||
       !Number.isInteger(slot)||slot<0||slot>=8||
       !graph?.capabilities?.supportedOps?.includes("LOAD_INSERT")||
       !rosterUnique(graph)||
       (hardwareConfiguration!==undefined&&
        !nativePhysicalFxReady(snapshot,hardwareConfiguration)))
      return {kind:"UNAVAILABLE"};
    const matches=graph.channels.filter(ch=>ch.id===id);
    if(matches.length!==1||!VALID_ROLES.has(matches[0].role)||
       matches[0].inserts.length!==slot)return {kind:"UNAVAILABLE"};
    return {kind:"READY",channelId:id,payload:{catalogId,slot}};
  }
  function nativeEditPlan(snapshot,{channelId:id,slot,instanceId,
                           move=false,destinationId=null,position=null,
                           hardwareConfiguration}={}) {
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
       !rosterUnique(graph)||
       (hardwareConfiguration!==undefined&&
        !nativePhysicalFxReady(snapshot,hardwareConfiguration)))
      return {kind:"UNAVAILABLE"};
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
  function nativeInsertSettlement(pending,snapshot,confirmed,
                                   hardwareConfiguration){
    if(!pending||pending.authority!=="CHANNEL_ID")return {kind:"NONE"};
    if(snapshot?.device?.epoch!==pending.epoch||
       !physicalFxHardwareMatches(pending,hardwareConfiguration)||
       (Object.prototype.hasOwnProperty.call(pending,"hardwareConfiguration")&&
        !nativePhysicalFxReady(snapshot,hardwareConfiguration)))
      return {kind:"STALE"};
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
  function nativePowerPlan(snapshot,{channelId:id,action,slot,
    pending=false,hardwareConfiguration}={}){
    const graph=snapshot?.graph;
    if(pending||snapshot?.admitted!==true||snapshot?.retained===true||
       !channelId(id)||!Array.isArray(graph?.channels)||!rosterUnique(graph)||
       (hardwareConfiguration!==undefined&&
        !nativePhysicalFxReady(snapshot,hardwareConfiguration)))
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
        insertId:insert?.id,bypassed,epoch:snapshot.device?.epoch,acked:false,
        ...(hardwareConfiguration===undefined?{}:{
          hardwareConfiguration,rosterRevision:graph.rosterRevision})},
      payload:{channelId:id,deviceEpoch:snapshot.device?.epoch,
        revision:graph.revision,value:bypassed,
        ...(rack?{}:{slot,insertId:insert.id})}};
  }
  function nativePowerAckResult(pending,ack){
    if(!pending||!ack||ack.requestId!==pending.requestId||
       pending.acked===true)return "IGNORE";
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
  function globalPowerContext(snapshot,verifiedM4=false,offline=false,
                              hardwareConfiguration){
    const session=snapshot?.raw?.mixerSession,device=snapshot?.device;
    if(offline||snapshot?.retained===true||!positiveId(device?.epoch)||
       session?.deviceEpoch!==device.epoch||
       (hardwareConfiguration!==undefined&&
        (typeof hardwareConfiguration!=="string"||!hardwareConfiguration)))
      return null;
    const physical=hardwareConfiguration===undefined?{}:{hardwareConfiguration};
    if(snapshot.admitted===true&&session.authority==="CHANNEL_ID"&&
       snapshot.graph?.capabilities?.callbackOwnsChannelIds===true&&
       positiveId(snapshot.graph.revision))
      return {authority:"CHANNEL_ID",epoch:device.epoch,
        revision:snapshot.graph.revision,...physical};
    if(snapshot.admitted===false&&verifiedM4===true&&
       session.authority==="M4"&&positiveId(session.routeRevision)&&
       session.routeRevision===snapshot.raw?.legacyM4?.routingRevision)
      return {authority:"M4",epoch:device.epoch,revision:session.routeRevision,
        ...physical};
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
    if(!pending||!ack||pending.requestId!==ack.requestId||
       pending.acked===true)return "IGNORE";
    return ack.ok===true&&ack.bypassed===pending.bypassed?"ACKED":"REJECTED";
  }
  function globalPowerSettlement(pending,snapshot,context){
    if(!pending)return "NONE";
    if(!context||pending.epoch!==context.epoch||
       pending.revision!==context.revision||
       pending.authority!==context.authority||
       !physicalFxHardwareMatches(pending,context.hardwareConfiguration))
      return "STALE";
    if(pending.acked!==true)return "WAIT";
    return typeof snapshot?.raw?.globalFxBypassed==="boolean"&&
      snapshot.raw.globalFxBypassed===pending.bypassed?"CONFIRMED":"WAIT";
  }
  // M4 compatibility FX slot ACK is NOT readback. Keep the per-slot lease
  // locked until independent accepted M4 snapshot proves its exact token.
  function m4SlotPowerAckResult(pending,ack){
    if(!pending||!ack||pending.requestId!==ack.requestId||
       pending.acked===true)return "IGNORE";
    if(ack.channel!==pending.channel||ack.slot!==pending.slot||
       ack.ok!==true)return "REJECTED";
    return "ACKED";
  }
  function m4SlotPowerSettlement(pending,snapshot,hardwareConfiguration){
    if(!pending)return "NONE";
    const session=snapshot?.raw?.mixerSession,m=snapshot?.raw?.legacyM4;
    if(snapshot?.retained===true||snapshot?.admitted===true||
       session?.authority!=="M4"||
       snapshot?.device?.epoch!==pending.epoch||
       session.deviceEpoch!==pending.epoch||
       session.routeRevision!==pending.revision||
       m?.routingRevision!==pending.revision||
       !physicalFxHardwareMatches(pending,hardwareConfiguration))return "STALE";
    const slots=m.fxInstances?.[pending.channel];
    const actual=Array.isArray(slots)?slots[pending.slot]:null;
    if(actual?.instanceId!==pending.token)return "STALE";
    if(pending.acked!==true)return "WAIT";
    return actual?.bypassed===pending.bypass?"CONFIRMED":"WAIT";
  }

  return Object.freeze({powerReadback,mountedRackMatches,
    m4Presentation,nativePowerSettlement,m4PowerPlan,m4RackAckResult,
    m4RackSettlement,m4EditorDispatchPlan,m4EditorDispatchLeaseMatches,
    nativeEditorDispatchPlan,nativeEditorAckResult,
    m4InsertPlan,m4InsertAckResult,m4InsertReadbackSettlement,
    editorNavigationPlan,editorNavigationLeaseMatches,editorSurfacePlan,m4EditorPlan,
    nativeInsertPlan,nativeEditPlan,nativeInsertSettlement,
    nativePowerPlan,nativePowerAckResult,
    globalPowerContext,globalPowerPlan,globalPowerAckResult,
    globalPowerSettlement,m4SlotPowerAckResult,m4SlotPowerSettlement});
});
