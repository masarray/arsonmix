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

test('G3 inline AUX sends mutate only one native routing draft with bounded PRE/POST semantics',()=>{
  assert.match(gui,/__arMixStageAuxSendEdit/);
  assert.match(gui,/__arMixStageAuxReturnEdit/);
  assert.match(gui,/data-send-focus/);
  assert.match(gui,/data-send-gain/);
  assert.match(gui,/data-send-pan/);
  assert.match(gui,/data-send-enable/);
  assert.match(gui,/data-send-apply/);
  assert.match(gui,/data-send-revert/);
  assert.match(monitor,/window\.__arMixStageAuxSendEdit=/);
  assert.match(monitor,/window\.__arMixStageAuxReturnEdit=/);
  assert.match(monitor,/window\.__arMixRevertMixerDraft=/);
  assert.match(monitor,/if\(draftSends\.length>=32\)/);
  assert.match(monitor,/original\.gain=0|item\.gain=0/);
  assert.match(monitor,/drawAuxSendMatrix\(\);drawSends\(\)/);
  assert.match(monitor,/window\.dispatchEvent\(new Event\("arMixRoutingDraftChanged"\)\)/);
  assert.match(monitor,/if\(patchPending\)return/);
  assert.match(monitor,/if\(state\.monitor\)/);
  assert.match(monitor,/api\.emitEvent\("arMixAction",\{action:"routingDryApply"/);
  assert.doesNotMatch(gui,/createPluginInstance|AudioContext|AudioWorklet|processBlock/);
});
test('G3 is computed from authoritative PRE/POST native renderer, not a visual-only mixer',()=>{
  const native=read('tests/dedicated_aux_bus_test.cpp');
  assert.match(native,/SendTap::preFader/);
  assert.match(native,/SendTap::postFader/);
  assert.match(native,/Zero source fader: PRE-FADER send stays audible/);
  assert.match(native,/duplicateSend/);
  assert.match(native,/invalidCount/);
  const css=read('web/mixer-io.css');
  assert.match(css,/\.arSendEditor\[hidden\]/);
  assert.match(css,/max-height:calc\(100dvh - 16px\)/);
  assert.doesNotMatch(css,/backdrop-filter\s*:|filter\s*:\s*blur/);
});
