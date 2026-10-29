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
  assert.match(e,/ADD\/REMOVE and preset recall use bounded graph handoff/);
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

test('R0B CH1 MOVE inside routed 10CH graph uses the same callback-boundary de-click',()=>{
  const move=e.slice(e.indexOf('bool moveSlot'),e.indexOf('juce::String applyDryRoutingLab'));
  assert.doesNotMatch(move,/dryRoutingLab_\.active\(\)\s*\|\|/);
  assert.match(move,/FxOrderPlan::moved/);
  assert.match(move,/fxMovePendingOrder_\.store/);
  assert.doesNotMatch(move,/detach\(|attach\(|prepareToPlay|releaseResources/);
  const begin=e.indexOf('if (dryRoutingLab_.active())',e.indexOf('callbackCount_.fetch_add'));
  const end=e.indexOf('const bool enabled = monitor_',begin);
  const routed=e.slice(begin,end);
  assert.match(routed,/FxMoveRamp::gain\(\s*routedFxMove,i,samples\)/);
  assert.match(routed,/fxMovePhase_\.store\(2,std::memory_order_release\)/);
  assert.match(routed,/fxMovePhase_\.store\(0,std::memory_order_release\)/);
  assert.match(routed,/cycle\.graph->orderPacked\.store/);
  assert.ok(routed.indexOf('FxMoveRamp::gain')<routed.indexOf('masterLoudness_.process'));
  assert.match(routed,/audibleLeft=\(routedFxMove!=0\|\|cycle\.fadeOut\|\|cycle\.fadeIn\)\?movedPeakL:left/);
  assert.doesNotMatch(routed,/detach\(|attach\(|prepareToPlay|releaseResources/);
});

test('R0B major milestone: ADD and REMOVE are prepared control-thread transactions',()=>{
  const add=e.slice(e.indexOf('juce::String addVst3('),e.indexOf('// Capture VST3 vendor binary state'));
  const remove=e.slice(e.indexOf('bool removeAt('),e.indexOf('bool moveSlot'));
  for(const part of [add,remove]){
    assert.doesNotMatch(part,/detach\s*\(|attach\s*\(|removeAudioCallback|addAudioCallback/);
    assert.match(part,/publishFxEdit\(/);
  }
  assert.match(add,/prepareToPlay\(rate,frames\)/);
  assert.match(add,/latency!=0/);
  assert.match(e,/std::shared_ptr<juce::AudioPluginInstance> processor/);
  assert.match(e,/struct PluginRelease final/);
  assert.match(e,/processor->releaseResources\(\)/);
  assert.match(e,/ownPlugin\s*\(/);
  assert.match(e,/old\.reset\(\); \/\/ Vendor cleanup only on final CONTROL-owned reference/);
  assert.match(e,/handoff_\.stage\(stagedOwner_\.get\(\)\)/);
  const callback=e.slice(e.indexOf('if (dryRoutingLab_.active())',e.indexOf('callbackCount_.fetch_add')),
                         e.indexOf('const bool enabled = monitor_',e.indexOf('callbackCount_.fetch_add')));
  assert.match(callback,/handoff_\.endCycle\(cycle\)/);
  assert.match(callback,/cycle\.fadeOut/);
  assert.match(callback,/cycle\.fadeIn/);
  assert.ok(callback.indexOf('handoff_.endCycle(cycle)')<callback.indexOf('masterLoudness_.process'));
  assert.doesNotMatch(callback,/prepareToPlay|releaseResources|make_shared|make_unique|detach\(/);
  const timer=m.slice(m.indexOf('if (engine_.finalizeGraphSwitch())'),
                     m.indexOf('if (timerTicks_ % 12',m.indexOf('if (engine_.finalizeGraphSwitch())')));
  assert.match(timer,/emitFxAddResult\(pendingFxAddRequestId_/);
  assert.match(timer,/FX_REMOVE_ACK/);
  assert.match(timer,/orbit_\.rebindSlot\(target\)/);
  assert.match(add,/auto prepared=ownPlugin\(std::move\(instance\)\)/);
  const addUi=m.slice(m.indexOf('if (action == "addCatalog")',m.indexOf('if (action == "addScanFolder")')),
                      m.indexOf('else if (action == "fxRemove")'));
  assert.match(addUi,/FX_ADD_STAGED/);
  assert.doesNotMatch(addUi,/\+\+fxGraphRevision_/);
});
