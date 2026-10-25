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
  assert.match(main.slice(timer,timer+420),/serviceDirectRestore\(\)/);
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
test('CI package describes the actual live BETA architecture, not the old offline-only core',()=>{
  assert.match(workflow,/Independent JUCE WASAPI capture and render device adapters are opt-in experimental/);
  assert.doesNotMatch(workflow,/An offline adaptive dual-clock FIR bridge is tested in CI but NOT connected/);
});
