// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const engine=read('src/LiveAudioEngine.h');
const main=read('src/Main.cpp');
const native=read('web/monitor.js');
const adapter=read('engine/device/JuceIndependentDeviceAdapter.h');

test('separate real JUCE input-only and output-only managers, callback ownership',()=>{
 assert.match(adapter,/captureManager_\.initialise \(1, 0, nullptr, false\)/);
 assert.match(adapter,/renderManager_\.initialise \(0, 2, nullptr, false\)/);
 assert.match(adapter,/pipeline_\.capture\(input,numInput,frames\)/);
 assert.match(adapter,/pipeline_\.render\(captureData,2,frames\)/);
 assert.match(adapter,/renderManager_\.removeAudioCallback \(&renderCallback_\)/);
 assert.match(adapter,/captureManager_\.removeAudioCallback \(&captureCallback_\)/);
 assert.match(adapter,/setCurrentAudioDeviceType/);
 assert.match(adapter,/captureConfig\.sampleRate = 0/);
});
test('host opt-in is reversible, mode truthful, exact capture endpoint xrun',()=>{
 assert.match(engine,/setIndependentAudioActive \(bool desired\)/);
 assert.match(engine,/graphOwned_->count != 0/);
 assert.match(engine,/deviceManager_\.closeAudioDevice\(\)/);
 assert.match(engine,/splitAdapter_\.prepare\(type,joinedBeforeSplit_,25\.0\)/);
 assert.match(engine,/splitAdapter_\.start\(\*this\)/);
 assert.match(engine,/splitAdapter_\.stop\(\)/);
 assert.match(engine,/deviceManager_\.setAudioDeviceSetup\(joinedBeforeSplit_,false\)/);
 assert.match(engine,/splitAudioActive_ \? captureDevice : d/);
 assert.match(engine,/JUCE_SPLIT_BRIDGED_0FX/);
 assert.match(engine,/p0BridgeUnderflowFrames/);
 assert.match(engine,/p0CaptureDropFrames/);
});
test('one-click GUI opt-in and automatic driver-error rollback; FX unchanged',()=>{
 assert.match(main,/action == "toggleSmartIO"/);
 assert.match(main,/P0_SPLIT_DEVICE_ERROR/);
 assert.match(main,/isIndependentAudioDriverError/);
 assert.match(main,/isIndependentAudioActive/);
 assert.match(main,/engine_\.activeOutputDevice\(\)/);
 assert.match(native,/arSmartToggle/);
 assert.match(native,/0 FX \(EXPERIMENTAL\)/);
 assert.match(native,/toggleSmartIO/);
 assert.match(native,/p0BridgeUnderflowFrames/);
});
