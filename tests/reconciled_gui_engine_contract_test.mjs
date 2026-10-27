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
  assert.match(bridge,/liveOutputLeft/);
  assert.match(bridge,/\["masterL",liveOutputLeft\]/);
  assert.match(bridge,/\["masterR",liveOutputRight\]/);
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

test('JUCE native resources track the approved calibrated GUI without maintaining a second mixer implementation', () => {
  const cmake=read('CMakeLists.txt');
  const cpp=read('src/Main.cpp');
  const sourceCss=read('web/styles-src/42-mixer-premium.css');
  const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
  assert.match(cmake,/juce_add_binary_data\(ARMixAssets SOURCES\s+web\/index\.html web\/styles\.css web\/app\.js web\/bridge\.js web\/monitor\.js\)/);
  for (const resource of ['index_html','styles_css','app_js','bridge_js','monitor_js']) {
    assert.ok(cpp.includes('BinaryData::'+resource),resource+' missing from WebView provider');
  }
  assert.ok(css.endsWith(sourceCss), 'native CSS must end with its authoritative calibration module');
  assert.ok(standalone.includes('<style>'+css+'</style>'), 'Pages/standalone CSS drift');
  assert.ok(standalone.includes('<script>'+app+'</script>'), 'Pages/standalone JS drift');
  for (const cls of ['gain-scale-travel','vu-scale-travel','meter-signal-area','fader-travel']) {
    assert.ok(app.includes(cls)||sourceCss.includes(cls), 'calibration module missing '+cls);
  }
  assert.match(app,/const levelToTop=db=>/);
  assert.match(app,/distance:Math\.max\(24,r\.height-77\)/);
});
