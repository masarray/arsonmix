// SPDX-License-Identifier: AGPL-3.0-only
// M4 operator intent ADMISSION for the ONE approved /preview/ + JUCE product.
// CONTROL PLANE ONLY: no audio buffers, second graph authority, plugin bus
// allocation, optimistic mutation, callback, or extra renderer.
// Accepts a previously normalized native v8/schemaVersion=6 readback and
// returns a controller.intent() request only when the existing native ABI
// can prove the requested result after ACK + matching accepted snapshot.
const MAX64=(1n<<64n)-1n;
export const M4_OPERATOR_ABI=Object.freeze({schemaVersion:6,
  nativeWireOperation:'APPLY_WIRE_TRANSACTION',supportedWireTap:'POST_FADER'});

const fail=(error,detail)=>({ok:false,error,detail});
const id=value=>{
  if(typeof value!=='string'||!/^[1-9][0-9]*$/.test(value))return false;
  try{return BigInt(value)<=MAX64;}catch{return false;}
};
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const safeGain=value=>typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=4;
function admittedView(accepted){
  // A Pages demo or raw JSON is NOT a native-accepted graph. The caller must
  // pass normalizeSnapshot() output from the existing M2 controller.
  if(!accepted||accepted.admitted!==true||!object(accepted.device)||
      !object(accepted.graph)||!object(accepted.graph.capabilities)||
      accepted.graph.capabilities.callbackOwnsChannelIds!==true ||
      !id(accepted.device.epoch)||!id(accepted.graph.revision)||
      !id(accepted.graph.rosterRevision)||
      !Array.isArray(accepted.graph.channels)||
      !Array.isArray(accepted.graph.edges))
    return fail('NATIVE_CHANNELID_NOT_ADMITTED');
  if(!Array.isArray(accepted.graph.capabilities.supportedOps)||
      !accepted.graph.capabilities.supportedOps.includes(M4_OPERATOR_ABI.nativeWireOperation))
    return fail('NATIVE_WIRE_OPERATION_UNAVAILABLE');
  const channels=new Map();
  let main=null;
  for(const ch of accepted.graph.channels){
    if(!object(ch)||!id(ch.id)||channels.has(ch.id)||
       !['INPUT','GROUP','FX_RETURN','MAIN','MONITOR'].includes(ch.role))
      return fail('INVALID_ACCEPTED_CHANNELS');
    channels.set(ch.id,ch);
    if(ch.role==='MAIN'){
      if(main!==null)return fail('MULTIPLE_NATIVE_MASTERS');
      main=ch.id;
    }
  }
  if(!main||accepted.graph.mainChannelId!==main)
    return fail('MISSING_NATIVE_MASTER');
  const edges=[],edgeIds=new Set(),arcs=new Map([...channels.keys()].map(x=>[x,[]]));
  for(const e of accepted.graph.edges){
    if(!object(e)||!id(e.edgeId)||edgeIds.has(e.edgeId)||
       !id(e.from)||!id(e.to)||!channels.has(e.from)||!channels.has(e.to)||
       e.from===e.to||e.from===main||e.signalKind!=='AUDIO'||
       e.enabled!==true||typeof e.primaryOutput!=='boolean'||
       !safeGain(e.gainLinear))
      return fail('INVALID_NATIVE_AUDIO_EDGE');
    edgeIds.add(e.edgeId);edges.push(e);arcs.get(e.from).push(e.to);
  }
  const path=(start,target,graph=arcs)=>{
    const visited=new Set(),stack=[start];
    while(stack.length){
      const cur=stack.pop();
      if(cur===target)return true;
      if(visited.has(cur))continue;
      visited.add(cur);
      for(const to of graph.get(cur)||[])stack.push(to);
    }
    return false;
  };
  // Do not use a rejected/feedback graph as a source of proposed changes.
  for(const [src,targets] of arcs.entries())
    if(targets.some(to=>path(to,src)))return fail('ACCEPTED_GRAPH_CYCLE');
  return {ok:true,channels,main,edges,arcs,path,accepted};
}
function sendsToMain(view,source,candidate){
  const next=new Map([...view.channels.keys()].map(x=>[x,[]]));
  for(const e of candidate){
    // FX return explicitly verified as 100% wet does not count as
    // second *dry* path. Unverified returns are conservatively counted.
    const dest=view.channels.get(e.to);
    if(dest?.role==='FX_RETURN'&&dest.wetOnly===true)continue;
    next.get(e.from).push(e.to);
  }
  const count=(node,seen)=>{
    if(node===view.main)return 1;
    if(seen.has(node))return 2;
    const scope=new Set(seen);scope.add(node);
    let n=0;
    for(const to of next.get(node)||[]){
      n+=count(to,scope);if(n>1)return 2;
    }
    return n;
  };
  return count(source,new Set());
}
function planWire(view,request){
  const {kind,sourceId,destinationId}=request;
  if(!id(sourceId)||!view.channels.has(sourceId)||
     !id(destinationId)||!view.channels.has(destinationId))
    return fail('UNKNOWN_NATIVE_CHANNELID');
  if(sourceId===destinationId)return fail('SELF_ROUTE_FORBIDDEN');
  const source=view.channels.get(sourceId),destination=view.channels.get(destinationId);
  if(['MAIN','MONITOR'].includes(source.role))return fail('OUTPUT_BUS_CANNOT_SOURCE_SEND');
  if(destination.role==='INPUT')return fail('INPUT_NOT_A_ROUTING_DESTINATION');
  if(view.path(destinationId,sourceId))return fail('ROUTING_FEEDBACK_CYCLE');
  const same=view.edges.filter(e=>e.from===sourceId&&e.to===destinationId);
  const currentPrimary=view.edges.filter(e=>e.from===sourceId&&e.primaryOutput);
  if(currentPrimary.length>1)return fail('MULTIPLE_ACCEPTED_PRIMARY_OUTPUTS');
  if(kind==='ROUTE_ONLY'){
    if(destination.role!=='GROUP'&&destination.role!=='MAIN')
      return fail('EXCLUSIVE_ROUTE_REQUIRES_GROUP_OR_MAIN');
    if(currentPrimary.length===1&&currentPrimary[0].to===destinationId)
      return fail('PRIMARY_ALREADY_AT_DESTINATION');
    if(same.length)return fail('DUPLICATE_NATIVE_ROUTE');
    const simulated=view.edges.filter(e=>
      !(e.from===sourceId&&e.primaryOutput));
    simulated.push({from:sourceId,to:destinationId,primaryOutput:true});
    if(sendsToMain(view,sourceId,simulated)>1)
      return fail('EXCLUSIVE_ROUTE_RETAINS_PARALLEL_DRY_PATH',
        'Native Route Only changes the primary OUT; existing parallel sends remain.');
    return {ok:true,intent:{operation:M4_OPERATOR_ABI.nativeWireOperation,
      targetChannelId:sourceId,payload:{changes:[{
        op:'ROUTE_ONLY',from:sourceId,to:destinationId,
        tap:'POST_FADER',gainLinear:1
      }]}},fence:fence(view)};
  }
  if(kind!=='SEND_POST')return fail('UNKNOWN_ROUTING_GESTURE');
  if(destination.role==='MAIN')
    return fail('MAIN_REQUIRES_PRIMARY_OUT','Use Route Only to MAIN.');
  if(same.length)return fail('DUPLICATE_NATIVE_ROUTE');
  if(destination.role==='FX_RETURN'&&destination.wetOnly!==true)
    return fail('FX_RETURN_WET_ONLY_NOT_VERIFIED',
      'Return must be proven 100% wet by native accepted readback.');
  // Non-unity gain is intentionally blocked: current v8 wire
  // readbackProvesIntent only confirms existence, NOT accepted send gain.
  if(request.gainDb!==undefined&&request.gainDb!==0)
    return fail('SEND_GAIN_ACK_READBACK_NOT_AVAILABLE');
  const simulated=[...view.edges,{from:sourceId,to:destinationId,primaryOutput:false}];
  if(destination.role!=='FX_RETURN'&&sendsToMain(view,sourceId,simulated)>1)
    return fail('DOUBLE_DRY_TO_MAIN',
      'Choose Route Only for subgroup instead of mixing direct MAIN twice.');
  return {ok:true,intent:{operation:M4_OPERATOR_ABI.nativeWireOperation,
    targetChannelId:sourceId,payload:{changes:[{
      op:'CONNECT',from:sourceId,to:destinationId,
      tap:'POST_FADER',gainLinear:1
    }]}},fence:fence(view)};
}
const fence=view=>Object.freeze({
  deviceEpoch:view.accepted.device.epoch,
  graphRevision:view.accepted.graph.revision,
  rosterRevision:view.accepted.graph.rosterRevision
});
function sidechainGate(view,request){
  if(!id(request.sourceId)||!id(request.destinationId)||
     !view.channels.has(request.sourceId)||!view.channels.has(request.destinationId))
    return fail('UNKNOWN_NATIVE_CHANNELID');
  if(request.sourceId===request.destinationId||
     view.path(request.destinationId,request.sourceId))
    return fail('SIDECHAIN_DEPENDENCY_CYCLE');
  const dest=view.channels.get(request.destinationId);
  if(dest.role==='MAIN'||dest.role==='MONITOR')
    return fail('SIDECHAIN_TARGET_NOT_AN_INSERT_CHANNEL');
  if(typeof request.pluginInstanceId!=='string'||
     !request.pluginInstanceId.trim()||
     !Array.isArray(dest.inserts)||
     !dest.inserts.some(fx=>fx.instanceId===request.pluginInstanceId||
       fx.id===request.pluginInstanceId))
    return fail('VST3_INSERT_LEASE_NOT_VERIFIED');
  const aux=request.nativeAuxInput;
  if(!object(aux)||aux.pluginInstanceId!==request.pluginInstanceId||
     !Number.isInteger(aux.busIndex)||aux.busIndex<1||
     ![1,2].includes(aux.channels)||aux.enabled!==true||
     aux.detectorOnly!==true)
    return fail('VST3_AUX_DETECTOR_BUS_NOT_NEGOTIATED');
  const caps=view.accepted.graph.capabilities;
  if(!caps.supportedOps.includes('CONNECT_PLUGIN_SIDECHAIN')||
     caps.nativeSidechainBusReady!==true)
    return fail('NATIVE_SIDECHAIN_CAPABILITY_UNAVAILABLE');
  const pdc=assessM4Pdc({latencySamples:aux.latencySamples,
    compensated:aux.compensated,monitorMode:request.monitorMode});
  if(!pdc.ok)return pdc;
  // Even if future metadata appears, existing M2 controller does not
  // prove a detector-only route in readbackProvesIntent(). Never emit an
  // unprovable accepted sidechain or covert audible send.
  return fail('SIDECHAIN_ACK_READBACK_UNIMPLEMENTED',
    'Require native detector-only edge + plugin-bus readback and exact ACK proof.');
}
export function assessM4Pdc({latencySamples,compensated,monitorMode}={}){
  if(!Number.isSafeInteger(latencySamples)||latencySamples<0)
    return fail('PLUGIN_LATENCY_UNVERIFIED');
  if(monitorMode!==undefined&&!['LIVE_LOW_LATENCY','STUDIO_COMPENSATED'].includes(monitorMode))
    return fail('UNKNOWN_MONITOR_LATENCY_MODE');
  if(latencySamples>0&&compensated!==true)
    return fail('NATIVE_GRAPH_PDC_UNAVAILABLE');
  if(latencySamples>0&&monitorMode==='LIVE_LOW_LATENCY')
    return fail('LIVE_MONITOR_LATENCY_POLICY_CONFLICT');
  return {ok:true,latencySamples};
}
/**
 * Produces a candidate native controller.intent operation ONLY.
 * No message is sent by this function; GUI/Integrator must dispatch with
 * existing M2 controller and wait for ACK + same-revision native readback.
 */
export function planM4OperatorIntent(accepted,request){
  const view=admittedView(accepted);
  if(!view.ok)return view;
  if(!object(request)||typeof request.kind!=='string')
    return fail('INVALID_OPERATOR_REQUEST');
  switch(request.kind){
    case 'ROUTE_ONLY':
    case 'SEND_POST':return planWire(view,request);
    case 'SEND_PRE':return fail('NATIVE_PRE_FADER_WIRE_NOT_ADMITTED',
      'ENGINE #266 WIRE decoder currently accepts POST_FADER only.');
    case 'SET_SEND_GAIN':return fail('SEND_GAIN_ACK_READBACK_NOT_AVAILABLE');
    case 'GROUP_SELECTED':return fail('GROUP_CREATE_ACK_READBACK_UNIMPLEMENTED');
    case 'NEW_FX_RETURN':return fail('FX_RETURN_CREATE_ACK_READBACK_UNIMPLEMENTED');
    case 'SIDECHAIN':return sidechainGate(view,request);
    default:return fail('UNKNOWN_OPERATOR_GESTURE');
  }
}
