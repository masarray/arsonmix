import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parseReleaseCommand, parsePortCommand, parsePublicPortCommand, trustedReleaseComment, releaseDispatch, portDispatch } from '../scripts/release_ci_dispatch.mjs';

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
test('Windows publisher distinguishes absent tag HTTP 404 from auth/error and does not leak gh exit 1',()=>{
  const workflow=readFileSync(new URL('../.github/workflows/alpha-preview-publish.yml',import.meta.url),'utf8');
  const first=workflow.split('      - name: Verify release identity and immutable source')[1]
    ?.split('      - uses: actions/setup-python@v5')[0]||'';
  assert.match(first,/Invoke-WebRequest -Method Get -Uri \$releaseUri -SkipHttpErrorCheck/);
  assert.match(first,/\$releaseProbe\.StatusCode -eq 200/);
  assert.match(first,/\$releaseProbe\.StatusCode -ne 404/);
  assert.doesNotMatch(first,/& gh release view/);
  const notes=readFileSync(new URL('../docs/releases/v0.5.0-alpha.4.md',import.meta.url),'utf8');
  assert.match(notes,/^# ArSonMix v0\.5\.0-alpha\.4/m);
  assert.match(notes,/Delete Track/);
  assert.match(notes,/Orbit/);
});

test('Published Windows alpha may dispatch exactly one authorized Pages refresh',()=>{
  const release=readFileSync(new URL('../.github/workflows/alpha-preview-publish.yml',import.meta.url),'utf8');
  const pages=readFileSync(new URL('../.github/workflows/pages-publish.yml',import.meta.url),'utf8');
  assert.ok(release.includes('permissions:\n  contents: write\n') && release.includes('  actions: write\n'));
  const first=pages.split('  push:')[1]?.split('  workflow_dispatch:')[0]||'';
  assert.ok(first.includes("'.github/workflows/alpha-preview-publish.yml'"));
  assert.ok(first.includes("'tests/release_ci_dispatch_contract.test.mjs'"));
  const gate=pages.split('while IFS= read -r path; do')[1]?.split('esac')[0]||'';
  assert.ok(gate.includes('.github/workflows/alpha-preview-publish.yml'));
  assert.ok(gate.includes('tests/release_ci_dispatch_contract.test.mjs'));
});

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
    if(path.includes('/runs?event=workflow_dispatch'))return {workflow_runs:[]};
    if(path.endsWith('/dispatches'))return null;
    throw Error('Unexpected path '+path);
  };
  const r=await portDispatch({event,api});
  assert.equal(r.kind,'dispatched');
  assert.deepEqual(calls.filter(c=>c.method==='POST').map(c=>c.data),[{
    ref:'feat/juce-linux-host',
    inputs:{platform:'linux',source_branch:'feat/juce-linux-host',expected_sha:SHA,release_tag:''}
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

test('macOS/Linux public comment is exact-source, owner-only and one OS, never a matrix',async()=>{
  const cmd='/arsonmix-publish macos '+TAG+' '+SHA;
  assert.deepEqual(parsePublicPortCommand(cmd),{
    platform:'macos',tag:TAG,sha:SHA,branch:BRANCH
  });
  assert.equal(parsePublicPortCommand('/arsonmix-publish windows '+TAG+' '+SHA),null);
  assert.equal(parsePublicPortCommand('/arsonmix-publish macos '+TAG+' '+'bad'),null);
  const calls=[];
  const api=async(path,method='GET',data)=>{
    calls.push({path,method,data});
    if(path==='/git/ref/heads/'+BRANCH)return {ref:'refs/heads/'+BRANCH,object:{sha:SHA}};
    if(path.startsWith('/issues/450/comments?'))return [];
    if(path==='/releases/tags/'+TAG)return {tag_name:TAG,target_commitish:SHA,draft:false,
      assets:[{name:'ArSonMix-'+TAG+'-Windows-x64-Setup.exe',state:'uploaded'}]};
    if(path.includes('/runs?event=workflow_dispatch'))return {workflow_runs:[]};
    if(path.endsWith('/dispatches'))return null;
    throw Error('Unexpected API '+path);
  };
  const result=await portDispatch({event:ev(cmd),api});
  assert.equal(result.kind,'dispatched');
  assert.deepEqual(calls.filter(c=>c.method==='POST').map(c=>c.data),[{
    ref:BRANCH,
    inputs:{platform:'macos',source_branch:BRANCH,expected_sha:SHA,release_tag:TAG}
  }]);
});

test('Public OS publishing rejects SHA mismatch, missing Windows, duplicate and active run',async()=>{
  const cmd='/arsonmix-publish linux '+TAG+' '+SHA;
  for(const fixture of [
    {release:{tag_name:TAG,target_commitish:'b'.repeat(40),assets:[]},message:/PORT_RELEASE_SOURCE_SHA_MISMATCH/},
    {release:{tag_name:TAG,target_commitish:SHA,assets:[]},message:/PORT_WINDOWS_ASSET_MISSING/},
  ]){
    const calls=[];
    const api=async(path,method='GET',data)=>{
      calls.push({path,method,data});
      if(path.startsWith('/git/ref/'))return {ref:'refs/heads/'+BRANCH,object:{sha:SHA}};
      if(path.startsWith('/issues/450/comments?'))return [];
      if(path==='/releases/tags/'+TAG)return fixture.release;
      throw Error('unexpected '+path);
    };
    await assert.rejects(portDispatch({event:ev(cmd),api}),fixture.message);
    assert.equal(calls.filter(c=>c.method==='POST').length,0);
  }
  const release={tag_name:TAG,target_commitish:SHA,draft:false,assets:[
    {name:'ArSonMix-'+TAG+'-Windows-x64-Setup.exe',state:'uploaded'}]};
  for(const run of [{head_sha:SHA,status:'in_progress',display_title:'Port linux '+TAG+' '+SHA},
                    {head_sha:SHA,status:'completed',conclusion:'success',display_title:'Port linux '+TAG+' '+SHA}]){
    const calls=[];
    const api=async(path,method='GET',data)=>{
      calls.push({path,method,data});
      if(path.startsWith('/git/ref/'))return {ref:'refs/heads/'+BRANCH,object:{sha:SHA}};
      if(path.startsWith('/issues/450/comments?'))return [];
      if(path==='/releases/tags/'+TAG)return release;
      if(path.includes('/runs?event=workflow_dispatch'))return {workflow_runs:[run]};
      throw Error('Unexpected '+path);
    };
    assert.equal((await portDispatch({event:ev(cmd),api})).kind,'skipped');
    assert.equal(calls.filter(c=>c.method==='POST').length,0);
  }
});

test('qualification and public publication are distinct milestones at the same exact OS/SHA', async()=>{
  const publish='/arsonmix-publish macos '+TAG+' '+SHA;
  const qualification='/arsonmix-port macos '+BRANCH+' '+SHA;
  const baseRun={head_sha:SHA,status:'completed',conclusion:'success'};
  for(const [command,history,mode] of [
    [publish, [{...baseRun,display_title:'Port macos QUALIFY '+SHA}],TAG],
    [qualification,[{...baseRun,display_title:'Port macos '+TAG+' '+SHA}],'']
  ]){
    const calls=[];
    const api=async(path,method='GET',data)=>{
      calls.push({path,method,data});
      if(path==='/git/ref/heads/'+BRANCH)
        return {ref:'refs/heads/'+BRANCH,object:{sha:SHA}};
      if(path.startsWith('/issues/450/comments?'))return [];
      if(path==='/releases/tags/'+TAG)
        return {tag_name:TAG,target_commitish:SHA,draft:false,assets:[
          {name:'ArSonMix-'+TAG+'-Windows-x64-Setup.exe',state:'uploaded'}]};
      if(path.includes('/runs?event=workflow_dispatch'))
        return {workflow_runs:history};
      if(path.endsWith('/dispatches'))return null;
      throw Error('Unexpected route '+path);
    };
    const result=await portDispatch({event:ev(command),api});
    assert.equal(result.kind,'dispatched');
    const dispatches=calls.filter(x=>x.method==='POST');
    assert.equal(dispatches.length,1);
    assert.equal(dispatches[0].data.inputs.release_tag,mode);
  }
});

test('Public port workflow includes no commercial signing or push-triggered build',()=>{
  const workflow=readFileSync(new URL('../.github/workflows/crossplatform-native-qualification.yml',import.meta.url),'utf8');
  assert.match(workflow,/workflow_dispatch:/);
  assert.doesNotMatch(workflow,/\n  push:/);
  assert.doesNotMatch(workflow,/\n  schedule:/);
  assert.doesNotMatch(workflow,/\n\s+matrix:/);
  assert.match(workflow,/bash "scripts\/package_native_\$PLATFORM.sh"/);
  assert.match(workflow,/gh release upload "\$TAG"/);
  assert.match(workflow,/SHA256SUMS-macOS.txt/);
  assert.match(workflow,/SHA256SUMS-Linux.txt/);
});

test('Windows public release ZIP is a verifiable wrapper of the same exact Inno EXE',()=>{
  const publish=readFileSync(new URL('../.github/workflows/alpha-preview-publish.yml',import.meta.url),'utf8');
  assert.match(publish,/Windows-x64-Setup\.zip/);
  assert.match(publish,/Compress-Archive -LiteralPath \$releasedExe -DestinationPath \$releasedZip/);
  assert.match(publish,/python scripts\/verify_release_setup_zip\.py --exe \$releasedExe --zip \$releasedZip/);
  assert.match(publish,/Installer ZIP failed byte-for-byte verification/);
  assert.match(publish,/if \(\$assets.Count -ne 3\)/);
  assert.match(publish,/if \(\$assets.Count -ne 7\)/);
  assert.match(publish,/SHA256SUMS\.txt/);
});
