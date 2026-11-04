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

test('JUCE native resources use EXACT assembled Pages /preview/, not archived web renderer', () => {
  const cmake=read('CMakeLists.txt');
  const cpp=read('src/Main.cpp');
  const generator=read('scripts/build_approved_native_gui.py');
  const pagesBuilder=read('scripts/build_pages.py');
  const nativeAdapter=read('web/approved-native-bridge.js');
  const oldSourceCss=read('web/styles-src/42-mixer-premium.css');
  const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
  assert.match(cmake,/build_approved_native_gui\.py/);
  assert.match(cmake,/ARMIX_APPROVED_GUI_BINARIES/);
  assert.match(cmake,/ARMIX_APPROVED_GUI_DIR/);
  assert.match(cpp,/ApprovedGuiResources\.h/);
  assert.match(cpp,/resolveApprovedGuiResource/);
  assert.doesNotMatch(cpp,/BinaryData::consolenative_html|BinaryData::index_html/);
  assert.doesNotMatch(cmake,/^\s+web\/index\.html web\/styles\.css/m);
  assert.match(generator,/scripts" \/ "build_pages\.py"/);
  assert.match(pagesBuilder,/APPROVED_SINGLE_GUI/);
  assert.match(pagesBuilder,/buttons_dest=dst\/'preview'/);
  assert.match(generator,/approvedPagesHtmlSha256/);
  assert.match(generator,/M2_NATIVE_ACK_SNAPSHOT_GATED__PCM_NOT_CERTIFIED/);
  assert.match(nativeAdapter,/acceptedGraphConnected: false/);
  assert.match(nativeAdapter,/productionCallbackLive: false/);
  // Historical web app still verifies its archival CSS/standalone consistency.
  // It is NEVER the native compiled mixer or a second GUI product.
  assert.ok(css.endsWith(oldSourceCss),'archival CSS byte relationship drift');
  assert.ok(standalone.includes('<style>'+css+'</style>'),'archival standalone CSS drift');
  assert.ok(standalone.includes('<script>'+app+'</script>'),'archival standalone JS drift');
});
