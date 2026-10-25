// SPDX-License-Identifier: AGPL-3.0-only
// Static guard: the native preset path must never silently fall back to demo
// localStorage or authorize an unmuted VST3 graph mutation.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const get=p=>readFileSync(p,'utf8');
const main=get('src/Main.cpp'), engine=get('src/LiveAudioEngine.h');
const io=get('src/NativePresetArchive.h'), bridge=get('web/bridge.js');
test('full vendor state is captured/restored by the native host',()=>{
  assert.match(engine,/getStateInformation \(state\.binary\)/);
  assert.match(engine,/setStateInformation \(/);
  assert.match(engine,/snapshot\.effects\.push_back/);
  assert.match(engine,/auto retired = std::move \(slots_\)/);
});
test('ZIP archive does not extract untrusted paths to disk',()=>{
  assert.match(io,/juce::ZipFile::Builder/);
  assert.match(io,/manifest\.json/);
  assert.match(io,/juce::SHA256/);
  assert.match(io,/maxStateBytes = 8 \* 1024 \* 1024/);
  assert.doesNotMatch(io,/uncompressTo|uncompressEntry/);
});
test('muted-only safety and transactional read',()=>{
  assert.match(main,/!engine_\.isOutputMuted\(\)/);
  assert.match(engine,/if \(!isOutputMuted\(\)\)/);
  assert.match(io,/output = std::move \(temp\)/);
});
test('separate native My Sounds not browser-only preset',()=>{
  assert.match(bridge,/data-my-sounds/);
  assert.match(bridge,/send\("presetSave",\{name\}\)/);
  assert.match(bridge,/send\("presetLoad",\{name\}\)/);
  assert.match(bridge,/arMixNativePresetLoaded/);
});
