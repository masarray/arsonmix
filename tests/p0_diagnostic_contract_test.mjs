// SPDX-License-Identifier: AGPL-3.0-only
// Guards the integration points. Pure oscillator behavior is verified in CTest.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=path=>readFileSync(path,'utf8');
const engine=read('src/LiveAudioEngine.h');
const host=read('src/Main.cpp');
const ui=read('web/monitor.js');
const osc=read('src/DiagnosticTone.h');
const build=read('CMakeLists.txt');

test('true output-only and capture-only device configurations are opened, not simulated',()=>{
  assert.match(engine,/isolated\.inputDeviceName\.clear\(\)/);
  assert.match(engine,/isolated\.inputChannels\.clear\(\)/);
  assert.match(engine,/isolated\.outputDeviceName\.clear\(\)/);
  assert.match(engine,/isolated\.outputChannels\.clear\(\)/);
  assert.match(engine,/setAudioDeviceSetup \(isolated, false\)/);
  assert.match(engine,/setAudioDeviceSetup \(restore, false\)/);
  assert.match(engine,/setAudioDeviceSetup \(original, false\)/);
  assert.match(engine,/savedLiveSetup_ = original/);
  assert.match(engine,/diagnosticRoute_\.store \(wanted/);
  assert.match(host,/!engine_\.hasTemporaryDiagnosticDevice\(\)/);
});
test('audio callback isolates low-amplitude oscillator from input and VST3',()=>{
  assert.match(engine,/const bool toneTest = route == armix::diagnostics::Route::outputOnlyTone/);
  assert.match(engine,/const bool captureTest = route == armix::diagnostics::Route::captureOnly/);
  assert.match(engine,/diagnosticTone_\.next \(audible\)/);
  assert.match(engine,/if \(!toneTest\)/);
  assert.match(engine,/if \(!dry\)/);
  assert.match(osc,/0\.0316227766/);
  assert.doesNotMatch(osc,/new |malloc\(|std::vector|mutex|lock_guard|fwrite\(/);
  assert.match(build,/P0DiagnosticTone/);
});
test('diagnostics preserve provenance and user-stamped glitch evidence',()=>{
  assert.match(engine,/backendXrunMeaning/);
  assert.match(engine,/WASAPI_CAPTURE_DISCONTINUITIES_ONLY/);
  assert.match(engine,/lateCallbackIntervals/);
  assert.match(engine,/renderUnderrunSource/);
  assert.match(host,/AUDIO_GLITCH_MARK/);
  assert.match(host,/AUDIO_TEST_MODE/);
  assert.match(host,/diagnosticRoute/);
  assert.match(ui,/arDiagnosticMode/);
  assert.match(ui,/MARK CRACKLE/);
  assert.match(ui,/OUTPUT_ONLY_TONE/);
  assert.match(ui,/CAPTURE_ONLY/);
  assert.match(ui,/Render underflow UNOBSERVABLE/);
});
