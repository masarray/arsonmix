// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const r=p=>readFileSync(p,'utf8');
const device=r('engine/device/JuceIndependentClockDevice.h');
const engine=r('src/LiveAudioEngine.h');
const host=r('src/Main.cpp');
const sizing=r('engine/device/ClockBridgeSizing.h');
const watchdog=r('engine/device/IndependentStreamWatchdog.h');
const ui=r('web/monitor.js');

test('independent source/render means two actual JUCE managers and distinct callback owners',()=>{
  assert.match(device,/AudioDeviceManager captureManager_, renderManager_/);
  assert.match(device,/struct Capture final : juce::AudioIODeviceCallback/);
  assert.match(device,/struct Render final : juce::AudioIODeviceCallback/);
  assert.match(device,/captureManager_\.addAudioCallback \(&captureCallback_\)/);
  assert.match(device,/renderManager_\.addAudioCallback \(&renderCallback_\)/);
  assert.match(device,/captureManager_\.removeAudioCallback \(&captureCallback_\)/);
  assert.match(device,/renderManager_\.removeAudioCallback \(&renderCallback_\)/);
  assert.match(device,/bridge_\.push \(inputs, inputChannels, samples\)/);
  assert.match(device,/bridge_\.pull \(input, 2, frames\)/);
  assert.match(device,/downstream_->audioDeviceIOCallbackWithContext/);
  assert.match(device,/capture.*getCurrentSampleRate|captureRate_ = capture->getCurrentSampleRate/);
  assert.match(device,/renderRate_ = render->getCurrentSampleRate/);
  assert.match(device,/setup\.inputChannels = capture \? saved\.inputChannels/);
  assert.match(device,/setup\.outputChannels = capture \? juce::BigInteger\(\) : saved\.outputChannels/);
  assert.match(device,/Selected microphone channel mask was not preserved/);
  assert.match(device,/Selected hardware output channel mask was not preserved/);
  assert.match(device,/ClockBridgeSizing::choose/);
  assert.match(sizing,/renderSourceFrames/);
  assert.match(sizing,/d\.minFrames = d\.renderSourceFrames/);
});
test('P0 immutable independent stream format fence blocks silent device/period/map substitutions',()=>{
  const fence=r('engine/device/IndependentStreamFormatFence.h');
  const cmake=r('CMakeLists.txt');
  const workflow=r('.github/workflows/p1-windows.yml');
  assert.match(fence,/class IndependentStreamFormatFence final/);
  assert.match(fence,/bool observeCallbackChannels/);
  assert.match(fence,/lifecyclePeriod/);
  assert.match(fence,/lifecycleChannelMap/);
  assert.match(device,/formatFence_\.prepare \(formatOf \(capture, true\), formatOf \(render, false\)\)/);
  assert.match(device,/formatFence_\.observeStart/);
  assert.match(device,/formatFence_\.observeCallbackChannels/);
  assert.match(device,/formatFence_\.resetStopped\(\)/);
  assert.match(engine,/bridgeNegotiationFault/);
  assert.match(host,/getProperty \("bridgeNegotiationFault", "NONE"\)/);
  assert.match(cmake,/IndependentStreamNegotiatedFormatFence/);
  assert.match(workflow,/ARMixIndependentFormatTests/);
});

test('live deployment preserves direct ASIO and bounded rollback semantics',()=>{
  assert.match(engine,/setIndependentClockEnabled \(bool wanted\)/);
  assert.match(engine,/getCurrentAudioDeviceType\(\) != "Windows Audio"/);
  assert.match(engine,/savedDirectSetup_ = original/);
  assert.match(engine,/independent_\.open/);
  assert.match(engine,/const auto restore = deviceManager_\.setAudioDeviceSetup \(original, false\)/);
  assert.match(engine,/independent_\.close\(\)/);
  assert.match(engine,/bridgeOverflowFrames/);
  assert.match(engine,/bridgeUnderflowFrames/);
  assert.match(engine,/bridgeCorrectionPpm/);
  assert.match(engine,/WASAPI_CAPTURE_DISCONTINUITIES_ONLY/);
  assert.match(host,/engine_\.independentClockFaulted\(\)/);
  assert.match(host,/serviceDirectRestore\(\)/);
  assert.match(host,/SMART_CLOCK_RECOVERY/);
  assert.match(host,/!engine_\.independentClockActive\(\)/);
  assert.match(host,/editing VST3 graph/);
  assert.match(host,/emitFxAddResult \(requestId, name, error, 0\)/);
});
test('approved mixer visuals unchanged; only native signal-check opt-in',()=>{
  assert.match(ui,/arSmartClockToggle/);
  assert.match(ui,/SMART CLOCK: INDEPENDENT \(BETA\)/);
  assert.match(ui,/action:"smartClock"/);
  assert.match(ui,/bridgeOverflowFrames/);
  assert.match(ui,/bridgeUnderflowFrames/);
});

test('silent JUCE callback watchdog routes through control timer and DIRECT failback',()=>{
  assert.match(watchdog,/silenceThresholdMs = 3000/);
  assert.match(watchdog,/captureCount != captureCount_/);
  assert.match(watchdog,/renderCount != renderCount_/);
  assert.match(device,/streamWatchdog_\.observe/);
  assert.match(engine,/pollIndependentClockHealth/);
  assert.match(host,/engine_\.pollIndependentClockHealth \(now\)/);
  assert.match(engine,/bridgeWatchdogFault/);
  assert.match(engine,/bridgeCaptureSilentMs/);
  assert.match(engine,/bridgeRenderSilentMs/);
});

test('watchdog fault cause precedes bounded diagnostics JSON on both recovery outcomes',()=>{
  assert.match(host,/const auto watchdogSummary =/);
  assert.match(host,/getProperty \("bridgeWatchdogFault", "NONE"\)/);
  assert.match(host,/getProperty \("bridgeCaptureSilentMs", 0\)/);
  assert.match(host,/getProperty \("bridgeRenderSilentMs", 0\)/);
  assert.equal((host.match(/watchdogSummary \+ " \| " \+ faultStatus/g)||[]).length,2);
});

test('sustained live FIFO loss feeds bounded DIRECT recovery without audio-callback policy work',()=>{
  const gate=r('engine/device/BridgeContinuityGate.h');
  assert.match(gate,/class BridgeContinuityGate final/);
  assert.match(gate,/requiredBadWindows = 3/);
  assert.match(gate,/warmupMs = 1500/);
  assert.match(device,/continuityGate_\.observe \(nowMs/);
  assert.match(device,/pcm\.underflowFrames/);
  assert.match(device,/pcm\.primingFrames/);
  assert.match(device,/pcm\.overflowFrames/);
  assert.match(engine,/bridgeContinuityFault/);
  assert.match(engine,/bridgeContinuityLossBp/);
  assert.match(host,/getProperty \("bridgeContinuityFault", "NONE"\)/);
  assert.match(host,/getProperty \("bridgeContinuityLossBp", 0\)/);
  assert.match(host,/engine_\.pollIndependentClockHealth \(now\)/);
});

test('dual-WASAPI opening is guarded by unique native endpoint IDs without GUI or RT work',()=>{
  const native=r('engine/device/WasapiEndpointIdentityGuard.h');
  const pure=r('engine/device/EndpointIdentityPolicy.h');
  assert.match(pure,/captureAmbiguous, renderAmbiguous/);
  assert.match(native,/EnumAudioEndpoints \(flow, DEVICE_STATE_ACTIVE/);
  assert.match(native,/EndpointIdentityPolicy::sameIdentity/);
  assert.match(engine,/WasapiEndpointIdentityGuard::resolve/);
  assert.match(device,/WasapiEndpointIdentityGuard::verify/);
  assert.match(device,/captureSetup\.inputDeviceName != selected\.inputDeviceName/);
  assert.match(device,/renderSetup\.outputDeviceName != selected\.outputDeviceName/);
  assert.match(engine,/bridgeCaptureEndpointIdHash/);
  assert.match(engine,/bridgeRenderEndpointIdHash/);
});

test('ASIO is the preferred first-run live backend only for one same-driver duplex route',()=>{
  const policy=r('engine/device/AsioStartupPolicy.h');
  const manager=r('engine/device/AsioPriorityDeviceManager.h');
  assert.match(policy,/captures\.size\(\) == 1 && renders\.size\(\) == 1/);
  assert.match(policy,/captures\.front\(\) == renders\.front\(\)/);
  assert.match(manager,/createAudioDeviceTypes/);
  assert.match(manager,/driver->scanForDevices\(\)/);
  assert.match(manager,/types\.move \(i, 0\)/);
  assert.match(engine,/savedSettings, !savedRoute/);
  assert.match(engine,/AUTO_ASIO_FAILED_WASAPI_DIRECT/);
  assert.match(engine,/Saved audio route identity changed/);
  assert.match(engine,/ASIO_DIRECT_ZERO_EXTRA_FIFO/);
  assert.match(engine,/driverLatencyProvenance/);
  assert.match(engine,/DRIVER_HINT_NOT_MEASURED_ROUND_TRIP/);
});

test('saved ASIO startup failure keeps the host callback registered for manual AUDIO SETUP recovery',()=>{
  const registration=engine.indexOf('deviceManager_.addAudioCallback (this);');
  const initialise=engine.indexOf('auto error = deviceManager_.initialise (1, 2, savedSettings, !savedRoute);');
  assert.ok(registration>=0 && registration<initialise);
  assert.match(engine,/attached_ = true;\s*const bool savedRoute/);
  assert.match(engine,/if \(error\.isNotEmpty\(\)\) return error;/);
  assert.match(host,/new juce::AudioDeviceSelectorComponent/);
  assert.match(host,/showDeviceSetup\(\)/);
});

test('P0 ASIO live direct route fence blocks hotplug substitution before DSP',()=>{
  const fence=r('engine/device/DirectRouteFence.h');
  assert.match(fence,/class DirectRouteFence final/);
  assert.match(fence,/void suspend\(\) noexcept/);
  assert.match(fence,/bool observeStart/);
  assert.match(fence,/bool resumeExpected/);
  assert.match(engine,/directFence_\.observeStart/);
  assert.match(engine,/directFence_\.permitted\(\)/);
  assert.match(engine,/directSuppressedFrames_\.fetch_add/);
  assert.match(engine,/directRouteProtection/);
  assert.match(host,/engine_\.beginManualAudioSetup\(\)/);
  assert.match(host,/engine_\.finishManualAudioSetup\(\)/);
  assert.match(host,/engine_\.directRouteAuthorized\(\)/);
});

test('unexpected ASIO/WASAPI switch notifies user once from control thread',()=>{
  assert.match(engine,/unexpectedDirectRoute\(\) const noexcept/);
  assert.match(host,/DIRECT_ROUTE_BLOCKED/);
  assert.match(host,/unexpectedRouteNoticeSent_/);
  assert.match(host,/confirm your ASIO\/WASAPI device in AUDIO SETUP/);
});

test('P1: dry-through live microphone is NEVER exempted from DIRECT route fencing',()=>{
  const callback=engine.slice(engine.indexOf('void audioDeviceIOCallbackWithContext ('));
  const begin=callback.indexOf('const auto activeDiagnosticRoute =');
  const end=callback.indexOf('// R0B B3: production NORMAL audio');
  assert.ok(begin>=0 && end>begin);
  // Bound this test to the DIRECT fence. The separate B3 gate below only
  // controls unpatched NORMAL audio; it must not alter diagnostic exemptions.
  const gate=callback.slice(begin,end);
  assert.match(gate,/activeDiagnosticRoute != armix::diagnostics::Route::outputOnlyTone/);
  assert.match(gate,/activeDiagnosticRoute != armix::diagnostics::Route::captureOnly/);
  assert.doesNotMatch(gate,/activeDiagnosticRoute == armix::diagnostics::Route::normal/);
  assert.match(gate,/directFence_\.permitted\(\)/);
  assert.match(gate,/FloatVectorOperations::clear/);
  assert.match(host,/diagnosticRoute != armix::diagnostics::Route::outputOnlyTone/);
  assert.match(host,/diagnosticRoute != armix::diagnostics::Route::captureOnly/);
});
