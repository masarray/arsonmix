// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {POLICY_EPOCH_PR, ownerOf, metadata, audit} from '../scripts/check_workstream_coordination.mjs';

const body=(lane,issue=397,parked=false)=>
  '<!-- arsonmix-workstream: '+lane+' -->\n'+
  '<!-- arsonmix-issue: #'+issue+' -->'+
  (parked?'\n<!-- arsonmix-coordination: parked -->':'');
const pr=(number,lane,branch,parked=false)=>({
  number,state:'open',head:{ref:branch},body:body(lane,397,parked)
});
const check=(current,files,others=[],filesByPR={})=>
  audit({pr:current,files,others,filesByPR});

test('only one approved GUI has GUI ownership; no accidental native source authority',()=>{
  for(const path of [
    'site/preview/index.html', 'site/preview-v2/index.html',
    'site/preview/assets/mute-solo-pfl-56x37.png',
    'web/mixer-layout.js','web/styles.css','scripts/build_pages.py'
  ]) assert.equal(ownerOf(path),'GUI',path);
  for(const path of [
    'engine/core/DynamicPreparedMixerBlock.h','src/LiveAudioEngine.h',
    'src/LiveDryRoutingLab.h','src/ConsoleLiveBridgeV6.h',
    'src/ApprovedNativeV8Projection.h'
  ]) assert.equal(ownerOf(path),'ENGINE',path);
  for(const path of [
    'src/Main.cpp','CMakeLists.txt','web/approved-native-bridge.js',
    'scripts/build_approved_native_gui.py','.github/workflows/ui-smoke.yml',
    'AGENTS.md','integration/console-v6/contract.json'
  ]) assert.equal(ownerOf(path),'INTEGRATOR',path);
  for(const path of ['scripts/package_native_windows.ps1',
    'scripts/verify_native_windows_artifact.ps1'])
    assert.equal(ownerOf(path),'FAT',path);
  assert.equal(ownerOf('README.md'),null);
});

test('new GUI PR may edit approved source but cannot change native CMake or PCM',()=>{
  assert.equal(check(pr(401,'GUI','gui/fader'),
    ['site/preview/index.html','site/preview/assets/meter.css']).ok,true);
  const crossed=check(pr(401,'GUI','gui/fader'),
    ['site/preview/index.html','CMakeLists.txt','src/LiveAudioEngine.h']);
  assert.equal(crossed.ok,false);
  assert.match(crossed.errors.join(' '),/CROSS-OWNER CMakeLists\.txt/);
  assert.match(crossed.errors.join(' '),/CROSS-OWNER src\/LiveAudioEngine\.h/);
});
test('new ENGINE PR cannot quietly replace JUCE native GUI bridge',()=>{
  const bad=check(pr(402,'ENGINE','engine/route'),
    ['engine/core/DirectedGraph.h','src/Main.cpp','web/approved-native-bridge.js']);
  assert.equal(bad.ok,false);
  assert.match(bad.errors.join(' '),/src\/Main.cpp/);
  assert.match(bad.errors.join(' '),/web\/approved-native-bridge\.js/);
});
test('FAT owns independent packaging tests, not production audio source',()=>{
  const good=check(pr(403,'FAT','fat/hardware'),[
    'scripts/package_native_windows.ps1','docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md'
  ]);
  assert.equal(good.ok,true);
  assert.equal(check(pr(403,'FAT','fat/hardware'),['src/LiveAudioEngine.h']).ok,false);
});
test('new PR must explicitly declare role and linked issue',()=>{
  const candidate={number:405,head:{ref:'gui/new'},body:''};
  const result=check(candidate,['site/preview/index.html']);
  assert.equal(result.ok,false);
  assert.match(result.errors.join('\n'),/arsonmix-workstream/);
  assert.match(result.errors.join('\n'),/arsonmix-issue/);
  assert.equal(metadata(body('INTEGRATOR')).owner,'INTEGRATOR');
});
test('same-owned-file overlapping LIVE PR is rejected, never two owners',()=>{
  const candidate=pr(405,'GUI','gui/fader');
  const peer=pr(399,'GUI','gui/approved');
  const result=check(candidate,['site/preview/index.html'],[peer],
    {'399':['site/preview/index.html','site/preview/assets/fx-rack.css']});
  assert.equal(result.ok,false);
  assert.match(result.errors.join('\n'),/ACTIVE COLLISION with PR #399/);
});
test('parked draft becomes nonblocking; unique changes can be reconciled later',()=>{
  const candidate=pr(405,'GUI','gui/fader');
  const peer=pr(393,'GUI','gui/one-approved-preview',true);
  assert.equal(check(candidate,['site/preview/index.html'],[peer],
    {'393':['site/preview/index.html']}).ok,true);
});
test('legacy cross-owner edits are warned but cannot block rightful integrator',()=>{
  const candidate=pr(405,'INTEGRATOR','integration/native');
  const stale=pr(266,'ENGINE','milestone/m5-universal-channelid',false);
  const result=check(candidate,['src/Main.cpp'],[stale],
    {'266':['src/Main.cpp','src/LiveAudioEngine.h']});
  assert.equal(result.ok,true);
  assert.match(result.warnings.join(' '),/outside its declared file owner/);
});
test('active same-owner native bridge collisions ARE blockers',()=>{
  const candidate=pr(405,'INTEGRATOR','integration/native');
  const current=pr(409,'INTEGRATOR','integration/bridge');
  const result=check(candidate,['src/Main.cpp'],[current],{'409':['src/Main.cpp']});
  assert.equal(result.ok,false);
  assert.equal(result.activeCollisions,1);
});
test('pre-policy PRs remain report-only during migration, not retroactively blocked',()=>{
  const old={number:POLICY_EPOCH_PR,draft:true,head:{ref:'integration/old'},body:''};
  const result=check(old,['src/LiveAudioEngine.h']);
  assert.equal(result.ok,true);
  assert.equal(result.legacy,true);
  assert.match(result.warnings.join(' '),/LEGACY migration/);
});
test('legacy PR becomes strictly checked on ready-for-review promotion',()=>{
  const old={number:POLICY_EPOCH_PR,draft:false,head:{ref:'integration/old'},body:''};
  const result=check(old,['src/LiveAudioEngine.h']);
  assert.equal(result.ok,false);
  assert.match(result.errors.join(' '),/arsonmix-workstream/);
});
test('same-owner shared contract/test files cannot be edited in parallel',()=>{
  const a=pr(405,'ENGINE','engine/route');
  const b=pr(408,'ENGINE','engine/latency');
  const x=check(a,['tests/engine_contract_test.mjs'],[b],
    {'408':['tests/engine_contract_test.mjs']});
  assert.equal(x.ok,false);
  assert.match(x.errors.join(' '),/ACTIVE COLLISION/);
});
test('coordinator uses existing native + Chromium CI, not a fifth workflow',()=>{
  const native=readFileSync('.github/workflows/p1-windows.yml','utf8');
  const ui=readFileSync('.github/workflows/ui-smoke.yml','utf8');
  for(const text of [native,ui]){
    assert.match(text,/scripts\/check_workstream_coordination\.mjs --pr/);
    assert.match(text,/pull-requests: read/);
    assert.match(text,/ready_for_review/);
    assert.match(text,/GH_TOKEN: \$\{\{ github\.token \}\}/);
  }
  const guide=readFileSync('AGENTS.md','utf8');
  assert.match(guide,/MULTI_AGENT_COORDINATION\.md/);
  assert.match(guide,/#397/);
});
