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
function harness({m4=false}={}){
 const sent=[],listeners=new Map(),timers=[],visual=[],meters=[];
 const m4Tracks=m4?Array.from({length:10},(_,i)=>({
   id:1000+i,name:"CH"+(i+1),kind:"audio",format:"mono",level:-6,
   pan:0,mute:false,solo:false,input:"None",output:"Master",fxOn:true,
   fx:Array(8).fill("None"),slotBypass:Array(8).fill(false)
 })):[];
 const roots=new Map(m4Tracks.map(t=>[t.id,{
   isConnected:true,
   querySelector:q=>q===".meter-fill.left"?{lane:t.id,side:"L"}:
     q===".meter-fill.right"?{lane:t.id,side:"R"}:null
 }]));
 let now=1000,nextId=100;
 const scope={
   tracks:m4Tracks,master:{id:1,name:"MAIN",kind:"master",level:0,
   pan:0,mute:false,output:"Realtek"},colors:["#568ead"],
   wireGraph:{validateCandidateGraph:()=>({ok:true})},
   make:(name,kind,color)=>({id:nextId++,name,kind,color,selected:false}),
   render(){},findTrack(el){return this.tracks[el.lane||0];},
   displayMeterDbfs(fill,db){meters.push({fill,db});},
   refreshStrip(){},allocateWireId(){return nextId++;},
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
   querySelector:q=>{
     const match=/\.strip\[data-id="(\d+)"\]/.exec(q);
     return match?roots.get(Number(match[1]))||null:null;
   },addEventListener(){}
 };
 const window={
   __JUCE__:{backend},ArSonApprovedNativeContract:protocol,
   __ARMIX_NATIVE_SCOPE__:scope,addEventListener(){},
   setTimeout(fn,delay=0){timers.push({fn,at:now+Math.max(0,Number(delay)||0)});return timers.length;}
 };
 vm.runInNewContext(read("web/approved-native-bridge.js"),{
   window,document,performance:{now:()=>now},console
 },{timeout:2500});
 return {scope,window,backend,sent,listeners,timers,visual,meters,
   tick(ms=40){
      now+=ms;
      for(let i=0;i<timers.length;){
        const timer=timers[i];
        if(timer.at>now){i++;continue;}
        timers.splice(i,1);timer.fn();
      }
    }
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
 assert.equal(h.window.__ARMIX_NATIVE_M2__.state().pending?.operation,"SET_FADER",
   "a 40ms UI tick must NEVER execute the native 8000ms timeout");
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
 assert.equal(h.visual.at(-1).db,-24,
   "old native ACK/readback may update model, never snap the visual fader backwards");
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
 // Pointer capture has ended. A delayed old native snapshot is NOT permission
 // to replace the final target that is still in flight.
 h.listeners.get("arMixConsoleSnapshot")(clone(updated));
 assert.equal(h.visual.at(-1).db,-30,"released cap holds final latest target");
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
 assert.match(bridge,/edit\.t\?\.kind==="master"\s*\|\|\s*m4ControlIsOwner\(\)/,
   "MAIN scalar uses guarded native host gain");
});

test("pending M4 fader ACK never stalls independently accepted meters, FX rack or mute",()=>{
 const h=harness({m4:true}),s=clone(fixture),rev="3";
 const controls=()=>({faderDb:-6,pan:0,mute:false,solo:false});
 const source=()=>({mode:"MONO",left:0,right:-1,main:true,channelFormat:"MONO"});
 s.legacyM4={
   active:true,routeAccepted:true,routingRevision:rev,
   channelControls:Array.from({length:10},controls),
   channels:Array.from({length:10},source),
   fxInstances:Array.from({length:10},()=>Array(8).fill(null))
 };
 s.mixerSession={
   authority:"M4",deviceEpoch:s.device.epoch,routeRevision:rev,
   channels:Array.from({length:10},()=>({
     source:source(),main:true,channelFormat:"MONO",controls:controls(),
     fxRackBypassed:false,peakL:0.1,peakR:0.1
   })),main:{db:0,mute:false}
 };
 const snapshot=h.listeners.get("arMixConsoleSnapshot");
 snapshot(s);
 assert.equal(h.meters.length,20,"ten-channel hardware meters are live");
 const ch5={dataset:{field:"level"},lane:4,value:"-12",min:"-60",max:"10"};
 assert.equal(h.window.__ARMIX_NATIVE_M2__.consumeField({
   target:ch5,type:"input"
 }),true);
 const command=h.sent.findLast(e=>
   e.name==="arMixAction"&&e.payload.action==="routingM4Control")?.payload;
 assert.equal(command?.channel,4);
 assert.equal(command.parameter,"faderDb");
 assert.equal(command.controlValue,-12);
 const next=clone(s);
 next.mixerSession.channels[0].fxRackBypassed=true;
 next.mixerSession.channels[3].controls.mute=true;
 next.mixerSession.channels[4].peakL=0.45;
 snapshot(next);
 assert.equal(h.meters.length,40,
   "pending CH5 gain must not freeze all 20 physical meter taps");
 assert.equal(h.scope.tracks[0].fxOn,false,
   "CH1 real rack state stays independent of CH5 pending gain");
 assert.equal(h.scope.tracks[3].mute,true,
   "CH4 accepted mute stays independent of CH5 pending gain");
 assert.equal(h.scope.tracks[4].level,-6,
   "unacknowledged CH5 gain must not appear as accepted PCM");
 assert.equal(h.visual.at(-1)?.db,-12,
   "the CH5 held fader cap keeps the latest unacknowledged target");
 h.listeners.get("arMixM4ControlAck")({
   requestId:command.requestId,ok:true,deviceEpoch:s.device.epoch,
   routingRevision:rev,channel:4,parameter:"faderDb"
 });
 const accepted=clone(next);
 accepted.mixerSession.channels[4].controls.faderDb=-12;
 accepted.legacyM4.channelControls[4].faderDb=-12;
 snapshot(accepted);
 assert.equal(h.scope.tracks[4].level,-12);
 assert.equal(h.meters.length,60);
});
