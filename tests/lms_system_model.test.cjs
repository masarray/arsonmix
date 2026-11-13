"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const M = require("../site/preview-v2/assets/lms-system-model.js");

test("stereo sections are explicit and never automatically patched to speakers",()=>{
 const s=M.createSystem({ways:3,ports:M.demoPorts(6)});
 assert.deepEqual(s.sections.map(x=>x.name),[
  "LOW L","MID L","HIGH L","LOW R","MID R","HIGH R"]);
 assert.deepEqual(s.routes,{});
 assert.equal(s.applied,false);
 for(const p of s.ports){
  assert.equal(s.dsp[p].muted,true);
  assert.equal(s.dsp[p].limiterEnabled,false);
  assert.equal(s.dsp[p].limiterThresholdDb,null);
 }
 assert.equal(M.validateSystem(s),true);
});

test("crossover ways are flexible and structural change clears ALL patch/output safety drafts",()=>{
 const s=M.createSystem({ways:2,ports:M.demoPorts(4)});
 const mapped=M.withRoute(s,s.ports[0],"L:1");
 const edited=M.withDsp(mapped,s.ports[0],{gainDb:-6,muted:false});
 const next=M.withWays(edited,3);
 assert.equal(next.sections.length,6);
 assert.equal(next.ports.length,4);
 assert.deepEqual(next.routes,{});
 assert.equal(next.dsp[s.ports[0]].gainDb,0);
 assert.equal(next.dsp[s.ports[0]].muted,true);
 assert.equal(next.applied,false);
 assert.equal(edited.ways,2); // immutable transaction, old accepted draft intact
 assert.equal(edited.routes[s.ports[0]],"L:1");
 const sixteen=M.withWays(next,16);
 assert.equal(sixteen.sections.length,32);
 assert.equal(sixteen.cutoffsHz.length,15);
 assert.equal(M.validateSystem(sixteen),true);
});

test("routing one output is exclusive; one source may fan out to multiple destinations",()=>{
 let s=M.createSystem({ways:3,ports:M.demoPorts(6)});
 s=M.withRoute(s,"DEMO-01","L:1");
 s=M.withRoute(s,"DEMO-02","L:1");
 assert.equal(s.routes["DEMO-02"],"L:1");
 s=M.withRoute(s,"DEMO-01","L:2");
 assert.equal(s.routes["DEMO-01"],"L:2");
 assert.equal(Object.keys(s.routes).length,2);
 s=M.withRoute(s,"DEMO-01","L:2"); // clicking active crosspoint disconnects
 assert.equal(s.routes["DEMO-01"],undefined);
 assert.throws(()=>M.withRoute(s,"DEMO-01","L:55"),/unknown crossover/);
 assert.throws(()=>M.withRoute(s,"ASIO 1","L:1"),/unknown output/);
});

test("port discovery changes never silently reuse ordinal pin assignments",()=>{
 let s=M.createSystem({ways:2,ports:["device-A#1","device-A#2"]});
 s=M.withRoute(s,"device-A#1","L:1");
 s=M.withPorts(s,["device-B#1","device-B#2"]);
 assert.deepEqual(s.routes,{});
 assert.equal(s.dsp["device-B#1"].muted,true);
 assert.equal(s.ports[0],"device-B#1");
 assert.equal(s.applied,false);
 assert.throws(()=>M.createSystem({ports:["same","same"]}),/duplicate/);
});

test("DLMS explicit ASIO reassignment retains tuning, never auto-maps or auto-unmutes",()=>{
 let s=M.createSystem({ways:2,ports:["DEMO-01","DEMO-02","DEMO-03"]});
 s=M.withRoute(s,"DEMO-01","L:1");
 s=M.withDsp(s,"DEMO-01",{gainDb:-8,delayMs:23.5,
   limiterEnabled:true,limiterThresholdDb:-14,muted:false,polarity:"invert"});
 s=M.withPeqBand(s,"DEMO-01",0,{frequencyHz:110,q:.9,gainDb:-2});
 s=M.withPosition(s,"DEMO-01",-5,12);
 const ports=["ASIO/verified1","ASIO/verified2"];
 assert.throws(()=>M.reassignVerifiedPorts(s,ports,{}),/Choose at least one/);
 assert.throws(()=>M.reassignVerifiedPorts(s,ports,{"DEMO-02":ports[0]}),/Assign DEMO-01/);
 assert.throws(()=>M.reassignVerifiedPorts(s,ports,{"DEMO-01":ports[0],"DEMO-02":ports[0]}),/Duplicate physical/);
 assert.throws(()=>M.reassignVerifiedPorts(s,ports,{"DEMO-01":"untrusted"}),/Unknown output/);
 assert.throws(()=>M.reassignVerifiedPorts(s,["DEMO-01"],{"DEMO-01":"DEMO-01"}),/verified port/);
 const next=M.reassignVerifiedPorts(s,ports,{"DEMO-01":ports[1]});
 assert.equal(s.routes["DEMO-01"],"L:1");
 assert.equal(s.dsp["DEMO-01"].muted,false);
 assert.deepEqual(next.routes,{[ports[1]]:"L:1"});
 assert.equal(next.dsp[ports[1]].muted,true);
 assert.equal(next.dsp[ports[1]].gainDb,-8);
 assert.equal(next.dsp[ports[1]].delayMs,23.5);
 assert.equal(next.dsp[ports[1]].polarity,"invert");
 assert.equal(next.dsp[ports[1]].limiterThresholdDb,-14);
 assert.equal(next.dsp[ports[1]].peq[0].gainDb,-2);
 assert.equal(next.positions[ports[1]].y,12);
 assert.deepEqual(next.dsp[ports[0]].peq,[]);
 assert.equal(next.revision,s.revision+1);
 assert.equal(next.applied,false);
 assert.equal(M.validateSystem(next),true);
});

test("boundaries must be ordered and physically plausible; invalid edit rolls back",()=>{
 const s=M.createSystem({ways:3});
 assert.deepEqual(s.cutoffsHz,[120,2400]);
 const edited=M.withCutoff(s,1,3000);
 assert.deepEqual(edited.cutoffsHz,[120,3000]);
 assert.deepEqual(s.cutoffsHz,[120,2400]);
 assert.throws(()=>M.withCutoff(s,0,3000),/strictly ordered/);
 assert.throws(()=>M.withCutoff(s,0,Infinity),/crossover frequency/);
 assert.throws(()=>M.withCutoff(s,0,0),/crossover frequency/);
});

test("visual distance alignment returns additional geometric delay for nearer speaker",()=>{
 let s=M.createSystem({ways:1,ports:["far","near"]});
 s=M.withListener(s,0,0);
 s=M.withPosition(s,"far",0,20);
 s=M.withPosition(s,"near",0,10);
 const values=M.alignment(s);
 assert.equal(values[0].additionalMs,0);
 assert.ok(Math.abs(values[1].additionalMs-10000/343)<1e-8);
 assert.equal(values[1].distanceM,10);
 assert.equal(s.dsp.near.delayMs,0); // cannot silently write acoustic estimate
 assert.throws(()=>M.withPosition(s,"near",NaN,0),/speaker x/);
});

test("output DSP is bounded, muted by default and never grants hardware authority",()=>{
 const s=M.createSystem({ports:["out1"]});
 const next=M.withDsp(s,"out1",{gainDb:-3,limiterThresholdDb:-12});
 assert.equal(next.dsp.out1.limiterEnabled,false);
 assert.equal(next.applied,false);
 assert.throws(()=>M.withDsp(s,"out1",{gainDb:30}),/gainDb/);
 assert.throws(()=>M.withDsp(s,"out1",{limiterThresholdDb:3}),/limiterThresholdDb/);
 assert.throws(()=>M.withDsp(s,"out1",{unsafeHardwareEnable:true}),/unknown DSP/);
});


test("native-calculated LR4 amplitude response is complementary at split",()=>{
 const low=M.crossoverMagnitude([1000],0,1000);
 const high=M.crossoverMagnitude([1000],1,1000);
 assert.ok(Math.abs(low-.5)<.002,(low).toString());
 assert.ok(Math.abs(high-.5)<.002,(high).toString());
 assert.ok(M.crossoverMagnitude([1000],0,100)>.99);
 assert.ok(M.crossoverMagnitude([1000],1,100)<.01);
 assert.throws(()=>M.crossoverMagnitude([1000],0,25000),/plot request/);
});

test("EQ band state and graph match native RBJ peak filter math",()=>{
 const original=M.createSystem({ways:2,ports:["port"]});
 const edited=M.withPeqBand(original,"port",0,
   {frequencyHz:1000,q:1,gainDb:6,enabled:true});
 assert.equal(original.dsp.port.peq.length,0);
 assert.equal(edited.dsp.port.peq[0].gainDb,6);
 const atPeak=M.peqMagnitude(edited.dsp.port.peq,1000);
 assert.ok(Math.abs(atPeak-6)<.01,atPeak.toString());
 assert.equal(M.peqMagnitude([{...edited.dsp.port.peq[0],enabled:false}],1000),0);
 assert.throws(()=>M.withPeqBand(edited,"port",1,{q:0}),/PEQ Q/);
 assert.throws(()=>M.withPeqBand(edited,"port",2,{}),/PEQ gap/);
 assert.throws(()=>M.withPeqBand(edited,"port",1,{notAllowed:1}),/unknown PEQ/);
 const removed=M.withoutPeqBand(edited,"port",0);
 assert.deepEqual(removed.dsp.port.peq,[]);
 assert.equal(removed.applied,false);
});

test("visual alignment explicitly copies geometric offsets to disconnected drafts",()=>{
 let s=M.createSystem({ways:2,ports:["far","near"]});
 s=M.withListener(s,0,0);
 s=M.withPosition(s,"far",0,30);
 s=M.withPosition(s,"near",0,10);
 const prior=s;
 const planned=M.withSuggestedDelays(s);
 assert.equal(prior.dsp.near.delayMs,0);
 assert.equal(planned.dsp.far.delayMs,0);
 assert.ok(Math.abs(planned.dsp.near.delayMs-58.31)<.015);
 assert.equal(planned.applied,false);
 assert.deepEqual(planned.routes,{});
 assert.equal(planned.dsp.near.muted,true);
 const waysChanged=M.withWays(planned,3);
 assert.equal(waysChanged.dsp.near.delayMs,0);
});

test("real five-workspace interactions show curves, PEQ and review rather than applying",()=>{
 const fs=require("node:fs");
 const ui=fs.readFileSync(require.resolve("../site/preview-v2/assets/lms-workspace.js"),"utf8");
 const callbacks={root:{},launcher:{}};
 const root={dataset:{},hidden:true,innerHTML:"",
   addEventListener(type,fn){callbacks.root[type]=fn}};
 const launcher={addEventListener(type,fn){callbacks.launcher[type]=fn},
   setAttribute(k,v){this[k]=v},textContent:""};
 const app={classList:{toggle(){}}};
 const document={getElementById(id){return id==="lmsWorkspace"?root:
     id==="lmsOpen"?launcher:null},
   querySelector(q){return q===".app"?app:null}};
 const window={ArSonMixLmsModel:M,addEventListener(){}};
 new Function("window","document",ui)(window,document);
 const click=dataset=>callbacks.root.click({target:{closest:()=>({dataset})}});
 callbacks.launcher.click();
 assert.ok(root.innerHTML.includes("Speaker system"));
 click({page:"crossover"});
 assert.ok(root.innerHTML.includes("LR4 · 24 dB/oct"));
 assert.ok(root.innerHTML.includes("Calculated electrical LR4"));
 click({page:"processing"});
 assert.ok(root.innerHTML.includes("Parametric EQ"));
 click({action:"addPeq"});
 assert.ok(root.innerHTML.includes("EQ 1"));
 callbacks.root.change({target:{dataset:{peqField:"gainDb"},value:"6"}});
 assert.ok(root.innerHTML.includes("6.0 dB"));
 click({dspTab:"protection"});
 assert.ok(root.innerHTML.includes("NOT CALIBRATED"));
 click({page:"alignment"});
 assert.ok(root.innerHTML.includes("Suggested"));
 click({action:"planDelay"});
 assert.ok(root.innerHTML.includes("Review structural change"));
 click({action:"cancel"});
 assert.ok(!root.innerHTML.includes("Review structural change"));
 click({action:"planDelay"});
 click({action:"confirm"});
 assert.ok(root.innerHTML.includes("revision "));
 assert.ok(root.innerHTML.includes("Not applied to audio"));
 assert.equal(launcher["aria-pressed"],"true");
});
