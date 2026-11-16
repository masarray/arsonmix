// SPDX-License-Identifier: AGPL-3.0-only
"use strict";
const fs=require("node:fs"),assert=require("node:assert/strict");
const ui=fs.readFileSync("site/preview-v2/assets/mixer-native-sonku.js","utf8");
const app=fs.readFileSync("src/Main.cpp","utf8");
const cmake=fs.readFileSync("CMakeLists.txt","utf8");
const core=fs.readFileSync("engine/builtin_fx/sonku/SonKuSpatialCore.h","utf8");
const source=fs.readFileSync("engine/builtin_fx/sonku/SonKuP99MusicalVocalDepth.h","utf8");
assert.match(cmake,/option\(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH[\s\S]*?OFF\)/);
assert.match(cmake,/P9\.9 requires P9\.8 native typed scene voicings/);
assert.match(app,/reply->setProperty\("p99MusicalVocalDepth",true\)/);
assert.match(ui,/p99Capable=p98Capable && r\.p99MusicalVocalDepth===true/);
assert.match(ui,/p99Capable&&s\.renderMode===9&&s\.sceneVoicing>0/);
assert.match(ui,/VOCAL BODY \+ CENTER CLARITY/);
assert.match(core,/p99VocalDepth_\.source\(/);
assert.match(core,/p99VocalDepth_\.early\(/);
assert.match(core,/p99VocalDepth_\.late\(/);
assert.match(source,/return \{left,right\}; \/\/ NO altered float arithmetic/);
assert.match(source,/No upper-air shelf, de-esser or narrow 4-8k peak/);
assert.match(ui,/spacePreset:3,sourceDistanceMeters:10,roomAmount:1,ambience:1,sceneVoicing:0/);
// v12 unchanged; this is an algorithm improvement to existing named scenes.
assert.doesNotMatch(cmake,/ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH[\s\S]{0,100}VERSION 13/);
console.log("P99_NATIVE_TRUTHFUL_V12_CAPABILITY_FACTORY_VENUE_LOCK_PASSED");
