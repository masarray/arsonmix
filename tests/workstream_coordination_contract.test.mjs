// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {POLICY_EPOCH_PR, ownerOf, metadata, audit, requiresUiSmoke, requiresNativeQualification} from '../scripts/check_workstream_coordination.mjs';

const body=(lane,issue=397,parked=false)=>
  '<!-- arsonmix-workstream: '+lane+' -->\n'+
  '<!-- arsonmix-issue: #'+issue+' -->'+
  (parked?'\n<!-- arsonmix-coordination: parked -->':'');
const pr=(number,lane,branch,parked=false)=>({
  number,state:'open',head:{ref:branch},body:body(lane,397,parked)
});
const check=(current,files,others=[],filesByPR={})=>
  audit({pr:current,files,others,filesByPR});

test('single-owner product recovery removes blocking lane and PR collisions but guards approved GUI',()=>{
  const current={number:420,state:'open',draft:false,
    head:{ref:'integration/first-sound-native-g0-patch-20261105'},
    body:'<!-- arsonmix-integration: single-owner -->'};
  const changed=['src/LiveAudioEngine.h','src/Main.cpp',
    'integration/ApprovedNativeRuntimeSnapshot.h','AGENTS.md'];
  const result=audit({pr:current,files:changed,
    others:[pr(421,'ENGINE','engine/competing'),pr(422,'INTEGRATOR','integration/competing')],
    filesByPR:{421:['src/LiveAudioEngine.h'],422:['src/Main.cpp']}});
  assert.equal(result.ok,true,'one integration PR is not blocked by inactive lane approvals');
  assert.equal(result.lane,'PRODUCT');
  assert.equal(result.activeCollisions,0);
  assert.equal(audit({pr:current,files:['web/console-native.html']}).ok,false,
    'the forbidden alternate GUI remains a release blocker');
  assert.equal(audit({pr:current,files:['web/console-native.html'],
    fileStatuses:{'web/console-native.html':'removed'}}).ok,true,
    'removing an obsolete visual variant is allowed');
});
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

test('release-admission scripts belong to INTEGRATOR, not unclassified shared code',()=>{
  for(const path of [
    'scripts/check_console_puzzle.mjs',
    'scripts/check_approved_native_gui_release.mjs',
    'scripts/check_pr278_console_authority.mjs'
  ]) {
    assert.equal(ownerOf(path),'INTEGRATOR',path);
    for(const lane of ['GUI','ENGINE','FAT']) {
      const result=check(pr(405,lane,lane.toLowerCase()+'/gate'),[path]);
      assert.equal(result.ok,false,lane+' wrongly edited '+path);
      assert.match(result.errors.join(' '),/CROSS-OWNER/);
    }
  }
});
test('hardware FAT evidence remains exclusively independent FAT owned',()=>{
  for(const path of [
    'scripts/collect_hardware_fat_windows.ps1',
    'scripts/console_v6_fat_gate.mjs',
    'scripts/run_console_native_s0.mjs',
    'scripts/verify_console_s0_native_fixture.mjs'
  ]) {
    assert.equal(ownerOf(path),'FAT',path);
    assert.equal(check(pr(406,'INTEGRATOR','coord/release'),[path]).ok,false);
    assert.equal(check(pr(406,'FAT','fat/evidence'),[path]).ok,true);
  }
});
test('different release-admission script filenames still collide in one runtime domain',()=>{
  const owner=pr(410,'INTEGRATOR','coord/release');
  const peer=pr(411,'INTEGRATOR','integration/admission');
  const result=check(owner,['scripts/check_approved_native_gui_release.mjs'],
    [peer],{'411':['scripts/check_console_puzzle.mjs']});
  assert.equal(result.ok,false);
  assert.match(result.errors.join(' '),/ACTIVE DOMAIN COLLISION.*release-admission/);
  const previousGate=pr(412,'INTEGRATOR','coord/old-gate');
  const differentScript=check(owner,['scripts/check_pr278_console_authority.mjs'],
    [previousGate],{'412':['scripts/check_console_puzzle.mjs']});
  assert.equal(differentScript.ok,false);
  assert.match(differentScript.errors.join(' '),/ACTIVE DOMAIN COLLISION.*release-admission/);
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
test('new ENGINE branch cannot duplicate native PCM using DIFFERENT filenames',()=>{
  const owner=pr(266,'ENGINE','milestone/m5-native');
  const newcomer=pr(420,'ENGINE','engine/split-work');
  const result=check(newcomer,['engine/core/BrandNewCallback.h'],[owner],
    {'266':['src/LiveAudioEngine.h','engine/core/DynamicAcceptedSession.h']});
  assert.equal(result.ok,false);
  assert.match(result.errors.join(' '),/ACTIVE DOMAIN COLLISION.*engine-pcm/);
});
test('new bridge PR cannot fork JS state into another filename',()=>{
  const newBridge=pr(421,'INTEGRATOR','integration/new-bridge');
  const existing=pr(396,'INTEGRATOR','integration/m3-gestures');
  const result=check(newBridge,['web/approved-native-m2-state.js'],[existing],
    {'396':['web/approved-native-bridge.js']});
  assert.equal(result.ok,false);
  assert.match(result.errors.join(' '),/ACTIVE DOMAIN COLLISION.*approved-native-bridge/);
});
test('policy and native host stay separate domains despite same INTEGRATOR owner',()=>{
  const newcomer=pr(422,'INTEGRATOR','coord/governance');
  const owner=pr(396,'INTEGRATOR','integration/gestures');
  const result=check(newcomer,['.github/workflows/p1-windows.yml'],[owner],
    {'396':['web/approved-native-bridge.js']});
  assert.equal(result.ok,true);
});
test('new alternate GUI is blocked but retiring old renderer is permitted',()=>{
  const author=pr(423,'GUI','gui/cleanup');
  assert.equal(check(author,['web/console-native.html']).ok,false);
  assert.equal(audit({pr:author,files:['web/console-native.html'],
    fileStatuses:{'web/console-native.html':'removed'}}).ok,true);
});
test('existing #396/#398 remain advisory, first strict PR is #400',()=>{
  assert.equal(POLICY_EPOCH_PR,399);
  assert.equal(check({...pr(398,'INTEGRATOR','integration/old'),draft:true},
    ['src/LiveAudioEngine.h']).ok,true);
  assert.equal(check(pr(400,'INTEGRATOR','integration/new'),
    ['src/LiveAudioEngine.h']).ok,false);
});

test('Coordination admission is historical only; native and UI runners have independent path scopes',()=>{
 const native=readFileSync('.github/workflows/p1-windows.yml','utf8');
 const ui=readFileSync('.github/workflows/ui-smoke.yml','utf8');
 for(const workflow of [native,ui]) {
   assert.match(workflow,/^  pull_request:/m);
   assert.match(workflow,/^    paths:/m);
   assert.doesNotMatch(workflow,/check_workstream_coordination\.mjs --pr/);
 }
 assert.doesNotMatch(native,/workflow_call:/);
 assert.doesNotMatch(ui,/name: PR coordination admission|name: Product Qualification/);
});
test('Draft commits cannot start full Chromium, but matching changes still run quick checks',()=>{
 const workflow=readFileSync('.github/workflows/ui-smoke.yml','utf8');
 assert.match(workflow,/^  quick:/m);
 assert.match(workflow,/^  frontend:/m);
 assert.match(workflow,/github\.event\.pull_request\.draft == false/);
 assert.match(workflow,/ready_for_review, converted_to_draft/);
 assert.match(workflow,/^    paths:/m);
 assert.doesNotMatch(workflow,/needs: coordination/);
});
test('P0 native qualification is precise: native ABI/release scripts yes, docs/GUI no',()=>{
  for(const path of [
    'src/LiveAudioEngine.h','engine/core/PreparedMixerBlock.h',
    'integration/ApprovedNativeRuntimeSnapshot.h',
    'integration/ApprovedNativeControlIntent.h',
    'integration/Deep/NativeBridge.cpp',
    'integration/console-v6/m4-operator-routing.mjs',
    'scripts/check_console_puzzle.mjs',
    'scripts/check_approved_native_gui_release.mjs',
    'scripts/package_native_windows.ps1',
    'tests/approved_native_v8_projection_test.cpp',
    '.github/workflows/p1-windows.yml'
  ]) assert.equal(requiresNativeQualification([path]),true,path);
  for(const path of [
    'README.md','docs/QUALITY.md','site/preview/index.html',
    'web/mixer-layout.js','tests/pages_wire_first_canonical_main_smoke.py',
    'tests/ci_workflow_efficiency_test.mjs',
    'integration/m4_operator_status.md',
    'integration/other_frontend.mjs'
  ]) assert.equal(requiresNativeQualification([path]),false,path);
  assert.equal(requiresNativeQualification(['README.md','integration/ApprovedNativeV8Projection.h']),true);
});

test('Production native CTest remains independent and after lightweight acceptance',()=>{
 const ui=readFileSync('.github/workflows/ui-smoke.yml','utf8');
 const windows=readFileSync('.github/workflows/p1-windows.yml','utf8');
 assert.doesNotMatch(ui,/Product Qualification|uses: \.\/\.github\/workflows\/p1-windows\.yml/);
 assert.match(windows,/needs: quick/);
 assert.match(windows,/^  pull_request:/m);
 assert.match(windows,/ctest --test-dir build -C Release --output-on-failure/);
 assert.match(windows,/package_fat/);
});
test('P0 Windows checkout CRLF cannot suppress the PR event in coordination test',()=>{
  const workflow=readFileSync('.github/workflows/ui-smoke.yml','utf8').replace(/\r\n/g,'\n');
  const windowsCheckout=workflow.replace(/\n/g,'\r\n');
  const normalized=windowsCheckout.replace(/\r\n/g,'\n');
  const prBlock=normalized.split('\n  pull_request:\n')[1]?.split('\n  # Manual full suite')[0];
  assert.ok(prBlock, 'CRLF normalization must keep real pull_request trigger');
});
