// SPDX-License-Identifier: AGPL-3.0-only
// Regression guard for nonresponsive ADD FX clicks and VST3 stereo/sidechain.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const js=readFileSync('web/bridge.js','utf8');
const host=readFileSync('src/Main.cpp','utf8');
const dsp=readFileSync('src/LiveAudioEngine.h','utf8');
const layout=readFileSync('src/VstBusLayout.h','utf8');

test('every ADD click identifies request and reports result inside the open selector',()=>{
  assert.match(js,/send\("addCatalog",\{id,requestId\}\)/);
  assert.match(js,/backend\.addEventListener\("arMixAddResult"/);
  assert.match(js,/nativeAddFeedback/);
  assert.match(js,/FX NOT LOADED:/);
  assert.match(js,/if\(result\.ok\)hideDialog\("nativeFxSelector"\)/);
  // Errors respond immediately; successful inserts ACK only AFTER the
  // audio callback swaps to the prepared graph and the control thread retires.
  assert.match(host,/emitFxAddResult \(requestId,name,error,elapsed\)/);
  const finalized=host.indexOf('if (engine_.finalizeGraphSwitch())');
  const accepted=host.indexOf('emitFxAddResult(pendingFxAddRequestId_',finalized);
  assert.ok(finalized>=0&&accepted>finalized);
  const staging=host.slice(host.indexOf('if (action == "addCatalog")',host.indexOf('if (action == "addScanFolder")')),
                           host.indexOf('else if (action == "fxRemove")'));
  assert.match(staging,/pendingFxAddRequestId_=requestId/);
  assert.doesNotMatch(staging,/\+\+fxGraphRevision_/);
  assert.match(host,/FX_LOAD_FAIL/);
});
test('plugin load and preset restore both negotiate existing VST3 bus layouts',()=>{
  assert.equal((dsp.match(/prepareStereoEffectLayout/g)||[]).length,2);
  assert.match(layout,/if \(compatible \(original\)\) return \{\};/);
  assert.match(layout,/requested\.inputBuses\.set \(i, disabled\)/);
  assert.match(layout,/processor\.setBusesLayout \(requested\)/);
});
test('GUI paints actual indeterminate feedback before synchronous plugin instantiate',()=>{
  assert.match(js,/requestAnimationFrame\(\(\)=>requestAnimationFrame\(\(\)=>send\("addCatalog"/);
  assert.match(js,/aria-live="polite"/);
  assert.match(js,/state\.adding=true/);
});
