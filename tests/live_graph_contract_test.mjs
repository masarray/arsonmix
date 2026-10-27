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
test('atomic MOVE keeps VST3 instances live while ADD/REMOVE remain guarded',()=>{
  assert.match(e,/if \(handoff_\.busy\(\)\) return false/);
  assert.match(e,/Add\/Remove still use a bounded offline lifecycle/);
  assert.match(e,/std::atomic<std::uint64_t> orderPacked/);
  const begin=e.indexOf('bool moveSlot (int from, int to)');
  const move=e.slice(begin,e.indexOf('juce::var status()',begin));
  assert.match(move,/FxOrderPlan::moved/);
  assert.match(move,/orderPacked.store/);
  assert.doesNotMatch(move,/detach\(|attach\(|releaseResources\(|prepareToPlay\(/);
  assert.match(e,/cycle\.graph->orderPacked.load/);
});
