// The built-in EQ editor is an independent component inside the ONE
// approved ArSonMix /preview/ renderer. No duplicate audio or route UI.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const html=fs.readFileSync('site/preview-v2/index.html','utf8');
const assembler=fs.readFileSync('scripts/build_pages.py','utf8');
const component=fs.readFileSync('scripts/compose_builtin_eq.py','utf8');
const js=fs.readFileSync('site/preview-v2/assets/mixer-builtin-eq.js','utf8');
const css=fs.readFileSync('site/preview-v2/assets/mixer-fx-rack.css','utf8');
const native=fs.readFileSync('web/approved-native-bridge.js','utf8');
assert.ok(assembler.includes('compose_builtin_eq(canonical_html)'));
assert.ok(component.includes('assets/mixer-builtin-eq.js'));
assert.ok(!html.includes('assets/mixer-builtin-eq.js'), 'V28 source HTML stays byte-frozen');
assert.ok(component.includes('data-action="openBuiltinEq"'));
assert.ok(component.includes('window.ArSonMixBuiltinEq?.open(t)'));
assert.match(native,/__ARMIX_BUILTIN_EQ_CHANNEL__/);
assert.match(js,/arMixBuiltinEqAck/);
assert.match(js,/arMixBuiltinEqSnapshot/);
assert.match(js,/DESIGN DEMO · NO AUDIO/);
assert.match(js,/snapshotReady/);
assert.ok(!['AudioContext','webkitAudioContext','createScriptProcessor','new Worker('].some(s=>js.includes(s)), 'Browser EQ must not own audio callback');
assert.ok(css.includes('#mxBuiltinEq[hidden]'));
console.log('Built-in Channel EQ GUI contract: PASS (isolated from VST3 and alt mixer)');

assert.ok(!html.includes('assets/mixer-builtin-eq.css'), 'No second CSS cascade owner');
