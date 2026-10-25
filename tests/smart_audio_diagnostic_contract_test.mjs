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
test('telemetry distinguishes measured host budget fractions from unobservable output underrun',()=>{
  assert.match(engine,/realtimeHistogram_\.observe \(elapsedUs, periodUs\)/);
  assert.match(engine,/callbackP95BudgetUpperPct/);
  assert.match(engine,/callbackP99BudgetUpperPct/);
  assert.match(engine,/UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE/);
  assert.match(health,/std::array<std::atomic<std::uint64_t>,13>/);
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
  assert.match(flow,/ARMixClockBridgeTests ARMixHealthTests ARMixPolicyTests/);
  assert.match(policy,/class AudioProfilePolicy final/);
  assert.match(policy,/clockSharedByHardware/);
  assert.match(policy,/unsupportedClockAdapter/);
});
