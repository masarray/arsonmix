// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const html=read('web/index.html'),app=read('web/app.js'),css=read('web/styles.css');
const bridge=read('web/bridge.js'),engine=read('src/LiveAudioEngine.h');
test('latest desktop GUI is present and actually loads native bridge',()=>{
  assert.match(app,/function openChannelIdentity\(/);
  assert.match(app,/id="masterLeft"/);
  assert.match(app,/class="fx-power/);
  assert.match(app,/data-meter="\$\{key\}"/);
  assert.match(html,/id="channelIdentity"/);
  assert.match(html,/src="bridge\.js"/);
  assert.match(html,/src="monitor\.js"/);
  assert.match(css,/master-fx-rack/);
});
test('native desktop does not run any visual-only sine meter loop',()=>{
  const scheduler=app.indexOf('function scheduleDemoMeter()');
  const fn=app.indexOf('function meters(t)');
  const early=app.indexOf('if(window.__JUCE__?.backend?.emitEvent||document.hidden||',fn);
  const synth=app.indexOf('channels.forEach((ch,i)=>{const wave=',fn);
  const scheduleGuard=app.indexOf('if(window.__JUCE__?.backend?.emitEvent||document.hidden||',scheduler);
  const timer=app.indexOf('meterTimer=setTimeout(',scheduler);
  assert.ok(scheduler>=0 && fn>scheduler && early>fn && early<synth);
  assert.ok(scheduleGuard>scheduler && scheduleGuard<timer);
  assert.ok(bridge.includes('percent(d.peakLeft)')&&bridge.includes('percent(d.peakRight)'));
  assert.match(bridge,/paintMeter\(document\.querySelector\('\[data-meter="masterL"\]'\)/);
  assert.match(bridge,/paintMeter\(document\.querySelector\('\[data-meter="masterR"\]'\)/);
  assert.ok(!bridge.includes('liveOutputLeft-5'),'native output must be raw before time-based visual motion');
  assert.match(bridge,/track\.dataset\.preview="1"/);
});
test('master non-native control surfaces explicitly excluded',()=>{
  assert.match(bridge,/data-master-insert/);
  assert.match(bridge,/data-master-power/);
  assert.match(bridge,/Master PAN DSP not yet implemented/);
  assert.match(bridge,/Only Channel 1|Channel 1/);
});
test('full engine capabilities preserved',()=>{
  assert.match(engine,/prepareStereoEffectLayout/);
  assert.match(engine,/NativePresetArchive/);
  assert.match(engine,/handoff_\.stage/);
  assert.match(engine,/handoff_\.beginCycle/);
  assert.match(bridge,/arMixAddResult/);
  assert.match(bridge,/arMixNativePresetLoaded/);
});

test('JUCE executable embeds ONLY the approved native Console while archived preview assets remain source-tested',()=>{
 const cmake=read('CMakeLists.txt'),cpp=read('src/Main.cpp');
 const nativeHtml=read('web/console-native.html'),nativeJs=read('web/console-native.js');
 const sourceCss=read('web/styles-src/42-mixer-premium.css');
 const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 const assetList=cmake.match(/juce_add_binary_data\(ARMixAssets SOURCES\s+([^)]+)\)/)?.[1];
 assert.ok(assetList,'native resource list missing');
 const listed=assetList.trim().split(/\s+/);
 assert.deepEqual(listed,['web/console-native.html','web/console-native.css','web/console-native.js']);
 assert.match(cpp,/path == "\/" \|\| path == "\/index\.html"/);
 assert.match(cpp,/data = BinaryData::consolenative_html/);
 assert.match(cpp,/BinaryData::consolenative_css/);
 assert.match(cpp,/BinaryData::consolenative_js/);
 assert.doesNotMatch(cpp,/BinaryData::index_html|BinaryData::app_js|BinaryData::monitor_js/);
 assert.match(nativeHtml,/ArSonMix — Native Live Console/);
 assert.match(nativeJs,/arMixConsoleSubscribe/);
 assert.match(nativeJs,/arMixConsoleAck/);
 assert.match(nativeJs,/channelPeaksL/);
 assert.ok(css.endsWith(sourceCss),'archived visual CSS source integrity lost');
 assert.ok(standalone.includes('<style>'+css+'</style>'),'Pages standalone CSS drift');
 assert.ok(standalone.includes('<script>'+app+'</script>'),'Pages standalone script drift');
 for(const cls of ['gain-scale-travel','vu-scale-travel','meter-signal-area','fader-travel'])
  assert.ok(app.includes(cls)||sourceCss.includes(cls),'historical calibration lost '+cls);
});
