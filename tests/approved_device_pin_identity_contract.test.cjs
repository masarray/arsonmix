// SPDX-License-Identifier: AGPL-3.0-only
// MV-8: Device Setup pin identity is an operator request, never PCM authority.
const test=require("node:test");
const assert=require("node:assert/strict");
const {readFileSync}=require("node:fs");
const vm=require("../web/approved-device-view-model.js");

function fixture(){
  const active={
    backend:"ASIO",captureDevice:"Studio ASIO",playbackDevice:"Studio ASIO",
    deviceGeneration:40,open:true,authorized:true,rate:48000,buffer:256,
    inputNames:["Mic 1","Mic 2","Mic 3","Mic 4"],
    outputNames:["Main L","Main R","Phones L","Phones R"],
    enabledInputs:[0,1],enabledOutputs:[0,1],mainOutputs:[0,1]
  };
  const inventory={active,types:[{
    name:"ASIO",separate:false,inputs:["Studio ASIO","Other ASIO"],
    outputs:["Studio ASIO","Other ASIO"]
  },{
    name:"Windows Audio",separate:true,inputs:["Microphone"],
    outputs:["Speakers"]
  }]};
  const draft={backend:"ASIO",capture:"Studio ASIO",playback:"Studio ASIO",
    inputPins:[0,2],outputPins:[2,3],rate:48000,buffer:256};
  const begin=(requestId="pin-1",d=draft,inv=inventory)=>
    vm.setupBegin(vm.setupInitial(),{inventory:inv,draft:d,
      requestId,baselineEpoch:"40"});
  const ack=(requestId="pin-1",overrides={})=>({
    requestId,ok:true,state:"ACCEPTED_MUTED",
    active:{...active,deviceGeneration:41,enabledInputs:[2,0],
      enabledOutputs:[3,2]},mixerBound:true,outputMuted:true,
    ...overrides
  });
  const readback={
    raw:{schemaVersion:6},admitted:false,retained:false,productLive:false,
    device:{epoch:"41",backend:active.backend,
      captureDevice:active.captureDevice,playbackDevice:active.playbackDevice,
      sampleRate:48000,bufferFrames:256,enabledInputs:[0,2],
      enabledOutputs:[2,3],outputArmed:false}
  };
  return {active,inventory,draft,begin,ack,readback};
}

test("MV-8 same-device Apply remembers explicit physical pin sets, not callback order",()=>{
  const f=fixture(),original=JSON.stringify(f.inventory);
  const begun=f.begin();
  assert.ok(begun);
  assert.deepEqual(begun.request.config.enabledInputs,[0,2]);
  assert.deepEqual(begun.request.config.enabledOutputs,[2,3]);
  assert.deepEqual(begun.state.pending.expected.requestedPins,{
    inputs:[0,2],outputs:[2,3]});
  const v=vm.setupAck(begun.state,f.ack());
  assert.equal(v.phase,"VERIFYING",
    "same physical sets in different order are still exactly installed pins");
  assert.equal(v.pending.ack.active.deviceGeneration,41);
  assert.equal(vm.setupReadback(v,{...f.readback,device:{
    ...f.readback.device,enabledInputs:[2,0],enabledOutputs:[3,2]
  }}).phase,"ACCEPTED");
  assert.equal(vm.setupReadback(v,f.readback).phase,"ACCEPTED");
  assert.equal(JSON.stringify(f.inventory),original,
    "DeviceVM cannot edit authoritative JUCE inventory");
});

test("MV-8 native accepted ACK with changed output pins never claims selected hardware",()=>{
  const f=fixture(),begun=f.begin("pin-2");
  const acceptedDifferent=f.ack("pin-2",{active:{
    ...f.active,deviceGeneration:41,
    enabledInputs:[0,2],enabledOutputs:[0,1]
  }});
  const verdict=vm.setupAck(begun.state,acceptedDifferent);
  assert.equal(verdict.phase,"RECOVERY_REQUIRED");
  assert.equal(verdict.pending,null);
  assert.match(verdict.message,/pins differ/i);
  assert.doesNotMatch(verdict.message,/Previous device restored|Output live/);
  assert.equal(vm.setupReadback(verdict,f.readback),verdict);
  assert.equal(vm.setupAck(verdict,f.ack("pin-2")),verdict,
    "late correct ACK cannot resurrect a finished mismatched request");
});

test("MV-8 altered capture mask and missing pins are rejected without claiming rollback",()=>{
  const f=fixture(),begun=f.begin("pin-3");
  const mismatchedInput=vm.setupAck(begun.state,f.ack("pin-3",{
    active:{...f.active,deviceGeneration:41,
      enabledInputs:[1,2],enabledOutputs:[2,3]}
  }));
  assert.equal(mismatchedInput.phase,"RECOVERY_REQUIRED");
  for(const output of [[2,2],[3,65],[2],[3,2,3],["2",3]]){
    const state=vm.setupAck(begun.state,f.ack("pin-3",{
      active:{...f.active,deviceGeneration:41,
        enabledInputs:[0,2],enabledOutputs:output}
    }));
    assert.notEqual(state.phase,"VERIFYING",
      "bad hardware evidence must never pass into VERIFYING");
    assert.notEqual(state.phase,"ACCEPTED");
  }
});

test("MV-8 stale, duplicate, out-of-range and fractional draft masks cannot apply",()=>{
  const f=fixture();
  for(const [inputPins,outputPins] of [
    [[0,0],[2,3]],[[0,2],[2,2]],[[0,2],[2,4]],
    [[0,2],[2,64]],[[1.5],[2,3]],[[0,2],[2]],
    [["0",2],[2,3]],[[0,2],[2,3,3]],[[0,-1],[2,3]]
  ]){
    const d={...f.draft,inputPins,outputPins};
    const view=vm.setupPresentation(f.inventory,d);
    assert.equal(view.canApply,false,JSON.stringify(d));
    assert.equal(view.canAssignMain,false);
    assert.equal(f.begin("bad-"+String(outputPins),d),null);
  }
  assert.equal(vm.setupPresentation(f.inventory,f.draft).canApply,true);
  assert.equal(vm.setupPresentation(f.inventory,f.draft).canAssignMain,true);
});

test("MV-8 readback mismatch remains pending after accepted ACK, then bounded timeout",()=>{
  const f=fixture();
  const pending=vm.setupAck(f.begin("pin-4").state,f.ack("pin-4"));
  assert.equal(pending.phase,"VERIFYING");
  assert.equal(vm.setupReadback(pending,{...f.readback,device:{
    ...f.readback.device,enabledOutputs:[0,1]
  }}),pending,"different driver output pins cannot settle this Apply");
  assert.equal(vm.setupReadback(pending,{...f.readback,device:{
    ...f.readback.device,enabledInputs:[1,2]
  }}),pending,"different physical capture pins cannot settle this Apply");
  const old={...f.readback,device:{...f.readback.device,epoch:"39"}};
  assert.equal(vm.setupReadback(pending,old).phase,"STALE");
  const timeout=vm.setupTimeout(pending,"pin-4");
  assert.equal(timeout.phase,"TIMEOUT");
  assert.equal(vm.setupReadback(timeout,f.readback),timeout);
});

test("MV-8 NEW device has driver-selected pins, never an old device mask",()=>{
  const f=fixture(),next={...f.draft,
    backend:"Windows Audio",capture:"",playback:"Speakers",
    inputPins:null,outputPins:null,rate:0,buffer:0};
  assert.equal(vm.setupPresentation(f.inventory,next).canApply,true);
  const begun=f.begin("new-io",next);
  assert.ok(begun);
  assert.deepEqual(begun.request.config.enabledInputs,[]);
  assert.deepEqual(begun.request.config.enabledOutputs,[]);
  assert.equal(begun.state.pending.expected.requestedPins,null);
  const ack=f.ack("new-io",{state:"ACCEPTED_MUTED",active:{
    ...f.active,deviceGeneration:41,backend:"Windows Audio",
    captureDevice:"",playbackDevice:"Speakers",enabledInputs:[],
    enabledOutputs:[0,1],rate:48000,buffer:256
  }});
  const verifying=vm.setupAck(begun.state,ack);
  assert.equal(verifying.phase,"VERIFYING");
  const readback={...f.readback,device:{
    ...f.readback.device,backend:"Windows Audio",captureDevice:"",
    playbackDevice:"Speakers",enabledInputs:[],enabledOutputs:[1,0]
  }};
  assert.equal(vm.setupReadback(verifying,readback).phase,"ACCEPTED");
});

test("MV-8 DeviceVM remains pure; exactly one unchanged native Apply transport",()=>{
  const source=readFileSync("web/approved-device-view-model.js","utf8");
  const bridge=readFileSync("web/approved-native-bridge.js","utf8");
  const region=bridge.slice(bridge.indexOf("  // Compact DAW-style device preferences"),
    bridge.indexOf("  function installFirstSoundMenu()"));
  assert.match(source,/function validPinMask\(/);
  assert.match(source,/function samePinMask\(/);
  assert.match(source,/requestedPins:same\?/);
  assert.match(source,/samePinMask\(requested.inputs,active.enabledInputs\)/);
  assert.match(source,/samePinMask\(d.enabledOutputs,expected.enabledOutputs\)/);
  assert.match(region,/deviceVM\.setupAck\(deviceState,ack\)/);
  assert.match(bridge,/deviceVM\.setupReadback\(deviceState,next\)/);
  assert.equal((region.match(/backend\.emitEvent\("arMixAction",begun\.request\)/g)||[]).length,1);
  assert.doesNotMatch(source,/document\.|window\.|AudioWorklet|new AudioContext|localStorage/);
});


test("MV-12 native lifecycle lease preserves exact uint64 device identity",()=>{
  const base={device:{epoch:"18446744073709551615",backend:"ASIO",
    captureDevice:"A",playbackDevice:"A"},graph:{revision:"9"}};
  const original=JSON.stringify(base);
  const lease=vm.nativeLifecycleLease(base,"CHANNEL_ID");
  assert.deepEqual(lease,{epoch:"18446744073709551615",backend:"ASIO",
    captureDevice:"A",playbackDevice:"A",authority:"CHANNEL_ID"});
  assert.equal(Object.isFrozen(lease),true);
  assert.equal(JSON.stringify(base),original);
  for(const epoch of [18446744073709551615,"01","-1","1.5","",
    "18446744073709551616",null]) {
    assert.equal(vm.nativeLifecycleLease({...base,
      device:{...base.device,epoch}},"CHANNEL_ID"),null,
      "rounding, overflow and noncanonical epoch must fail closed");
  }
  for(const kind of ["INVALID","DEMO","",null])
    assert.equal(vm.nativeLifecycleLease(base,kind),null);
  assert.equal(vm.nativeLifecycleLease({device:{...base.device,
    captureDevice:undefined}},"CHANNEL_ID"),null);
});

test("MV-12 native lifecycle expires old device intents even when both graphs are admitted",()=>{
  const s=(epoch="41",backend="ASIO",name="Studio ASIO")=>
    ({device:{epoch,backend,captureDevice:name,playbackDevice:name}});
  const first=vm.nativeLifecycleLease(s(),"CHANNEL_ID");
  const same=vm.nativeLifecycleLease({...s(),graph:{revision:"500",
    rosterRevision:"700"}},"CHANNEL_ID");
  assert.deepEqual(vm.nativeLifecycleDecision(null,first),
    {kind:"INITIAL",expirePending:false});
  assert.deepEqual(vm.nativeLifecycleDecision(first,same),
    {kind:"SAME",expirePending:false},
    "graph/roster revisions and scalar telemetry cannot cancel same-device gestures");
  const afterReconnect=vm.nativeLifecycleLease(s("42"),"CHANNEL_ID");
  assert.deepEqual(vm.nativeLifecycleDecision(first,afterReconnect),
    {kind:"DEVICE_CHANGED",expirePending:true},
    "back-to-back admitted PCM snapshots must still invalidate old leases");
  const otherDriver=vm.nativeLifecycleLease(s("41","ASIO","Backup ASIO"),"CHANNEL_ID");
  assert.equal(vm.nativeLifecycleDecision(first,otherDriver).kind,"DEVICE_CHANGED",
    "same-epoch endpoint replacement is never the old physical owner");
  assert.equal(vm.nativeLifecycleDecision(first,{...first,
    backend:"Windows Audio"}).expirePending,true);
  assert.equal(vm.nativeLifecycleDecision(first,null).kind,"INVALID");
});

test("MV-12 authority transitions expire stale M4/ChannelId work; no duplicate owner",()=>{
  const snapshot={device:{epoch:"91",backend:"ASIO",
    captureDevice:"One",playbackDevice:"One"}};
  const mk=kind=>vm.nativeLifecycleLease(snapshot,kind);
  for(const [a,b] of [["M4","CHANNEL_ID"],["CHANNEL_ID","NEEDS_REMAP"],
    ["NEEDS_REMAP","CHANNEL_ID"],["LOGICAL_ONLY","CHANNEL_ID"],
    ["CHANNEL_ID","UNADMITTED"],["UNADMITTED","M4"],
    ["STALE","CHANNEL_ID"]]) {
    assert.deepEqual(vm.nativeLifecycleDecision(mk(a),mk(b)),
      {kind:"AUTHORITY_CHANGED",expirePending:true},a+" -> "+b);
  }
  for(const kind of ["M4","CHANNEL_ID","LOGICAL_ONLY","NEEDS_REMAP",
    "UNADMITTED","STALE"])
    assert.equal(vm.nativeLifecycleDecision(mk(kind),mk(kind)).expirePending,
      false,"identical authority must not erase pending current work");
  const source=readFileSync("web/approved-device-view-model.js","utf8");
  assert.doesNotMatch(source,/document\.|window\.|AudioWorklet|new AudioContext|localStorage/);
});

test("MV-12 approved bridge invalidates transient UI leases, not durable JUCE state",()=>{
  const source=readFileSync("web/approved-native-bridge.js","utf8");
  const start=source.indexOf("  function expireTransientNativeUi(reason) {");
  const end=source.indexOf("  function onNativeReadback(next,confirmed)",start);
  assert.ok(start>0&&end>start);
  const cleanup=source.slice(start,end);
  for(const token of ["pendingNativeSources.clear()","pendingSourceQueue.clear()",
    "pendingNativeControl.clear()","liveFaderVisualTargets.clear()",
    "lastNativeControlSent.clear()","pendingWireGains.clear()",
    "pendingM4Controls.clear()","pendingM4RackPowers.clear()",
    "pendingNativeFxPower=null","pendingGlobalFx=null",
    "activeNativeSource=null","pendingIdentity"] )
    assert.ok(cleanup.includes(token),"missing old transaction cancellation: "+token);
  assert.doesNotMatch(cleanup,/backend\.emitEvent|arMixAction|routingNative|audioDeviceApply|\.smix|new AudioContext/);
  const readback=source.slice(end,source.indexOf("  if (connected) {\n    controller = protocol.createController",end));
  assert.match(readback,/deviceVM\.nativeLifecycleLease\(next,readModel\.kind\)/);
  assert.match(readback,/deviceVM\.nativeLifecycleDecision\(lastNativeLifecycleLease,lease\)/);
  assert.match(readback,/if\(lifecycle\.expirePending\)/);
  assert.match(readback,/confirmed=null;/);
  assert.ok(readback.indexOf("if(readModel.kind===\"INVALID\")")<
    readback.indexOf("deviceVM.nativeLifecycleLease("),
    "invalid raw readback must not create a new device owner");
  assert.ok(readback.indexOf("expireTransientNativeUi(lifecycle.kind)")<
    readback.indexOf("appearanceFromNative(next)"),
    "stale cosmetic ACK cannot settle before device identity fence");
});
