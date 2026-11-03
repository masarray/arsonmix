// SPDX-License-Identifier: AGPL-3.0-only
// Product integration quality gate: no fake universal ChannelIds and only
// real fixed-M4 native PCM permissions may be surfaced in the GUI.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=path=>readFileSync(new URL('../'+path,import.meta.url),'utf8');
const m4=read('web/m4-native-console.js');
const html=read('web/index.html');
const css=read('web/m4-native-console.css');
const bridge=read('src/ConsoleLiveBridgeV6.h');
const native=read('src/Main.cpp');
test('integrated mixer mounts only after original GUI native socket',()=>{
  assert.ok(html.includes('src="mixer-layout.js"'));
  assert.ok(html.includes('src="m4-native-console.js"'));
  assert.ok(html.indexOf('src="m4-native-console.js"')>
            html.indexOf('src="mixer-layout.js"'));
  assert.ok(html.includes('href="m4-native-console.css"'));
  assert.ok(css.includes('m4-physical-active'));
});
test('M4 native control admissions retain independent physical authority fences',()=>{
  assert.ok(bridge.includes('LEGACY_M4_ORDINAL'));
  assert.ok(bridge.includes('acceptedChannels'));
  assert.ok(bridge.includes('legacyRoutingRevision'));
  assert.ok(bridge.includes('PRODUCTION_GRAPH_NOT_ADMITTED'));
  assert.ok(bridge.includes('w.productionCallbackLive=false'));
  assert.ok(native.includes('arMixConsoleIntent'));
  assert.ok(native.includes('processProductionIntent'));
  assert.ok(native.includes('arMixConsoleAck'));
});
test('browser never derives a fake ChannelId graph or mutates actual PCM',()=>{
  assert.ok(m4.includes('productionCallbackLive!==false'));
  assert.ok(m4.includes('s.graph.channels?.length!==0'));
  assert.ok(m4.includes('legacy.controlPlane'));
  assert.ok(m4.includes("targetChannelId:String(lane)"));
  assert.ok(m4.includes('legacyRoutingRevision:s.legacyM4.routingRevision'));
  assert.ok(m4.includes('req.acked&&matches(req,s)'));
  assert.ok(m4.includes('readsAfterAck>=5'));
  assert.ok(m4.includes("requestSnapshot(); // Register first"));
  assert.ok(!m4.includes('AudioContext'));
  assert.ok(!m4.includes('new Audio'));
  assert.ok(!m4.includes('createChannel('));
});
test('negative and success WebView2 socket flows have a real Chromium spec',()=>{
  const smoke=read('tests/m4_native_gui_bridge_smoke.py');
  for(const word of ['arMixConsoleIntent','arMixConsoleAck',
     'legacyRoutingRevision','NATIVE_CONTROL_REJECTED',
     'pendingCount','productionCallbackLive'])
    assert.ok(smoke.includes(word),word);
});

test('Windows JUCE BinaryData embeds every live GUI script and image referenced by CSS',()=>{
  const cmake=read('CMakeLists.txt'), cpp=read('src/Main.cpp');
  const expected=[
    ['web/hardware-keys.js','/hardware-keys.js','hardwarekeys_js'],
    ['web/m4-native-console.js','/m4-native-console.js','m4nativeconsole_js'],
    ['web/m4-native-console.css','/m4-native-console.css','m4nativeconsole_css'],
    ['web/assets/fader-silver2.png','/assets/fader-silver2.png','fadersilver2_png'],
    ['web/assets/faders/FADER-ARI-SILVER.png','/assets/faders/FADER-ARI-SILVER.png','FADERARISILVER_png'],
    ['web/assets/faders/FADER-ARI-ROSEGOLD.png','/assets/faders/FADER-ARI-ROSEGOLD.png','FADERARIROSEGOLD_png'],
    ['web/assets/pan/PAN-SLIDER.png','/assets/pan/PAN-SLIDER.png','PANSLIDER_png']
  ];
  for(const [source,route,symbol] of expected){
    assert.ok(cmake.includes(source),'CMake BinaryData missing '+source);
    assert.ok(cpp.includes('path == "'+route+'"'),'resource URL missing '+route);
    assert.ok(cpp.includes('BinaryData::'+symbol),'resource name missing '+symbol);
  }
});
