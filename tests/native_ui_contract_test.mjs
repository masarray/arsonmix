// SPDX-License-Identifier: AGPL-3.0-only
// No dependencies; catches regressions between the legacy v0.3 GUI and native host.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const app=readFileSync('web/app.js','utf8');
const bridge=readFileSync('web/bridge.js','utf8');
const engine=readFileSync('src/LiveAudioEngine.h','utf8');
const main=readFileSync('src/Main.cpp','utf8');
const recorder=readFileSync('src/DiagnosticsRecorder.h','utf8');
test('native app never runs demo sine meters',()=>{
  const fn=app.indexOf('function meters(t)');
  const nativeExit=app.indexOf('if(window.__JUCE__?.backend?.emitEvent)return;',fn);
  const demoLoop=app.indexOf('requestAnimationFrame(meters)',fn);
  assert.ok(fn>=0 && nativeExit>fn && nativeExit<demoLoop);
});
test('native meter reads DSP and hides all 11 visual-only channels',()=>{
  assert.match(bridge,/inputLevel=percent\(d\.inputPeak\)/);
  assert.match(bridge,/outputLevel=percent\(d\.peak\)/);
  assert.match(bridge,/track\.dataset\.preview="1"/);
  assert.match(bridge,/\.strip\[data-preview="1"\] \.meter-fill/);
});
test('quick FX bypass is wired to native slot instead of demo mock',()=>{
  assert.match(bridge,/data-native-fx="bypass" data-slot="\$\{i\}"/);
  assert.match(bridge,/aria-pressed="\$\{!p\.bypass\}"/);
  assert.match(bridge,/send\("fxBypass",\{slot,value:!state\.chain\[slot\]\?\.bypass\}\)/);
  assert.match(engine,/bypassFlags_\[index\]\.store/);
});
test('GUI pan event reaches actual callback pan matrix',()=>{
  assert.match(app,/send\('pan',c\.pan,idx\)/);
  assert.match(main,/name == "pan" && channel == 1/);
  assert.match(engine,/StereoPanMatrix::fromNormalized/);
});
test('health logger has room for complete JSON snapshots',()=>{
  assert.match(recorder,/char data\[4096\]/);
  assert.match(recorder,/truncatedRecords\(\)/);
});
