// SPDX-License-Identifier: AGPL-3.0-only
// Glue contracts complement Release-active CTest for the pure recovery policy.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const main=read('src/Main.cpp');
const engine=read('src/LiveAudioEngine.h');
const gate=read('engine/device/DirectRestoreBackoff.h');
const cmake=read('CMakeLists.txt');
const workflow=read('.github/workflows/p1-windows.yml');

test('actual device recoveries occur on message thread, not inside audio callback',()=>{
  const timer=main.indexOf('void timerCallback() override');
  const runner=main.indexOf('void serviceDirectRestore()');
  assert.ok(timer>=0 && runner>0);
  // Extract the actual initial control-timer phase rather than relying on
  // a brittle 420-character prefix: safe notification / fault reporting may
  // precede recovery without moving a device restart to the audio thread.
  const graphAck=main.indexOf('if (engine_.finalizeGraphSwitch())', timer);
  assert.ok(graphAck>timer);
  assert.match(main.slice(timer,graphAck),/serviceDirectRestore\(\)/);
  assert.match(main.slice(runner,main.indexOf('void emitNotice',runner)),
    /engine_\.setIndependentClockEnabled \(false\)/);
  assert.match(main,/engine_\.independentClockFaulted\(\)/);
  assert.match(engine,/hasPendingDirectRestore\(\) const noexcept/);
  assert.match(engine,/verifyRestoredDirectDevice \(\*savedDirectSetup_\)/);
  assert.match(engine,/verifyRestoredDirectDevice \(original\)/);
  assert.match(engine,/current\.inputDeviceName == expected\.inputDeviceName/);
  assert.match(engine,/current\.outputDeviceName == expected\.outputDeviceName/);
  assert.match(engine,/getActiveInputChannels\(\)\.isZero\(\)/);
  assert.match(engine,/getActiveOutputChannels\(\)\.isZero\(\)/);
  assert.match(gate,/class DirectRestoreBackoff final/);
  assert.match(gate,/static constexpr unsigned maxAttempts/);
  assert.match(gate,/generation == generation_/);
  assert.match(gate,/State::exhausted/);
});
test('restore retry is bounded and disabled on success, explicit direct and setup paths',()=>{
  assert.match(main,/directRestore_\.failed \(snapshot\.generation, now\)/);
  assert.match(main,/directRestore_\.recovered \(snapshot\.generation\)/);
  assert.match(main,/directRestore_\.cancel\(\)/);
  assert.match(main,/independentClockActive\(\) \|\| engine_\.hasPendingDirectRestore\(\)/);
  assert.match(main,/directRestoreAttempts/);
  assert.match(main,/directRestoreNextMs/);
  assert.match(main,/directRestoreError/);
  assert.match(main,/DIRECT_RESTORE_BEGIN/);
  assert.match(main,/SMART_CLOCK_RECOVERY/);
  assert.match(main,/annotateRecovery \(status\)/);
  assert.match(main,/annotateRecovery \(state\)/);
  assert.match(cmake,/BoundedDirectDeviceRecovery/);
  assert.match(workflow,/ARMixRecoveryTests/);
  assert.match(workflow,/direct_restore_recovery_contract_test.mjs/);
});
test('verified native FAT package preserves actual beta live-bridge provenance, not older offline-only claim',()=>{
  const fatGuide=read('docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md');
  const packaging=read('scripts/package_native_windows.ps1');
  // Authoritative backend facts live in C++ + a versioned FAT protocol, not
  // inside a brittle PowerShell free-text Out-File string.
  assert.match(engine,/JuceIndependentClockDevice independent_/);
  assert.match(fatGuide,/Do not enable beta independent clocks/);
  assert.match(fatGuide,/ASRC_INPUT_UNDERFLOW_FRAMES/);
  assert.match(packaging,/FAT_GUIDE\\.md/);
  assert.match(packaging,/EXPERIMENTAL_NOT_HARDWARE_QUALIFIED/);
  assert.doesNotMatch(packaging,/An offline adaptive dual-clock FIR bridge is tested in CI but NOT connected/);
});

test('healthy independent sessions never arm restoration; setup can replace missing devices',()=>{
  assert.match(main,/\(!engine_\.independentClockActive\(\) && engine_\.hasPendingDirectRestore\(\)\)/);
  assert.match(engine,/bool abandonPendingDirectRestoreForSetup\(\)/);
  assert.match(engine,/deviceManager_\.addAudioCallback \(this\)/);
  assert.match(main,/DIRECT_RESTORE_ABANDON/);
  assert.match(main,/engine_\.devices\(\)\.getCurrentAudioDevice\(\) != nullptr/);
  assert.match(main,/Choose a replacement in AUDIO SETUP/);
});

test('ASIO/WASAPI DIRECT authorizes the exact active input/output channel map',()=>{
  const fence=read('engine/device/DirectRouteFence.h');
  // Bind BOTH untruncated JUCE BigInteger masks at startup, manual selection,
  // and verified DIRECT restore, rather than only the friendly device name.
  assert.equal(engine.split('getActiveInputChannels().toString (16)').length-1,3);
  assert.equal(engine.split('getActiveOutputChannels().toString (16)').length-1,3);
  for (const operation of ['approve','resumeExpected','observeStart']) {
    // Match the verified native device/mask inputs independently of cosmetic
    // line wrapping introduced by the ASIO control-plane identity guard.
    const call = new RegExp('directFence_\\.' + operation +
      '\\s*\\(\\s*backend\\.toRawUTF8\\(\\),\\s*name\\.toRawUTF8\\(\\),');
    assert.match(engine, call);
  }
  assert.ok(engine.includes('EXACT_ACTIVE_INPUT_AND_OUTPUT_CHANNEL_BITMASKS'));
  assert.ok(fence.includes('UNEXPECTED_DIRECT_BACKEND_DEVICE_OR_CHANNEL_MASK'));
  assert.ok(fence.includes('!validMask(inputMask)'));
  assert.ok(fence.includes('!validMask(outputMask)'));
  const callback=engine.slice(engine.indexOf('void audioDeviceIOCallbackWithContext'));
  assert.ok(callback.indexOf('!directFence_.permitted()')>=0);
  assert.ok(callback.indexOf('!directFence_.permitted()')<callback.indexOf('handoff_.beginCycle()'));
  assert.ok(!callback.includes('fingerprint(') && !callback.includes('toString (16)'));
  assert.ok(cmake.includes('P0AsioLiveDirectRouteFence'));
});
