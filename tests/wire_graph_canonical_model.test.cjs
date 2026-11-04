#!/usr/bin/env node
'use strict';
const assert=require('node:assert/strict');
const graph=require('../site/preview-v2/assets/wire-graph-model.js');
const channels=[{id:1,name:'CH1',output:'Master'},{id:2,name:'CH2',output:'Master'},
 {id:3,name:'CH3',output:'Master'}];
const base=graph.migrateLegacy(channels,9);
assert.equal(base.ok,true,JSON.stringify(base));
assert.equal(base.edges.length,3);
assert.equal(new Set(base.edges.map(e=>e.id)).size,3);
const state={channels,mainId:9,edges:base.edges,nextId:base.nextId};
const main1=state.edges.find(e=>e.sourceId===1);
const unplug=graph.applyWireTransaction(state,[{type:'REMOVE',sourceId:1,edgeId:main1.id}]);
assert.equal(unplug.ok,true);
assert.equal(unplug.edges.filter(e=>e.destId===9).length,2);
const unchanged=JSON.stringify(state);
const connected=graph.applyWireTransaction({...state,edges:unplug.edges,nextId:unplug.nextId},
 [{type:'CONNECT',sourceId:1,destId:3}]);
assert.equal(connected.ok,true);
assert.ok(connected.edges.some(e=>e.sourceId===1&&e.destId===3));
const cycle=graph.applyWireTransaction({...state,edges:connected.edges,nextId:connected.nextId},
 [{type:'CONNECT',sourceId:3,destId:1}]);
assert.deepEqual({ok:cycle.ok,error:cycle.error},{ok:false,error:'FEEDBACK_CYCLE'});
const duplicate=graph.applyWireTransaction(state,[{type:'CONNECT',sourceId:1,destId:9}]);
assert.equal(duplicate.error,'DUPLICATE_ROUTE');
const replug=graph.applyWireTransaction({...state,edges:connected.edges,nextId:connected.nextId},
 [{type:'RETARGET',sourceId:1,edgeId:connected.edges.at(-1).id,destId:2}]);
assert.equal(replug.ok,true);
assert.equal(replug.edges.at(-1).id,connected.edges.at(-1).id);
const routeOnly=graph.applyWireTransaction(state,[{type:'ROUTE_ONLY',sourceId:1,destId:3}]);
assert.equal(routeOnly.ok,true);
assert.equal(routeOnly.edges.some(e=>e.sourceId===1&&e.destId===9),false);
const batchFail=graph.applyWireTransaction(state,[{type:'CONNECT',sourceId:1,destId:3},
 {type:'CONNECT',sourceId:3,destId:1}]);
assert.equal(batchFail.error,'FEEDBACK_CYCLE');
assert.equal(JSON.stringify(state),unchanged,'rejected batch must preserve original');
const intentionallyUnplugged=graph.migrateLegacy(channels,9,unplug.edges,unplug.nextId,{graphVersion:1});
assert.equal(intentionallyUnplugged.ok,true);
assert.equal(intentionallyUnplugged.edges.some(e=>e.sourceId===1&&e.destId===9),false);
// The active slot is represented by live channel fields, as in the V54 model.
const legacy=graph.migrateLegacy([{...channels[0],send:'CH2',sendOn:true,sendLevel:-6,
 sendSlots:[{send:'CH2',sendOn:true,sendLevel:-6}],activeSend:0},
 channels[1],channels[2]],9);
assert.equal(legacy.ok,true);
assert.equal(legacy.edges.filter(e=>e.sourceId===1&&e.destId===2).length,1);
// Imported saved patches must never vanish because a renamed destination is
// missing. No partial migration/ghost route is allowed on Scene recall.
const badOutput=graph.migrateLegacy([{...channels[0],output:'Deleted Bus'},channels[1],channels[2]],9);
assert.equal(badOutput.error,'UNRESOLVED_LEGACY_OUTPUT');
const badSend=graph.migrateLegacy([{...channels[0],send:'Deleted Bus',sendOn:true,
 sendSlots:[{send:'Deleted Bus',sendOn:true,sendLevel:-6}],activeSend:0},
 channels[1],channels[2]],9);
assert.equal(badSend.error,'UNRESOLVED_LEGACY_SEND');
const badGain=graph.migrateLegacy([{...channels[0],send:'CH2',sendOn:true,
 sendSlots:[{send:'CH2',sendOn:true,sendLevel:NaN}],activeSend:0,sendLevel:NaN},
 channels[1],channels[2]],9);
assert.equal(badGain.error,'INVALID_LEGACY_SEND_GAIN');
const badSplit=graph.migrateLegacy([{...channels[0],input:'CH99 PRE-FX SPLIT'},channels[1],channels[2]],9);
assert.equal(badSplit.error,'INVALID_EDGE');
const unrecognizedSplit=graph.migrateLegacy([{...channels[0],input:'CH?? PRE-FX SPLIT'},channels[1],channels[2]],9);
assert.equal(unrecognizedSplit.error,'UNRESOLVED_LEGACY_SPLIT');
// Deterministic random edit sequences check that no rejected candidate
// changes graph data and all accepted candidates remain acyclic.
let seed=0x5eeda11;
const rand=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed;};
for(let sequence=0;sequence<32;sequence++){
 let cur={channels,mainId:9,edges:base.edges.map(e=>({...e})),nextId:base.nextId};
 for(let step=0;step<48;step++){
  const src=1+(rand()%3),dst=[1,2,3,9][rand()%4];
  const existing=cur.edges.filter(e=>e.sourceId===src);
  const pick=existing.length?existing[rand()%existing.length]:null;
  const op=pick&&(rand()%3===0)?{type:'REMOVE',sourceId:src,edgeId:pick.id}:
   {type:'CONNECT',sourceId:src,destId:dst};
  const oldJson=JSON.stringify(cur),result=graph.applyWireTransaction(cur,[op]);
  assert.equal(JSON.stringify(cur),oldJson,'pure reducer must never mutate input');
  if(result.ok){
   cur={...cur,edges:result.edges,nextId:result.nextId};
   assert.equal(graph.validateCandidateGraph(channels,9,cur.edges).ok,true);
   assert.equal(new Set(cur.edges.map(e=>e.id)).size,cur.edges.length);
   assert.equal(new Set(cur.edges.map(e=>e.sourceId+':'+e.destId)).size,cur.edges.length);
  }
 }
}
// Safety contract: every entry point (including Scenes and untrusted imports)
// must fail closed for unknown kinds/taps, never silently create a SEND.
const unknownKind={...state.edges[0],kind:'phantom'};
assert.equal(graph.validateCandidateGraph(channels,9,[unknownKind]).error,'INVALID_EDGE_KIND');
for(const mode of ['pluginSidechain','preMaster','routeOnly']){
 const bad={...state.edges[0],id:200,sourceId:1,destId:2,kind:'send',mode};
 assert.equal(graph.validateCandidateGraph(channels,9,[bad]).error,'INVALID_EDGE_TAP');
}
assert.equal(graph.applyWireTransaction(state,[
 {type:'CONNECT',sourceId:1,destId:2,mode:'silentUnsupportedTap'}
]).error,'INVALID_EDGE_TAP');
assert.equal(graph.applyWireTransaction({...state,edges:null},[
 {type:'REMOVE',sourceId:1,edgeId:main1.id}
]).error,'INVALID_GRAPH');
assert.equal(JSON.stringify(state),unchanged,'rejected operations cannot mutate committed state');
console.log('PASS WIRE canonical model: MAIN defaults, exact-edge unplug/replug, atomic DAG, strict kinds/taps, migration');
