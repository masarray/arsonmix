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

test('release requires the installed native producer, dispatcher and PCM callback',()=>{
  const qualified=inspect(sources);
  assert.deepEqual(qualified.integration.filter(x=>!x.pass).map(x=>x.id),[]);
  const without=(key,symbol)=>inspect({...sources,
    [key]:sources[key].replaceAll(symbol,'DISCONNECTED'),
  });
  assert.equal(status(without('entry','projectInstalledNativeChannelIdGraph'),
    'real-v8-producer-installed').pass,false);
  assert.equal(status(without('runtimeSnapshot','actualFlag(route,"nativeChannelIdPcmActive")'),
    'real-v8-producer-installed').pass,false);
  assert.equal(status(without('entry','executeApprovedNativeControl'),
    'intent-dispatch-not-g0-rejection').pass,false);
  assert.equal(status(without('nativeIntent','callbackOwnsChannelIds'),
    'intent-dispatch-not-g0-rejection').pass,false);
  assert.equal(status(without('engine','dryRoutingLab_.process'),
    'actual-channelid-callback-readback').pass,false);
  assert.equal(status(without('dryRouting','nativePublisher_.render'),
    'actual-channelid-callback-readback').pass,false);
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
