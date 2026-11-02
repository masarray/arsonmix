// Static design-token regression, zero dependencies; complements smoke.test.cjs.
// Run: node design-references/audition-mixer-study/hardware_key_style.test.cjs
const fs=require('node:fs');
const assert=require('node:assert/strict');
const path=require('node:path');
const html=fs.readFileSync(path.join(__dirname,'index.html'),'utf8');
const css=html.split('</style>')[0].split('ARSONMIX GLOBAL HARDWARE KEY V1')[1];
assert.ok(css,'global hardware key token missing');
for(const check of [
 '--am-hw-key-radius:2px',
 '--am-hw-key-upper-glint:linear-gradient(90deg,transparent',
 '--am-hw-key-lower-glint:linear-gradient(90deg,transparent',
 '--am-hw-key-relief:',
 '--am-hw-key-flat:',
 'height:2px!important',
 'height:1px!important',
 'transform:translateY(2px)!important',
 'box-shadow:var(--am-hw-key-flat)!important',
 '.command-bar button.tool',
 '.console-body .strip .switch-section button.switch',
 '.route-choice-option',
 '.console-body .strip .switch-section button.switch.on.mute',
 '.console-body .strip .switch-section button.switch.on.solo',
 '.console-body .strip .switch-section button.switch.on.cue',
 'button.hw-key',
 '@media(prefers-reduced-motion:reduce)'
])assert.ok(css.includes(check),'missing hardware key acceptance token: '+check);
const source=css.split('/* ARSONMIX GLOBAL HARDWARE KEY V1')[0]||'';
assert.ok(!/\.wire-node-out\s*(?:,|\{)/.test(css),'WIRE OUT jack must not be styled as key');
assert.ok(!/\.wire-node-in\s*(?:,|\{)/.test(css),'WIRE IN jack must not be styled as key');
for(const action of ['data-action="mute"','data-action="solo"','data-action="cue"'])
 assert.ok(html.includes(action),'existing MUTE/SOLO/PFL interaction markup removed');
console.log('Hardware Key V1 design token static checks PASS');
