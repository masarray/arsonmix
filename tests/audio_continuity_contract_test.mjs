// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(new URL('../'+p,import.meta.url),'utf8');
const main=read('src/Main.cpp');
const engine=read('src/LiveAudioEngine.h');
const policy=read('engine/device/AudioContinuityPolicy.h');
const coordinator=read('src/NativeDeviceApplyCoordinator.h');
const cmake=read('CMakeLists.txt');

test('display sleep never changes logical mixer intent or device configuration',()=>{
  assert.match(main,/WM_POWERBROADCAST/);
  assert.match(main,/PBT_APMSUSPEND/);
  assert.match(main,/PBT_APMRESUMEAUTOMATIC/);
  assert.match(main,/Screen\/monitor power events are NOT a system suspend/);
  assert.match(policy,/displayOff, displayOn, systemSuspend, systemResume/);
  assert.match(policy,/changeMixerIntent\(NativePowerEvent\)/);
  assert.match(engine,/mainOutputIntent_\.operatorRequested\(value\)/);
  assert.match(engine,/mainOutputIntent_\.deviceInterrupted\(\)/);
  const listener=main.split('void changeListenerCallback (juce::ChangeBroadcaster*) override')[1]
    .split('void timerCallback() override')[0];
  assert.doesNotMatch(listener,/deviceSettingsDirty_\s*=\s*true/);
});

test('reconnect is to the exact previously selected endpoint without silent fallback',()=>{
  assert.match(main,/rememberSavedWasapiProfile\(saved.get\(\)\)/);
  assert.match(main,/verifiedWasapiSetup_/);
  assert.match(main,/serviceWasapiDeviceContinuity\(\)/);
  assert.match(main,/finishAudioSetupAndRecoverMixer\(\)/);
  assert.match(main,/!device \|\| !device->isOpen\(\) \|\| directCallbackStalled_/);
  assert.match(main,/actual\.outputDeviceName!=wanted\.outputDeviceName/);
  assert.match(main,/actual\.inputDeviceName!=wanted\.inputDeviceName/);
  assert.match(coordinator,/wasapiRecovery/);
  assert.match(coordinator,/setup\.inputDeviceName\.isEmpty\(\)\?0:2/);
  assert.match(main,/if\(powerNoticeHwnd_\)/);
  assert.doesNotMatch(main,/new juce::AudioDeviceManager/);
});

test('master mute is only automatic for a positively identified feedback self loop',()=>{
  const protector=main.split('void protectKnownFeedbackLoop(const juce::String& fault)')[1]
    .split('void changeListenerCallback')[0];
  assert.match(protector,/VB-Cable capture\/playback self-loop/);
  assert.match(protector,/engine_\.setMasterMuted\(true\)/);
  assert.doesNotMatch(protector,/engine_\.setOutputMuted\(true\)/);
  const stopped=engine.split('void audioDeviceStopped() override')[1]
    .split('static void renderOriginalChannelOneGraph')[0];
  assert.doesNotMatch(stopped,/setMasterMuted/);
  assert.match(engine,/mainOutputIntent_\.deviceInterrupted\(\)/);
});

test('bounded retry + callback watchdog cannot spin in realtime PCM',()=>{
  assert.match(policy,/30000ull/);
  assert.match(main,/callbackWatchTickMs_/);
  assert.match(main,/now-callbackProgressMs_>=3500/);
  assert.match(main,/now-callbackWatchTickMs_>1500/);
  assert.match(main,/AudioContinuityRetry wasapiRecovery_/);
  assert.match(cmake,/P0DisplayIdlePreservesAudioAndDeviceRecoveryIntent/);
});
