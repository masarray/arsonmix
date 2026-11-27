// SPDX-License-Identifier: AGPL-3.0-only
// Actual selector / resolver A/B, not end-to-end GUI or audio callback timing.
import {execFileSync} from "node:child_process";
import {readFileSync} from "node:fs";
import {performance} from "node:perf_hooks";
import assert from "node:assert/strict";
const base="e66ca97ecc72c1895213d80ce151dda05658da9d", path="web/approved-device-view-model.js";
const load=src=>{const mod={exports:{}};new Function("module",src)(mod);return mod.exports;};
const old=load(execFileSync("git",["show",base+":"+path],{encoding:"utf8"}));
const next=load(readFileSync(path,"utf8"));
for(const pins of [2,8,32,64]){
  let reads=0;
  const snapshot={device:{inputEndpoints:Array.from({length:pins},(_,port)=>({
    port,get active(){reads++;return true;},label:"Input "+(port+1)
  })),enabledInputs:Array.from({length:pins},(_,i)=>i)},admitted:true,
    graph:{channels:Array.from({length:24},(_,i)=>({source:{sourceFormat:"STEREO",
      portId:"in:"+(i%pins),portIdRight:"in:"+((i+3)%pins)}}))}};
  const rows=Array.from({length:24},(_,i)=>({mode:"STEREO",left:i%pins,right:(i+3)%pins}));
  const fn=[()=>rows.map(row=>old.inputLabel(snapshot,row)),()=>rows.map(next.inputLabelResolver(snapshot))];
  const counts=fn.map(f=>{reads=0;f();return reads;});
  assert.deepEqual(fn[0](),fn[1]());assert.deepEqual(counts,[24*pins,pins]);
  for(const f of fn)for(let i=0;i<50;i++)f();
  const samples=[[],[]],loops=200;
  for(let j=0;j<5;j++)for(const i of j%2?[1,0]:[0,1]){
    const start=performance.now();for(let q=0;q<loops;q++)fn[i]();
    samples[i].push((performance.now()-start)*1000/loops);
  }
  console.log(JSON.stringify({node:process.version,pins,channels:24,activeReads:counts,
    medianUs:samples.map(a=>[...a].sort((x,y)=>x-y)[2]),samplesUs:samples,parity:true}));
}
