// SPDX-License-Identifier: AGPL-3.0-only
const test=require("node:test");
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const M=require("../site/preview-v2/assets/lms-system-model.js");
const source=fs.readFileSync(path.join(__dirname,
 "../site/preview-v2/assets/lms-workspace.js"),"utf8");

function fixture(withBackend){
 const handlers={},events=[],dom={};
 const backend=withBackend?{
  addEventListener(type,fn){handlers[type]=fn},
  emitEvent(type,data){events.push({type,data})}
 }:null;
 const launch={textContent:"",addEventListener(type,fn){dom["launch:"+type]=fn},
  setAttribute(key,value){this[key]=value}};
 const root={hidden:true,dataset:{},innerHTML:"",
  addEventListener(type,fn){dom[type]=fn}};
 const app={classList:{toggle(){}}};
 const document={
  getElementById(id){return id==="lmsOpen"?launch:
   id==="lmsWorkspace"?root:null},
  querySelector(q){return q===".app"?app:null}
 };
 const window={ArSonMixLmsModel:M,addEventListener(){},
  ...(backend?{__JUCE__:{backend}}:{})};
 new Function("window","document",source)(window,document);
 const click=dataset=>dom.click({target:{closest:()=>({dataset})}});
 const change=(dataset,value)=>dom.change({target:{dataset,value}});
 const consume=(type)=>{const found=events.filter(e=>e.type===type);assert.ok(found.length,"expected "+type);return found.at(-1).data};
 return {backend,handlers,events,dom,launch,root,click,change,consume,
  open(){dom["launch:click"]()}};
}
function nativeProjection(design) {
 return {...design,outputs:design.outputs.map(x=>({
  ...x,limiterThresholdDb:x.limiterThresholdDb===null?-6:x.limiterThresholdDb,
  voltageModelPresent:false
 }))};
}
function inspection(id,{ready=true,outputDisarmed=true,revision="0",
  design=null,epoch="21",graph="42"}={}){
 return {schema:"arsonmix.lms.native-inspection.v1",requestId:id,
  ready,hardwareApplied:false,deviceEpoch:epoch,graphRevision:graph,
  designRevision:revision,sampleRate:48000,maxFrames:256,
  activeOutputChannels:8,outputDisarmed,validatedDesign:design,
  ports:Array.from({length:8},(_,i)=>({
   identity:"ASIO-EXACT-DEVICE/PIN-"+i,callbackOrdinal:i,occupied:i<4
  }))};
}
test("public /preview/ remains an offline design without native event claims",()=>{
 const app=fixture(false);app.open();
 assert.ok(app.root.innerHTML.includes("DESIGN ONLY · OUTPUT DISCONNECTED"));
 assert.ok(!app.root.innerHTML.includes("NATIVE INSPECTION"));
 assert.equal(app.events.length,0);
 app.click({page:"crossover"});
 assert.ok(app.root.innerHTML.includes("calculated at 48 kHz"));
});
test("native validation requires inspected physical pins, disarmed outputs and later readback",()=>{
 const app=fixture(true);app.open();
 const first=app.consume("arMixLmsInspect");
 assert.ok(!app.root.innerHTML.includes("Validate design"));
 app.handlers.arMixLmsInspection(inspection(first.requestId,{ready:false}));
 assert.ok(app.root.innerHTML.includes("unavailable"));
 const invalid=app.consume("arMixLmsInspect");
 assert.equal(invalid.requestId,first.requestId);
 app.click({action:"nativeInspect"});
 const current=app.consume("arMixLmsInspect");
 app.handlers.arMixLmsInspection(inspection(current.requestId));
 assert.ok(app.root.innerHTML.includes("4 free ASIO pins"));
 app.click({action:"nativePorts"});
 assert.ok(app.root.innerHTML.includes("Review structural change"));
 app.click({action:"confirm"});
 assert.ok(app.root.innerHTML.includes("Verified ASIO ports"));
 app.click({page:"matrix"});
 const port="ASIO-EXACT-DEVICE/PIN-4";
 app.click({routePort:port,routeSection:"L:1"});
 assert.ok(app.root.innerHTML.includes("Native validation"));
 app.click({action:"nativeValidate"});
 const sent=app.consume("arMixLmsValidate");
 assert.equal(sent.schema,"arsonmix.lms.native-design-intent.v1");
 assert.equal(sent.design.schema,"arsonmix.lms.draft.v1");
 assert.equal(sent.design.outputs.length,1);
 assert.equal(sent.design.outputs[0].portIdentity,port);
 assert.equal(sent.design.outputs[0].sourceSide,0);
 assert.equal(sent.design.outputs[0].muted,true);
 assert.equal(sent.design.outputs[0].limiterThresholdDb,null);
 assert.equal(sent.expectedDesignRevision,"0");
 assert.equal(sent.deviceEpoch,"21");
 assert.equal(sent.graphRevision,"42");
 assert.equal(sent.design.applied,false);
 assert.ok(!app.root.innerHTML.includes("VALIDATED DRAFT"));
 app.handlers.arMixLmsDesignAck({
  schema:"arsonmix.lms.native-design-ack.v1",
  requestId:sent.requestId,status:"VALIDATED_PENDING_READBACK",
  hardwareApplied:false,designRevision:"1",errorCode:"NONE"});
 assert.ok(!app.root.innerHTML.includes("VALIDATED DRAFT"));
 const refresh=app.consume("arMixLmsInspect");
 app.handlers.arMixLmsInspection(inspection(refresh.requestId,{
  revision:"1",
  design:{ways:sent.design.ways,stereo:sent.design.stereo,
    cutoffsHz:sent.design.cutoffsHz,outputs:nativeProjection(sent.design).outputs}
 }));
 assert.ok(app.root.innerHTML.includes("VALIDATED DRAFT"));
 assert.ok(app.root.innerHTML.includes("hardware NOT applied"));
 app.click({page:"crossover"});
 app.change({cutoff:"0"},"200");
 assert.ok(!app.root.innerHTML.includes("VALIDATED DRAFT"));
 assert.ok(app.root.innerHTML.includes("native validation required"));
});

test("native readback must match EVERY output DSP field before VALIDATED",()=>{
 const mutations=[
  ["gain",x=>{x.gainDb=-13;}],
  ["delay",x=>{x.delayMs=48.25;}],
  ["polarity",x=>{x.polarityInverted=true;}],
  ["limiter",x=>{x.limiterEnabled=true;}],
  ["digital ceiling",x=>{x.limiterThresholdDb=-17.25;}],
  ["missing digital ceiling",x=>{delete x.limiterThresholdDb;}],
  ["unacknowledged voltage profile",x=>{x.voltageModelPresent=true;}],
  ["eq insertion",x=>{x.peq=[{frequencyHz:1000,q:1,gainDb:6,enabled:true}];}],
  ["missing eq",x=>{delete x.peq;}],
  ["missing delay",x=>{delete x.delayMs;}],
  ["unmute",x=>{x.muted=false;}],
 ];
 for(const [name,mutate] of mutations){
  const app=fixture(true);app.open();
  const first=app.consume("arMixLmsInspect");
  app.handlers.arMixLmsInspection(inspection(first.requestId));
  app.click({action:"nativePorts"});app.click({action:"confirm"});
  app.click({routePort:"ASIO-EXACT-DEVICE/PIN-4",routeSection:"L:1"});
  app.click({action:"nativeValidate"});
  const sent=app.consume("arMixLmsValidate");
  app.handlers.arMixLmsDesignAck({
   schema:"arsonmix.lms.native-design-ack.v1",requestId:sent.requestId,
   status:"VALIDATED_PENDING_READBACK",hardwareApplied:false,
   designRevision:"1",errorCode:"NONE"
  });
  const checked=app.consume("arMixLmsInspect");
  const actual=nativeProjection(structuredClone(sent.design));
  mutate(actual.outputs[0]);
  app.handlers.arMixLmsInspection(inspection(checked.requestId,{
   revision:"1",design:actual
  }));
  assert.ok(!app.root.innerHTML.includes("VALIDATED DRAFT"),name);
  assert.ok(app.root.innerHTML.includes("readback mismatch"),name);
 }
});
test("guided DLMS ASIO reassign retains DSP, requires manual pin mapping and readback",()=>{
 const app=fixture(true);app.open();
 const id=app.consume("arMixLmsInspect").requestId;
 app.handlers.arMixLmsInspection(inspection(id));
 app.click({routePort:"DEMO-01",routeSection:"L:1"});
 app.click({selectPort:"DEMO-01"});
 app.change({dsp:"gainDb"},"-7");
 app.change({dsp:"muted"},"0");
 assert.ok(app.root.innerHTML.includes("Reassign + keep DSP"));
 app.click({action:"nativeReassign"});
 assert.ok(app.root.innerHTML.includes("Review explicit output reassignment"));
 app.click({action:"confirmReassign"});
 assert.ok(app.root.innerHTML.includes("Choose at least one explicit"));
 assert.equal(app.events.filter(e=>e.type==="arMixLmsValidate").length,0);
 app.change({rebindSource:"DEMO-01"},"ASIO-EXACT-DEVICE/PIN-4");
 app.click({action:"confirmReassign"});
 assert.ok(app.root.innerHTML.includes("saved as MUTED draft"),
   "Reassignment status: "+app.root.innerHTML.slice(0,3800));
 assert.ok(!app.root.innerHTML.includes("Review explicit output reassignment"));
 assert.equal(app.events.filter(e=>e.type==="arMixLmsValidate").length,0);
 app.click({action:"nativeValidate"});
 const sent=app.consume("arMixLmsValidate");
 assert.equal(sent.design.outputs.length,1);
 assert.equal(sent.design.outputs[0].portIdentity,"ASIO-EXACT-DEVICE/PIN-4");
 assert.equal(sent.design.outputs[0].gainDb,-7);
 assert.equal(sent.design.outputs[0].muted,true);
 assert.equal(sent.design.applied,false);
});
test("DLMS mapping review aborts if a fresh inspection changes device epoch",()=>{
 const app=fixture(true);app.open();
 const id=app.consume("arMixLmsInspect").requestId;
 app.handlers.arMixLmsInspection(inspection(id));
 app.click({routePort:"DEMO-01",routeSection:"L:1"});
 app.click({action:"nativeReassign"});
 assert.ok(app.root.innerHTML.includes("Review explicit output reassignment"));
 app.click({action:"nativeInspect"});
 const refresh=app.consume("arMixLmsInspect");
 app.handlers.arMixLmsInspection(inspection(refresh.requestId,{epoch:"999"}));
 assert.ok(!app.root.innerHTML.includes("Review explicit output reassignment"));
 assert.equal(app.events.filter(e=>e.type==="arMixLmsValidate").length,0);
});

test("native readback cannot validate when hardware is armed",()=>{
 const app=fixture(true);app.open();
 const first=app.consume("arMixLmsInspect");
 app.handlers.arMixLmsInspection(inspection(first.requestId));
 app.click({action:"nativePorts"});app.click({action:"confirm"});
 app.click({routePort:"ASIO-EXACT-DEVICE/PIN-4",routeSection:"L:1"});
 app.click({action:"nativeValidate"});
 const sent=app.consume("arMixLmsValidate");
 app.handlers.arMixLmsDesignAck({
  schema:"arsonmix.lms.native-design-ack.v1",requestId:sent.requestId,
  status:"VALIDATED_PENDING_READBACK",hardwareApplied:false,
  designRevision:"1",errorCode:"NONE"
 });
 const checked=app.consume("arMixLmsInspect");
 app.handlers.arMixLmsInspection(inspection(checked.requestId,{
  revision:"1",design:nativeProjection(sent.design),outputDisarmed:false
 }));
 assert.ok(!app.root.innerHTML.includes("VALIDATED DRAFT"));
});
test("native rejection and armed-device snapshot never invent APPLIED",()=>{
 const app=fixture(true);app.open();
 let req=app.consume("arMixLmsInspect");
 app.handlers.arMixLmsInspection(inspection(req.requestId,{outputDisarmed:false}));
 app.click({action:"nativePorts"});app.click({action:"confirm"});
 const port="ASIO-EXACT-DEVICE/PIN-4";
 app.click({routePort:port,routeSection:"L:1"});
 assert.ok(app.root.innerHTML.includes("disabled"));
 app.click({action:"nativeValidate"});
 assert.equal(app.events.filter(e=>e.type==="arMixLmsValidate").length,0);
 app.click({action:"nativeInspect"});
 req=app.consume("arMixLmsInspect");
 app.handlers.arMixLmsInspection(inspection(req.requestId));
 app.click({action:"nativeValidate"});
 const sent=app.consume("arMixLmsValidate");
 app.handlers.arMixLmsDesignAck({
  schema:"arsonmix.lms.native-design-ack.v1",
  requestId:sent.requestId,status:"REJECTED",
  hardwareApplied:false,errorCode:"NATIVE_LMS_STALE_GRAPH"});
 assert.ok(app.root.innerHTML.includes("REJECTED"));
 assert.ok(!app.root.innerHTML.includes("VALIDATED DRAFT"));
});
