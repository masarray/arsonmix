// SPDX-License-Identifier: AGPL-3.0-only
// First-run is native and one-shot; NEVER an HTML/VB-CABLE name-only fiction.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=path=>readFileSync(new URL('../'+path,import.meta.url),'utf8');
const main=read('src/Main.cpp');
const discovery=read('engine/device/WindowsFirstRunEndpointDiscovery.h');
const policy=read('engine/device/FirstRunEndpointPolicy.h');
test('native first run does not overwrite explicit ASIO, old sessions or user routing',()=>{
  assert.match(main,/saved == nullptr &&[\s\S]*?NativeMixerSessionStore\(settingsDirectory\(\)\)\.exists\(\)/);
  assert.match(main,/startupError_\.isEmpty\(\)\)\s*inspectFirstRunAudio\(\)/);
  assert.match(main,/engine_\.hasPristineStartupMixer\(\)/);
  assert.match(policy,/if \(savedExplicitProfile\) return \{ Outcome::preserveSavedProfile \}/);
});
test('CABLE input is proven from MMDevice identity and JUCE before native APPLY',()=>{
  assert.match(discovery,/GetDefaultAudioEndpoint/);
  assert.match(discovery,/EnumAudioEndpoints/);
  assert.match(discovery,/PKEY_DeviceInterface_FriendlyName/);
  assert.match(discovery,/PKEY_AudioEngine_DeviceFormat/);
  assert.match(discovery,/uniqueNativeName = std::count/);
  assert.match(discovery,/contains \(name, L"cable output"\)/);
  assert.match(main,/result\.inspected && d\.mayAttemptApply\(\)/);
  assert.match(main,/applyCustomAudioDevice \(juce::var \(args\.release\(\)\)\)/);
  assert.match(main,/WasapiEndpointIdentityGuard::verify/);
});
test('verified first sound uses original PCM graph and never unconditionally unmutes',()=>{
  assert.match(main,/pins\.findNextSetBit \(0\)/);
  assert.match(main,/switchDryRoutingLabSource \(/);
  assert.match(main,/engine_\.dryRoutingLabEpoch\(\), engine_\.dryRoutingLabRevision\(\)/);
  assert.match(main,/requestAutomaticMain \(true\)/);
  assert.match(main,/engine_\.setOutputMuted \(true\)/);
  assert.doesNotMatch(main,/SetDefaultEndpoint|setDefaultAudioEndpoint/);
});
