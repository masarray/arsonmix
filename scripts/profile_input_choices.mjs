// SPDX-License-Identifier: AGPL-3.0-only
// Isolated actual selector A/B; no DOM, native callback or ASIO timing claim.
import {execFileSync} from "node:child_process";
import {readFileSync} from "node:fs";
import {performance} from "node:perf_hooks";
import assert from "node:assert/strict";
const baseline="a61749fbcfa3f11a3268528af4c7cf9fd780e740";
const path="web/approved-device-view-model.js";
const load=source=>{const mod={exports:{}};new Function("module",source)(mod);return mod.exports.inputChoices;};
const fs=[load(execFileSync("git",["show",baseline+":"+path],{encoding:"utf8"})),load(readFileSync(path,"utf8"))];
for(const k of [2,8,16,32,64]){
  const make=()=>({device:{inputEndpoints:Array.from({length:k},(_,port)=>({port,active:true,label:"Input "+(port+1)})),enabledInputs:Array.from({length:k},(_,i)=>i)},admitted:true,graph:{channels:Array.from({length:24},(_,i)=>({source:{sourceFormat:"STEREO",portId:"in:"+(i%k),portIdRight:"in:"+((i+3)%k)}}))}});
  const x=make();
  assert.deepEqual(fs[0](x),fs[1](x));
  let reads=0;
  for(const p of x.device.inputEndpoints)Object.defineProperty(p,"active",{get(){reads++;return true;}});
  const counts=fs.map(f=>{reads=0;for(let i=0;i<120;i++)f(x);return reads;});
  assert.equal(counts[1],120*k);
  const y=make(),samples=[[],[]],loops=1200;
  for(const f of fs)for(let i=0;i<300;i++)f(y);
  for(let j=0;j<5;j++)for(const i of j%2?[1,0]:[0,1]){
    const t=performance.now();for(let q=0;q<loops;q++)fs[i](y);
    samples[i].push((performance.now()-t)*1000/loops);
  }
  console.log(JSON.stringify({node:process.version,pins:k,channels:24,activeReads:counts,medianUs:samples.map(a=>[...a].sort((a,b)=>a-b)[2]),samplesUs:samples,parity:true}));
}
