// SPDX-License-Identifier: AGPL-3.0-only
// Exact source regression of the one native FX preset + picker workflow.
// Structural checks intentionally never claim physical ASIO sound evidence.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=file=>readFileSync(new URL('../'+file,import.meta.url),'utf8');
const host=read('src/Main.cpp');
const preset=read('site/preview-v2/assets/mixer-builtin-fx-preset-bar.js');
const harmony=read('site/preview-v2/assets/mixer-native-harmony.js');
const throwFx=read('site/preview-v2/assets/mixer-native-vocal-throw.js');
const bridge=read('web/approved-native-bridge.js');
const closeCss=read('site/preview-v2/assets/mixer-contextual-buttons.css');
const rackCss=read('site/preview-v2/assets/mixer-fx-rack.css');
const controller=read('site/preview-v2/assets/mixer-contextual-buttons.js');

test('built-in FX preset store works with verified offline logical workspace',()=>{
  assert.match(host,/logicalPreset=fxRequest\.valid&&fxRequest\.unbound/);
  assert.match(host,/logicalFxTarget\(args,logicalChannel,logicalSlot\)/);
  assert.match(host,/logicalChannel==channel&&logicalSlot==static_cast/);
  assert.match(host,/!logicalMixer_\.readOnly\(\)/);
  assert.match(host,/auto offline=logicalPreset\?openLogicalBuiltinFx/);
  assert.match(host,/auto lease=physicalPreset\?engine_\.leaseNativeVst3Editor/);
  assert.match(host,/auto\* processor=logicalPreset\?offline\.get\(\)/);
  assert.match(host,/!logicalPreset&&\s*!realtime->stateSnapshotQuiescent\(\)/);
  assert.match(host,/BuiltInFxAcceptedSnapshot\s*::acceptedSettings/);
  assert.match(host,/result->setProperty\("workspaceToken",args\.getProperty/);
  assert.match(preset,/logical\?c\.deviceEpoch==="0":c\.deviceEpoch!=="0"/);
  assert.match(preset,/c\.workspaceToken&&reply\.workspaceToken!==c\.workspaceToken/);
  assert.match(harmony,/presetContextReady\(pc\)/);
  assert.match(harmony,/presetContextReady\(cPreset\)/);
  assert.match(harmony,/c\.workspaceToken&&reply\.workspaceToken!==c\.workspaceToken/);
});
test('FX picker closes and opens an actual editor only after accepted native or logical readback',()=>{
  assert.match(bridge,/function openVerifiedInsertedFx\(channelId,slot,catalogId,instanceId\)/);
  assert.match(bridge,/effect\.id!==instanceId\|\|effect\.catalogId!==catalogId/);
  assert.match(bridge,/justInsertedLogicalFx/);
  assert.match(bridge,/scope\?\.closeNativePicker\?\.\(\);\s*openVerifiedInsertedFx\(p\.channelId,p\.slot,p\.catalogId,inserted\.id\)/);
  assert.match(bridge,/openVerifiedInsertedFx\(p\.channelId,p\.slot,p\.id,inserted\.id\)/);
  assert.match(bridge,/consumeAction\?\.\(track,"openPlugin"/);
});
test('Vocal Throw wheel does not flicker performance button and presets have dark dropdown',()=>{
  assert.doesNotMatch(throwFx,/vtThrow"\)\.disabled=value!=="on"\|\|!!pending/);
  assert.match(throwFx,/vtThrow"\)\.disabled=value!=="on"/);
  assert.match(throwFx,/wheelFlush=setTimeout\(\(\)=>\{/);
  assert.match(rackCss,/\.mx-vthrow \.vt-preset select\{[\s\S]*?color-scheme:dark/);
  assert.match(rackCss,/\.mx-vthrow \.vt-preset :is\(option,optgroup\)\{background-color:#272b34;color:#e8eaf1/);
});
test('modeless utility windows are centered/dismissible without dismissing FX editors',()=>{
  for(const id of ['pluginBrowser','arsonPlayer','wireWorkspace','wireConfigWindow'])
    assert.match(controller,new RegExp("'"+id+"'"));
  assert.match(controller,/function centerFirst\(panel\)/);
  assert.match(controller,/function dismissOutside\(target\)/);
  assert.match(controller,/close\.click\(\)/);
  assert.match(controller,/\.mx-comp-slider,\.mx-harm-slider input\[type="range"\]/);
  assert.match(controller,/input\.dispatchEvent\(new Event\('input'/);
  assert.match(closeCss,/content:"×"!important;background:none!important/);
});
