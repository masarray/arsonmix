// SPDX-License-Identifier: AGPL-3.0-only
// Permanent source contracts. CPU/FIFO functional correctness is independently
// tested by Release-active native CTest, not source regexes alone.
import test from 'node:test';
import {analyzeHealthLog,compareDryFx,p95p99FromDelta,parseHealthLog,markdownReport,SCHEMA} from '../scripts/audio_reliability_profile.mjs';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read = p=>readFileSync(p,'utf8');
const host=read('src/Main.cpp');
const diag=read('src/DiagnosticsRecorder.h');
const engine=read('src/LiveAudioEngine.h');
const ui=read('web/monitor.js');
const bridge=read('engine/device/AdaptiveClockBridge.h');
const policy=read('engine/device/AudioProfilePolicy.h');
const health=read('engine/core/RealtimeHealthHistogram.h');
const cmake=read('CMakeLists.txt');

test('native one-click copy uses system clipboard and includes last event markers',()=>{
  assert.match(host,/if \(action == "copyDiagnosticReport"\)/);
  assert.match(host,/makeAudioReport\(\)/);
  assert.match(host,/juce::SystemClipboard::copyTextToClipboard \(report\)/);
  assert.match(host,/getAvailableSampleRates\(\)/);
  assert.match(host,/getAvailableBufferSizes\(\)/);
  assert.match(host,/getOutputLatencyInSamples\(\)/);
  assert.match(host,/diagnostics_\.recentReport \(48\)/);
  assert.match(host,/arMixDiagnosticReportCopied/);
  assert.match(diag,/recentCapacity = 64/);
  assert.match(diag,/importantCapacity = 24/);
  assert.match(diag,/kind != "AUDIO_HEALTH"/);
  assert.match(host,/diagnostics_\.significantReport\(\)/);
  assert.match(diag,/recent_\[recentHead_\] =/);
  assert.match(ui,/COPY FULL REPORT/);
  assert.match(ui,/action:"copyDiagnosticReport"/);
  assert.match(ui,/arMixDiagnosticReportCopied/);
});
test('P0 capability report uses real independent endpoints and never auto-applies buffer hints',()=>{
  const adapter=read('engine/device/PeriodCapabilityEvidence.h');
  assert.match(engine,/juce::AudioIODevice\* diagnosticRenderDevice\(\) noexcept/);
  assert.match(engine,/juce::AudioIODevice\* diagnosticCaptureDevice\(\) noexcept/);
  assert.match(engine,/independent_\.active\(\) \? independent_\.renderDevice\(\)/);
  assert.match(engine,/independent_\.active\(\) \? independent_\.captureDevice\(\)/);
  assert.match(host,/auto\* device = engine_\.diagnosticRenderDevice\(\)/);
  assert.match(host,/auto\* capture = engine_\.diagnosticCaptureDevice\(\)/);
  assert.match(host,/PeriodCapabilityEvidence::inspect/);
  assert.match(host,/NOT_FAT_QUALIFIED_DO_NOT_AUTO_APPLY/);
  assert.match(host,/WASAPI shared can/);
  assert.match(adapter,/std::span<const int>/);
  assert.match(cmake,/add_test\(NAME NativeDriverPeriodEvidence COMMAND ARMixPeriodEvidenceTests\)/);
  assert.match(policy,/unqualifiedLowLatency/);
  assert.match(policy,/explicitExclusiveApprovalRequired/);
});

test('P0 ASIO native COM-driver registration attestation runs only on control plane',()=>{
  const registry=read('engine/device/AsioRegistryIdentityProbe.h');
  const policy=read('engine/device/AsioRegistrationPolicy.h');
  assert.match(registry,/RegOpenKeyExW\(HKEY_LOCAL_MACHINE/);
  assert.match(registry,/L"SOFTWARE\\\\ASIO"/);
  assert.match(registry,/RegEnumKeyExW/);
  assert.match(registry,/InprocServer32/);
  assert.match(policy,/ambiguousName/);
  assert.match(policy,/canonicalGuid/);
  assert.match(engine,/savedAsioPreflight/);
  assert.match(engine,/safeRequirements\.setAttribute \("deviceType", "Windows Audio"\)/);
  assert.match(engine,/deviceManager_\.initialise \(1, 2, &safeRequirements, false\)/);
  assert.match(engine,/deviceManager_\.closeAudioDevice\(\)/);
  assert.match(engine,/expectedAsio->sameDriverClass\(selected\.registration\.identity\)/);
  const approval = engine.slice(engine.indexOf('bool approveDirectRoute ('),
                                engine.indexOf('bool resumeVerifiedDirectRoute()'));
  assert.ok(approval.indexOf('expectedAsio->sameDriverClass') >= 0);
  assert.ok(approval.indexOf('expectedAsio->sameDriverClass') <
            approval.indexOf('directFence_.approve'));
  assert.match(engine,/asioPinnedRegistration_\.sameDriverClass/);
  assert.match(engine,/asioRegisteredClassHash/);
  assert.match(host,/WINDOWS ASIO DRIVER REGISTRATION/);
  assert.match(cmake,/add_test\(NAME AsioDriverClassRegistryAttestation COMMAND ARMixAsioRegistryIdentityTests\)/);
});
test('P0 render callback cadence uses previous quantum and labels provenance rather than inventing XRUNs',()=>{
  const cadence=read('engine/core/CallbackCadenceEvidence.h');
  const cadenceTest=read('tests/callback_cadence_evidence_test.cpp');
  assert.match(cadence,/previousFrames_/);
  assert.match(cadence,/1000000\.0 \* previousFrames \/ rateHz_/);
  assert.match(cadence,/NOT_RENDER_XRUN/);
  assert.match(cadenceTest,/100000/);
  assert.match(cadenceTest,/C::Kind::clockRegression/);
  assert.match(engine,/cadence_\.prepare \(sampleRateHz_\)/);
  assert.match(engine,/cadence_\.observe \(beginUs, samples\)/);
  assert.match(engine,/callbackCadenceProvenance/);
  assert.match(engine,/callbackCadenceComparedIntervals/);
  assert.match(engine,/callbackCadenceClockRegressions/);
  assert.match(host,/START-to-START estimates use PREVIOUS callback frame count/);
  assert.match(cmake,/add_test\(NAME P0RenderCallbackVariableQuantumCadence COMMAND ARMixCadenceTests\)/);
  assert.doesNotMatch(engine,/previousCallbackStartUs_/);
});
test('telemetry distinguishes measured host budget fractions from unobservable output underrun',()=>{
  assert.match(engine,/realtimeHistogram_\.observe \(elapsedUs, periodUs\)/);
  assert.match(engine,/callbackP95BudgetUpperPct/);
  assert.match(engine,/callbackP99BudgetUpperPct/);
  assert.match(engine,/UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE/);
  assert.match(health,/std::array<std::atomic<std::uint64_t>,13>/);
});
test('P0 deterministic virtual clock soak is enforced in Windows CTest',()=>{
  const soak=read('tests/adaptive_clock_virtual_soak_test.cpp');
  assert.match(soak,/ASRC_SOAK/);
  assert.match(soak,/simulatedMilliseconds = 8000/);
  assert.match(soak,/96k_to_48k_minus300/);
  assert.match(soak,/48k_to_96k_plus300/);
  assert.match(soak,/std::abs\(crossMean\)/);
  assert.match(soak,/steadyUnderflow/);
  assert.match(soak,/p99Budget/);
  assert.doesNotMatch(soak,/std::this_thread::sleep_for\s*\(|std::random_device\s*[({]/);
  assert.match(cmake,/add_test\(NAME IndependentClockASRCDriftJitterSoak COMMAND ARMixVirtualClockSoakTests\)/);
});
test('clock bridge has no callback alloc / blocking and is gated behind independent stream adapter',()=>{
  assert.match(bridge,/class AdaptiveClockBridge final/);
  assert.match(bridge,/std::atomic<std::uint64_t> write_/);
  assert.match(bridge,/std::atomic<std::uint64_t> read_/);
  assert.match(bridge,/Blackman window/);
  assert.match(bridge,/anti-windup/);
  assert.match(bridge,/void pull \(float\* const\* render, int outputChannels, int frames\) noexcept/);
  assert.match(bridge,/int push \(const float\* const\* capture, int sourceChannels, int frames\) noexcept/);
  assert.match(bridge,/Not yet wired to JUCE joined WASAPI callback/);
  assert.doesNotMatch(engine,/AdaptiveClockBridge\s+clockBridge_/);
  assert.match(cmake,/AdaptiveDualClockBridge/);
  assert.match(cmake,/RealtimeHealthHistogram/);
  for (const target of ['ARMixClockBridgeTests','ARMixVirtualClockSoakTests',
                        'ARMixHealthTests','ARMixPolicyTests'])
    assert.match(cmake, new RegExp('add_test\\(NAME \\w+ COMMAND '+target+'\\)'));
  assert.match(policy,/class AudioProfilePolicy final/);
  assert.match(policy,/clockSharedByHardware/);
  assert.match(policy,/unsupportedClockAdapter/);
});

test('P0 capture block-head evidence is a single-pass RT heuristic, not invented renderer xrun',()=>{
 const probe=read('engine/core/CaptureBoundaryEvidence.h');
 const fixture=read('tests/capture_boundary_evidence_test.cpp');
 assert.match(probe,/class CaptureBoundaryEvidence final/);
 assert.match(probe,/std::atomic<std::uint64_t> candidateBlocks_/);
 assert.match(probe,/EXACT_ZERO_CAPTURE_BLOCK_HEAD_AFTER_NONZERO_TAIL_HEURISTIC_NOT_DRIVER_XRUN/);
 assert.match(engine,/captureBoundary_\.observe\(/);
 assert.match(engine,/captureBoundary_\.reset\(\)/);
 assert.match(engine,/captureZeroHeadCandidateBlocks/);
 assert.match(engine,/captureZeroHeadCandidateFrames/);
 assert.match(engine,/captureZeroHeadProvenance/);
 assert.match(engine,/captureNonFiniteSamples/);
 assert.doesNotMatch(engine,/rawCapturePeak=juce::jmax\(/);
 assert.match(fixture,/100000/);
 assert.match(cmake,/add_test\(NAME P0CaptureZeroHeadBoundaryCandidates COMMAND ARMixCaptureBoundaryTests\)/);
});


// INT-1B: a real 1Hz log reader with deterministic native-source fixtures.
function int1bSample(second,{dryTest=true,deviceGeneration=2,device="ASIO Driver",
  rate=48000,buffer=256,over=0,late=0,connected=true}={}) {
  const cumulative=(second+1)*100;
  const buckets=Array(13).fill(0);
  buckets[3]=cumulative-over;
  buckets[12]=over;
  return {schema:SCHEMA,connected,liveBackendClass:"ASIO_DIRECT_ZERO_EXTRA_FIFO",
    driver:"ASIO",device,rate,buffer,periodUs:1000000*buffer/rate,
    deviceGeneration,deviceStartCount:deviceGeneration,deviceStopCount:0,
    diagnosticRoute:"NORMAL",productionRoutingState:"NATIVE_ROUTE_ACCEPTED",
    directRouteAuthorized:true,dryTest,callbackCount:cumulative,
    callbackBudgetBuckets:buckets,overBudgetCount:over,slowCallbackCount:over,
    lateCallbackIntervals:late,callbackGapCount:0,
    callbackCadenceClockRegressions:0,backendXrunMeaning:"UNAVAILABLE",
    renderUnderrunSource:"UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE"};
}
function int1bLine(second,properties){
  return new Date(Date.UTC(2026,10,20,8,0,second)).toISOString()+
    ' | AUDIO_HEALTH | '+JSON.stringify(int1bSample(second,properties));
}
test('INT-1B native log is bounded and uses one existing off-RT 1Hz snapshot',()=>{
  assert.match(engine,/callbackBudgetBuckets/);
  assert.match(engine,/HOST_CALLBACK_BUDGET_CUMULATIVE_13_BUCKETS_NOT_DRIVER_RENDER_XRUN/);
  assert.match(host,/arsonmix\.audio-health\.int1b\.v1/);
  assert.match(host,/makeAudioHealthRecord \(const juce::var& state\)/);
  assert.match(host,/juce::JSON::toString \(makeAudioHealthRecord \(health\), false\)/);
  assert.match(host,/juce::JSON::toString \(makeAudioHealthRecord \(state\), false\)/);
  assert.doesNotMatch(engine,/realtimeHistogram_\.reset\(/);
  assert.ok(int1bLine(0,{dryTest:false}).length<4096,
    'single schema record fits existing rotating log queue');
});
test('INT-1B interval histogram is a 13-bin delta, not cumulative p99',()=>{
  const pct=p95p99FromDelta([0,0,0,950,0,0,0,0,0,0,0,0,50]);
  assert.equal(pct.p95,8);
  assert.equal(pct.p99,null); // over deadline, never fabricate 100% p99
  assert.equal(pct.samples,1000);
  assert.equal(p95p99FromDelta(Array(13).fill(0)).p95,null);
  assert.equal(p95p99FromDelta([1]).samples,0);
});
test('INT-1B dry vs FX matched 60s evidence and no fabricated physical XRUN',()=>{
  const lines=[];
  for(let s=0;s<=60;s++)lines.push(int1bLine(s,{dryTest:true}));
  // Separate 1Hz capture, same physical driver/port/rate/buffer but FX chain.
  for(let s=61;s<=122;s++)lines.push(int1bLine(s,{
    dryTest:false,over:(s-61)*4,late:s-61
  }));
  const report=analyzeHealthLog(lines.join('\n'));
  assert.equal(report.totalSamples,123);
  assert.equal(report.segments.length,2);
  assert.equal(report.segments[0].durationSeconds,60);
  assert.equal(report.segments[0].p99BudgetUpperPct,8);
  assert.equal(report.segments[0].overBudgetCallbacks,0);
  assert.equal(report.segments[1].p99BudgetUpperPct,null);
  assert.equal(report.segments[1].overBudgetPer1000,40);
  assert.equal(report.segments[1].diagnosis,
    'BOTH_DSP_BUDGET_AND_SCHEDULING_INDICATORS');
  assert.equal(report.segments[1].renderHardwareXrun,
    'UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE');
  const compared=compareDryFx(report);
  assert.equal(compared.comparable,true);
  assert.equal(compared.deltaOverBudgetPer1000,40);
  assert.match(markdownReport(report),/not physical render-XRUN/i);
});
test('INT-1B refuses device mismatches, resets, gaps, malformed and legacy logs',()=>{
  const legacy='2026-11-20T08:00:00.000Z | AUDIO_HEALTH | {"callbackCount":99}';
  const malformed='2026-11-20T08:00:00.000Z | AUDIO_HEALTH | {"bad":';
  const badLog=[legacy,malformed,int1bLine(0,{}),int1bLine(1,{}),
    int1bLine(7,{deviceGeneration:3}),int1bLine(8,{deviceGeneration:3}),
    int1bLine(9,{deviceGeneration:2})].join('\n');
  const parsed=parseHealthLog(badLog);
  assert.equal(parsed.samples.length,5);
  assert.equal(parsed.warnings.length,2);
  const analysis=analyzeHealthLog(badLog);
  assert.equal(analysis.segments.length,3);
  assert.equal(compareDryFx(analysis).comparable,false);
  assert.ok(analysis.warnings.some(w=>w.includes('scope/device epoch')));
  // An interval with a counter going backwards is NOT negative optimization.
  const previous=int1bSample(20,{});
  const invalid={...int1bSample(21,{}),callbackCount:1};
  const time=s=>new Date(Date.UTC(2026,10,20,8,0,s)).toISOString();
  const report=analyzeHealthLog(
    time(20)+' | AUDIO_HEALTH | '+JSON.stringify(previous)+'\n'+
    time(21)+' | AUDIO_HEALTH | '+JSON.stringify(invalid));
  assert.equal(report.segments.length,2);
});
test('INT-1B rejects missed/unknown data rather than counting missing as zero',()=>{
  const s=int1bSample(1,{});
  delete s.callbackBudgetBuckets;
  const p=parseHealthLog(
    new Date(Date.UTC(2026,10,20,8,0,1)).toISOString()+
    ' | AUDIO_HEALTH | '+JSON.stringify(s));
  assert.equal(p.samples.length,0);
  assert.match(p.warnings[0],/missing\/invalid 13/);
});
