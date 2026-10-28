// SPDX-License-Identifier: AGPL-3.0-only
// Static wiring contracts supplement the 12k native handoff stress cycles.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const e=readFileSync('src/LiveAudioEngine.h','utf8');
const h=readFileSync('src/LiveGraphHandoff.h','utf8');
const m=readFileSync('src/Main.cpp','utf8');
const b=readFileSync('web/bridge.js','utf8');
test('live recall stages complete VST state before pointer publication',()=>{
  const restore=e.slice(e.indexOf('juce::String restoreSnapshot'), e.indexOf('bool setBypass'));
  assert.match(restore,/prepareToPlay \(rate, buffer\)/);
  assert.match(restore,/setStateInformation/);
  assert.match(restore,/handoff_\.stage \(stagedOwner_\.get\(\)\)/);
  assert.doesNotMatch(restore,/\bdetach\s*\(/);
});
test('RT callback owns pointer switch and message thread owns third party destruction',()=>{
  assert.match(e,/handoff_\.beginCycle\(\)/);
  assert.match(e,/handoff_\.endCycle \(cycle\)/);
  assert.match(e,/handoff_\.takeRetired\(\)/);
  assert.match(e,/handoff_\.completeRetirement\(\)/);
  assert.match(h,/std::atomic<Graph\*> active_/);
  assert.match(h,/std::atomic<int> phase_/);
});
test('preset UI reflects callback-acknowledged state not queued state',()=>{
  assert.match(m,/if \(engine_\.finalizeGraphSwitch\(\)\)/);
  assert.match(m,/pendingPresetUI_/);
  assert.match(m,/arMixNativePresetLoaded/);
  assert.match(b,/RECALL LIVE/);
});
test('ADD/REMOVE remain guarded while MOVE publishes physical-slot order',()=>{
  assert.match(e,/fxMovePhase_\.load/);
  assert.match(e,/FxOrderPlan::moved/);
  assert.match(e,/Add\/Remove still use a bounded offline lifecycle/);
  const move=e.slice(e.indexOf('bool moveSlot'),e.indexOf('juce::String applyDryRoutingLab'));
  assert.doesNotMatch(move,/detach\(|attach\(/);
});

test('bypass is sample-smoothed on audio callback without control-thread CPU work',()=>{
 assert.match(e,/bypassEnvelope/);
 assert.match(e,/bypass\.request\(cycle\.graph->bypassFlags/);
 assert.match(e,/bypass\.transitioning\(\)/);
 assert.match(e,/dryScratch_\.setSize \(2, maxFrames/);
 assert.match(e,/bypass\.next\(\)/);
 assert.match(e,/bypass\.shouldProcessWet\(\)/);
 assert.doesNotMatch(e.slice(e.indexOf('bool setBypass'),e.indexOf('bool removeAt')),/detach\(|prepareToPlay|releaseResources/);
});
