// SPDX-License-Identifier: AGPL-3.0-only
// P0: native fader targets must leave the GUI while the pointer is still held.
// This is a bridge/ACK regression, NOT a physical speaker FAT.
const test=require("node:test");
const assert=require("node:assert/strict");
const {readFileSync}=require("node:fs");
const vm=require("node:vm");
const protocol=require("../web/approved-native-contract.js");
const read=p=>readFileSync(p,"utf8");
const clone=x=>JSON.parse(JSON.stringify(x));
const fixture=JSON.parse(read("integration/console-v6/fixtures.json")).g0;
function nativeChannelIdFixture(){
 const s=clone(fixture),main="18446744073709551615";
 s.device.epoch="18446744073709551610";
 s.productionCallbackLive=true;
 s.graph.revision="5";s.graph.rosterRevision="7";
 s.graph.mainChannelId=main;
 s.graph.capabilities.callbackOwnsChannelIds=true;
 s.graph.capabilities.supportedOps=["SET_FADER","SET_PAN","SET_MUTE","APPLY_WIRE_TRANSACTION"];
 const ids=Array.from({length:10},(_,i)=>String(9007199254740994n+BigInt(i)));
 s.graph.channels=ids.map((id,i)=>({
   id,role:"INPUT",name:"CH"+(i+1),processingFormat:"MONO",
   primaryOutId:main,
   controls:{faderDb:-6,pan:0,mute:false,solo:false},inserts:[],sends:[]
 }));
 s.graph.channels.push({
   id:main,role:"MAIN",name:"MAIN",processingFormat:"STEREO",
   controls:{faderDb:0,pan:0,mute:false,solo:false},inserts:[],sends:[]
 });
 s.graph.destinations=[{id:main}];s.graph.edges=[];
 s.legacyM4={routingRevision:"3"};
 s.mixerSession={
   authority:"CHANNEL_ID",deviceEpoch:s.device.epoch,
   routeRevision:s.graph.revision,rosterRevision:s.graph.rosterRevision,
   channels:s.graph.channels,main:{db:0,mute:false}
 };
 return s;
}
function harness(){
 const sent=[],listeners=new Map(),timers=[],visual=[];
 let now=1000,nextId=100;
 const scope={
   tracks:[],master:{id:1,name:"MAIN"},colors:["#568ead"],
   wireGraph:{validateCandidateGraph:()=>({ok:true})},
   make:(name,kind,color)=>({id:nextId++,name,kind,color,selected:false}),
   render(){},findTrack(el){return this.tracks[el.lane||0];},
   displayMeterDbfs(){},allocateWireId(){return nextId++;},
   installGraph(channels,master,edges){this.tracks=channels;this.master=master;this.edges=edges;},
   syncFaderVisual(t){visual.push({id:t.id,db:t.level});},
   stopDemo(){}
 };
 const backend={
   emitEvent:(name,payload)=>sent.push({name,payload}),
   addEventListener:(name,fn)=>listeners.set(name,fn)
 };
 const document={
   readyState:"complete",documentElement:{dataset:{}},
   getElementById:id=>id==="info"?{textContent:""}:
     id==="preview"?{disabled:false,setAttribute(){}}:null,
   querySelector:()=>null,addEventListener(){}
 };
 const window={
   __JUCE__:{backend},ArSonApprovedNativeContract:protocol,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
   setTimeout(fn){timers.push(fn);return timers.length;}
 };
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
   window,document,performance:{now:()=>now},console
 },{timeout:2500});
 return {scope,window,backend,sent,listeners,timers,visual,
   tick(ms=40){now+=ms;const due=timers.splice(0);for(const fn of due)fn();}
 };
}
test("held ChannelId drag publishes first gain and coalesces latest BEFORE release",()=>{
 const h=harness(),s=nativeChannelIdFixture();
 h.listeners.get("arMixConsoleSnapshot")(s);
 assert.equal(h.window.__ARMIX_APPROVED_NATIVE_GUI__.acceptedGraphConnected,true);
 // Pointerdown and successive pointermoves: the approved Pages setFaderFromPointer
 // calls handleField(type=input), which the native-only builder forwards here.
 const fader={dataset:{field:"level"},lane:4,value:"-12",min:"-60",max:"10",closest:()=>null};
 const move=db=>{
   fader.value=String(db);
   assert.equal(h.window.__ARMIX_NATIVE_M2__.consumeField({target:fader,type:"input"}),true);
 };
 move(-12); // pointerdown (button still pressed)
 assert.equal(h.visual.at(-1).db,-12,"readout immediately follows finger, not native ACK");
 const first=h.sent.findLast(e=>e.name==="arMixConsoleIntent").payload;
 assert.equal(first.operation,"SET_FADER");
 assert.equal(first.targetChannelId,s.graph.channels[4].id,"CH5 exact native ChannelId");
 assert.equal(first.payload.valueDb,-12,"native target emitted without pointerup");
 move(-18);move(-24);h.tick();
 assert.equal(h.sent.filter(e=>e.name==="arMixConsoleIntent").length,1,
   "one in-flight native ACK: latest queued, no unbounded event storm");
 assert.equal(h.visual.at(-1).db,-24,"CH5 readout updates during mouse hold");
 const old=clone(s); // ACK alone does not repaint the accepted model
 h.listeners.get("arMixConsoleAck")({
   requestId:first.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:s.device.epoch,graphRevision:"5",rosterRevision:"7"
 });
 assert.equal(h.scope.tracks[4].level,-6);
 old.graph.channels[4].controls.faderDb=-12;
 h.listeners.get("arMixConsoleSnapshot")(old);
 h.tick();
 const second=h.sent.filter(e=>e.name==="arMixConsoleIntent").at(-1).payload;
 assert.equal(second.payload.valueDb,-24,
   "latest intermediate target sent while pointer remains HELD");
 assert.notEqual(second.requestId,first.requestId);
 // This is still BEFORE pointerup; the native backend can apply a second
 // PCM gain target through its existing atomic ChannelId SET_FADER setter.
 h.listeners.get("arMixConsoleAck")({
   requestId:second.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:s.device.epoch,graphRevision:"5",rosterRevision:"7"
 });
 const updated=clone(old);updated.graph.channels[4].controls.faderDb=-24;
 h.listeners.get("arMixConsoleSnapshot")(updated);
 assert.equal(h.scope.tracks[4].level,-24);
 move(-30); // final mouse pointerup will commit this new value
 h.window.__ARMIX_NATIVE_M2__.consumeField({target:fader,type:"change"});
 const final=h.sent.filter(e=>e.name==="arMixConsoleIntent").at(-1).payload;
 assert.equal(final.payload.valueDb,-30,"release flushes final value");
 assert.equal(h.scope.tracks[4].level,-24,"no optimistic native readback");
 h.listeners.get("arMixConsoleAck")({
   requestId:final.requestId,status:"ACCEPTED",errorCode:"NONE",
   deviceEpoch:s.device.epoch,graphRevision:"5",rosterRevision:"7"
 });
 const accepted=clone(updated);accepted.graph.channels[4].controls.faderDb=-30;
 h.listeners.get("arMixConsoleSnapshot")(accepted);
 assert.equal(h.scope.tracks[4].level,-30);
});
test("all CH1/CH2/CH5/CH10 and MAIN pointermove use native scalar path",()=>{
 const source=read("site/preview-v2/index.html");
 const builder=read("scripts/build_approved_native_gui.py");
 const bridge=read("web/approved-native-bridge.js");
 const native=read("src/LiveDryRoutingLab.h");
 assert.match(source,/function setFaderFromPointer\(ev,gesture\)/);
 assert.match(source,/handleField\(\{target:input,type:'input'/);
 assert.match(source,/if\(!faderGesture\|\|ev.pointerId!==faderGesture.pointerId\)return;\s*setFaderFromPointer\(ev,faderGesture\);/);
 assert.match(builder,/window\.__ARMIX_NATIVE_M2__\?\.consumeField\?\.\(event\)/);
 assert.match(bridge,/mixerSession\(\)\?\.authority==="CHANNEL_ID"/);
 assert.match(bridge,/parameter==="faderDb"\?"SET_FADER":"SET_PAN"/);
 assert.match(native,/updated\.linear=value<= -60\.f\?0\.f:std::pow\(10\.f,value\/20\.f\)/);
 assert.match(native,/setNativeInputMix\(expectedEpoch,nativeAccepted_\.pcmRevision/);
 // Valid input faders retain strict ChannelId mapping across a channel reorder.
 // A real callback/PCM FAT is still required; gain math alone is NOT a sound test.
 const h=harness(),s=nativeChannelIdFixture();
 // The physical-host MAIN gateway uses safe integer transport tokens, while
 // ChannelId itself deliberately remains an exact uint64 decimal string.
 s.device.epoch="77";s.mixerSession.deviceEpoch="77";
 h.listeners.get("arMixConsoleSnapshot")(s);
 let accepted=s;
 for(const lane of [0,1,4,9]){
   const value=-8-lane;
   const fader={dataset:{field:"level"},lane,value:String(value)};
   assert.equal(h.window.__ARMIX_NATIVE_M2__.consumeField({target:fader,type:"input"}),true);
   const cmd=h.sent.filter(e=>e.name==="arMixConsoleIntent").at(-1).payload;
   assert.equal(cmd.targetChannelId,accepted.graph.channels[lane].id,
     "CH"+(lane+1)+" exact ChannelId before pointerup");
   assert.equal(cmd.payload.valueDb,value);
   h.listeners.get("arMixConsoleAck")({
     requestId:cmd.requestId,status:"ACCEPTED",errorCode:"NONE",
     deviceEpoch:accepted.device.epoch,graphRevision:"5",rosterRevision:"7"
   });
   const next=clone(accepted);next.graph.channels[lane].controls.faderDb=value;
   h.listeners.get("arMixConsoleSnapshot")(next);
   assert.equal(h.scope.tracks[lane].level,value);
   accepted=next;h.tick();
 }
 const master={dataset:{field:"level"},lane:10,value:"-9"};
 h.scope.findTrack=el=>el.lane===10?h.scope.master:h.scope.tracks[el.lane||0];
 assert.equal(h.window.__ARMIX_NATIVE_M2__.consumeField({target:master,type:"input"}),true);
 const main=h.sent.filter(e=>e.name==="arMixAction").at(-1);
 assert.equal(main.payload.action,"routingM4Control");
 assert.equal(main.payload.channel,-1);
 assert.equal(main.payload.parameter,"masterGain");
 assert.equal(main.payload.controlValue,-9);
 assert.match(bridge,/t\\?\\.kind==="master"\\s*\\|\\|\\s*m4ControlIsOwner\\(\\)/,
   "MAIN scalar uses guarded native host gain");
});
