// SPDX-License-Identifier: AGPL-3.0-only
// MV-10: deterministic scalar scheduler with no DOM, clocks, timers, PCM or extra store.
const test=require("node:test");
const assert=require("node:assert/strict");
const {readFileSync}=require("node:fs");
const vm=require("../web/approved-channel-strip-view-model.js");

const identity={epoch:"18446744073709551610",authority:"CHANNEL_ID",
  channelId:"9007199254740993"};
const edit={parameter:"faderDb",value:-14.5,t:{kind:"audio",id:3},
  ...identity};
const lease=at=>vm.scalarDispatchLease(edit,at);
const ready=(overrides={})=>({
  identity,ready:true,logical:false,logicalPending:false,m4Owner:false,
  nativePending:false,...overrides
});
const gesture=(overrides={})=>({
  final:false,logical:false,now:135,lastSent:lease(120),
  identity:edit,...overrides
});

test("MV-10 throttle lease binds exact uint64 identity, not recycled strip ordinal",()=>{
  assert.deepEqual(lease(120),{at:120,epoch:identity.epoch,
    authority:"CHANNEL_ID",channelId:identity.channelId,
    hardwareConfiguration:undefined});
  assert.equal(vm.scalarGestureFlushPlan(gesture()),"QUEUE");
  assert.equal(vm.scalarGestureFlushPlan(gesture({now:149})),"QUEUE");
  assert.equal(vm.scalarGestureFlushPlan(gesture({now:150})),"FLUSH");
  assert.equal(vm.scalarGestureFlushPlan(gesture({
    identity:{...edit,channelId:"9007199254740994"}})),"FLUSH");
  assert.equal(vm.scalarGestureFlushPlan(gesture({
    identity:{...edit,epoch:"18446744073709551611"}})),"FLUSH");
  assert.equal(vm.scalarGestureFlushPlan(gesture({
    identity:{...edit,authority:"M4"}})),"FLUSH");
  assert.equal(vm.scalarGestureFlushPlan(gesture({
    identity:{...edit,channelId:Number(identity.channelId)}})),"FLUSH");
});

test("MV-10 pointer release is never swallowed by throttling or offline edit",()=>{
  assert.equal(vm.scalarGestureFlushPlan(gesture({final:true,now:120})),"FLUSH");
  assert.equal(vm.scalarGestureFlushPlan(gesture({final:true,now:121,logical:true})),"FLUSH");
  assert.equal(vm.scalarGestureFlushPlan(gesture({logical:true,lastSent:null})),"QUEUE");
  assert.equal(vm.scalarGestureFlushPlan(gesture({logical:true,now:900})),"QUEUE",
    "offline mousemove should not emit one session generation per frame");
});

test("MV-10 clock rollover, bad timestamp and first gesture cannot be delayed",()=>{
  for(const g of [
    {lastSent:null},{now:0},{lastSent:lease(Infinity)},
    {lastSent:lease(NaN)},{now:NaN},{now:Infinity},
    {lastSent:{at:120,epoch:"0",authority:"CHANNEL_ID",channelId:identity.channelId}}
  ])assert.equal(vm.scalarGestureFlushPlan(gesture(g)),"FLUSH",
    "invalid/older clock cannot block last target");
});

test("MV-10 one native ChannelId fader operation from accepted edit",()=>{
  const before=JSON.stringify(edit);
  const plan=vm.scalarDispatchPlan(edit,ready());
  assert.deepEqual(plan,{kind:"INTENT",operation:"SET_FADER",
    payload:{valueDb:-14.5}});
  assert.equal(JSON.stringify(edit),before);
  const pan=vm.scalarDispatchPlan({...edit,parameter:"pan",value:-36},ready());
  assert.deepEqual(pan,{kind:"INTENT",operation:"SET_PAN",payload:{value:-.36}});
  const main=vm.scalarDispatchPlan({...edit,parameter:"masterGain",value:2.5},ready());
  assert.deepEqual(main,{kind:"INTENT",operation:"SET_FADER",payload:{valueDb:2.5}});
});

test("MV-10 ChannelId readiness and exact identity fail closed before emit",()=>{
  for(const opts of [
    {identity:{...identity,epoch:"18446744073709551611"}},
    {identity:{...identity,channelId:"9007199254740994"}},
    {identity:{...identity,authority:"LOGICAL"}},
    {identity:null},{ready:false}
  ])assert.deepEqual(vm.scalarDispatchPlan(edit,ready(opts)),{kind:"REJECT"});
  for(const bad of [
    {...edit,value:NaN},{...edit,value:Infinity},{...edit,value:"-14.5"},
    {...edit,parameter:"mute"},{...edit,parameter:"foo"}
  ])assert.deepEqual(vm.scalarDispatchPlan(bad,ready()),{kind:"REJECT"});
});

test("MV-10 native pending controller holds latest target, never reports rejection",()=>{
  assert.equal(vm.scalarDispatchPlan(edit,ready({nativePending:true})).kind,"WAIT");
  assert.equal(vm.scalarDispatchPlan({...edit,parameter:"pan"},ready({nativePending:true})).kind,"WAIT");
  assert.equal(vm.scalarDispatchPlan(edit,ready({nativePending:false})).kind,"INTENT");
  // A stalled logical edit is also deferred; native does not supersede it.
  assert.equal(vm.scalarDispatchPlan(edit,ready({
    logical:true,logicalPending:true})).kind,"WAIT");
  assert.equal(vm.scalarDispatchPlan(edit,ready({
    logical:true,logicalPending:false})).kind,"INTENT");
});

test("MV-10 M4/MAIN continue to use the one existing routing transport",()=>{
  assert.deepEqual(vm.scalarDispatchPlan(edit,ready({m4Owner:true})),{kind:"M4"});
  assert.deepEqual(vm.scalarDispatchPlan(edit,ready({
    m4Owner:true,nativePending:true})),{kind:"M4"},
    "MAIN/M4 must not be blocked by unrelated ChannelId controller");
  assert.deepEqual(vm.scalarDispatchPlan(edit,ready({
    logical:true,m4Owner:true})),{kind:"INTENT",operation:"SET_FADER",
    payload:{valueDb:-14.5}}, "logical .smix edit never emits native M4");
  assert.deepEqual(vm.scalarDispatchPlan(edit,ready({
    m4Owner:true,ready:false})),{kind:"REJECT"});
});

test("MV-10 two-strip burst stays isolated, pointerup releases last writer",()=>{
  const targets=new Map(),sent=new Map();
  const a={...edit,channelId:"9007199254740993",value:-18};
  const b={...edit,channelId:"9007199254740994",value:-10};
  function queue(key,item,at,final=false){
    targets.set(key,item);
    return vm.scalarGestureFlushPlan({
      identity:item,now:at,final,lastSent:sent.get(key)});
  }
  assert.equal(queue("fader:3",a,0),"FLUSH");
  sent.set("fader:3",vm.scalarDispatchLease(a,0));
  assert.equal(queue("fader:3",{...a,value:-16},8),"QUEUE");
  assert.equal(queue("fader:4",b,9),"FLUSH");
  sent.set("fader:4",vm.scalarDispatchLease(b,9));
  assert.equal(queue("fader:3",{...a,value:-12},10,true),"FLUSH");
  assert.equal(targets.get("fader:3").value,-12);
  assert.equal(targets.get("fader:4").value,-10);
  assert.equal(queue("fader:3",{...b,value:-3},11),"FLUSH",
    "reused local fader ordinal cannot inherit old channel time lease");
});

test("MV-10 bridge has ONE scalar queue/transport and immutable ViewModel policy",()=>{
  const bridge=readFileSync("web/approved-native-bridge.js","utf8");
  const source=readFileSync("web/approved-channel-strip-view-model.js","utf8");
  assert.equal((bridge.match(/const liveFaderVisualTargets=new Map\(\)/g)||[]).length,1);
  assert.equal((bridge.match(/pendingNativeControl=new Map\(\)/g)||[]).length,1);
  assert.equal((bridge.match(/function queueLiveControlFlush\(/g)||[]).length,1);
  assert.equal((bridge.match(/function flushLiveScalars\(/g)||[]).length,1);
  assert.match(bridge,/stripVM\.scalarGestureFlushPlan\(/);
  assert.match(bridge,/stripVM\.scalarDispatchPlan\(/);
  assert.match(bridge,/stripVM\.scalarDispatchLease\(edit,performance\.now\(\)\)/);
  assert.match(bridge,/lastNativeControlSent\.clear\(\)/);
  assert.equal((bridge.match(/action:"routingM4Control"/g)||[]).length,1);
  assert.doesNotMatch(source,/window\.|document\.|setTimeout|AudioContext|backend\.emitEvent/);
});

// G-MV28: a physically different pin/rate/buffer configuration cannot inherit
// a pending scalar target or its 30ms throttle lease, even at the same epoch.
test("G-MV28 same-epoch hardware remap retires gesture and throttle",()=>{
  const old={...edit,hardwareConfiguration:"pins:A;rate:48000"};
  const next={...old,hardwareConfiguration:"pins:B;rate:48000"};
  const prior=vm.scalarDispatchLease(old,120);
  assert.equal(vm.scalarIdentityMatches(old,next),false);
  assert.equal(vm.scalarGestureFlushPlan({now:125,lastSent:prior,identity:next}),"FLUSH");
  assert.deepEqual(vm.scalarDispatchPlan(old,ready({identity:next})),{kind:"REJECT"});
  assert.equal(vm.scalarVisualSettlement(old,-14.5,next),"STALE");
  assert.equal(vm.scalarIdentityMatches(old,{...old}),true);
  assert.equal(vm.scalarGestureFlushPlan({now:125,lastSent:prior,identity:old}),"QUEUE");
  assert.equal(vm.scalarIdentityMatches(old,{...old,hardwareConfiguration:undefined}),false);
  const nativeBridge=readFileSync("web/approved-native-bridge.js","utf8");
  assert.ok(nativeBridge.includes("hardwareConfiguration:logicalView?undefined:hardware?.hardwareConfiguration"));
});
