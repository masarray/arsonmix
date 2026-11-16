// SPDX-License-Identifier: AGPL-3.0-only
"use strict";
const fs=require("node:fs"),assert=require("node:assert/strict");
const vm=require("node:vm");
const ui=fs.readFileSync("site/preview-v2/assets/mixer-native-sonku.js","utf8");
const app=fs.readFileSync("src/Main.cpp","utf8");
const cmake=fs.readFileSync("CMakeLists.txt","utf8");
const core=fs.readFileSync("engine/builtin_fx/sonku/SonKuSpatialCore.h","utf8");
const source=fs.readFileSync("engine/builtin_fx/sonku/SonKuP99MusicalVocalDepth.h","utf8");
const processor=fs.readFileSync("engine/builtin_fx/SonKuSpatialProcessor.h","utf8");
const factory=fs.readFileSync("engine/builtin_fx/sonku/SonKuP99DefaultOrbitStage.h","utf8");
assert.match(cmake,/option\(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH[\s\S]*?OFF\)/);
assert.match(cmake,/P9\.9 requires P9\.8 native typed scene voicings/);
assert.match(app,/reply->setProperty\("p99MusicalVocalDepth",true\)/);
assert.match(ui,/p99Capable=p98Capable && r\.p99MusicalVocalDepth===true/);
assert.match(ui,/p99Capable&&s\.renderMode===9&&s\.sceneVoicing>0/);
assert.match(ui,/VOCAL BODY \+ CENTER CLARITY/);
assert.match(core,/p99VocalDepth_\.source\(/);
assert.match(core,/p99VocalDepth_\.early\(/);
assert.match(core,/p99VocalDepth_\.late\(/);
assert.match(core,/p99VocalDepth_\.roomEarlyFeed\(x0,x1\)/);
assert.match(core,/p99VocalDepth_\.roomLateFeed\(x0,x1\)/);
assert.match(core,/room_\.process\(\(p90World\|\|transparentBass\)\?earlyFeedL/);
assert.match(core,/reverb_\.process\(\(p90World\|\|transparentBass\)\?lateFeedL/);
assert.match(source,/roomWarmSignal_=attention_\*warm/);
assert.match(source,/return \{left\+body,right\+body\}/);
assert.match(source,/lateLow_=alpha\(1350\.\)/);

assert.match(source,/return \{left,right\}; \/\/ NO altered float arithmetic/);
assert.match(source,/No upper-air shelf, de-esser or narrow 4-8k peak/);
assert.match(ui,/spacePreset:3,sourceDistanceMeters:10,roomAmount:1,ambience:1,sceneVoicing:0/);
// Screenshot-specific DEFAULT Orbit must be ACK-applied native state, not
// a cosmetic UI preset, and must be unavailable in older P9.8 binaries.
const from=ui.indexOf("const factoryPresets=Object.freeze(");
const until=ui.indexOf("const valid=s=>",from);
assert(from>=0 && until>from);
const {factoryPresets,matchFactoryPreset}=vm.runInNewContext(
 "(function(){"+ui.slice(from,until)+
 "\nreturn {factoryPresets,matchFactoryPreset}})()",{});
const p=factoryPresets.orbitStage;
assert(p,"new screenshot preset must exist");
const expected={moving:true,clockwise:true,startAzimuthDegrees:0,
 revolutionSeconds:12,stereoSpreadDegrees:127,spatialMix:1,outputTrim:1.8,
 spacePreset:3,sourceDistanceMeters:2.7,roomAmount:1,ambience:.77,sceneVoicing:1};
for(const [key,value] of Object.entries(expected))assert.equal(p[key],value,key);
assert.equal(matchFactoryPreset({...p,renderMode:9},true),"orbitStage");
assert.equal(matchFactoryPreset({...p,renderMode:9},false),"CUSTOM");
assert.equal(matchFactoryPreset({...p,renderMode:9,sceneVoicing:0},true),"CUSTOM");
assert.match(ui,/<option value="orbitStage">ORBIT STAGE · DEFAULT<\/option>/);
assert.match(ui,/defaultOrbitOption\.disabled=!p99Capable/);
assert.match(ui,/picker\.value==="orbitStage"&&!p99Capable/);
assert.match(ui,/!p98Capable \|\| a\.sceneVoicing===b\.sceneVoicing/);
assert.match(ui,/!p88Capable \|\| a\.hrtfElevationTrimDegrees===b\.hrtfElevationTrimDegrees/);
assert.match(processor,/accepted_=armix::sonku::p99::DefaultOrbitStage::settings\(\)/);
const nativeExpected=[
 /revolutionSeconds=12\.f/,/spreadDegrees=127\.f/,
 /outputGain=1\.8f/,/sourceDistanceMeters=2\.7f/,
 /reflections=1\.f/,/reverb=\.77f/,/voicing=1/,
 /room=3/,/s\.moving=true/,/s\.clockwise=true/
];
for(const test of nativeExpected)assert.match(factory,test);
assert.match(factory,/static Settings settings\(\) noexcept/);
// 2.7m live Orbit is brand-new only. Venue 10m/106°/100% untouched.
assert.match(ui,/spacePreset:3,sourceDistanceMeters:10,roomAmount:1,ambience:1,sceneVoicing:0/);
assert.match(source,/\{\{\.112f,\.200f,\.105f,\.108f,\.55f,\.25f\}\}/);
// v12 unchanged; this is an algorithm improvement to existing named scenes.
assert.doesNotMatch(cmake,/ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH[\s\S]{0,100}VERSION 13/);
console.log("P99_NATIVE_TRUTHFUL_V12_CAPABILITY_FACTORY_VENUE_LOCK_PASSED");
