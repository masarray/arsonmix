// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {parseMilestoneCommand,isTrustedOwnerComment,validateMilestone,
  hadEarlierIdenticalRequest,hasPriorDispatch,quickResult,performMilestone}
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
    ref:'integration/daw-direct-hardware-input-20261105',
    inputs:{expected_sha:sha,package_fat:true}
  });
  const failed=async(path,method,data)=>path.includes('/actions/runs/7/jobs')?
    {jobs:[{name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'failure'}]}:
    api(path,method,data);
  await assert.rejects(performMilestone({event:event(),repo,api:failed}),/CI_QUICK_FAILED/);
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
