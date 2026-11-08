import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseReleaseCommand, parsePortCommand, trustedReleaseComment, releaseDispatch, portDispatch } from '../scripts/release_ci_dispatch.mjs';

const SHA='a'.repeat(40),TAG='v0.5.0-alpha.2',BRANCH='releases/alpha/'+TAG;
const ev=(body='/arsonmix-release '+TAG+' '+SHA)=>({
  repository:{full_name:'masarray/arsonmix'},issue:{number:450},
  comment:{id:100,body,author_association:'OWNER',user:{login:'masarray'}}
});
function fakeApi({refSha=SHA,comments=[],runs=[],released=false,notesReady=true,notesSize=200,nativeVersion='0.5.0'}={}) {
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
    if(path==='/contents/docs/releases/'+TAG+'.md?ref='+SHA) {
      if(!notesReady)throw Error('GITHUB_API_404');
      return {type:'file',size:notesSize};
    }
    if(path==='/contents/CMakeLists.txt?ref='+SHA)
      return {type:'file',encoding:'base64',content:Buffer.from(
        'project(ARMixConsole VERSION '+nativeVersion+' LANGUAGES C CXX)'
      ).toString('base64')};
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

test('Missing release notes or wrong native version fails before paid Windows dispatch',async()=>{
  for(const [candidate,expected] of [
    [{notesReady:false},/RELEASE_NOTES_MISSING_PRECHECK/],
    [{notesSize:0},/RELEASE_NOTES_EMPTY_PRECHECK/],
    [{nativeVersion:'0.4.9'},/RELEASE_CMAKE_VERSION_PRECHECK/],
  ]) {
    const gate=fakeApi(candidate);
    await assert.rejects(releaseDispatch({event:ev(),api:gate.api}),expected);
    assert.equal(gate.calls.filter(c=>c.method==='POST').length,0);
  }
});

test('Cross-platform command validates OS, source branch and immutable SHA',async()=>{
  const cmd='/arsonmix-port linux feat/juce-linux-host '+SHA;
  assert.deepEqual(parsePortCommand(cmd),
    {platform:'linux',branch:'feat/juce-linux-host',sha:SHA});
  assert.equal(parsePortCommand('/arsonmix-port macos ../private '+SHA),null);
  assert.equal(parsePortCommand('/arsonmix-port windows main '+SHA),null);
  const event=ev(cmd);
  const calls=[];
  const api=async(path,method='GET',data)=>{
    calls.push({path,method,data});
    if(path==='/git/ref/heads/feat/juce-linux-host')
      return {ref:'refs/heads/feat/juce-linux-host',object:{sha:SHA}};
    if(path.startsWith('/issues/450/comments'))return [];
    if(path.endsWith('/dispatches'))return null;
    throw Error('Unexpected path '+path);
  };
  const r=await portDispatch({event,api});
  assert.equal(r.kind,'dispatched');
  assert.deepEqual(calls.filter(c=>c.method==='POST').map(c=>c.data),[{
    ref:'feat/juce-linux-host',
    inputs:{platform:'linux',source_branch:'feat/juce-linux-host',expected_sha:SHA}
  }]);
  const dup={...event,comment:{...event.comment,id:102}};
  const prev={...event.comment,id:100};
  const noSecond=await portDispatch({event:dup,api:async(path,method)=>{
    if(path.startsWith('/git/ref/'))return {ref:'refs/heads/feat/juce-linux-host',object:{sha:SHA}};
    if(path.includes('/comments?'))return [prev];
    if(method==='POST')throw Error('Unexpected duplicate expensive build');
  }});
  assert.equal(noSecond.kind,'skipped');
});

test('Windows prerelease probes treat missing release as HTTP 404, never a leaked CLI exit 1', () => {
  const yaml=readFileSync(new URL('../.github/workflows/alpha-preview-publish.yml', import.meta.url),'utf8');
  // Recheck both before expensive build and immediately before immutable upload.
  assert.equal((yaml.match(/\$releaseResponse\.StatusCode -ne 404/g)||[]).length,2);
  assert.equal((yaml.match(/-SkipHttpErrorCheck -TimeoutSec 20/g)||[]).length,2);
  assert.equal((yaml.match(/Release existence check failed with HTTP/g)||[]).length,2);
  assert.doesNotMatch(yaml, /gh release view \$tag/);
});
