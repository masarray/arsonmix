// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {parseMilestoneCommand,isTrustedOwnerComment,validateMilestone,
  hadEarlierIdenticalRequest,hasPriorDispatch,quickResult,performMilestone,
  focusedHarmonyFileScope,parseWindowsMilestoneRun,guardWindowsMilestone}
  from '../scripts/chat_ci_dispatch.mjs';

const sha='a'.repeat(40),repo='masarray/arsonmix';
const event=(body='/arsonmix-ci fat '+sha)=>({
  repository:{full_name:repo},
  issue:{number:418,pull_request:{url:'https://api.github.com/repos/masarray/arsonmix/pulls/418'}},
  comment:{id:48,body,author_association:'OWNER',user:{login:'masarray'}}
});
const pr=(head=sha)=>({
  number:418,state:'open',base:{ref:'main',repo:{full_name:repo}},
  head:{ref:'integration/daw-direct-hardware-input-20261105',sha:head,repo:{full_name:repo}}
});

test('owner command parser requires a single bounded command and lowercase SHA',()=>{
  for(const lane of ['ui','native','fat'])
    assert.deepEqual(parseMilestoneCommand('/arsonmix-ci '+lane+' '+sha),{lane,sha});
  for(const bad of ['/arsonmix-ci all '+sha,'/arsonmix-ci fat main',
    '/arsonmix-ci fat '+sha+'; rm -rf .','/arsonmix-ci fat '+sha+'\necho unsafe',
    '/arsonmix-ci fat '+sha.toUpperCase(),'/arsonmix-ci FAT '+sha])
    assert.equal(parseMilestoneCommand(bad),null);
});

test('authorization only allows same-repo OWNER comment and current PR HEAD',()=>{
  assert.equal(isTrustedOwnerComment(event(),repo),true);
  const other=event();other.comment.user.login='intruder';
  assert.equal(isTrustedOwnerComment(other,repo),false);
  other.comment.user.login='masarray';other.comment.author_association='NONE';
  assert.equal(isTrustedOwnerComment(other,repo),false);
  const target=validateMilestone(event(),repo,pr());
  assert.deepEqual([target.workflow,target.lane,target.sha],['p1-windows.yml','fat',sha]);
  assert.equal(validateMilestone(event('/arsonmix-ci ui '+sha),repo,pr()).workflow,'ui-smoke.yml');
  assert.throws(()=>validateMilestone(event(),repo,pr('b'.repeat(40))),/CI_STALE_SHA/);
  const fork=pr();fork.head.repo.full_name='untrusted/fork';
  assert.throws(()=>validateMilestone(event(),repo,fork),/CI_PR_INVALID/);
});

test('duplicate milestone comments and previously dispatched jobs deduplicate',()=>{
  const comment=(id,lane,user='masarray')=>({id,body:'/arsonmix-ci '+lane+' '+sha,
    author_association:user==='masarray'?'OWNER':'NONE',user:{login:user}});
  assert.equal(hadEarlierIdenticalRequest([comment(47,'fat')],event(),repo),true);
  assert.equal(hadEarlierIdenticalRequest([comment(47,'ui')],event(),repo),false);
  assert.equal(hadEarlierIdenticalRequest([comment(47,'fat','untrusted')],event(),repo),false);
  assert.equal(hasPriorDispatch([{event:'workflow_dispatch',head_sha:sha,status:'in_progress'}],sha),true);
  assert.equal(hasPriorDispatch([{event:'workflow_dispatch',head_sha:sha,status:'completed',conclusion:'success'}],sha),true);
  assert.equal(hasPriorDispatch([{event:'workflow_dispatch',head_sha:sha,status:'completed',conclusion:'failure'}],sha),false);
  assert.equal(hasPriorDispatch([{event:'pull_request',head_sha:sha,status:'completed',conclusion:'success'}],sha),false);
});

test('exact SHA and FAT are dispatched only when lightweight checks pass',async()=>{
  assert.equal(quickResult([{name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'failure'}],'fat'),'failure');
  const calls=[];
  const api=async(path,method,data)=>{
    calls.push({path,method,data});
    if(path.startsWith('/pulls/'))return pr();
    if(path.startsWith('/issues/'))return [];
    if(path.includes('/runs?event=pull_request'))return {workflow_runs:[{id:7,head_sha:sha,event:'pull_request'}]};
    if(path.includes('/actions/runs/7/jobs'))return {jobs:[{name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'success'}]};
    if(path.endsWith('/dispatches'))return null;
    return {workflow_runs:[]};
  };
  const got=await performMilestone({event:event(),repo,api});
  assert.equal(got.kind,'dispatched');
  assert.deepEqual(calls.at(-1).data,{
    ref:'main',
    inputs:{expected_sha:sha,package_fat:true,request_pr_number:'418'}
  });
  const failed=async(path,method,data)=>path.includes('/actions/runs/7/jobs')?
    {jobs:[{name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'failure'}]}:
    api(path,method,data);
  await assert.rejects(performMilestone({event:event(),repo,api:failed}),/CI_QUICK_FAILED/);
});

test('file-scoped Harmonice FAT is opt-in only with complete, isolated PR file proof',async()=>{
  const files=[
    'engine/builtin_fx/HarmonyDsp.h',
    'engine/builtin_fx/HarmonyPitchSynchronous.h',
    'site/preview-v2/assets/mixer-native-harmony.js',
    'site/preview-v2/assets/mixer-fx-rack.css',
    'tests/harmony_dsp_test.cpp',
    'tools/harmony_offline_ab.cpp',
    '.github/workflows/harmony-fast-dsp.yml'
  ];
  assert.equal(focusedHarmonyFileScope(files),true);
  for(const unsafe of ['src/Main.cpp','src/LiveAudioEngine.h',
    'engine/device/AsioStartupPolicy.h',
    '.github/workflows/p1-windows.yml','installer/ARsonMix.iss']){
    assert.equal(focusedHarmonyFileScope([...files,unsafe]),false,
      'Unexpected bypass of full MSVC test gate: '+unsafe);
  }
  assert.equal(focusedHarmonyFileScope([]),false);
  assert.equal(focusedHarmonyFileScope(['README.md']),false);
  const harmonyPr=pr();
  harmonyPr.head.ref='integration/harmonice-lead-ensemble-20261111';
  const calls=[];
  const makeApi=changed=>async(path,method,data)=>{
    if(path.startsWith('/pulls/418/files?'))return changed.map(filename=>({filename}));
    if(path.startsWith('/pulls/'))return harmonyPr;
    if(path.startsWith('/issues/'))return [];
    if(path.includes('/runs?event=pull_request'))
      return {workflow_runs:[{id:9,head_sha:sha,event:'pull_request'}]};
    if(path.includes('/actions/runs/9/jobs'))
      return {jobs:[{name:'Fast native contracts (no Windows / JUCE checkout)',
        conclusion:'success'}]};
    if(path.endsWith('/dispatches')){calls.push(data);return null;}
    return {workflow_runs:[]};
  };
  const eligible=await performMilestone({
    event:event(),repo,api:makeApi(files)
  });
  assert.equal(eligible.kind,'dispatched');
  assert.deepEqual(calls.at(-1),{
    // The CI definition comes from main; the source still comes from SHA.
    ref:'main',
    inputs:{expected_sha:sha,package_fat:true,
      request_pr_number:'418',focused_harmony_fat:true}
  });
  await performMilestone({
    event:event(),repo,api:makeApi([...files,'src/Main.cpp'])
  });
  assert.deepEqual(calls.at(-1),{
    ref:'main',inputs:{expected_sha:sha,package_fat:true,request_pr_number:'418'}
  });
  // The protection fails closed on unknown or truncated PR file listings.
  const broken=async(path,...args)=>{
    if(path.startsWith('/pulls/418/files?'))return null;
    return makeApi(files)(path,...args);
  };
  await assert.rejects(performMilestone({event:event(),repo,api:broken}),
    /CI_FILES_INVALID/);
});

test('dispatcher stays on main with read-only checkout; both lanes pin SHA',()=>{
  const dispatch=readFileSync('.github/workflows/chat-ci-milestone.yml','utf8');
  const windows=readFileSync('.github/workflows/p1-windows.yml','utf8');
  const ui=readFileSync('.github/workflows/ui-smoke.yml','utf8');
  assert.match(dispatch,/issue_comment:\s*\n\s*types: \[created\]/);
  assert.match(dispatch,/actions: write/);
  assert.match(dispatch,/author_association == 'OWNER'/);
  assert.match(dispatch,/ref: \$\{\{ github\.event\.repository\.default_branch \}\}/);
  assert.match(dispatch,/persist-credentials: false/);
  for(const wf of [windows,ui]){
    assert.match(wf,/expected_sha:/);
    assert.match(wf,/inputs\.expected_sha/);
    assert.match(wf,/needs: quick/);
  }
  assert.match(windows,/FAT_EXACT_PR_HEAD_CHECKOUT/);
  assert.match(windows,/ctest --test-dir build -C Release --output-on-failure/);
  assert.match(windows,/github\.event_name == 'workflow_dispatch' && inputs\.package_fat == true/);
});


const windowsRun=(id,lane,number,source,status='in_progress',conclusion=null)=>({
  id,event:'workflow_dispatch',
  display_title:'ArSonMix '+lane.toUpperCase()+' PR'+number+' '+source,
  status,conclusion,head_branch:'main',head_sha:'c'.repeat(40)
});
const checkedApi=(runs,calls)=>async(path,method,data)=>{
  calls.push({path,method:method||'GET',data});
  if(path.startsWith('/pulls/418'))return pr();
  if(path.startsWith('/issues/418/comments'))return [];
  if(path.includes('event=pull_request'))return {
    workflow_runs:[{id:7,head_sha:sha,event:'pull_request'}]
  };
  if(path==='/actions/runs/7/jobs?per_page=100')
    return {jobs:[{name:'Fast native contracts (no Windows / JUCE checkout',
                   conclusion:'success'}]};
  if(path.includes('event=workflow_dispatch'))return {workflow_runs:runs};
  if(path.endsWith('/dispatches')||path.endsWith('/cancel'))return null;
  throw Error('Unexpected mocked endpoint: '+path);
};

test('Windows milestone identity uses verified PR and checked-out source SHA, not workflow-ref SHA',()=>{
  assert.deepEqual(parseWindowsMilestoneRun(windowsRun(1,'fat',418,sha)),
    {lane:'fat',number:418,sha});
  assert.equal(parseWindowsMilestoneRun({
    ...windowsRun(1,'native',418,sha),display_title:'ArSonMix Windows Native VST3 Host'
  }),null);
  assert.equal(parseWindowsMilestoneRun({
    ...windowsRun(1,'fat',418,sha),event:'push'
  }),null);
  assert.equal(parseWindowsMilestoneRun({
    ...windowsRun(1,'fat',418,sha),display_title:'ArSonMix FAT PR418 main'
  }),null);
});

test('FAT promotion cancels only obsolete same-PR native worker before one immutable FAT',async()=>{
  const calls=[];
  const api=checkedApi([
    windowsRun(100,'native',418,sha,'in_progress'),
    windowsRun(200,'native',419,'d'.repeat(40),'in_progress'),
    windowsRun(99,'native',418,'e'.repeat(40),'completed','success')
  ],calls);
  const result=await performMilestone({event:event(),repo,api});
  assert.equal(result.kind,'dispatched');
  const writes=calls.filter(c=>c.method==='POST');
  assert.deepEqual(writes.map(c=>c.path),[
    '/actions/runs/100/cancel',
    '/actions/workflows/p1-windows.yml/dispatches'
  ]);
  assert.deepEqual(writes.at(-1).data,{
    ref:'main',inputs:{expected_sha:sha,package_fat:true,request_pr_number:'418'}
  });
});

test('repeat FAT, already-running FAT and completed FAT source are never rebuilt',async()=>{
  for(const state of [
    windowsRun(100,'fat',418,sha,'in_progress'),
    windowsRun(100,'fat',418,sha,'completed','success'),
    windowsRun(100,'fat',418,'b'.repeat(40),'queued')
  ]) {
    const calls=[];
    const result=await performMilestone({event:event(),repo,
      api:checkedApi([state],calls)});
    assert.equal(result.kind,'skipped');
    assert.equal(calls.some(c=>c.method==='POST'),false);
  }
  const calls=[];
  await assert.rejects(
    performMilestone({event:event(),repo,api:checkedApi([
      windowsRun(100,'fat',418,sha,'completed','failure')
    ],calls)}),
    /CI_FAT_FAILED_FIX_SOURCE_BEFORE_REDISPATCH/
  );
  assert.equal(calls.some(c=>c.method==='POST'),false);
});

test('native request never duplicates a same-source native or already-running FAT',async()=>{
  const source=event('/arsonmix-ci native '+sha);
  for(const lane of ['native','fat']) {
    const calls=[];
    const result=await performMilestone({event:source,repo,
      api:checkedApi([windowsRun(901,lane,418,sha,'in_progress')],calls)});
    assert.equal(result.kind,'skipped');
    assert.equal(calls.some(c=>c.method==='POST'),false);
  }
});

test('CI workflow preserves FAT while deduplicating cheaper native builds and upload compression',()=>{
  const wf=readFileSync('.github/workflows/p1-windows.yml','utf8');
  assert.match(wf,/run-name: "ArSonMix/);
  assert.match(wf,/request_pr_number:/);
  assert.match(wf,/inputs\.package_fat && github\.run_id/);
  assert.match(wf,/cancel-in-progress: .*?!inputs\.package_fat/);
  assert.equal(wf.match(/compression-level: 0/g)?.length,2);
  assert.match(wf,/Build executable and native RT tests/);
  assert.match(wf,/ctest --test-dir build -C Release --output-on-failure/);
  assert.match(wf,/build_verify_windows_installer\.ps1/);
});
