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
  rate=48000,buffer=256,over=0,late=0,connected=true,
  nativeGlobalFxBypassed=dryTest,
  nativeFxProcessingCount=nativeGlobalFxBypassed?0:1,
  nativeExternalCh1Leased=false,
  nativeExternalCh1InstalledCount=0,
  nativeExternalCh1ProcessingCount=0}={}) {
  const cumulative=(second+1)*100;
  const buckets=Array(13).fill(0);
  buckets[3]=cumulative-over;
  buckets[12]=over;
  return {schema:SCHEMA,connected,liveBackendClass:"ASIO_DIRECT_ZERO_EXTRA_FIFO",
    driver:"ASIO",device,inputDevice:"Interface Input",outputDevice:"Interface Output",
    deviceInputChannels:2,deviceOutputChannels:2,
    activeInputMaskHex:"3",activeOutputMaskHex:"3",
    nativeFxGraphRevision:"100",
    rate,buffer,periodUs:1000000*buffer/rate,
    deviceGeneration,deviceStartCount:deviceGeneration,deviceStopCount:0,
    diagnosticRoute:"NORMAL",productionRoutingState:"NATIVE_ROUTE_ACCEPTED",
    directRouteAuthorized:true,dryTest,
    nativeGlobalFxBypassed,nativeFxInstalledCount:1,nativeFxProcessingCount,
    nativeFxActivityComplete:true,
    nativeExternalCh1Leased,nativeExternalCh1InstalledCount,
    nativeExternalCh1ProcessingCount,
    callbackCount:cumulative,
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
  assert.match(engine,/nativeFxProcessingCount/);
  assert.match(engine,/dryRoutingLab_\.globalFxBypassed\(\)/);
  assert.match(host,/nativeFxInstalledCount/);
  assert.match(engine,/originalCh1GraphLeased/);
  assert.match(engine,/acceptedGraph->rackBypassed\.load/);
  assert.match(engine,/acceptedGraph->bypassFlags\[physical\]\.load/);
  assert.match(host,/nativeExternalCh1InstalledCount/);
  assert.match(engine,/acceptedOriginalCh1ExternalLeaseForControl\(\)/);
  assert.match(engine,/acceptedExternal->render == &processOwnedChannelOne/);
  assert.match(engine,/nativeFxActivityComplete/);
  assert.match(host,/nativeFxActivityComplete/);
  assert.match(engine,/nativeFxGraphRevision/);
  assert.match(engine,/activeInputMaskHex/);
  assert.match(host,/makeAudioHealthRecord \(const juce::var& state,/);
  assert.match(host,/juce::JSON::toString \(makeAudioHealthRecord \(health,evidence\.memory\), true\)/);
  assert.match(host,/juce::JSON::toString \(makeAudioHealthRecord \(state,evidence\.memory\), true\)/);
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
  assert.equal(report.segments[0].nativeGlobalFxBypassed,true);
  assert.equal(report.segments[0].nativeFxProcessingCount,0);
  assert.equal(report.segments[1].nativeFxProcessingCount,1);
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
  assert.ok(analysis.warnings.some(w=>w.includes('device/graph/epoch')));
  // An interval with a counter going backwards is NOT negative optimization.
  const previous=int1bSample(20,{});
  const invalid={...int1bSample(21,{}),callbackCount:1};
  const time=s=>new Date(Date.UTC(2026,10,20,8,0,s)).toISOString();
  const report=analyzeHealthLog(
    time(20)+' | AUDIO_HEALTH | '+JSON.stringify(previous)+'\n'+
    time(21)+' | AUDIO_HEALTH | '+JSON.stringify(invalid));
  assert.equal(report.segments.length,2);
  const swapped={...int1bSample(21,{}),inputDevice:"Other Driver Input"};
  const mismatch=analyzeHealthLog(
    time(20)+' | AUDIO_HEALTH | '+JSON.stringify(previous)+'\n'+
    time(21)+' | AUDIO_HEALTH | '+JSON.stringify(swapped));
  assert.equal(mismatch.segments.length,2,
    'Physical input endpoint change disqualifies the previous timing window');
  const changedPins=analyzeHealthLog(
    time(20)+' | AUDIO_HEALTH | '+JSON.stringify(previous)+'\n'+
    time(21)+' | AUDIO_HEALTH | '+
      JSON.stringify({...int1bSample(21,{}),activeInputMaskHex:"c"}));
  assert.equal(changedPins.segments.length,2,
    'The same ASIO channel count can hide changed hardware pin ordinals');
  const missing=analyzeHealthLog(
    time(20)+' | AUDIO_HEALTH | '+JSON.stringify(previous)+'\n'+
    time(20)+' | AUDIO_HEALTH | null'+'\n'+
    time(21)+' | AUDIO_HEALTH | '+JSON.stringify(int1bSample(21,{})));
  assert.equal(missing.segments.length,2,
    'A malformed/unknown middle snapshot is a hard gap, not a green interval');
  const changedGeneration=[];
  for(let s=0;s<=40;s++)changedGeneration.push(int1bLine(s,{dryTest:true}));
  for(let s=41;s<=82;s++)changedGeneration.push(int1bLine(s,{
    dryTest:false,deviceGeneration:3
  }));
  assert.equal(compareDryFx(analyzeHealthLog(changedGeneration.join('\n'))).comparable,false,
    'Same-named driver reopened in a new generation is not a matched DRY/FX pair');
});
test('INT-1B ignores legacy dryTest visual intent when native FX still processes',()=>{
  const lines=[];
  for(let s=0;s<=40;s++)lines.push(int1bLine(s,{
    dryTest:true,nativeGlobalFxBypassed:false,nativeFxProcessingCount:1
  }));
  const result=analyzeHealthLog(lines.join('\n'));
  assert.equal(result.segments.length,1);
  assert.equal(result.segments[0].mode,'FX',
    'The only DRY proof is actual native DSP bypass/insert readback');
  assert.equal(compareDryFx(result).comparable,false);
});

test('INT-1B never pairs another app session or a stale DRY baseline',()=>{
  const makeWindow=(start,end,dryTest)=>Array.from(
    {length:end-start+1},(_,i)=>int1bLine(start+i,{dryTest}));
  const sessionHeader=new Date(Date.UTC(2026,10,20,8,0,40)).toISOString()+
    ' | SESSION | started';
  const differentApp=analyzeHealthLog([
    ...makeWindow(0,40,true),sessionHeader,...makeWindow(41,81,false)
  ].join('\n'));
  assert.equal(differentApp.segments.length,2);
  assert.equal(compareDryFx(differentApp).comparable,false,
    'the same generation on a new process is not comparable');
  const stale=analyzeHealthLog([
    ...makeWindow(0,40,true),...makeWindow(200,240,false)
  ].join('\n'));
  assert.equal(compareDryFx(stale).comparable,false,
    'a session gap must not silently pair different performances');
  const intervening=analyzeHealthLog([
    ...makeWindow(0,40,true),...makeWindow(41,81,false),
    ...makeWindow(82,122,true)
  ].join('\n'));
  assert.equal(compareDryFx(intervening).comparable,true,
    'a complete adjacent dry then fx pair remains valid even with a later DRY run');
});

test('INT-1B external-only CH1 vendor rack is FX when powered and DRY when bypassed',()=>{
  const lines=[];
  for(let s=0;s<=40;s++)lines.push(int1bLine(s,{
    dryTest:false,nativeGlobalFxBypassed:true,nativeFxProcessingCount:0,
    nativeExternalCh1Leased:true,nativeExternalCh1InstalledCount:1,
    nativeExternalCh1ProcessingCount:0
  }));
  for(let s=41;s<=81;s++)lines.push(int1bLine(s,{
    dryTest:false,nativeGlobalFxBypassed:false,nativeFxProcessingCount:1,
    nativeExternalCh1Leased:true,nativeExternalCh1InstalledCount:1,
    nativeExternalCh1ProcessingCount:1
  }));
  const report=analyzeHealthLog(lines.join('\n'));
  assert.deepEqual(report.segments.map(s=>s.mode),['DRY','FX']);
  assert.equal(report.segments[0].nativeExternalCh1InstalledCount,1);
  assert.equal(report.segments[1].nativeExternalCh1ProcessingCount,1);
  assert.equal(compareDryFx(report).comparable,true,
    'External-only original CH1 Graph is not silently omitted from native FX');
  const malformed=lines.slice();
  malformed[50]=malformed[50].replace('"nativeExternalCh1ProcessingCount":1',
      '"nativeExternalCh1ProcessingCount":2');
  assert.ok(analyzeHealthLog(malformed.join('\n')).warnings.some(s=>s.includes('invalid source metadata')),
    'External count cannot exceed the accepted authoritative total');
});

test('INT-1B P2: no DRY/FX match through malformed or opaque-lease telemetry',()=>{
  const dry=[],fx=[];
  for(let s=0;s<=40;s++)dry.push(int1bLine(s,{dryTest:true}));
  for(let s=41;s<=82;s++)fx.push(int1bLine(s,{dryTest:false}));
  const baseline=analyzeHealthLog([...dry,...fx].join('\n'));
  assert.equal(compareDryFx(baseline).comparable,true,
    'An accepted global bypass ON -> OFF transition is allowed');
  assert.equal(baseline.segments[1].invalidBefore,false);

  const midpoint='2026-11-20T08:00:40.500Z | AUDIO_HEALTH | {"invalid":';
  const malformed=analyzeHealthLog([...dry,midpoint,...fx].join('\n'));
  assert.equal(malformed.segments.length,2);
  assert.equal(malformed.segments[1].invalidBefore,true);
  assert.equal(compareDryFx(malformed).comparable,false,
    'Malformed source between two valid 40s windows must invalidate pairing');

  const opaque={...int1bSample(41,{dryTest:false}),nativeFxActivityComplete:false};
  const rejected='2026-11-20T08:00:41.000Z | AUDIO_HEALTH | '+JSON.stringify(opaque);
  const incomplete=analyzeHealthLog([...dry,rejected,...fx.slice(1)].join('\n'));
  assert.equal(incomplete.segments.length,2);
  assert.equal(incomplete.segments[1].invalidBefore,true);
  assert.equal(compareDryFx(incomplete).comparable,false,
    'Rejected opaque external CH1 FX lease is a hard comparability barrier');

  const regressed={...int1bSample(41,{dryTest:false}),callbackCount:1};
  const counterReset=analyzeHealthLog([...dry,
    '2026-11-20T08:00:41.000Z | AUDIO_HEALTH | '+JSON.stringify(regressed),
    ...fx.slice(1)].join('\n'));
  assert.equal(compareDryFx(counterReset).comparable,false,
    'Counter regression during mode transition must not be a valid pair');

  const unprovenDry=[],unprovenFx=[];
  for(let s=0;s<=40;s++)unprovenDry.push(int1bLine(s,{
    dryTest:true,nativeGlobalFxBypassed:false,nativeFxProcessingCount:0
  }));
  for(let s=41;s<=82;s++)unprovenFx.push(int1bLine(s,{dryTest:false}));
  assert.equal(compareDryFx(analyzeHealthLog(
    [...unprovenDry,...unprovenFx].join('\n'))).comparable,false,
    'Global power OFF -> OFF is not a verified global FX bypass transition');
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

test("P11 render timing covers final output publication and flags incomplete sampling",()=>{
  // A split ASIO singer monitor is a physical render, but STREAM alone
  // is not evidence of armed physical hardware output. Verify both sinks.
  assert.match(engine,/const bool wroteMain=outputGate\.hardware&&routed&&!splitMain&&nativeMain\[0\]&&nativeMain\[1\];/);
  assert.match(engine,/const bool wroteMonitor=splitMain&&outputGate\.hardware&&routed&&nativeMonitor\[0\]&&nativeMonitor\[1\];/);
  assert.match(engine,/if\(wroteMonitor\)\{[\s\S]*?publishDriverBufferPeaks\(l,r\);\s*\}else publishDriverBufferPeaks\(wroteMain\?audibleLeft:0\.f,wroteMain\?audibleRight:0\.f\);\s*const auto pressureRevision=nativePcm&&\(wroteMain\|\|wroteMonitor\)&&!cycle\.fadeIn&&!cycle\.fadeOut&&\s*routedFxMove==0&&!probePermit&&patchFade\.phase==armix::mixer::BusPatchEnvelope::idle\?\s*dryRoutingLab_\.renderedNativeRevision\(\):0;[\s\S]*?busPatchEnvelope_\.acknowledge\(patchFade\);\s*recordRenderTiming\(beginUs,periodUs,outputLease\.epoch,pressureRevision\);\s*return;/);
  // Diagnostics retain lifetime histogram coverage but cannot extend an
  // armed-native pressure segment (default zero epoch/revision).
  assert.match(engine,/void recordRenderTiming\(std::uint64_t beginUs,double periodUs,\s*std::uint64_t epoch=0,std::uint64_t revision=0\) noexcept/);
  assert.match(engine,/const auto endUs=clockMicros\(\);\s*if\(endUs<beginUs\)\{renderPressure_\.breakSequence\(\);cadence_\.breakSequence\(\);return;\}\s*const auto elapsedUs = endUs - beginUs;\s*renderPressure_\.finish\(epoch,revision,beginUs,elapsedUs,periodUs\)/);
  assert.equal((engine.match(/realtimeHistogram_\.observe \(elapsedUs, periodUs\)/g)||[]).length,1);
  assert.match(host,/"callbackTimingScope"/);
  const v2=(second,props={},partial=false)=>{
    const sample=int1bSample(second,props);
    sample.callbackTimingScope="RENDER_THROUGH_FINAL_OUTPUT_PUBLICATION_V2";
    if(partial)sample.callbackBudgetBuckets[3]=Math.floor(sample.callbackBudgetBuckets[3]/2);
    return new Date(Date.UTC(2026,10,20,8,0,second)).toISOString()+
      " | AUDIO_HEALTH | "+JSON.stringify(sample);
  };
  const full=analyzeHealthLog(Array.from({length:41},(_,s)=>v2(s)).join("\n"));
  assert.equal(full.segments[0].histogramCoveragePct,100);
  assert.equal(full.segments[0].comparable,true);
  const partial=analyzeHealthLog(Array.from({length:41},(_,s)=>v2(s,{},true)).join("\n"));
  assert.equal(partial.segments[0].histogramCoveragePct,50);
  assert.equal(partial.segments[0].comparable,false);
  assert.equal(partial.segments[0].diagnosis,"INSUFFICIENT_EVIDENCE");
  const mixed=analyzeHealthLog([int1bLine(0,{}),v2(1),v2(2)].join("\n"));
  assert.equal(mixed.segments.length,2);
  const broken=analyzeHealthLog([v2(0),"2026-11-20T08:00:00.500Z | AUDIO_HEALTH | {",v2(1),v2(2)].join("\n"));
  assert.equal(broken.segments.length,2);
  assert.equal(broken.segments[1].invalidBefore,true);
});

test("P11 resource summaries preserve unknowns, provenance and sampled peaks",()=>{
  const lines=Array.from({length:41},(_,s)=>{
    const sample=int1bSample(s,{});
    if(s>0){
      sample.footerProcessCpuPercent=s===20?18:4;
      sample.footerCpuProvenance="WINDOWS_PROCESS_CPU_ALL_LOGICAL_CORES_1HZ_NOT_AUDIO_CALLBACK_OR_SYSTEM_TOTAL";
      sample.processWorkingSetMiB=s===20?180:100+s;
      sample.processPrivateCommitMiB=200+s;
    }
    if(s===10)sample.footerCpuProvenance="UNKNOWN";
    return new Date(Date.UTC(2026,10,20,8,0,s)).toISOString()+" | AUDIO_HEALTH | "+JSON.stringify(sample);
  });
  const report=analyzeHealthLog(lines.join("\n"));
  const s=report.segments[0];
  assert.equal(s.processCpuAllCorePct.samples,39);
  assert.equal(s.processCpuAllCorePct.peak,18);
  assert.equal(s.workingSetMiB.peak,180);
  assert.equal(s.privateCommitMiB.delta,39);
  assert.equal(analyzeHealthLog(int1bLine(0,{})).segments[0].processCpuAllCorePct.mean,null);
  assert.match(markdownReport(report),/CPU mean \/ peak/);
});

test("P11 driver lifecycle or callback clock regression invalidates a measured window",()=>{
  for(const key of ["deviceStartCount","deviceStopCount","callbackCadenceClockRegressions"]){
    const log=Array.from({length:41},(_,s)=>{
      const sample=int1bSample(s,{});
      sample.callbackTimingScope="RENDER_THROUGH_FINAL_OUTPUT_PUBLICATION_V2";
      if(s>=20)sample[key]+=1;
      return new Date(Date.UTC(2026,10,20,8,0,s)).toISOString()+" | AUDIO_HEALTH | "+JSON.stringify(sample);
    }).join("\n");
    const result=analyzeHealthLog(log);
    assert.equal(result.segments.length,1);
    assert.equal(result.segments[0].stableLifecycle,false);
    assert.equal(result.segments[0].comparable,false);
  }
});
