"use strict";
// V66 gesture: unplug ONLY the grabbed EdgeId; no first-edge heuristics.
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const src=fs.readFileSync(path.join(__dirname,"../web/approved-native-bridge.js"),"utf8");
const marker="    consumeNativeWireReplug(";
const start=src.indexOf(marker),end=src.indexOf("\n    consumeWire(ops,retries=0)",start);
assert(start>=0&&end>start,"native handler exists");
const handler=src.slice(start,end).trim().replace(/,\s*$/,"");
const body="let pendingNativeWireRetract=null;\n"+
  "return {run:({"+handler+"}).consumeNativeWireReplug,held:()=>pendingNativeWireRetract};";
const instantiate=new Function("scope","connected","logicalView","controller",
  "nativeByUiId","wireByUiId","flags","pendingLogical","nativeHooks",body);
const edges=[
  {id:111,sourceId:7,destId:99,enabled:true,kind:"send"},
  {id:222,sourceId:7,destId:99,enabled:true,kind:"send"},
  {id:333,sourceId:7,destId:99,enabled:true,kind:"primary"}
];
const scope={wireEdges:edges};
const nativeByUiId=new Map([[7,"71"],[99,"91"],[100,"101"]]);
const wireByUiId=new Map([[111,"9991"],[222,"9992"],[333,"9993"]]);
const controller={state:()=>({current:{
  admitted:true,graph:{capabilities:{supportedOps:["APPLY_WIRE_TRANSACTION"]}}
},pending:null})};
let seen=[];
const nativeHooks={consumeWire:ops=>seen.push(...ops)};
const make=(options={})=>instantiate(scope,options.connected??true,
  options.logicalView??null,controller,nativeByUiId,wireByUiId,
  {acceptedGraphConnected:true},false,nativeHooks);
const point={x:408,y:222};
let instance=make();
assert.equal(instance.run(7,99,null,point,999),false);
assert.equal(seen.length,0);
assert.equal(instance.run(7,99,null,point,222),false);
assert.deepEqual(seen,[{type:"REMOVE",sourceId:7,edgeId:222}]);
assert.equal(instance.held().nativeEdge,"9992");
assert.equal(instance.held().oldDest,99);
assert.equal(instance.held().primary,false);
assert.equal(scope.wireEdges.length,3);
seen=[];
instance=make();
assert.equal(instance.run(7,99,100,point,111),false);
assert.deepEqual(seen,[{type:"RETARGET",sourceId:7,edgeId:111,destId:100}]);
assert.equal(instance.held(),null);
seen=[];
instance=make();
assert.equal(instance.run(7,99,null,undefined,333),false);
assert.equal(seen.length,0);
assert.equal(instance.held(),null);
instance=make({connected:false});
assert.equal(instance.run(7,99,null,point,333),false);
assert.equal(seen.length,0);
instance=make({logicalView:{readOnly:false}});
assert.equal(instance.run(7,99,null,point,333),false);
assert.deepEqual(seen,[{type:"REMOVE",sourceId:7,edgeId:333}]);
assert.equal(instance.held().primary,true);
console.log("PASS V66 selected EdgeId, parallel sends, replug, logical and invalid pointer fences");
