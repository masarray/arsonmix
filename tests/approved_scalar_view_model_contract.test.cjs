// SPDX-License-Identifier: AGPL-3.0-only
// MV-9: pure Channel Strip scalar policy, with ONE native bridge/PCM authority.
const test=require("node:test");
const assert=require("node:assert/strict");
const {readFileSync}=require("node:fs");
const vm=require("../web/approved-channel-strip-view-model.js");

const bigId="9007199254740993";
const bigNext="9007199254740994";
const bigEpoch="18446744073709551610";
const identity={epoch:bigEpoch,authority:"CHANNEL_ID",channelId:bigId};
const edit={...identity,parameter:"faderDb",value:-8.5};
const m4Epoch="14",m4Revision="23";
function m4Snapshot({
  epoch=m4Epoch,revision=m4Revision,authority="M4",
  fader=-7.5,pan=.25,mute=false,solo=false,master=-3.0
}={}) {
  return {device:{epoch},raw:{
    legacyM4:{routingRevision:revision},
    mixerSession:{authority,routeRevision:revision,
      channels:Array.from({length:10},(_,i)=>({
        controls:{faderDb:i===2?fader:0,pan:i===2?pan:0,
          mute:i===2?mute:false,solo:i===2?solo:false}
      })),main:{db:master,mute:false}}}};
}
const m4Pending=(parameter,value,channel=2)=>({
  requestId:"m4-45",channel,parameter,value,
  epoch:m4Epoch,revision:m4Revision,acked:false
});
const ack=(p,extra={})=>({
  requestId:p.requestId,deviceEpoch:Number(p.epoch),
  routingRevision:Number(p.revision),channel:p.channel,
  parameter:p.parameter,ok:true,...extra
});

test("MV-9 exact native edit identity never rounds uint64 channel/device values",()=>{
  assert.equal(vm.scalarIdentityMatches(edit,identity),true);
  assert.equal(vm.scalarIdentityMatches(edit,{...identity,channelId:bigNext}),false);
  assert.equal(vm.scalarIdentityMatches(edit,{...identity,epoch:"18446744073709551611"}),false);
  assert.equal(vm.scalarIdentityMatches(edit,{...identity,authority:"M4"}),false);
  assert.equal(vm.scalarIdentityMatches(edit,{...identity,channelId:Number(bigId)}),false);
  assert.equal(vm.scalarIdentityMatches(null,identity),false);
  assert.equal(vm.scalarIdentityMatches(edit,null),false);
});

test("MV-9 last fader cap remains display-only until separately accepted readback",()=>{
  assert.equal(vm.scalarVisualSettlement(null,-8.5,identity),"NONE");
  assert.equal(vm.scalarVisualSettlement(edit,-12.5,identity),"WAIT");
  assert.equal(vm.scalarVisualSettlement(edit,-8.41,identity),"CONFIRMED");
  assert.equal(vm.scalarVisualSettlement(edit,-8.62,identity),"WAIT");
  assert.equal(vm.scalarVisualSettlement(edit,NaN,identity),"WAIT");
  assert.equal(vm.scalarVisualSettlement(edit,Infinity,identity),"WAIT");
  assert.equal(vm.scalarVisualSettlement({...edit,value:NaN},0,identity),"STALE");
  assert.equal(vm.scalarVisualSettlement(edit,-8.5,{...identity,channelId:bigNext}),"STALE",
    "old channel's target cannot override the next channel's fader");
  assert.equal(vm.scalarVisualSettlement(edit,-8.5,{...identity,epoch:"18446744073709551612"}),"STALE",
    "device reconnect must drop last gesture display");
});

test("MV-9 M4 ACK must correlate exact request/channel, epoch, revision, parameter",()=>{
  const p=m4Pending("faderDb",-7.5);
  assert.equal(vm.m4ScalarAckResult(null,ack(p)),"IGNORE");
  assert.equal(vm.m4ScalarAckResult(p,{...ack(p),requestId:"m4-44"}),"IGNORE");
  assert.equal(vm.m4ScalarAckResult(p,ack(p)),"ACKED");
  for(const patch of [
    {ok:false},{channel:3},{parameter:"pan"},{deviceEpoch:15},
    {routingRevision:24},{routingRevision:null}
  ])assert.equal(vm.m4ScalarAckResult(p,ack(p,patch)),"REJECTED",
      "stale or wrong ACK is not installed native scalar proof");
});

test("MV-9 M4 fader ACK alone is not accepted fader readback",()=>{
  const p=m4Pending("faderDb",-7.5),unchanged=m4Snapshot({fader:0});
  assert.equal(vm.m4ScalarReadbackSettlement(p,unchanged),"WAIT");
  assert.equal(vm.m4ScalarReadbackSettlement({...p,acked:true},unchanged),"WAIT");
  const readback=m4Snapshot();
  assert.equal(vm.m4ScalarReadbackSettlement(p,readback),"WAIT");
  assert.equal(vm.m4ScalarReadbackSettlement({...p,acked:true},readback),"CONFIRMED");
  assert.equal(vm.m4ScalarReadbackSettlement({...p,acked:true},
    m4Snapshot({fader:-7.63})),"WAIT");
});

test("MV-9 native M4 scalar is invalidated by route/epoch and authority changes",()=>{
  const p={...m4Pending("faderDb",-7.5),acked:true};
  assert.equal(vm.m4ScalarReadbackSettlement(p,m4Snapshot({epoch:"15"})),"STALE");
  assert.equal(vm.m4ScalarReadbackSettlement(p,m4Snapshot({revision:"24"})),"STALE");
  assert.equal(vm.m4ScalarReadbackSettlement(p,m4Snapshot({authority:"CHANNEL_ID"})),"STALE");
  assert.equal(vm.m4ScalarReadbackSettlement(p,null),"STALE");
  assert.equal(vm.m4ScalarReadbackSettlement(null,m4Snapshot()),"NONE");
});

test("MV-9 M4 PAN conversion preserves percent target and existing tolerance",()=>{
  const pan={...m4Pending("pan",25),acked:true};
  assert.equal(vm.m4ScalarReadbackSettlement(pan,m4Snapshot({pan:.25})),"CONFIRMED");
  assert.equal(vm.m4ScalarReadbackSettlement(pan,m4Snapshot({pan:.257})),"CONFIRMED");
  assert.equal(vm.m4ScalarReadbackSettlement(pan,m4Snapshot({pan:.26})),"WAIT");
  assert.equal(vm.m4ScalarReadbackSettlement(pan,m4Snapshot({pan:NaN})),"WAIT");
  assert.equal(vm.m4ScalarReadbackSettlement({...pan,value:NaN},m4Snapshot()),"WAIT");
  assert.equal(vm.m4ScalarReadbackSettlement({...pan,channel:9},m4Snapshot()),"WAIT",
    "an unrelated channel cannot satisfy this M4 PAN edit");
});

test("MV-9 M4 MUTE/SOLO boolean state cannot be confirmed by truthy coercion",()=>{
  const mute={...m4Pending("mute",true),acked:true};
  const solo={...m4Pending("solo",false),acked:true};
  assert.equal(vm.m4ScalarReadbackSettlement(mute,m4Snapshot({mute:true})),"CONFIRMED");
  assert.equal(vm.m4ScalarReadbackSettlement(mute,m4Snapshot({mute:false})),"WAIT");
  assert.equal(vm.m4ScalarReadbackSettlement(solo,m4Snapshot({solo:false})),"CONFIRMED");
  assert.equal(vm.m4ScalarReadbackSettlement(solo,m4Snapshot({solo:true})),"WAIT");
});

test("MV-9 M4 MAIN gain uses existing legacy routing epoch not ChannelId revision",()=>{
  const main={...m4Pending("masterGain",-3,-1),acked:true};
  assert.equal(vm.m4ScalarReadbackSettlement(main,m4Snapshot()),"CONFIRMED");
  assert.equal(vm.m4ScalarReadbackSettlement(main,m4Snapshot({master:2})),"WAIT");
  const changed=m4Snapshot();changed.raw.legacyM4.routingRevision="24";
  assert.equal(vm.m4ScalarReadbackSettlement(main,changed),"STALE");
  const other=m4Snapshot({authority:"CHANNEL_ID"});
  assert.equal(vm.m4ScalarReadbackSettlement(main,other),"CONFIRMED",
    "existing native MAIN host scalar is not M4 channel ordinal");
});

test("MV-9 bridge uses exactly one real scalar transport and pure decisions",()=>{
  const model=readFileSync("web/approved-channel-strip-view-model.js","utf8");
  const bridge=readFileSync("web/approved-native-bridge.js","utf8");
  assert.match(bridge,/stripVM\.scalarIdentityMatches\(edit,nativeEditIdentity\(edit\.t\)\)/);
  assert.match(bridge,/stripVM\.scalarVisualSettlement\(pending,actualDb,/);
  assert.match(bridge,/stripVM\.m4ScalarReadbackSettlement\(p,next\)/);
  assert.match(bridge,/stripVM\.m4ScalarAckResult\(p,ack\)/);
  assert.match(bridge,/if\(!Number\.isFinite\(value\)\|\|!liveScalarReady\(parameter,t\)\)return false;/);
  assert.equal((bridge.match(/backend\.addEventListener\("arMixM4ControlAck"/g)||[]).length,1);
  assert.equal((bridge.match(/action:"routingM4Control"/g)||[]).length,1);
  assert.doesNotMatch(model,/document\.|window\.|backend\.emitEvent|localStorage|AudioContext/);
});
