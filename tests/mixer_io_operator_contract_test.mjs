// G1/G2: reject UI-only hardware claims and controller duplication.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(new URL('../'+p,import.meta.url),'utf8');
const gui=read('web/mixer-io.js'),monitor=read('web/monitor.js');
const main=read('src/Main.cpp'),cmake=read('CMakeLists.txt'),html=read('web/index.html');
test('single native controller stages MONO/STEREO through muted APPLY + revision ACK',()=>{
  assert.match(gui,/__arMixStageChannelInput/);
  assert.match(gui,/__arMixApplyMixerDraft/);
  assert.match(monitor,/patchPreflight\(\)/);
  assert.match(monitor,/window\.__arMixApplyMixerDraft/);
  assert.match(monitor,/routingDryApply/);
  assert.match(monitor,/arMixRoutingLabAck/);
  assert.match(monitor,/draftEpoch!==epoch/);
  assert.match(monitor,/const active=new Set\(patchPorts\(\)/);
  assert.match(monitor,/if\(state\.monitor\)/);
});
test('FX rack stays mounted, mixer bank only changes presentation',()=>{
  assert.match(gui,/\.insertAdjacentElement\('afterend',bank\)/);
  assert.match(gui,/mixer\.dataset\.ioMode=mode/);
  assert.doesNotMatch(gui,/new AudioContext|AudioWorklet|createPluginInstance|processBlock/);
  assert.match(read('web/mixer-io.css'),/\.rack\{display:none!important\}/);
});
test('physical output is readback only; AUX returns not labeled hardware jacks',()=>{
  assert.match(gui,/s\.outputPorts/);
  assert.match(gui,/READ ONLY/);
  assert.match(gui,/internal stereo FX returns/);
  assert.doesNotMatch(gui,/audioSelectDevice|setCurrentAudioDeviceType/);
});
test('native device inventory and packaged WebView2 assets are wired',()=>{
  assert.match(main,/action == "audioDeviceInventory"/);
  assert.match(main,/getAvailableDeviceTypes\(\)/);
  assert.match(main,/arMixAudioDeviceInventory/);
  for(const f of ['mixer-io.js','mixer-io.css']){
    assert.ok(html.includes(f));
    assert.ok(cmake.includes('web/'+f));
    assert.ok(main.includes('/'+f));
  }
});
