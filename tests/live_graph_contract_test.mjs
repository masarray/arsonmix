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


test('R0B B3 NORMAL output is fail-silent while FX graph retirement still progresses',()=>{
  const beginning=e.indexOf('void audioDeviceIOCallbackWithContext');
  const fence=e.indexOf('if (!independent_.active()',beginning);
  const lease=e.indexOf('const auto cycle = handoff_.beginCycle()',beginning);
  const failSilent=e.indexOf('if (activeDiagnosticRoute == armix::diagnostics::Route::normal',beginning);
  const callbackMetrics=e.indexOf('const std::uint64_t beginUs = clockMicros()',beginning);
  assert.ok(beginning>=0 && fence>beginning && lease>fence &&
    failSilent>lease && callbackMetrics>failSilent,
    'DIRECT fence first, then FX cycle lease, then fail-silent native gate');
  const gate=e.slice(failSilent,callbackMetrics);
  assert.match(gate,/!dryRoutingLab_\.active\(\)/);
  assert.match(gate,/FloatVectorOperations::clear\(outputs\[c\], samples\)/);
  assert.match(gate,/outputPeakL_\.store\(0\.f/);
  assert.match(gate,/handoff_\.endCycle\(cycle\)/,
    'FX ADD/REMOVE and recall must ACK even when the production route is unpatched');
  assert.doesNotMatch(gate,/detach\(|attach\(|new |malloc\(|prepareToPlay|processBlock|AudioDeviceManager/);
  assert.match(e,/setProperty \("productionRoutingState",/);
  assert.match(e,/"UNPATCHED_SILENT"/);
  assert.match(e,/"NATIVE_ROUTE_STALE_FAIL_SILENT"/);
});


test('R0B B3 always-on native 10CH startup is verified, muted and unpatched',()=>{
  const initial=e.slice(e.indexOf('juce::String start ('),e.indexOf('void stop()'));
  assert.match(initial,/monitor_\.store \(false/);
  assert.match(initial,/approveDirectRoute/);
  assert.match(initial,/startUnpatchedProductionSession\(\)/);
  assert.ok(initial.indexOf('approveDirectRoute')<initial.lastIndexOf('startUnpatchedProductionSession()'));
  const start=e.slice(e.indexOf('juce::String startUnpatchedProductionSession()'),
                      e.indexOf('juce::String clearProductionRouting()'));
  assert.match(start,/setOutputMuted\(true\)/);
  assert.match(start,/!directFence_\.permitted\(\)/);
  assert.match(start,/startUnpatchedSession\(/);
  assert.match(start,/device->getCurrentSampleRate\(\)/);
  assert.doesNotMatch(start,/attach\(|detach\(|addAudioCallback|newGraph|startAudioDevice|openAudioDevice/);
  const lab=readFileSync('src/LiveDryRoutingLab.h','utf8');
  const factory=lab.slice(lab.indexOf('juce::String startUnpatchedSession'),
                          lab.indexOf('juce::String apply ('));
  assert.match(factory,/row->setProperty\("mode","NONE"\)/);
  assert.match(factory,/row->setProperty\("left",-1\)/);
  assert.match(factory,/row->setProperty\("right",-1\)/);
  assert.match(factory,/const std::array<FxSelection,fxSlots> none\{\}/);
  assert.match(factory,/return apply\(/);
  assert.match(factory,/sampleRate,deviceFrames,false\)/);
  assert.doesNotMatch(factory,/autoMap|firstInput|callbackIndex|start\(|attach\(|detach\(/);
  assert.match(lab,/setProperty\("hasAcceptedRoute",hasExplicitRoute\(\)\)/);
  assert.match(e,/NATIVE_UNPATCHED_SILENT/);
  const clear=e.slice(e.indexOf('juce::String clearProductionRouting()'),
                      e.indexOf('// Explicit message-thread transaction.',e.indexOf('juce::String clearProductionRouting()')));
  assert.match(clear,/dryRoutingLab_\.disable\(\)/);
  assert.match(clear,/startUnpatchedProductionSession\(\)/);
  assert.doesNotMatch(clear,/monitor_\.store\(true\)|setOutputMuted\(false\)/);
});

test('R0B B3 preserves diagnostic isolation without exposing an accepted patch',()=>{
 const diagnostic=e.slice(e.indexOf('juce::String setDiagnosticRoute'),
                          e.indexOf('armix::diagnostics::Route diagnosticRoute()'));
 assert.match(diagnostic,/dryRoutingLab_\.hasExplicitRoute\(\)/);
 assert.match(diagnostic,/dryRoutingLab_\.disable\(\)/);
 assert.match(diagnostic,/startUnpatchedProductionSession\(\)/);
 assert.match(diagnostic,/resumeVerifiedDirectRoute\(\)/);
 const manual=e.slice(e.indexOf('void beginManualAudioSetup()'),
                      e.indexOf('bool independentClockActive()'));
 assert.match(manual,/setOutputMuted\(true\)/);
 assert.match(manual,/dryRoutingLab_\.disable\(\)/);
 assert.match(manual,/startUnpatchedProductionSession\(\)/);
 assert.doesNotMatch(manual,/setOutputMuted\(false\)/);
});


test('R0B B3 unpatched session preserves CH1 presets and zero-PDC gates only on explicit patch',()=>{
  const capture=e.slice(e.indexOf('juce::String captureSnapshot'),
                        e.indexOf('juce::String restoreSnapshot'));
  const restore=e.slice(e.indexOf('juce::String restoreSnapshot'),
                        e.indexOf('bool setBypass'));
  assert.match(capture,/dryRoutingLab_\.hasExplicitRoute\(\)/);
  assert.match(restore,/dryRoutingLab_\.hasExplicitRoute\(\)/);
  assert.doesNotMatch(capture,/dryRoutingLab_\.active\(\)/);
  assert.doesNotMatch(restore,/dryRoutingLab_\.active\(\)/);
  assert.match(capture,/isOutputMuted\(\)/);
  assert.match(restore,/handoff_\.stage/);
  assert.match(e,/if\(dryRoutingLab_\.hasExplicitRoute\(\) && latency!=0\)/);
  assert.match(e,/if\(dryRoutingLab_\.hasExplicitRoute\(\) && !value &&/);
  assert.match(e,/CH1 rack contains a latent VST3: zero-latency settings are required/);
});

test('R0B B3 never processes MASTER FX, strips or AUX until explicit ACK',()=>{
  const lab=readFileSync('src/LiveDryRoutingLab.h','utf8');
  const body=lab.slice(lab.indexOf('bool process(const float* const* inputs'),
                       lab.indexOf('juce::var status('));
  const zero=body.indexOf('FloatVectorOperations::clear(outputs[c],frames)');
  const guard=body.indexOf('!userPatched_.load(std::memory_order_acquire)');
  const lease=body.indexOf('publisher_.acquire()');
  const dsp=body.indexOf('snapshot->prepared->process(');
  assert.ok(zero>=0&&guard>zero&&lease>guard&&dsp>lease);
  assert.match(body,/if\(!userPatched_\.load\(std::memory_order_acquire\)\)/);
  assert.match(body,/inputCount!=callbackInputCount_\.load/);
  assert.match(body.slice(guard,lease),/return true/);
  const apply=lab.slice(lab.indexOf('juce::String apply ('),
                        lab.indexOf('bool setChannelParameter('));
  assert.match(apply,/bool explicitSelection=true/);
  assert.match(apply,/acceptedRoute_=explicitSelection\?command:juce::var\(\)/);
  assert.match(apply,/userPatched_\.store\(explicitSelection,std::memory_order_release\)/);
  const empty=lab.slice(lab.indexOf('juce::String startUnpatchedSession'),
                        lab.indexOf('juce::String apply ('));
  assert.match(empty,/sampleRate,deviceFrames,false/);
  assert.match(lab,/userPatched_\.store\(false,std::memory_order_release\)/);
  assert.match(lab,/std::atomic<bool> userPatched_\{false\}/);
});
