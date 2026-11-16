// SPDX-License-Identifier: AGPL-3.0-only
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const vm=require("node:vm");
const ui=fs.readFileSync("site/preview-v2/assets/mixer-native-sonku.js","utf8");
const bridge=fs.readFileSync("src/SonKuNativeBridgeContract.h","utf8");
const native=fs.readFileSync("engine/builtin_fx/SonKuSpatialProcessor.h","utf8");
const core=fs.readFileSync("engine/builtin_fx/sonku/SonKuSpatialCore.h","utf8");
function slice(a,b){
 const i=ui.indexOf(a),j=ui.indexOf(b,i);
 assert(i>=0&&j>i,"missing "+a);
 return ui.slice(i,j);
}
const f=slice("const factoryPresets=Object.freeze(","const valid=s=>");
const values=vm.runInNewContext(
 "(function(){"+f+"\nreturn {factoryPresets,matchFactoryPreset}})()",{});
const {factoryPresets,matchFactoryPreset}=values;
const expected={music:1,studio:2,wide:3,deep:0,orbit:0};
for(const [id,scene] of Object.entries(factoryPresets)){
 assert.equal(scene.sceneVoicing,expected[id]);
 assert.equal(matchFactoryPreset({...scene,renderMode:9}),id);
 const legacy={...scene,renderMode:9};
 delete legacy.sceneVoicing;
 assert.equal(matchFactoryPreset(legacy),id,
    "P9.7 v11 without voicing must keep its old preset labels");
}
const venue=factoryPresets.deep;
assert.equal(venue.sourceDistanceMeters,10);
assert.equal(venue.stereoSpreadDegrees,106);
assert.equal(venue.roomAmount,1);
assert.equal(venue.ambience,1);
assert.equal(venue.outputTrim,1.8);
assert.equal(venue.spatialMix,1);
assert.match(ui,/if\(!p98Capable\)delete scene\.sceneVoicing/);
assert.match(ui,/r\.schemaVersion===12/);
assert.match(ui,/r\.p98SceneVoicing===true/);
assert.match(ui,/p90Capable=\(r\.schemaVersion===11\|\|r\.schemaVersion===12\)/);
assert.match(bridge,/return 12;/);
assert.match(bridge,/if\(!voicing\.isInt\(\)\)return false;/);
assert.match(bridge,/values->setProperty\("sceneVoicing",s\.sceneVoicing\)/);
assert.match(native,/out\.writeInt\(12\)/);
assert.match(native,/out\.writeInt\(accepted_\.sceneVoicing\)/);
assert.match(native,/if\(version>=12\)next\.sceneVoicing=in\.readInt\(\)/);
assert.match(native,/accepted_\.sceneVoicing=1; \/\/ SIGNATURE/);
assert.match(core,/p98::SceneVoicing::valid\(s\.sceneVoicing\)/);
assert.match(core,/p98Voicing_\.finish\(outL,outR\)/);
console.log("P98_GUI_ACK_FACTORY_SIGNATURE_AND_IMMUTABLE_VENUE_CONTRACT_PASSED");
