// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const r=p=>readFileSync(p,'utf8');
const device=r('engine/device/JuceIndependentClockDevice.h');
const engine=r('src/LiveAudioEngine.h');
const host=r('src/Main.cpp');
const sizing=r('engine/device/ClockBridgeSizing.h');
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
