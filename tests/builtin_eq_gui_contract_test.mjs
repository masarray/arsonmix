// The built-in EQ editor is an independent component inside the ONE
// approved ArSonMix /preview/ renderer. No duplicate audio or route UI.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const html=fs.readFileSync('site/preview-v2/index.html','utf8');
const js=fs.readFileSync('site/preview-v2/assets/mixer-builtin-eq.js','utf8');
const css=fs.readFileSync('site/preview-v2/assets/mixer-builtin-eq.css','utf8');
const native=fs.readFileSync('web/approved-native-bridge.js','utf8');
for(const asset of ['mixer-builtin-eq.css','mixer-builtin-eq.js']){
  assert.equal((html.match(new RegExp('assets/'+asset.replace('.', '\\.'),'g'))||[]).length,1);
}
assert.match(html,/data-action="openBuiltinEq"/);
assert.match(html,/ArSonMixBuiltinEq\\?\\.open/);
assert.match(native,/__ARMIX_BUILTIN_EQ_CHANNEL__/);
assert.match(js,/arMixBuiltinEqAck/);
assert.match(js,/arMixBuiltinEqSnapshot/);
assert.match(js,/DESIGN DEMO · NO AUDIO/);
assert.match(js,/snapshotReady/);
assert.ok(!['AudioContext','webkitAudioContext','createScriptProcessor','new Worker('].some(s=>js.includes(s)), 'Browser EQ must not own audio callback');
assert.match(css,/#mxBuiltinEq\\[hidden\\]/);
console.log('Built-in Channel EQ GUI contract: PASS (isolated from VST3 and alt mixer)');
