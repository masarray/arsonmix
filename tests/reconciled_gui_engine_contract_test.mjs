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
  const fn=app.indexOf('function meters(t)');
  assert.ok(fn>0);
  const early=app.indexOf('if(window.__JUCE__?.backend?.emitEvent)return;',fn);
  const osc=app.indexOf('requestAnimationFrame(meters)',fn);
  assert.ok(early>fn&&early<osc);
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
