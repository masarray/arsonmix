// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const r=p=>readFileSync(p,'utf8');
const engine=r('src/LiveAudioEngine.h'), host=r('src/Main.cpp'),
      adapter=r('src/SplitDuplexAdapter.h'),ui=r('web/monitor.js'),
      cmake=r('CMakeLists.txt');
test('actual separate device managers and callbacks, not synthetic split of joined callback',()=>{
  assert.match(adapter,/juce::AudioDeviceManager captureManager_/);
  assert.match(adapter,/struct CaptureTap final : juce::AudioIODeviceCallback/);
  assert.match(adapter,/captureManager_\.addAudioCallback \(&tap_\)/);
  assert.match(adapter,/captureManager_\.removeAudioCallback \(&tap_\)/);
  assert.match(adapter,/captureManager_\.closeAudioDevice\(\)/);
  assert.match(adapter,/capture\.outputDeviceName\.clear\(\)/);
  assert.match(engine,/outputOnly\.inputDeviceName\.clear\(\)/);
  assert.match(adapter,/bridge_\.prepare \(captureRate, outputRate, 2, 16384, fifoTargetMs\)/);
  assert.match(engine,/splitAdapter_\.pull \(target, 2, frames\)/);
  assert.match(cmake,/src\/SplitDuplexAdapter\.h/);
});
test('unknown clocks are never labeled direct without proof or force resampled on ASIO',()=>{
  assert.match(engine,/!type\.startsWithIgnoreCase \("Windows Audio"\)/);
  assert.match(engine,/type\.containsIgnoreCase \("Exclusive"\)/);
  assert.match(engine,/configureStartupAuto\(\)/);
  assert.match(ui,/AUTO · ADAPTIVE/);
  assert.match(ui,/DIRECT · NO SRC/);
  assert.match(ui,/STABLE · MORE HEADROOM/);
  assert.match(ui,/LOW LATENCY/);
});
test('both callbacks quiesce before buffer destruction or device rerouting',()=>{
  const stop =engine.slice(engine.indexOf('void stop()'),engine.indexOf('void setMonitorEnabled'));
  assert.ok(stop.indexOf('detach();')<stop.indexOf('splitAdapter_.stop()'));
  assert.match(engine,/routeReconfiguring_\.store \(true/);
  assert.match(engine,/const auto rollback = deviceManager_\.setAudioDeviceSetup \(original, false\)/);
  assert.match(engine,/setOutputMuted \(true\)/);
  assert.match(engine,/disableSmartBridge\(\)/);
  assert.match(host,/SMART_WATCHDOG_FALLBACK/);
  assert.match(host,/smart-audio-profile\.txt/);
  assert.match(host,/!engine_\.hasTemporaryAudioRouting\(\)/);
});
test('clipboard report and UI include observed FIFO faults and independent clock source',()=>{
  assert.match(engine,/bridgeConsumerUnderflowFrames/);
  assert.match(engine,/bridgeProducerOverflowFrames/);
  assert.match(engine,/bridgeCaptureBackendXRuns/);
  assert.match(engine,/HOST_CAPTURE_FIFO_READ_STARVATION_NOT_HARDWARE_RENDER_XRUN/);
  assert.match(engine,/UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE/);
  assert.match(ui,/bridgeCorrectionPpm/);
  assert.match(ui,/bridgeFillFrames/);
  assert.match(host,/makeAudioReport\(\)/);
});
