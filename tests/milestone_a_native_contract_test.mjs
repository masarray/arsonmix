// SPDX-License-Identifier: AGPL-3.0-only
// Milestone A: STATIC negative source-integration regression tests.
// These cannot replace real JUCE PCM/Windows screenshot/physical hardware FAT.
import test from 'node:test';
import assert from 'node:assert/strict';
import {collectSources,inspectMilestoneA} from '../scripts/check_console_puzzle.mjs';

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

test('engine-only graph headers or a fake native producer cannot pass milestone A',()=>{
  const fake={...sources,
    nativeV8:'PuzzleV8Wire nativeBus acceptedRouting',
    projection:'mainChannelId processingFormat',
    entry:sources.entry+'#include "ConsoleLiveBridgeV6.h"\n',
    engine:sources.engine+' nativeChannelIdPcmActive ',
  };
  assert.equal(status(inspect(fake),'real-v8-producer-installed').pass,true,
    'a static adapter reference alone can match SOURCE, but is NOT audio proof');
  assert.equal(status(inspect(fake),'intent-dispatch-not-g0-rejection').pass,false,
    'G0 REJECTED dispatch must still prohibit release');
  assert.equal(inspect(fake).integration.every(x=>x.pass),false);
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
