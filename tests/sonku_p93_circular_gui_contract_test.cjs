// SPDX-License-Identifier: AGPL-3.0-only
// Source-bound tests: exact browser geometry and native FX UI contract.
"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm");
const source=fs.readFileSync("site/preview-v2/assets/mixer-native-sonku.js","utf8");
const css=fs.readFileSync("site/preview-v2/assets/mixer-fx-rack.css","utf8");
const between=(a,b)=>{
 const i=source.indexOf(a),j=source.indexOf(b,i+a.length);
 assert(i>=0&&j>i,"Production SonKu code boundary missing: "+a);
 return source.slice(i,j);
};
const geometry=between("const STAGE=Object.freeze","const root=document.createElement");
const g=vm.runInNewContext("(function(){const angle=(v)=>((v%360)+360)%360;"+
 geometry+"return {STAGE,stagePoint,stageAzimuth,displayPair};})()",{});
// The ellipse is the APPROVED oblique 3D visualization of a circular world
// orbit. A 360° spatial acoustic scene must not be confused with SVG shape.
assert.equal(g.STAGE.rx,148);
assert.equal(g.STAGE.ry,83);
for(const azimuth of [0,22.5,45,90,135,180,225,270,315,359.9,-90,-179.9]){
 const p=g.stagePoint(azimuth);
 assert(Math.abs(((p.x-200)/148)**2+((p.y-174)/83)**2-1)<1.e-8);
 const recovered=g.stageAzimuth(p.x,p.y);
 assert(Math.abs((((recovered-azimuth+540)%360)+360)%360-180)<1.e-7);
}
const same=g.displayPair([0,0]);
assert(Math.abs(same[0].anchor.x-same[1].anchor.x)<1.e-9);
assert(Math.hypot(same[0].visible.x-same[1].visible.x,
                  same[0].visible.y-same[1].visible.y)>=45);
assert.match(source,/M348 174 A148 83 0 0 0 52 174/);
assert.match(source,/M52 174 A148 83 0 0 0 348 174/);
const f=between("const factoryPresets=Object.freeze","const valid=s=>");
const p=vm.runInNewContext("(function(){"+f+
 "return {factoryPresets,matchFactoryPreset};})()",{});
// All five approved historic scenes remain present, and the sixth is
// genuinely gated to the P9.9 engine; P9.8 must never fake its existence.
assert.deepEqual(Object.keys(p.factoryPresets),
 ["orbitStage","music","studio","wide","deep","orbit"]);
for(const [id,scene] of Object.entries(p.factoryPresets)){
 assert.equal(scene.spatialMix,1);
 assert(scene.outputTrim>=1.5&&scene.outputTrim<=2.0); // User-selected powerful scene, bounded linked limiter.
 assert(scene.sourceDistanceMeters>=1.6);
 const max=[10,3.1,6.7,10][scene.spacePreset];
 assert(scene.sourceDistanceMeters<=max);
 assert(scene.stereoSpreadDegrees>=0&&scene.stereoSpreadDegrees<=180);
 assert(scene.roomAmount>=0&&scene.roomAmount<=1);
 assert(scene.ambience>=0&&scene.ambience<=1);
 if(id==="orbitStage"){
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9},true),id);
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9},false),"CUSTOM");
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9,sceneVoicing:0},true),"CUSTOM");
 }else{
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9}),id);
 }
 assert.equal(p.matchFactoryPreset({...scene,renderMode:8},true),"CUSTOM");
}
const modified={...p.factoryPresets.music,renderMode:9,outputTrim:2.238721};
assert.equal(p.matchFactoryPreset(modified),"CUSTOM");
const sync=between("function syncPower(){","const context=()=>{");
const state={graph:{channels:[{id:"ch-1",fxRackBypassed:false,
 inserts:[{id:"insert",catalogId:"armix.builtin.8d-sonku.v1",bypassed:false}]}]}};
const element={dataset:{},disabled:true,
 setAttribute(k,v){this[k]=v}};
const root={dataset:{},querySelector(){return {textContent:""};}},current={track:{},slot:0};
const power=vm.runInNewContext("(function(){let current=__current;"+
 "let root=__root;const ID=\"armix.builtin.8d-sonku.v1\";let $=()=>__element;let snapshot=()=>__state;"+
 "let native=()=>true;"+sync+"return syncPower;})()",{
 __current:current,__root:root,__element:element,__state:state,
 window:{__ARMIX_NATIVE_M2__:{nativeIdFor(){return "ch-1";}}},
 document:{querySelector(){return {classList:{contains(){return false}}};}}
});
power();assert.equal(element.dataset.fxState,"on");assert.equal(element.disabled,false);
state.graph.channels[0].inserts[0].bypassed=true;power();
assert.equal(element.dataset.fxState,"off");assert.equal(element.disabled,false);
state.graph.channels[0].fxRackBypassed=true;power();
assert.equal(element.dataset.fxState,"rack-off");assert.equal(element.disabled,true);
state.graph.channels[0].fxRackBypassed=false;
state.graph.channels[0].inserts[0].catalogId="unrelated-fx";power();
assert.equal(element.dataset.fxState,"unknown");assert.equal(element.disabled,true);
assert.match(source,/consumeAction\?\.\(\s*current\.track,"slotPower"/);
assert.match(source,/root\.querySelector\("\.sk-stage"\)\.appendChild\(root\.querySelector\("\.sk-space-panel"\)\)/);
assert.match(source,/head\.setPointerCapture\(e\.pointerId\)/);
assert.match(source,/window\.addEventListener\("resize",constrainWindow\)/);
assert.match(source,/if\(!p91p92Capable\|\|draft\.renderMode!==9\|\|!preset\|\|pending\|\|/);
assert.match(source,/picker\.value==="orbitStage"&&!p99Capable/);
assert.match(source,/root\.dataset\.fxState==="on"/);
assert.match(source,/if\(previous!==state\)/);
assert.match(css,/\.mx-sonku \.sk-stage \.sk-space-panel\{display:grid/);
assert.match(source,/current\.track\.slotBypass\[current\.slot\]/);
console.log("PASS SonKu P9.3: approved perspective orbit, inverse drag, native power readback, draggable window, five historic + gated new default Orbit presets, compact SPACE placement");
