// SPDX-License-Identifier: AGPL-3.0-only
// Permanent source contracts. CPU/FIFO functional correctness is independently
// tested by Release-active native CTest, not source regexes alone.
import test from 'node:test';
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
const flow=read('.github/workflows/p1-windows.yml');

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
  assert.match(cmake,/NativeDriverPeriodEvidence/);
  assert.match(flow,/ARMixPeriodEvidenceTests/);
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
  assert.match(cmake,/AsioDriverClassRegistryAttestation/);
  assert.match(flow,/ARMixAsioRegistryIdentityTests/);
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
  assert.match(cmake,/IndependentClockASRCDriftJitterSoak/);
  assert.match(flow,/ARMixVirtualClockSoakTests/);
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
    assert.ok(flow.includes(target), 'Windows CI is missing ' + target);
  assert.match(policy,/class AudioProfilePolicy final/);
  assert.match(policy,/clockSharedByHardware/);
  assert.match(policy,/unsupportedClockAdapter/);
});
