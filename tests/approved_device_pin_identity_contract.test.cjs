// SPDX-License-Identifier: AGPL-3.0-only
// MV-8: Device Setup pin identity is an operator request, never PCM authority.
const test=require("node:test");
const assert=require("node:assert/strict");
const {readFileSync}=require("node:fs");
const vm=require("../web/approved-device-view-model.js");
const protocol=require("../web/approved-native-contract.js");
// The v6 controller is the only normalizer of installed physical identity.
// Minimal VM fixtures explicitly reuse that canonical read-only fingerprint.
const lease=(snapshot,kind)=>vm.nativeLifecycleLease({
  ...snapshot,
  hardwareConfiguration:protocol.nativeHardwareConfiguration(snapshot?.device)
},kind);

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

test("G-MV21 Device Setup first ACK wins even if native sends contradictory duplicates",()=>{
 const f=fixture(),begun=f.begin("ack-once");
 assert.ok(begun);
 const accepted=f.ack("ack-once"),verifying=vm.setupAck(begun.state,accepted);
 assert.equal(verifying.phase,"VERIFYING");
 const original=JSON.stringify(verifying);
 for(const duplicate of [accepted,
   {...accepted,ok:false,state:"REJECTED",error:"Late failed driver"},
   {...accepted,active:{...accepted.active,playbackDevice:"Wrong ASIO"}},
   {...accepted,active:{...accepted.active,enabledOutputs:[0,1]}},
   {...accepted,active:{...accepted.active,deviceGeneration:999}}]){
   assert.strictEqual(vm.setupAck(verifying,duplicate),verifying);
   assert.equal(JSON.stringify(verifying),original);
 }
 assert.equal(vm.setupReadback(verifying,f.readback).phase,"ACCEPTED");
 const expired=vm.setupTimeout(verifying,"ack-once");
 assert.equal(expired.phase,"TIMEOUT");
 assert.strictEqual(vm.setupAck(expired,accepted),expired);
});

test("G-MV21 only verified device readback refreshes Audio I/O inventory",()=>{
 const bridge=readFileSync("web/approved-native-bridge.js","utf8");
 const a=bridge.indexOf('backend.addEventListener("arMixAudioDeviceApplyAck",ack=>');
 const b=bridge.indexOf("  function installFirstSoundMenu()",a);
 assert.ok(a>0&&b>a);
 const ack=bridge.slice(a,b);
 assert.match(ack,/deviceVM\.setupAck\(deviceState,ack\)/);
 assert.match(ack,/if\(deviceBusy\(\)\)[\s\S]*?else\{[\s\S]*?finishDeviceApplyUi\(\);[\s\S]*?action:"audioDeviceInventory"/,
   "even rejected/NEEDS_REMAP outcomes must refresh native inventory for recovery");
 assert.doesNotMatch(ack,/deviceInventory\s*=|inventory\.active\s*=|updateHardwarePanel/,
   "unverified or rejected ACK cannot mutate native hardware inventory");
 const s=bridge.indexOf("  function onNativeReadback(next,confirmed) {");
 const e=bridge.indexOf("  if (connected) {\n    controller = protocol.createController",s);
 assert.ok(s>0&&e>s);
 const readback=bridge.slice(s,e);
 assert.match(readback,/deviceVM\.setupReadback\(deviceState,next\)/);
 assert.match(readback,/finishDeviceApplyUi\(next\)/);
 assert.match(readback,/deviceState\.phase==="ACCEPTED"&&connected[\s\S]*?action:"audioDeviceInventory"/);
 const finish=bridge.slice(bridge.indexOf("  function finishDeviceApplyUi("),a);
 assert.match(finish,/verifiedSnapshot\?\.device/);
 assert.match(finish,/active\?\.sampleRate/);
 assert.doesNotMatch(finish,/deviceInventory\?\.active/);
});
test("G-MV21 NEEDS_REMAP native inventory refresh restores reuse-pins recovery",()=>{
 const f=fixture(),begin=f.begin("needs-remap");
 const newActive={...f.active,deviceGeneration:42,enabledInputs:[0,1],
   enabledOutputs:[0,1]};
 const state=vm.setupAck(begin.state,{requestId:"needs-remap",
   ok:false,state:"NEEDS_REMAP",error:"NEEDS_REMAP: Confirm physical pins",
   canReusePins:true,active:newActive});
 assert.equal(state.phase,"REJECTED");
 assert.equal(state.remapGeneration,42);
 assert.equal(vm.setupPresentation(f.inventory,f.draft,state).canReusePins,false,
   "stale old endpoint generation cannot authorize a remap");
 const refreshed={...f.inventory,active:newActive};
 assert.equal(vm.setupPresentation(refreshed,f.draft,state).canReusePins,true,
   "fresh native inventory unlocks explicit recovery, not ack.active");
 const retry=vm.setupBegin(state,{inventory:refreshed,draft:f.draft,
   requestId:"needs-remap-retry",baselineEpoch:"42",
   confirmRetainedPinMapping:true});
 assert.ok(retry,"recovery request must use fresh endpoint generation");
 assert.equal(retry.request.expectedDeviceGeneration,42);
 assert.equal(retry.request.config.confirmRetainedPinMapping,true);
 assert.equal(retry.request.config.sampleRate,newActive.rate);
 assert.equal(retry.request.config.bufferSize,newActive.buffer);
 assert.deepEqual(f.inventory.active.enabledOutputs,[0,1],
   "negative Apply ACK cannot mutate old installed inventory");
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
    captureDevice:"A",playbackDevice:"A",sampleRate:48000,bufferFrames:256,
    enabledInputs:[1,0],enabledOutputs:[0,1]},graph:{revision:"9"}};
  const original=JSON.stringify(base);
  const identityLease=lease(base,"CHANNEL_ID");
  assert.deepEqual(identityLease,{epoch:"18446744073709551615",backend:"ASIO",
    captureDevice:"A",playbackDevice:"A",
    hardwareConfiguration:JSON.stringify([48000,256,[0,1],[0,1]]),
    authority:"CHANNEL_ID"});
  assert.equal(Object.isFrozen(identityLease),true);
  assert.equal(JSON.stringify(base),original);
  for(const epoch of [18446744073709551615,"01","-1","1.5","",
    "18446744073709551616",null]) {
    assert.equal(lease({...base,
      device:{...base.device,epoch}},"CHANNEL_ID"),null,
      "rounding, overflow and noncanonical epoch must fail closed");
  }
  for(const kind of ["INVALID","DEMO","",null])
    assert.equal(lease(base,kind),null);
  assert.equal(lease({device:{...base.device,
    captureDevice:undefined}},"CHANNEL_ID"),null);
});

test("MV-12 native lifecycle expires old device intents even when both graphs are admitted",()=>{
  const s=(epoch="41",backend="ASIO",name="Studio ASIO")=>
    ({device:{epoch,backend,captureDevice:name,playbackDevice:name,
      sampleRate:48000,bufferFrames:256,
      enabledInputs:[0,1],enabledOutputs:[0,1]}});
  const first=lease(s(),"CHANNEL_ID");
  const same=lease({...s(),graph:{revision:"500",
    rosterRevision:"700"}},"CHANNEL_ID");
  assert.deepEqual(vm.nativeLifecycleDecision(null,first),
    {kind:"INITIAL",expirePending:false});
  assert.deepEqual(vm.nativeLifecycleDecision(first,same),
    {kind:"SAME",expirePending:false},
    "graph/roster revisions and scalar telemetry cannot cancel same-device gestures");
  const afterReconnect=lease(s("42"),"CHANNEL_ID");
  assert.deepEqual(vm.nativeLifecycleDecision(first,afterReconnect),
    {kind:"DEVICE_CHANGED",expirePending:true},
    "back-to-back admitted PCM snapshots must still invalidate old leases");
  const otherDriver=lease(s("41","ASIO","Backup ASIO"),"CHANNEL_ID");
  assert.equal(vm.nativeLifecycleDecision(first,otherDriver).kind,"DEVICE_CHANGED",
    "same-epoch endpoint replacement is never the old physical owner");
  assert.equal(vm.nativeLifecycleDecision(first,{...first,
    backend:"Windows Audio"}).expirePending,true);
  assert.equal(vm.nativeLifecycleDecision(first,null).kind,"INVALID");
});

test("MV-12 authority transitions expire stale M4/ChannelId work; no duplicate owner",()=>{
  const snapshot={device:{epoch:"91",backend:"ASIO",
    captureDevice:"One",playbackDevice:"One",sampleRate:48000,
    bufferFrames:256,enabledInputs:[0,1],enabledOutputs:[0,1]}};
  const mk=kind=>lease(snapshot,kind);
  for(const [a,b] of [["M4","CHANNEL_ID"],["CHANNEL_ID","NEEDS_REMAP"],
    ["NEEDS_REMAP","CHANNEL_ID"],["LOGICAL_ONLY","CHANNEL_ID"],
    ["CHANNEL_ID","UNADMITTED"],["STALE","CHANNEL_ID"]]) {
    assert.deepEqual(vm.nativeLifecycleDecision(mk(a),mk(b)),
      {kind:"AUTHORITY_CHANGED",expirePending:true},a+" -> "+b);
  }
  assert.deepEqual(vm.nativeLifecycleDecision(mk("UNADMITTED"),mk("M4")),
    {kind:"M4_FIRST_SOUND",expirePending:false},
    "first accepted M4 input must not silently discard its own pending source");
  const switched={device:{...snapshot.device,epoch:"92"}};
  assert.deepEqual(vm.nativeLifecycleDecision(mk("UNADMITTED"),
    lease(switched,"M4")),
    {kind:"DEVICE_CHANGED",expirePending:true},
    "first-sound exemption must never bypass a physical device change");
  for(const kind of ["M4","CHANNEL_ID","LOGICAL_ONLY","NEEDS_REMAP",
    "UNADMITTED","STALE"])
    assert.equal(vm.nativeLifecycleDecision(mk(kind),mk(kind)).expirePending,
      false,"identical authority must not erase pending current work");
  const source=readFileSync("web/approved-device-view-model.js","utf8");
  assert.doesNotMatch(source,/document\.|window\.|AudioWorklet|new AudioContext|localStorage/);
});


test("MV-18 physical device swap cancels output and FX leases even at unchanged epoch",()=>{
 const snapshot=(backend,capture,playback)=>({device:{
   epoch:"18446744073709551614",backend,captureDevice:capture,
   playbackDevice:playback,sampleRate:48000,bufferFrames:256,
   enabledInputs:[0,1],enabledOutputs:[0,1]}});
 const old=lease(snapshot("ASIO","Old ASIO","Old ASIO"),"M4");
 const changed=lease(snapshot("ASIO","New ASIO","New ASIO"),"M4");
 assert.deepEqual(vm.nativeLifecycleDecision(old,changed),
   {kind:"DEVICE_CHANGED",expirePending:true},
   "a driver can change even if the epoch was reused");
 assert.equal(vm.nativeLifecycleDecision(old,{
   ...old,backend:"Windows Audio"}).expirePending,true);
 assert.deepEqual(vm.nativeLifecycleDecision(old,old),
   {kind:"SAME",expirePending:false});
 assert.deepEqual(vm.nativeLifecycleDecision(
   lease(snapshot("ASIO","Old ASIO","Old ASIO"),"UNADMITTED"),
   old),{kind:"M4_FIRST_SOUND",expirePending:false},
   "first M4 input patch must preserve its own pending source");
 const bridge=readFileSync("web/approved-native-bridge.js","utf8");
 const a=bridge.indexOf("  function discardTransientNativeUi({preserveRetainedIdentity=false}={}) {");
 const b=bridge.indexOf("  function onNativeReadback(next,confirmed)",a);
 const cleanup=bridge.slice(a,b);
 assert.match(cleanup,/pendingExternal\.clear\(\);pendingSlotPowers\.clear\(\)/);
 assert.match(cleanup,/function expireTransientNativeUi\(reason\)\s*\{\s*discardTransientNativeUi\(\)/,
   "physical change must use the same complete GUI lease discard");
 assert.doesNotMatch(cleanup,/backend\.emitEvent|audioDeviceApply|routingExternalOutput|routingFxBypass/,
   "UI cleanup cannot send native commands, reset FX, or reopen devices");
});

test("G-MV20 same-epoch native hardware remap invalidates transient GUI leases",()=>{
  const snapshot=(overrides={})=>({device:{
    epoch:"18446744073709551614",backend:"ASIO",
    captureDevice:"Studio ASIO",playbackDevice:"Studio ASIO",
    sampleRate:48000,bufferFrames:256,
    enabledInputs:[0,2],enabledOutputs:[0,1,4,5],
    outputArmed:false,...overrides}});
  const base=snapshot(),before=JSON.stringify(base);
  const old=lease(base,"M4");
  assert.ok(old);
  for(const changes of [
    {enabledInputs:[0,1]}, // physically different capture pins
    {enabledOutputs:[0,1]}, // hardware output pair disappeared
    {sampleRate:44100},
    {bufferFrames:512}
  ]) {
    const next=lease(snapshot(changes),"M4");
    assert.deepEqual(vm.nativeLifecycleDecision(old,next),
      {kind:"DEVICE_CHANGED",expirePending:true},
      "stale FX, input/output, and WIRE intents must not cross "+JSON.stringify(changes));
  }
  const reordered=lease(snapshot({
    enabledInputs:[2,0],enabledOutputs:[5,4,1,0],
    outputArmed:true}),"M4");
  assert.equal(reordered.hardwareConfiguration,old.hardwareConfiguration);
  assert.deepEqual(vm.nativeLifecycleDecision(old,reordered),
    {kind:"SAME",expirePending:false},
    "pin enumeration/monitor outputArmed must not reset current gestures");
  assert.deepEqual(vm.nativeLifecycleDecision(
    lease(snapshot({outputArmed:false}),"UNADMITTED"),reordered),
    {kind:"M4_FIRST_SOUND",expirePending:false},
    "first route must preserve queued second input on unchanged hardware");
  assert.deepEqual(vm.nativeLifecycleDecision(
    lease(snapshot({enabledInputs:[0,1]}),"UNADMITTED"),reordered),
    {kind:"DEVICE_CHANGED",expirePending:true},
    "first-sound exemption must not keep a source from a different pin mask");
  assert.equal(JSON.stringify(base),before,"no native snapshot mutation");
  for(const bad of [
    {enabledInputs:[0,0]},{enabledOutputs:[0,256]},
    {enabledInputs:[0,"2"]},{sampleRate:NaN},
    {bufferFrames:1.5},{enabledOutputs:null},{sampleRate:undefined}
  ])assert.equal(lease(snapshot(bad),"M4"),null,
    "incomplete or malformed hardware identity must fail closed");
  const bridge=readFileSync("web/approved-native-bridge.js","utf8");
  assert.match(bridge,/deviceVM\.nativeLifecycleDecision\(lastNativeLifecycleLease,lease\)/);
  assert.match(bridge,/if\(lifecycle\.expirePending\)\s*\{\s*expireTransientNativeUi\(lifecycle\.kind\)/);
});

test("MV-19 invalid read-model and malformed device identity cannot mutate hardware UI",()=>{
  const bridge=readFileSync("web/approved-native-bridge.js","utf8");
  const readbackStart=bridge.indexOf("  function onNativeReadback(next,confirmed) {");
  const readbackEnd=bridge.indexOf("  if (connected) {\n    controller = protocol.createController",readbackStart);
  assert.ok(readbackStart>0&&readbackEnd>readbackStart);
  const source=bridge.slice(readbackStart,readbackEnd);
  const at=token=>{
    const pos=source.indexOf(token);
    assert.ok(pos>=0,"MV-19 lost ordered admission step: "+token);
    return pos;
  };
  const selector=at("protocol.selectNativeReadModel(next,m4Verified(next))");
  const invalid=at('if(readModel.kind==="INVALID")');
  const lease=at("deviceVM.nativeLifecycleLease(next,readModel.kind)");
  const noLease=at("if(!lease)");
  const fence=at("lastNativeLifecycleLease=lease");
  const setup=at("deviceVM.setupReadback(deviceState,next)");
  const hardware=at("updateHardwarePanel(next)");
  const paint=at("paintExternalReadback(next)");
  const wirePaint=at("scope?.syncNativeWireGains?.([])");
  assert.ok(selector<invalid&&invalid<lease&&lease<noLease&&
    noLease<fence&&fence<wirePaint&&wirePaint<setup&&setup<hardware&&hardware<paint,
    "invalid/stale candidates must not change Device Setup, hardware or selector UI");
  assert.doesNotMatch(source.slice(0,fence),
    /updateHardwarePanel\(next\)|deviceVM\.setupReadback\(deviceState,next\)|syncNativeWireGains\?\.\(\[\]\)/,
    "no hardware mutation or drag reset before candidate admission");
  const verified=bridge.slice(bridge.indexOf("  const m4Verified="),
    bridge.indexOf("  // Live gestures choose the installed PCM authority."));
  assert.match(verified,/m4Verified=\(snapshot=hardware\)/);
  assert.match(verified,/snapshot\?\.raw\?\.legacyM4/);
  assert.doesNotMatch(verified,/const m=legacy\(\)/,
    "read-model M4 evidence must not use the prior hardware cache");
});

test("G-MV24 readback policy quarantines retained PCM without starving first M4 input",()=>{
 const policy=vm.nativeTransientReadbackPolicy;
 for(const [kind,expected] of [
   ["NEEDS_REMAP",{discardRetained:true,drainM4Sources:false,
     drainNativeSources:false}],
   ["CHANNEL_ID",{discardRetained:false,drainM4Sources:false,
     drainNativeSources:true}],
   ["M4",{discardRetained:false,drainM4Sources:true,
     drainNativeSources:false}],
   ["UNADMITTED",{discardRetained:false,drainM4Sources:true,
     drainNativeSources:false}],
   ["LOGICAL_ONLY",{discardRetained:false,drainM4Sources:false,
     drainNativeSources:false}],
   ["STALE",{discardRetained:false,drainM4Sources:false,
     drainNativeSources:false}],
   ["INVALID",{discardRetained:false,drainM4Sources:false,
     drainNativeSources:false}],
   [null,{discardRetained:false,drainM4Sources:false,
     drainNativeSources:false}]
 ]){
   const result=policy(kind);
   assert.deepEqual(result,expected,String(kind));
   assert.equal(Object.isFrozen(result),true);
 }
 assert.doesNotMatch(policy.toString(),/document|window|backend|AudioContext|setTimeout/,
   "readback quarantine decision belongs to the pure Device ViewModel");
 const bridge=readFileSync("web/approved-native-bridge.js","utf8");
 const start=bridge.indexOf("  function discardTransientNativeUi({preserveRetainedIdentity=false}={}) {");
 const expire=bridge.indexOf("  function expireTransientNativeUi(reason) {",start);
 const readback=bridge.indexOf("  function onNativeReadback(next,confirmed)",expire);
 assert.ok(start>0&&expire>start&&readback>expire);
 const discard=bridge.slice(start,expire);
 const expiration=bridge.slice(expire,readback);
 const region=bridge.slice(readback,
   bridge.indexOf("  if (connected) {\n    controller = protocol.createController",readback));
 assert.match(expiration,/discardTransientNativeUi\(\)/,
   "DEVICE_CHANGED and AUTHORITY_CHANGED reuse the sole GUI discard");
 assert.match(region,/if\s*\(transientPlan\.discardRetained\)\s*\{/);
 const retained=region.slice(region.indexOf("if (transientPlan.discardRetained)"),
   region.indexOf("if (!nativeAdmitted)",region.indexOf("if (transientPlan.discardRetained)")));
 assert.match(retained,/discardTransientNativeUi\(\{preserveRetainedIdentity:true\}\)/,
   "NEEDS_REMAP must discard physical leases but retain legal cosmetic edits");
 assert.match(discard,/if\(pendingIdentity&&!preserveRetainedIdentity\)/,
   "retained ChannelId name/color/icon edits must finish on native ACK/readback");
 assert.match(expiration,/discardTransientNativeUi\(\)/,
   "real authority or physical device change still cancels cosmetic edit");
 assert.doesNotMatch(retained,/pendingNativeSources\.clear|pendingNativeFxPower=null|pendingWireGains\.clear|activeNativeSource=null/,
   "there must be no second partial cleanup list to drift from reconnect");
 for(const token of ["pendingNativeSources.clear()","pendingSourceQueue.clear()",
   "pendingM4RackPowers.clear()","pendingExternal.clear()",
   "pendingSlotPowers.clear()","pendingGlobalFx=null",
   "pendingNativeFxPower=null","pendingWireGains.clear()",
   "pendingTrackDelete=null","pendingFxInsert=null","pendingIdentity"]){
   assert.ok(discard.includes(token),"retained cleanup must cover "+token);
 }
 assert.equal((bridge.match(/pendingSourceQueue\.clear\(\)/g)||[]).length,1,
   "source queue has one complete cleanup owner, including reconnect and retention");
 assert.match(region,/transientPlan\.drainM4Sources\s*&&\s*!nativeUiSourcePending/);
 assert.match(region,/transientPlan\.drainNativeSources\s*&&\s*!activeNativeSource/);
 assert.ok(region.indexOf("if (transientPlan.drainM4Sources")<
   region.indexOf("if (transientPlan.discardRetained)"));
 assert.doesNotMatch(region.slice(region.indexOf("if (transientPlan.discardRetained)"),
   region.indexOf("if (!nativeAdmitted)",region.indexOf("if (transientPlan.discardRetained)"))),
   /window\.queueMicrotask|backend\.emitEvent|controller\?\.intent/,
   "quarantined retained graph cannot create routing operations");
 assert.doesNotMatch(discard,/lastPaintedSignature=|lastNativeTopologySignature=|backend\.emitEvent|localStorage/,
   "repeated retained readbacks must preserve paint signature and never change PCM");
 assert.match(expiration,/lastPaintedSignature="";lastNativeTopologySignature="";/,
   "only a real authority/physical device change invalidates full GUI paint");
});

test("MV-12 approved bridge invalidates transient UI leases, not durable JUCE state",()=>{
  const source=readFileSync("web/approved-native-bridge.js","utf8");
  const start=source.indexOf("  function discardTransientNativeUi({preserveRetainedIdentity=false}={}) {");
  const end=source.indexOf("  function onNativeReadback(next,confirmed)",start);
  assert.ok(start>0&&end>start);
  const cleanup=source.slice(start,end);
  for(const token of ["pendingNativeSources.clear()","pendingSourceQueue.clear()",
    "pendingNativeControl.clear()","liveFaderVisualTargets.clear()",
    "lastNativeControlSent.clear()","pendingWireGains.clear()",
    "pendingM4Controls.clear()","pendingM4RackPowers.clear()",
    "pendingNativeFxPower=null","pendingGlobalFx=null",
    "pendingExternal.clear()","pendingSlotPowers.clear()",
    "activeNativeSource=null","pendingIdentity"] )
    assert.ok(cleanup.includes(token),"missing old transaction cancellation: "+token);
  assert.doesNotMatch(cleanup,/backend\.emitEvent|arMixAction|routingNative|audioDeviceApply|\.smix|new AudioContext/);
  const readback=source.slice(end,source.indexOf("  if (connected) {\n    controller = protocol.createController",end));
  assert.match(readback,/deviceVM\.nativeLifecycleLease\(next,readModel\.kind\)/);
  assert.match(readback,/deviceVM\.nativeLifecycleDecision\(lastNativeLifecycleLease,lease\)/);
  assert.match(readback,/if\(lifecycle\.expirePending\)/);
  assert.match(readback,/confirmed=null;/);
  const fence=readback.indexOf("expireTransientNativeUi(lifecycle.kind)");
  const accepted=readback.indexOf("lastNativeLifecycleLease=lease");
  const paint=readback.indexOf("paintExternalReadback(next)");
  assert.ok(fence>=0&&accepted>fence&&paint>accepted,
    "EXTERNAL OUT must repaint after device cleanup on one accepted snapshot");
  assert.doesNotMatch(readback.slice(0,fence),/paintExternalReadback\(next\)/,
    "old hardware output lease must never be projected ahead of lifecycle");
  assert.ok(readback.indexOf("if(readModel.kind===\"INVALID\")")<
    readback.indexOf("deviceVM.nativeLifecycleLease("),
    "invalid raw readback must not create a new device owner");
  assert.ok(readback.indexOf("expireTransientNativeUi(lifecycle.kind)")<
    readback.indexOf("appearanceFromNative(next)"),
    "stale cosmetic ACK cannot settle before device identity fence");
});


test("P0 approved Audio I/O latency shows real driver frames, never a fake loopback",()=>{
  const active={open:true,backend:"ASIO",rate:48000,buffer:256,
    driverInputLatencyFrames:240,driverOutputLatencyFrames:360,
    hostFxLatencyFrames:96,latencyClockScope:"SINGLE_CLOCK_DRIVER_HINTS",
    latencyProvenance:"JUCE_DRIVER_REPORTED_NOT_MEASURED_LOOPBACK_OR_ASIO_RTL"};
  const ready=vm.setupLatencyReadback(active);
  assert.equal(ready.ready,true);
  assert.equal(ready.line,"Input 5.00 ms · Output 7.50 ms · Buffer 5.33 ms · FX/PDC 2.00 ms");
  assert.equal(ready.driverIoHintMs,12.5);
  assert.equal(ready.estimatedTotalMs,14.5);
  assert.match(ready.note,/Estimated total I\/O 14.50 ms/);
  assert.equal(vm.setupLatencyReadback({...active,authorized:false}).ready,false);
  assert.equal(vm.setupLatencyReadback({...active,hostFxLatencyFrames:null}).estimatedTotalMs,null);
  assert.equal(vm.setupLatencyReadback({...active,rate:44100,driverInputLatencyFrames:1,
    driverOutputLatencyFrames:1,hostFxLatencyFrames:1}).estimatedTotalMs,3000/44100);
  assert.equal(vm.setupLatencyReadback({...active,rate:0}).ready,false);
  const missing=vm.setupLatencyReadback({...active,driverInputLatencyFrames:null});
  assert.equal(missing.inputMs,null);
  assert.equal(missing.driverIoHintMs,null);
  assert.match(missing.line,/Input N\/A/);
  const separate=vm.setupLatencyReadback({...active,
    latencyClockScope:"INDEPENDENT_CLOCKS_NO_DIRECT_ROUNDTRIP_ESTIMATE"});
  assert.equal(separate.driverIoHintMs,null);
  assert.equal(separate.estimatedTotalMs,null);
  assert.equal(vm.setupLatencyReadback({...active,driverOutputLatencyFrames:-1}).outputMs,null);
  assert.equal(vm.setupLatencyReadback({...active,driverInputLatencyFrames:0}).inputMs,0);
  const bridge=readFileSync("web/approved-native-bridge.js","utf8");
  assert.match(bridge,/data-audio-latency/);
  assert.match(bridge,/deviceVM\.setupLatencyReadback\(active\)/);
  assert.match(bridge,/Electrical round-trip requires physical loopback calibration/);
  const host=readFileSync("src/Main.cpp","utf8");
  for(const key of ["driverInputLatencyFrames","driverOutputLatencyFrames",
                    "hostFxLatencyFrames","latencyClockScope","latencyProvenance"])
    assert.ok(host.includes("setProperty(\""+key+"\""));
});

test("IO diagnostics: selected Windows capture with zero active pins offers explicit verified pin Apply",()=>{
 const f=fixture(),a={...f.active,backend:"Windows Audio",captureDevice:"VB-Cable",playbackDevice:"Headphones",inputNames:["Left","Right"],enabledInputs:[]};
 const inv={active:a,types:[{name:"Windows Audio",separate:true,inputs:["VB-Cable"],outputs:["Headphones"]}]};
 const draft=vm.setupDraft(inv),before=JSON.stringify(inv);
 assert.match(vm.signalPathHint(a),/ZERO input pins/);
 const recovered=vm.captureRecoveryDraft(inv,draft);
 assert.deepEqual(recovered.inputPins,[0,1]);
 assert.deepEqual(recovered.outputPins,draft.outputPins);
 const begun=vm.setupBegin(vm.setupInitial(),{inventory:inv,draft:recovered,requestId:"enable-capture"});
 assert.deepEqual(begun.request.config.enabledInputs,[0,1]);
 assert.equal(JSON.stringify(inv),before);
 assert.equal(vm.captureRecoveryDraft({...inv,active:{...a,inputNames:[]}},draft),null);
 assert.equal(vm.captureRecoveryDraft({...inv,active:{...a,authorized:false}},draft),null);
 assert.equal(vm.captureRecoveryDraft(inv,{...draft,capture:""}),null);
 assert.equal(vm.captureRecoveryDraft(inv,{...draft,inputPins:[1]}),null);
 assert.equal(vm.captureRecoveryDraft(f.inventory,f.draft),null);
 assert.match(vm.signalPathHint({...a,inputNames:[]}),/driver reports no input pins/);
});
