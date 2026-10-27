// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const gui=readFileSync('web/monitor.js','utf8');
test('Capture evidence is neither render-xrun proof nor auto-reconfiguration',()=>{
 assert.match(gui,/WASAPI_CAPTURE_DISCONTINUITIES_ONLY/);
 assert.match(gui,/Number\(d\?\.chainCount\)===0/);
 assert.match(gui,/Number\(d\?\.overBudgetCount\)===0/);
 assert.match(gui,/This is not proof of render underruns/);
 assert.match(gui,/TRY INDEPENDENT CLOCK \(BETA\)/);
 assert.match(gui,/addEventListener\("click",\(\)=>\s*api\.emitEvent\("arMixAction",\{action:"smartClock"/);
 // The new detector only provides an explicit UI choice.
 assert.doesNotMatch(gui,/captureFaultEvidence\(d\)\s*\&\&\s*api\.emitEvent/);
});
