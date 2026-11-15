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


test('WASAPI recovery fails closed against substituted endpoint names or pin masks',()=>{
  const comparison=main.split('static bool matchesPinnedWasapiReadback(')[1]
      .split('void rememberVerifiedWasapiSetup()')[0];
  assert.match(comparison,/actual\.inputDeviceName==wanted\.inputDeviceName/);
  assert.match(comparison,/actual\.outputDeviceName==wanted\.outputDeviceName/);
  assert.match(comparison,/getActiveInputChannels\(\)==wanted\.inputChannels/);
  assert.match(comparison,/getActiveOutputChannels\(\)==wanted\.outputChannels/);
  const reconnect=main.split('void serviceWasapiDeviceContinuity()')[1]
      .split('void protectKnownFeedbackLoop(')[0];
  assert.match(reconnect,/matchesPinnedWasapiReadback\(\*verifiedWasapiSetup_,live,device\)/);
  assert.match(reconnect,/matchesPinnedWasapiReadback\(wanted,actual,opened\)/);
  const remember=main.split('void rememberVerifiedWasapiSetup()')[1]
      .split('void observeNativeCallbackHeartbeat()')[0];
  assert.match(remember,/pinned\.useDefaultOutputChannels=false/);
  assert.match(remember,/pinned\.useDefaultInputChannels=false/);
});

test('session capture and shutdown keep requested MAIN through a device STOP',()=>{
  const capture=main.split('juce::String saveMixerSession(bool resumeMain')[1]
      .split('const auto canonical=')[0];
  assert.match(capture,/if\(monitoring\)engine_\.setOutputMuted\(true\)/);
  assert.match(capture,/engine_\.setOutputMuted\(false\)/);
  assert.doesNotMatch(capture,/engine_\.setMonitorEnabled\(/);
  const shutdown=main.split('~ConsoleWindow() override')[1]
      .split('engine_\.disconnectSecondaryOutput\(\)')[0];
  assert.match(shutdown,/engine_\.wantsMainContinuation\(\) && !operatorOutputMuted_/);
  assert.match(shutdown,/saveMixerSession\(resumeMain,\{\},true\)/);
});

test('automatic checkpoints cannot overwrite saved MAIN and device binding during STOP',()=>{
  const save=main.split('juce::String saveMixerSession(bool resumeMain')[1]
      .split('void openMixerFile(')[0];
  assert.match(save,/!audio \|\| !audio->isOpen\(\)/);
  assert.match(save,/if\(automatic && !shutdownCheckpoint\)/);
  assert.match(save,/engine_\.mainInterruptedByDevice\(\)/);
  assert.match(save,/doc\.resumeMain=resumeMain \|\|/);
  assert.match(save,/engine_\.wantsMainContinuation\(\) && !operatorOutputMuted_/);
  const persistence=main.split('if (deviceSettingsDirty_)')[1]
      .split('if (timerTicks_')[0];
  assert.match(persistence,/activeDevice != nullptr && activeDevice->isOpen\(\)/);
  assert.match(persistence,/deviceSettingsDirty_ = false/);
  assert.ok(persistence.indexOf('deviceSettingsDirty_ = false')>
            persistence.indexOf('activeDevice->isOpen()'),
            'Do not consume a pending device profile while its driver is absent');
});

test('bounded retry + callback watchdog cannot spin in realtime PCM',()=>{
  assert.match(policy,/30000ull/);
  assert.match(main,/callbackWatchTickMs_/);
  assert.match(main,/now-callbackProgressMs_>=3500/);
  assert.match(main,/now-callbackWatchTickMs_>1500/);
  assert.match(main,/AudioContinuityRetry wasapiRecovery_/);
  assert.match(cmake,/P0DisplayIdlePreservesAudioAndDeviceRecoveryIntent/);
});
