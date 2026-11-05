// SPDX-License-Identifier: AGPL-3.0-only
// Milestone A: STATIC negative source-integration regression tests.
// These cannot replace real JUCE PCM/Windows screenshot/physical hardware FAT.
import test from 'node:test';
import assert from 'node:assert/strict';
import {collectSources,inspectMilestoneA,PATHS} from '../scripts/check_console_puzzle.mjs';

const sources=collectSources();
const inspect=s=>inspectMilestoneA(s);
const status=(result,id)=>[...result.baseline,...result.integration].find(x=>x.id===id);

test('the current main-approved Windows source has no competing native mixer',()=>{
  const result=inspect(sources);
  const failed=result.baseline.filter(x=>!x.pass);
  assert.deepEqual(failed.map(x=>x.id),[],JSON.stringify(failed,null,2));
  assert.equal(status(result,'single-approved-pages-source').pass,true);
  assert.equal(status(result,'no-retired-console-renderer').pass,true);
  assert.equal(status(result,'native-four-event-socket').pass,true);
  assert.equal(status(result,'gui-four-event-consumer').pass,true);
});

test('obsolete renderer regression immediately fails the non-release source preflight',()=>{
  const a={...sources,cmake:sources.cmake+
    '\njuce_add_binary_data(ARMixAssets SOURCES web/console-native.html)\n'};
  assert.equal(status(inspect(a),'no-retired-console-renderer').pass,false);
  const b={...sources,entry:sources.entry+
    '\ndata = BinaryData::consolenative_html;\n'};
  assert.equal(status(inspect(b),'no-retired-console-renderer').pass,false);
  const c={...sources,cmake:sources.cmake.replaceAll(
    'build_approved_native_gui.py','nonapproved_gui.py')};
  assert.equal(status(inspect(c),'single-approved-pages-source').pass,false);
});

test('breaking native event protocol or accepted-readback reconciliation blocks integration',()=>{
  const brokenEntry={...sources,entry:sources.entry.replaceAll('arMixConsoleAck','BAD_ACK')};
  assert.equal(status(inspect(brokenEntry),'native-four-event-socket').pass,false);
  const brokenGui={...sources,gui:sources.gui.replaceAll('arMixConsoleSnapshot','BAD_SNAPSHOT')};
  assert.equal(status(inspect(brokenGui),'gui-four-event-consumer').pass,false);
  const brokenProtocol={...sources,protocol:sources.protocol.replaceAll('readbackProvesIntent','ACK_ONLY_IS_SUCCESS')};
  assert.equal(status(inspect(brokenProtocol),'gui-four-event-consumer').pass,false);
});

test('a fake ENGINE adapter cannot pass installed native callback release checks',()=>{
  const fake={...sources,
    nativeV8:'PuzzleV8Wire nativeBus acceptedRouting',
    installedProjection:'// old adapter that never verified installed native callbacks',
    nativeMixer:'// no native PCM publisher',
    entry:sources.entry+'#include "ConsoleLiveBridgeV6.h"\\n',
    engine:sources.engine+' nativeChannelIdPcmActive ',
  };
  assert.equal(status(inspect(fake),'real-v8-producer-installed').pass,false,
    'old ENGINE symbol names cannot fake an installed render authority');
  assert.equal(status(inspect(fake),'actual-channelid-callback-readback').pass,false,
    'a fake adapted bus cannot fake real JUCE PCM');
  const rejected={...sources,entry:sources.entry.replaceAll(
    'handleApprovedNativeControl','G0_BLANKET_REJECTION')};
  assert.equal(status(inspect(rejected),'intent-dispatch-not-g0-rejection').pass,false,
    'removing actual native control dispatcher must block release');
  const withoutSetter={...sources,nativeIntent:'// UI-only optimistic controls'};
  assert.equal(status(inspect(withoutSetter),'intent-dispatch-not-g0-rejection').pass,false);
});

test('current installed callback and ACK/readback pass the *source* convergence gate',()=>{
  const checked=inspect(sources);
  assert.deepEqual(checked.integration.filter(x=>!x.pass).map(x=>x.id),[],
    JSON.stringify(checked.integration,null,2));
  // Source is necessary but never sufficient: no physical listening here.
});

test('accepted native callback still requires a proper event dispatcher and physical FAT',()=>{
  const g0=inspect(sources);
  if(sources.entry.includes('native_console_m2::reject (args, accepted)')) {
    assert.equal(status(g0,'intent-dispatch-not-g0-rejection').pass,false);
  }
  // A pure source check is never proof of hardware.
  assert.equal(typeof g0.g0Truthful,'boolean');
  assert.ok(g0.integration.every(x=>typeof x.pass==='boolean'&&x.release));
  assert.ok(g0.baseline.every(x=>x.release===false));
});

test('release preflight reads M3C INTEGRATOR-owned V8 projection rather than abandoned ENGINE path',()=>{
  assert.equal(collectSources().projection,sources.projection);
  assert.equal(PATHS.projection,'integration/ApprovedNativeV8Projection.h');
  assert.notEqual(PATHS.projection,'src/ApprovedNativeV8Projection.h');
  // This is a source-path regression check, never a permanent G0 assertion:
  // a future fully admitted product must also pass this test.
  if (sources.projection) {
    assert.match(sources.projection,/mainChannelId/);
    assert.match(sources.projection,/processingFormat/);
  }
  assert.ok(inspect(sources).integration.every(x=>typeof x.pass==='boolean'));
});
