// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const app=read('web/app.js'), css=read('web/styles.css'), html=read('web/index.html');
const bridge=read('web/bridge.js'), src=read('src/LiveAudioEngine.h'), offline=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
test('GUI v0.8 remains the canonical host, not an older v0.5 reset',()=>{
 assert.match(app,/function openChannelIdentity\(/);
 assert.match(app,/function masterFxRows\(/);
 assert.match(app,/class="master-analysis"/);
 assert.match(app,/LUFS-I/);
 assert.match(app,/dBTP/);
 assert.match(html,/id="channelIdentity"/);
 assert.match(css,/rail-input-footer/);
 assert.match(css,/master-analysis/);
 assert.match(html,/src="bridge\.js"/);
 assert.match(html,/src="monitor\.js"/);
});
test('offline standalone is the byte-matched MAIN v0.8 demo',()=>{
 assert.ok(offline.includes('<script>'+app+'</script>'));
 assert.ok(offline.includes('<style>'+css+'</style>'));
});
test('no fake oscillating VUs inside native JUCE',()=>{
 const a=app.indexOf('function meters(t)'),guard=app.indexOf('if(window.__JUCE__?.backend?.emitEvent)return;',a),raf=app.indexOf('requestAnimationFrame(meters)',a);
 assert.ok(a>=0&&guard>a&&guard<raf);
 assert.match(bridge,/\["masterL",liveOutputLeft\]/);
 assert.match(bridge,/\["masterR",liveOutputRight\]/);
});
test('native plugin state and graph preserved',()=>{
 assert.match(src,/handoff_\.beginCycle\(/);
 assert.match(src,/handoff_\.stage\(/);
 assert.match(bridge,/arMixNativePresetLoaded/);
 assert.match(bridge,/Master PAN DSP not yet implemented/);
 assert.match(bridge,/data-fxpower/);
});
