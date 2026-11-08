import test from 'node:test';
import assert from 'node:assert/strict';
import { parseReleaseCommand, trustedReleaseComment, releaseDispatch } from '../scripts/release_ci_dispatch.mjs';

const SHA='a'.repeat(40),TAG='v0.5.0-alpha.2',BRANCH='releases/alpha/'+TAG;
const ev=(body='/arsonmix-release '+TAG+' '+SHA)=>({
  repository:{full_name:'masarray/arsonmix'},issue:{number:450},
  comment:{id:100,body,author_association:'OWNER',user:{login:'masarray'}}
});
function fakeApi({refSha=SHA,comments=[],runs=[],released=false}={}) {
  const calls=[];
  const api=async(path,method='GET',data)=>{
    calls.push({path,method,data});
    if(path==='/git/ref/heads/'+BRANCH)
      return {ref:'refs/heads/'+BRANCH,object:{sha:refSha}};
    if(path.startsWith('/issues/450/comments'))return comments;
    if(path==='/releases/tags/'+TAG){
      if(released)return {tag_name:TAG};
      throw Error('GITHUB_API_404');
    }
    if(path.includes('/runs?event=workflow_dispatch'))return {workflow_runs:runs};
    if(path.endsWith('/dispatches'))return null;
    throw Error('unexpected API path '+path);
  };
  return {calls,api};
}
test('Only exact owner issue comment is recognized',()=>{
  assert.deepEqual(parseReleaseCommand('/arsonmix-release '+TAG+' '+SHA),{
    tag:TAG,sha:SHA,branch:BRANCH});
  for(const malformed of ['/arsonmix-release '+TAG+' '+SHA+'\nextra',
                           '/arsonmix-release beta '+TAG+' '+SHA,
                           '/arsonmix-release '+TAG+' '+'x'.repeat(40),
                           '/arsonmix-release v0.5.0-alpha.3 '+SHA+' junk'])
    assert.equal(parseReleaseCommand(malformed),null);
  assert.equal(trustedReleaseComment(ev()),true);
  for(const patch of [
    {comment:{id:10,body:'x',author_association:'MEMBER',user:{login:'masarray'}}},
    {issue:{number:449}},
    {issue:{number:450,pull_request:{}}},
    {repository:{full_name:'attacker/fork'}}
  ])assert.equal(trustedReleaseComment({...ev(),...patch}),false);
});
test('One approved comment schedules one exact SHA build',async()=>{
  const x=fakeApi(),out=await releaseDispatch({event:ev(),api:x.api});
  assert.equal(out.kind,'dispatched');
  const dispatch=x.calls.filter(c=>c.method==='POST');
  assert.equal(dispatch.length,1);
  assert.equal(dispatch[0].path,'/actions/workflows/alpha-preview-publish.yml/dispatches');
  assert.deepEqual(dispatch[0].data,{ref:BRANCH,inputs:{release_tag:TAG,expected_sha:SHA}});
});
test('Stale SHA rejects without costly workflow dispatch',async()=>{
  const x=fakeApi({refSha:'b'.repeat(40)});
  await assert.rejects(releaseDispatch({event:ev(),api:x.api}),/RELEASE_STALE_SHA/);
  assert.equal(x.calls.filter(c=>c.method==='POST').length,0);
});
test('Duplicate owner comment and already-published tag are no-op',async()=>{
  const comments=[{id:88,body:ev().comment.body,user:{login:'masarray'},author_association:'OWNER'}];
  for(const input of [{comments},{released:true}]){
    const x=fakeApi(input);
    const out=await releaseDispatch({event:ev(),api:x.api});
    assert.equal(out.kind,'skipped');
    assert.equal(x.calls.some(c=>c.method==='POST'),false);
  }
});
test('Already running/successful does not run twice; failed same SHA requires source fix',async()=>{
  for(const entry of [
    {status:'in_progress',conclusion:null},
    {status:'completed',conclusion:'success'}
  ]){
    const x=fakeApi({runs:[{...entry,head_sha:SHA}]});
    assert.equal((await releaseDispatch({event:ev(),api:x.api})).kind,'skipped');
    assert.equal(x.calls.some(c=>c.method==='POST'),false);
  }
  const x=fakeApi({runs:[{status:'completed',conclusion:'failure',head_sha:SHA}]});
  await assert.rejects(releaseDispatch({event:ev(),api:x.api}),/RELEASE_FAILED_SAME_SHA/);
  assert.equal(x.calls.some(c=>c.method==='POST'),false);
});
