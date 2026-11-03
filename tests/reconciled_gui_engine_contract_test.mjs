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

test('JUCE native resources track the approved calibrated GUI without maintaining a second mixer implementation', () => {
  const cmake=read('CMakeLists.txt');
  const cpp=read('src/Main.cpp');
  // The approved GUI has N source-owned CSS modules, not a single appended
  // "calibration module". Compare the ENTIRE generated output byte-for-byte
  // against the same deterministic input order as build_ui_css.py.
  const order=['00-font-and-assets.css','10-console-legacy.css',
    '20-button-180.css','21-navigation-fx-power.css',
    '30-selection-effects.css','40-rail-pan.css',
    '41-mixer-performance.css','42-mixer-premium.css',
    '43-hardware-backlit.css'];
  const builtCss=order.map(name=>read('web/styles-src/'+name)).join('');
  const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
  // Verify legacy assets in original order, while allowing additive UI modules.
  const assetList=cmake.match(/juce_add_binary_data\(ARMixAssets SOURCES\s+([^)]+)\)/)?.[1];
  assert.ok(assetList,'native WebView2 source list missing');
  const expected=['web/index.html','web/styles.css','web/plugin-ui.css','web/app.js',
    'web/plugin-ui.js','web/bridge.js','web/monitor.js','web/mixer-io.js','web/mixer-io.css',
    'web/mixer-layout.js','web/mixer-layout.css','web/hardware-keys.js',
    'web/assets/fader-silver2.png','web/assets/faders/FADER-ARI-SILVER.png',
    'web/assets/faders/FADER-ARI-ROSEGOLD.png','web/assets/pan/PAN-SLIDER.png'];
  const listed=assetList.trim().split(/\s+/);
  for(const asset of expected) assert.ok(listed.includes(asset),'missing WebView2 asset '+asset);
  assert.equal(new Set(listed).size,listed.length,'duplicate native WebView2 resource');
  for (const resource of ['index_html','styles_css','pluginui_css','app_js','pluginui_js','bridge_js','monitor_js']) {
    assert.ok(cpp.includes('BinaryData::'+resource),resource+' missing from WebView provider');
  }
  assert.equal(css,builtCss,'native CSS must equal ALL approved source-owned modules, in order');
  assert.ok(standalone.includes('<style>'+css+'</style>'), 'Pages/standalone CSS drift');
  assert.ok(standalone.includes('<script>'+app+'</script>'), 'Pages/standalone JS drift');
  for (const cls of ['gain-scale-travel','vu-scale-travel','meter-signal-area','fader-travel']) {
    assert.ok(app.includes(cls)||css.includes(cls), 'approved CSS/JS calibration missing '+cls);
  }
  assert.match(app,/const levelToTop=db=>/);
  assert.match(app,/distance:Math\.max\(24,r\.height-77\)/);
});
