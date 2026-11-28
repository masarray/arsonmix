// Offline A/B for the actual production WIRE readback VM; no audio/DOM claim.
import {execFileSync} from "node:child_process";
import {readFileSync} from "node:fs";
import {performance} from "node:perf_hooks";
import assert from "node:assert/strict";
const baseline="3659f61c1a1f666fd84a4c43f0c2309c3128277c";
const path="web/approved-wire-view-model.js";
const load=source=>{const mod={exports:{}};new Function("module",source)(mod);return mod.exports.gainReadback;};
const f=[load(execFileSync("git",["show",baseline+":"+path],{encoding:"utf8"})),load(readFileSync(path,"utf8"))];
for(const n of [12,24,64,128]){
 const plain=Array.from({length:n},(_,i)=>({id:String(9007199254740993n+BigInt(i)),primaryOutId:"18446744073709551615",controls:{faderDb:-3}}));
 const edges=plain.flatMap((ch,i)=>[{edgeId:"p"+i,from:ch.id,to:ch.primaryOutId,enabled:true,primaryOutput:true,gainLinear:1},{edgeId:"s"+i,from:ch.id,to:"90",enabled:true,primaryOutput:false,gainLinear:.5}]);
 const ids=new Map(edges.map((e,i)=>[e.edgeId,i])),pending=new Map([["s1",true]]);
 let reads=0;const channels=plain.map(ch=>({...ch,get id(){reads++;return ch.id}}));
 const counts=f.map(fn=>{reads=0;for(let i=0;i<120;i++)fn(edges,ids,pending,channels);return reads;});
 assert.deepEqual(f[0](edges,ids,pending,plain),f[1](edges,ids,pending,plain));
 assert.deepEqual(counts,[n*n*120,2*n*120]);
 for(const fn of f)for(let i=0;i<300;i++)fn(edges,ids,pending,plain);
 const samples=[[],[]];for(let k=0;k<7;k++)for(const mode of k%2?[1,0]:[0,1]){
  const t=performance.now();for(let i=0;i<2000;i++)f[mode](edges,ids,pending,plain);
  samples[mode].push((performance.now()-t)*1000/2000);
 }
 console.log(JSON.stringify({node:process.version,channels:n,edges:edges.length,identityReads:counts,
   medianUs:samples.map(a=>[...a].sort((a,b)=>a-b)[3]),samplesUs:samples,parity:true}));
}
