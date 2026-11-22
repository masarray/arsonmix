// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {availableFeatureIds,featureById,selectWindowsFeatureScope,
 validateWindowsFeaturePlan} from '../scripts/windows_feature_scope.mjs';

const cmake=readFileSync('CMakeLists.txt','utf8');
const cases=[
 ['audio-io','engine/device/DeviceSessionTransitionPolicy.h'],
 ['mixer-routing','tests/channel_routing_contract_test.cpp'],
 ['graphic-eq','engine/builtin_fx/GraphicEq31Core.h'],
 ['compressor','engine/builtin_fx/ChannelCompressor.h'],
 ['player','engine/player/PlayerPcmRing.h'],
 ['vst-host','tests/vst3_startup_safety_test.cpp'],
 ['vocal-fx','engine/builtin_fx/HarmonyDsp.h'],
 ['spatial','engine/builtin_fx/SonKuSpatialProcessor.h'],
 ['session','tests/durable_mixer_session_test.cpp']
];
test('all feature families map to real CMake targets and CTests',()=>{
 assert.equal(availableFeatureIds().length,cases.length);
 for(const [id,path] of cases){
   const plan=selectWindowsFeatureScope([path,'docs/architecture/FOCUSED_FAT.md']);
   assert.equal(plan?.id,id,path);
   assert.ok(plan.targets.length>=2&&plan.tests.length>=2);
   for(const target of plan.targets)
     assert.match(cmake,new RegExp('^\\s*(?:add_executable|juce_add_console_app|juce_add_gui_app)\\(\\s*'+target+'\\b','m'),id+':'+target);
   for(const name of plan.tests)
     assert.match(cmake,new RegExp('add_test\\(NAME\\s+'+name+'\\b'),id+':'+name);
   assert.equal(validateWindowsFeaturePlan(plan,cmake,plan.tests),true);
 }
});
test('mixed, unknown, and shared native edits fail closed',()=>{
 for(const files of [
   [],['docs/README.md'],['src/Main.cpp'],['CMakeLists.txt'],
   ['engine/builtin_fx/GuitarRigEditor.h'],
   ['engine/device/DeviceSessionTransitionPolicy.h','src/LiveAudioEngine.h'],
   ['engine/builtin_fx/HarmonyDsp.h','engine/builtin_fx/ChannelCompressor.h'],
   ['engine/device/DeviceSessionTransitionPolicy.h','tests/channel_routing_contract_test.cpp'],
   ['tests/unknown_test.cpp'],['../outside.cpp']])
   assert.equal(selectWindowsFeatureScope(files),null,files.join(','));
 assert.equal(featureById('unreviewed'),null);
});
test('unrecognized test or target fails before compilation',()=>{
 const plan=featureById('compressor');
 assert.throws(()=>validateWindowsFeaturePlan(plan,'',plan.tests),/FEATURE_UNDECLARED_CMAKE_TARGET/);
 assert.throws(()=>validateWindowsFeaturePlan(plan,cmake,[]),/FEATURE_MISSING_CTEST/);
});
test('generic FAT contains no plugin-named jobs; scoped test is never full',()=>{
 const w=readFileSync('.github/workflows/p1-windows.yml','utf8');
 assert.doesNotMatch(w,/focused_harmony_fat|FOCUSED_HARMONY_FAT|focused vocal harmony/i);
 assert.match(w,/feature_scope:/);
 assert.match(w,/^    name: Windows MSVC \/ realtime CTest$/m);
 assert.match(w,/^    name: Windows FEATURE scoped native CTest \(NOT full\)$/m);
 assert.match(w,/windows-feature:/);
 assert.match(w,/inputs.feature_scope == ''/);
 assert.match(w,/Windows FEATURE scoped native CTest \(NOT full\)/);
 assert.match(w,/Feature-scoped Windows MSVC targets \(development ONLY\)/);
 assert.match(w,/Run feature-scoped CTests \(development ONLY\)/);
 assert.match(w,/Run full realtime CTest \(release or nativeFull\)/);
 assert.match(w,/FEATURE_CANNOT_PACKAGE_OR_PREVIEW/);
 assert.match(w,/--target \$targets/);
 assert.match(w,/ctest --test-dir build -C Release --output-on-failure -R \$pattern/);
 assert.match(w,/run: cmake --build build --config Release --parallel 2\n/);
 assert.match(w,/run: ctest --test-dir build -C Release --output-on-failure/);
});
