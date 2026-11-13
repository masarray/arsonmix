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
