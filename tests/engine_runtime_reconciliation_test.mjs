// SPDX-License-Identifier: AGPL-3.0-only
// Fast G0 architecture gate. Tests the INSTALLED owner (M4 compatibility)
// without pretending that the prepared ChannelId graph is live.
// When ChannelId cutover is actually approved, this gate MUST change in
// the same exact-SHA integration commit as the JUCE callback and native ACK.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read=p=>readFileSync(p,'utf8');
const engine=read('src/LiveAudioEngine.h');
const host=read('src/Main.cpp');
const cmake=read('CMakeLists.txt');
const authority=read('engine/core/NativeRenderAuthority.h');
const bridge=read('src/ConsoleLiveBridgeV6.h');
const wire=read('engine/core/ConsolePuzzleV8Wire.h');
const schema=JSON.parse(read('contracts/console-puzzle-v8.json'));
const worker=read('src/NativeShowSaveWorker.h');
const physical=read('src/LiveDryRoutingLab.h');
const dynamic=read('engine/core/DynamicNativeSessionPublisher.h');
const realtimePermit=read('engine/core/RealtimeRenderPermit.h');
const start=engine.indexOf('void audioDeviceIOCallbackWithContext (');
assert.ok(start>0,'missing ONE JUCE production callback');
const callback=engine.slice(start);
const count=(s,pattern)=>[...s.matchAll(pattern)].length;

test('exactly one production audio callback and one native PCM owner',()=>{
  assert.equal(count(engine,/void audioDeviceIOCallbackWithContext\s*\(/g),1);
  assert.equal(count(host,/LiveAudioEngine engine_;/g),1);
  assert.match(engine,/#include "\.\.\/engine\/core\/NativeRenderAuthority\.h"/);
  assert.match(callback,/NativeRenderAuthority::select\(/);
  assert.match(callback,/NativeRenderOwner::failSilentUnpatched/);
  assert.match(callback,/NativeRenderOwner::acceptedM4Session/);
  assert.equal(count(callback,/dryRoutingLab_\.process\(/g),1,
    'two native M4 PCM renders would double-sum or double-process vendor FX');
  assert.doesNotMatch(callback,/DynamicNativeSessionPublisher|DynamicAcceptedSession|\.publisher_\.render\(/,
    'unadmitted universal graph must never run alongside real M4 callback');
  assert.match(authority,/acceptedM4Session/);
  assert.match(authority,/isolatedDiagnostic/);
});

test('unpatched is silent and diagnostic path cannot host a legacy second VST engine',()=>{
  const unpatched=callback.indexOf('NativeRenderOwner::failSilentUnpatched');
  const routed=callback.indexOf('NativeRenderOwner::acceptedM4Session');
  const isolated=callback.indexOf('// The only remaining stereo renderer is DIAGNOSTIC');
  const finish=callback.indexOf('const auto elapsedUs = clockMicros() - beginUs',isolated);
  assert.ok(unpatched>=0&&routed>unpatched&&isolated>routed&&finish>isolated);
  const first=callback.slice(unpatched,routed);
  assert.match(first,/FloatVectorOperations::clear\(outputs\[c\], samples\)/);
  assert.match(first,/handoff_\.endCycle\(cycle\)/,
    'plugin owner must still retire on the CONTROL thread when unpatched');
  const tail=callback.slice(isolated,finish);
  assert.doesNotMatch(tail,/processor->processBlock|MidiBuffer midi|createPluginInstance|prepareToPlay/,
    'do not revive a hidden stereo plugin engine in diagnostic fallback');
  assert.match(tail,/diagnosticTone_\.next/);
});

test('original CH1 vendor FX/Orbit is leased exactly once in accepted M4 renderer',()=>{
  const old=engine.indexOf('static void processLegacyChannelOne(');
  const end=engine.indexOf('void audioDeviceIOCallbackWithContext',old);
  assert.ok(old>=0&&end>old);
  const fx=engine.slice(old,end);
  assert.match(fx,/slot\.processor->processBlock\(buffer,midi\)/);
  assert.match(fx,/bypass\.request\(graph->bypassFlags/);
  assert.match(fx,/catch\(\.\.\.\)/);
  assert.match(callback,/LegacyChannelOneLease ch1Lease\{this,cycle\.graph\}/);
  assert.match(callback,/&processLegacyChannelOne/);
  assert.doesNotMatch(callback,/std::make_unique<|std::make_shared<|createPluginInstance\(/,
    'no FX instantiation or graph allocation in device callback');
});

test('shipping application does not compile a second test-only native mixer',()=>{
  const app=cmake.split(/\r?\n/).find(l=>l.startsWith('target_sources(ARMixConsole PRIVATE'));
  assert.ok(app,'native JUCE application source list missing');
  assert.match(app,/src\/LiveAudioEngine\.h/);
  assert.match(app,/engine\/core\/NativeRenderAuthority\.h/);
  assert.doesNotMatch(app,/PreparedNativeMixerSession|DynamicNativeSessionPublisher|DynamicPreparedMixerBlock|UnifiedLegacyProductionAdmission|LegacyLiveGraphMigration/,
    'prepared graph may be compiled as tests/includes, not advertised as a second application entry point');
  assert.match(cmake,/target_sources\(ARMixNativeMixerSessionTests PRIVATE tests\/prepared_native_mixer_session_test\.cpp engine\/core\/PreparedNativeMixerSession\.h\)/);
});

test('GUI projection cannot falsely claim the prepared ChannelId engine is live',()=>{
  assert.equal(schema.schemaVersion,6);
  assert.deepEqual(schema.wire.events,
    ['arMixConsoleSubscribe','arMixConsoleSnapshot','arMixConsoleIntent','arMixConsoleAck']);
  for(const item of ['deviceEpoch','baseGraphRevision','baseRosterRevision','requestId','targetChannelId'])
    assert.ok(schema.intentRequired.includes(item));
  assert.ok(bridge.includes('w.productionCallbackLive=false'));
  assert.match(bridge,/legacyM4/);
  assert.match(wire,/callbackOwnsChannelIds\\":false/);
  assert.match(wire,/fullVendorFxMigration\\":false/);
  assert.ok(wire.includes('supportedOps'));
  assert.match(bridge,/parseNativeChannelId/);
  assert.match(bridge,/validConsoleRequestId/);
});

test('workers handle Show IO outside the audio callback, not as a second DSP engine',()=>{
  assert.match(worker,/class NativeShowSaveWorker final : private juce::Thread/);
  assert.match(worker,/store\.commit\(show,\{snapshot_\},blobs_\)/);
  assert.match(worker,/void releaseJob\(\)/);
  assert.doesNotMatch(callback,/NativeShowStore|\.loadWithStates\(|\.commit\(show/);
});

test('installed M4 and future ChannelId share one nonblocking render permit',()=>{
  assert.ok(physical.includes('RealtimeRenderPermit exclusive{rendering_}'));
  assert.ok(dynamic.includes('RealtimeRenderPermit exclusive{rendering_}'));
  assert.ok(realtimePermit.includes('test_and_set(std::memory_order_acquire)'));
  assert.ok(realtimePermit.includes('clear(std::memory_order_release)'));
  assert.ok(!realtimePermit.includes('std::mutex') && !realtimePermit.includes('condition_variable'));
  assert.ok(physical.includes('if(!exclusive)'));
  assert.ok(dynamic.includes('if(!exclusive)'));
});

test('legacy M4 operations are advertised separately, not as fake universal ChannelIds',()=>{
  assert.ok(bridge.includes('LEGACY_M4_ORDINAL'));
  assert.ok(bridge.includes('acceptedChannels'));
  assert.ok(bridge.includes('route.getProperty("topologyFault",false)'));
  assert.ok(bridge.includes('object->setProperty("supportedOps",juce::var(allowed))'));
  assert.ok(bridge.includes('w.productionCallbackLive=false'));
  assert.ok(wire.includes('supportedOps'));
});

test('legacy M4 GUI commands require accepted physical authority, not just guessed ordinal',()=>{
  assert.ok(bridge.includes('.getProperty("controlPlane","").toString()!="LEGACY_M4_ORDINAL"'));
  assert.ok(bridge.includes('PRODUCTION_GRAPH_NOT_ADMITTED'));
  assert.ok(bridge.includes('legacyRoutingRevision'));
});
