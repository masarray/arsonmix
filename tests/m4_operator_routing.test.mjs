// SPDX-License-Identifier: AGPL-3.0-only
// M4 acceptance: one approved native graph, no fake PCM/sidechain/PDC.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createRequire} from 'node:module';
import {planM4OperatorIntent,assessM4Pdc,M4_OPERATOR_ABI} from
  '../integration/console-v6/m4-operator-routing.mjs';
const require=createRequire(import.meta.url);
const {normalizeSnapshot,createController}=require('../web/approved-native-contract.js');
const clone=x=>structuredClone(x);
const g0=JSON.parse(readFileSync('integration/console-v6/fixtures.json','utf8')).g0;
const ids={v:'9007199254740993',kick:'9007199254740994',
  group:'100',wet:'101',main:'18446744073709551615'};
function base(){
  const s=clone(g0);
  s.graph.revision='3';s.graph.rosterRevision='2';
  s.graph.mainChannelId=ids.main;
  s.graph.capabilities.callbackOwnsChannelIds=true;
  s.graph.capabilities.supportedOps=['APPLY_WIRE_TRANSACTION'];
  s.graph.capabilities.groups=true;s.graph.capabilities.fxReturns=true;
  const channel=(id,role,out,extra={})=>({
    id,role,name:role+' '+id,processingFormat:'STEREO',
    ...(out?{primaryOutId:out}:{}),
    controls:{faderDb:0,pan:0,mute:false,solo:false},
    inserts:[],sends:[],...extra
  });
  s.graph.channels=[
    channel(ids.v,'INPUT',ids.main),
    channel(ids.kick,'INPUT',ids.main),
    channel(ids.group,'GROUP',ids.main),
    channel(ids.wet,'FX_RETURN',ids.main,{wetOnly:true}),
    channel(ids.main,'MAIN')
  ];
  s.graph.destinations=s.graph.channels.slice(2).map(x=>({id:x.id}));
  const edge=(edgeId,from,to,primaryOutput=true)=>({
    edgeId,from,to,primaryOutput,tap:'POST_FADER',gainLinear:1,
    enabled:true,signalKind:'AUDIO'
  });
  s.graph.edges=[
    edge('1',ids.v,ids.main),edge('2',ids.kick,ids.main),
    edge('3',ids.group,ids.main),edge('4',ids.wet,ids.main)
  ];
  return s;
}
const accepted=(raw=base())=>{
  const a=normalizeSnapshot(raw);
  assert.ok(a?.admitted,'fixture must pass real public M2 native contract');
  return a;
};
test('G0/no callback and raw browser graphs never create native intents',()=>{
  assert.equal(planM4OperatorIntent(normalizeSnapshot(g0),
    {kind:'ROUTE_ONLY',sourceId:ids.v,destinationId:ids.group}).error,
    'NATIVE_CHANNELID_NOT_ADMITTED');
  assert.equal(planM4OperatorIntent(base(),
    {kind:'ROUTE_ONLY',sourceId:ids.v,destinationId:ids.group}).ok,false);
});
test('one operator Route Only emits exact native wire operation and revision fence',()=>{
  const snapshot=accepted(),before=clone(snapshot.raw);
  const planned=planM4OperatorIntent(snapshot,{
    kind:'ROUTE_ONLY',sourceId:ids.v,destinationId:ids.group});
  assert.equal(planned.ok,true);
  assert.deepEqual(planned.intent,{
    operation:'APPLY_WIRE_TRANSACTION',targetChannelId:ids.v,
    payload:{changes:[{op:'ROUTE_ONLY',from:ids.v,to:ids.group,
      tap:'POST_FADER',gainLinear:1}]}
  });
  assert.deepEqual(planned.fence,{deviceEpoch:'69',graphRevision:'3',rosterRevision:'2'});
  assert.deepEqual(snapshot.raw,before,'planner must never write accepted graph');
  assert.equal(M4_OPERATOR_ABI.schemaVersion,6);
});
test('keep-main post send to verified wet FX Return is allowed, no dry path removal',()=>{
  const snapshot=accepted();
  const p=planM4OperatorIntent(snapshot,{kind:'SEND_POST',
    sourceId:ids.v,destinationId:ids.wet,gainDb:0});
  assert.equal(p.ok,true);
  assert.equal(p.intent.payload.changes[0].op,'CONNECT');
  assert.equal(p.intent.payload.changes[0].tap,'POST_FADER');
  assert.equal(snapshot.graph.channels[0].primaryOutId,ids.main,
    'dry MAIN remains untouched; wet send must be truly wet-only');
});
test('subgroup parallel send is blocked before double-dry, route only is advised',()=>{
  const p=planM4OperatorIntent(accepted(),{kind:'SEND_POST',
    sourceId:ids.v,destinationId:ids.group});
  assert.equal(p.ok,false);assert.equal(p.error,'DOUBLE_DRY_TO_MAIN');
});
test('route only preserves parallel wet send but replaces primary, never both dry',()=>{
  const s=base();s.graph.edges.push({edgeId:'5',from:ids.v,to:ids.wet,
    tap:'POST_FADER',gainLinear:.5,primaryOutput:false,enabled:true,signalKind:'AUDIO'});
  assert.equal(planM4OperatorIntent(accepted(s),{kind:'ROUTE_ONLY',
    sourceId:ids.v,destinationId:ids.group}).ok,true);
});
test('route only refuses existing parallel dry path to MAIN',()=>{
  const s=base();s.graph.edges.push({edgeId:'6',from:ids.v,to:ids.group,
    tap:'POST_FADER',gainLinear:.5,primaryOutput:false,enabled:true,signalKind:'AUDIO'});
  const p=planM4OperatorIntent(accepted(s),{kind:'ROUTE_ONLY',
    sourceId:ids.v,destinationId:ids.main});
  assert.equal(p.ok,false);
  assert.equal(p.error,'PRIMARY_ALREADY_AT_DESTINATION');
  // Now route to a second group while parallel dry path remains.
  s.graph.channels.push({id:'102',role:'GROUP',name:'BUS 2',
    processingFormat:'STEREO',primaryOutId:ids.main,
    controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[],sends:[]});
  s.graph.destinations.push({id:'102'});
  s.graph.edges.push({edgeId:'7',from:'102',to:ids.main,
    tap:'POST_FADER',gainLinear:1,primaryOutput:true,enabled:true,signalKind:'AUDIO'});
  const q=planM4OperatorIntent(accepted(s),{kind:'ROUTE_ONLY',
    sourceId:ids.v,destinationId:'102'});
  assert.equal(q.error,'EXCLUSIVE_ROUTE_RETAINS_PARALLEL_DRY_PATH');
});
test('accepted feedback path and self-send cannot be planned',()=>{
  const s=base();
  // Making group -> vocal also creates a cycle only if vocal -> group. First
  // change source to group, then target vocal which is already upstream.
  s.graph.edges[0]={...s.graph.edges[0],to:ids.group};
  s.graph.channels[0].primaryOutId=ids.group;
  assert.equal(planM4OperatorIntent(accepted(s),{kind:'SEND_POST',
    sourceId:ids.group,destinationId:ids.v}).error,'ROUTING_FEEDBACK_CYCLE');
  assert.equal(planM4OperatorIntent(accepted(),{kind:'SEND_POST',
    sourceId:ids.v,destinationId:ids.v}).error,'SELF_ROUTE_FORBIDDEN');
});
test('duplicate path and prohibited output-to-input routing are rejected',()=>{
  const s=base();
  s.graph.edges.push({edgeId:'8',from:ids.v,to:ids.wet,
    tap:'POST_FADER',gainLinear:1,primaryOutput:false,enabled:true,signalKind:'AUDIO'});
  assert.equal(planM4OperatorIntent(accepted(s),{kind:'SEND_POST',
    sourceId:ids.v,destinationId:ids.wet}).error,'DUPLICATE_NATIVE_ROUTE');
  assert.equal(planM4OperatorIntent(accepted(),{kind:'SEND_POST',
    sourceId:ids.main,destinationId:ids.group}).error,'OUTPUT_BUS_CANNOT_SOURCE_SEND');
  assert.equal(planM4OperatorIntent(accepted(),{kind:'SEND_POST',
    sourceId:ids.group,destinationId:ids.kick}).error,
    'INPUT_NOT_A_ROUTING_DESTINATION');
});
test('nonverified FX wet-only routing refuses double-dry risk',()=>{
  const s=base();delete s.graph.channels[3].wetOnly;
  assert.equal(planM4OperatorIntent(accepted(s),{kind:'SEND_POST',
    sourceId:ids.v,destinationId:ids.wet}).error,'FX_RETURN_WET_ONLY_NOT_VERIFIED');
});
test('send level/ PRE send/ structural bus creation never fake accepted effects',()=>{
  const current=accepted();
  const requests=[
    ['SEND_PRE','NATIVE_PRE_FADER_WIRE_NOT_ADMITTED'],
    ['SET_SEND_GAIN','SEND_GAIN_ACK_READBACK_NOT_AVAILABLE'],
    ['GROUP_SELECTED','GROUP_CREATE_ACK_READBACK_UNIMPLEMENTED'],
    ['NEW_FX_RETURN','FX_RETURN_CREATE_ACK_READBACK_UNIMPLEMENTED']];
  for(const [kind,error] of requests){
    const p=planM4OperatorIntent(current,{kind,sourceId:ids.v,
      destinationId:ids.group});
    assert.equal(p.ok,false);assert.equal(p.error,error);assert.equal(p.intent,undefined);
  }
  assert.equal(planM4OperatorIntent(current,{kind:'SEND_POST',
    sourceId:ids.v,destinationId:ids.wet,gainDb:-18}).error,
    'SEND_GAIN_ACK_READBACK_NOT_AVAILABLE');
});
test('uint64 identities remain exact strings; malformed ids and stale capability fail',()=>{
  const current=accepted();
  for(const bad of ['01','18446744073709551616',1,9007199254740993,'0']){
    assert.equal(planM4OperatorIntent(current,{kind:'ROUTE_ONLY',
      sourceId:bad,destinationId:ids.group}).ok,false);
  }
  const s=base();s.graph.capabilities.supportedOps=[];
  assert.equal(planM4OperatorIntent(accepted(s),{kind:'ROUTE_ONLY',
    sourceId:ids.v,destinationId:ids.group}).error,
    'NATIVE_WIRE_OPERATION_UNAVAILABLE');
});
test('sidechain is NEVER an audible WIRE edge and cannot claim a missing VST3 bus',()=>{
  const s=base();s.graph.channels[0].inserts.push({
    instanceId:'native-vst-1',name:'Compressor',bypassed:false});
  let p=planM4OperatorIntent(accepted(s),{kind:'SIDECHAIN',
    sourceId:ids.kick,destinationId:ids.v,pluginInstanceId:'native-vst-1'});
  assert.equal(p.error,'VST3_AUX_DETECTOR_BUS_NOT_NEGOTIATED');
  assert.equal(p.intent,undefined);
  s.graph.capabilities.supportedOps.push('CONNECT_PLUGIN_SIDECHAIN');
  s.graph.capabilities.nativeSidechainBusReady=true;
  p=planM4OperatorIntent(accepted(s),{kind:'SIDECHAIN',
    sourceId:ids.kick,destinationId:ids.v,pluginInstanceId:'native-vst-1',
    nativeAuxInput:{pluginInstanceId:'native-vst-1',busIndex:1,
      channels:2,enabled:true,detectorOnly:true,latencySamples:0,compensated:true}});
  assert.equal(p.error,'SIDECHAIN_ACK_READBACK_UNIMPLEMENTED');
  assert.equal(p.intent,undefined,'unproven detector-only route is never dispatched');
});
test('sidechain feedback dependency fails independently of audible double-dry',()=>{
  const s=base();s.graph.edges[0]={...s.graph.edges[0],to:ids.group};
  s.graph.channels[0].primaryOutId=ids.group;
  const p=planM4OperatorIntent(accepted(s),{kind:'SIDECHAIN',
    sourceId:ids.group,destinationId:ids.v,pluginInstanceId:'x'});
  assert.equal(p.error,'SIDECHAIN_DEPENDENCY_CYCLE');
});
test('PDC policy blocks unknown and unaligned latency, preserves live monitor',()=>{
  assert.equal(assessM4Pdc({}).error,'PLUGIN_LATENCY_UNVERIFIED');
  assert.equal(assessM4Pdc({latencySamples:-1}).ok,false);
  assert.equal(assessM4Pdc({latencySamples:128,compensated:false}).error,
    'NATIVE_GRAPH_PDC_UNAVAILABLE');
  assert.equal(assessM4Pdc({latencySamples:128,compensated:true,
    monitorMode:'LIVE_LOW_LATENCY'}).error,
    'LIVE_MONITOR_LATENCY_POLICY_CONFLICT');
  assert.deepEqual(assessM4Pdc({latencySamples:128,compensated:true,
    monitorMode:'STUDIO_COMPENSATED'}),{ok:true,latencySamples:128});
});
test('no version of plan can update browser acceptance without ACK + readback',()=>{
  const current=accepted(),before=JSON.stringify(current.raw);
  const p=planM4OperatorIntent(current,{kind:'ROUTE_ONLY',
    sourceId:ids.v,destinationId:ids.group});
  assert.equal(p.ok,true);
  assert.equal(Object.hasOwn(p,'acceptedGraph'),false);
  assert.equal(Object.hasOwn(p,'productionCallbackLive'),false);
  assert.equal(JSON.stringify(current.raw),before);
});

test('Route Only ACK never approves new GROUP cable with old MAIN double-dry',()=>{
  const raw=base(),snapshot=accepted(raw);
  const plan=planM4OperatorIntent(snapshot,{kind:'ROUTE_ONLY',
    sourceId:ids.v,destinationId:ids.group});
  assert.ok(plan.ok);
  const controller=createController({emit:()=>{}});
  assert.equal(controller.onSnapshot(raw),true);
  const req=controller.intent(plan.intent.operation,plan.intent.targetChannelId,
    plan.intent.payload);
  assert.equal(req.ok,true);
  controller.onAck({requestId:req.requestId,status:'ACCEPTED',errorCode:'NONE',
    deviceEpoch:'69',graphRevision:'4',rosterRevision:'2'});
  const lied=clone(raw);lied.graph.revision='4';
  lied.graph.channels[0].primaryOutId=ids.group;
  lied.graph.edges.push({edgeId:'9',from:ids.v,to:ids.group,
    primaryOutput:true,tap:'POST_FADER',gainLinear:1,enabled:true,signalKind:'AUDIO'});
  assert.equal(controller.onSnapshot(lied),false,'old MAIN still audible; reject');
  assert.equal(controller.state().lastError,'ACK_READBACK_EFFECT_MISMATCH');
  assert.equal(controller.state().current.graph.revision,'3');

  const validController=createController({emit:()=>{}});
  validController.onSnapshot(raw);
  const good=validController.intent(plan.intent.operation,plan.intent.targetChannelId,
    plan.intent.payload);
  validController.onAck({requestId:good.requestId,status:'ACCEPTED',errorCode:'NONE',
    deviceEpoch:'69',graphRevision:'4',rosterRevision:'2'});
  const actual=clone(raw);actual.graph.revision='4';
  actual.graph.channels[0].primaryOutId=ids.group;
  actual.graph.edges[0].to=ids.group;
  assert.equal(validController.onSnapshot(actual),true);
  assert.equal(validController.state().current.graph.channels[0].primaryOutId,ids.group);
});

test('POST send ACK requires actual accepted gain and tap, not cable existence',()=>{
  const raw=base(),snapshot=accepted(raw);
  const plan=planM4OperatorIntent(snapshot,{kind:'SEND_POST',
    sourceId:ids.v,destinationId:ids.wet,gainDb:0});
  assert.ok(plan.ok);
  const check=(gain,tap)=>{
    const c=createController({emit:()=>{}});
    c.onSnapshot(raw);
    const req=c.intent(plan.intent.operation,plan.intent.targetChannelId,
      plan.intent.payload);
    c.onAck({requestId:req.requestId,status:'ACCEPTED',errorCode:'NONE',
      deviceEpoch:'69',graphRevision:'4',rosterRevision:'2'});
    const next=clone(raw);next.graph.revision='4';
    next.graph.edges.push({edgeId:'90',from:ids.v,to:ids.wet,
      primaryOutput:false,tap,gainLinear:gain,enabled:true,signalKind:'AUDIO'});
    return {pass:c.onSnapshot(next),error:c.state().lastError};
  };
  assert.equal(check(0.5,'POST_FADER').pass,false);
  assert.equal(check(1,'PRE_FADER').pass,false);
  assert.equal(check(1,'POST_FADER').pass,true);
});
