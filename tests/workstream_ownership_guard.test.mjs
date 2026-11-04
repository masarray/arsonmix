// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {auditWorkstreams,region,STRICT_FROM_PR,POLICY_ISSUE}
  from '../scripts/check_workstream_ownership.mjs';
const pr=(number,files,state='open')=>({number,files,state});
test('M2.5 recognizes ALL existing runtime owner boundaries',()=>{
 assert.equal(POLICY_ISSUE,399);assert.equal(STRICT_FROM_PR,400);
 for(const [file,owner] of [
  ['engine/core/DynamicAcceptedSession.h','engine'],
  ['src/LiveAudioEngine.h','engine'],
  ['src/ConsoleLiveBridgeV6.h','engine'],
  ['src/Main.cpp','host'],
  ['web/approved-native-contract.js','native-gui-bridge'],
  ['web/approved-native-bridge.js','native-gui-bridge'],
  ['scripts/build_approved_native_gui.py','native-gui-bridge'],
  ['site/preview-v2/index.html','approved-gui'],
  ['site/preview/index.html','approved-gui'],
  ['scripts/build_pages.py','approved-gui'],
  ['scripts/check_console_puzzle.mjs','release-admission'],
  ['scripts/collect_hardware_fat_windows.ps1','hardware-fat'],
  ['tests/new_file_test.cpp',null],
  ['docs/workstreams/M2_5_RECONCILIATION.md',null]
 ])assert.equal(region(file),owner,file);
});
test('new PR cannot create second native bridge while #396/#395 still own it',()=>{
 const now=pr(400,['web/approved-native-bridge.js']);
 const open=[pr(396,['web/approved-native-bridge.js']),
             pr(395,['web/approved-native-m2-state.js'])];
 const result=auditWorkstreams(now,open);
 assert.equal(result.enforced,true);
 assert.equal(result.hard.length,2);
 assert.ok(result.hard.every(x=>x.reason==='duplicate-native-gui-bridge'));
});
test('same engine DOMAIN, even distinct files, collides with #266',()=>{
 const out=auditWorkstreams(pr(401,['engine/core/NewAudioCallback.h']),[
  pr(266,['src/LiveAudioEngine.h','engine/core/DynamicAcceptedSession.h']),
  pr(270,['engine/core/PreparedMixerBlock.h'])
 ]);
 assert.deepEqual(out.hard.map(x=>x.other),[266,270]);
});
test('legacy PRs get actionable warnings, not endless CI cancellations',()=>{
 const out=auditWorkstreams(pr(396,['web/approved-native-bridge.js']),[
  pr(395,['web/approved-native-bridge.js'])
 ]);
 assert.equal(out.enforced,false);assert.equal(out.hard.length,1);
});
test('coordinating docs/workflow overlap is review-only, not another engine owner',()=>{
 const out=auditWorkstreams(pr(400,[
  '.github/workflows/p1-windows.yml','AGENTS.md',
  'docs/workstreams/M2_5_RECONCILIATION.md'
 ]),[
  pr(266,['.github/workflows/p1-windows.yml','src/LiveAudioEngine.h']),
  pr(393,['.github/workflows/ui-smoke.yml'])
 ]);
 assert.equal(out.enforced,true);
 assert.equal(out.hard.length,0);
 assert.equal(out.advisory.length,1);
});
test('retired closed PR no longer blocks real native work',()=>{
 const out=auditWorkstreams(pr(401,['web/approved-native-bridge.js']),[
  pr(395,['web/approved-native-bridge.js'],'closed')
 ]);
 assert.equal(out.hard.length,0);
});
test('new alternate console renderer is rejected; deletion is allowed',()=>{
 const alt='web/console-native.html';
 assert.equal(auditWorkstreams(pr(401,[{filename:alt,status:'added'}]),[]).hard.length,1);
 assert.equal(auditWorkstreams(pr(401,[{filename:alt,status:'removed'}]),[]).hard.length,0);
});
test('renamed protected path cannot bypass domain collision',()=>{
 const out=auditWorkstreams(pr(401,['site/preview/index.html']),[
  pr(393,[{filename:'archive/preview-old.html',previous_filename:'site/preview-v2/index.html',
    status:'renamed'}])
 ]);
 assert.equal(out.hard.length,1);
});
