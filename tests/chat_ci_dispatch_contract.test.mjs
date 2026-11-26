// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {parseMilestoneCommand,isTrustedOwnerComment,validateMilestone,
  hadEarlierIdenticalRequest,hasPriorDispatch,quickResult,performMilestone,
  focusedImmerSIBFileScope,parseImmerSIBFocusedRun,
  parseWindowsMilestoneRun,guardWindowsMilestone,
  planSmartMilestone,verifySmartFast,performSmartMilestone}
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
  for(const lane of ['check','fast','full','ui','native','fat','field','feature','immersib','stop'])
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
  assert.equal(validateMilestone(event('/arsonmix-ci feature '+sha),repo,pr()).workflow,'p1-windows.yml');
  assert.equal(validateMilestone(event('/arsonmix-ci immersib '+sha),repo,pr()).workflow,
    'immersib-focused-native.yml');
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

test('early milestone waits automatically and refuses a superseded SHA',async()=>{
  for(const stale of [false,true]){
    const writes=[];
    let waits=0;
    const api=async(path,method)=>{
      if(method==='POST'){writes.push(path);return null;}
      if(path.startsWith('/pulls/'))return pr(stale&&waits?'b'.repeat(40):sha);
      if(path.startsWith('/issues/'))return [];
      if(path.includes('/runs?event=pull_request'))return {workflow_runs:[{id:7,head_sha:sha}]};
      if(path.includes('/actions/runs/7/jobs'))return {jobs:[{
        name:'Fast native contracts (no Windows / JUCE checkout)',
        conclusion:waits?'success':null
      }]};
      return {workflow_runs:[]};
    };
    const request=performMilestone({event:event(),repo,api,waitForQuick:async()=>{waits++;}});
    if(stale){
      await assert.rejects(request,/CI_STALE_SHA/);
      assert.deepEqual(writes,[]);
    }else{
      assert.equal((await request).kind,'dispatched');
      assert.deepEqual(writes,['/actions/workflows/p1-windows.yml/dispatches']);
    }
    assert.equal(waits,1);
  }
});

test('generic FEATURE chooses only verified changed-file scope and never a release FAT',async()=>{
  const req=event('/arsonmix-ci feature '+sha),writes=[];
  const makeApi=changed=>async(path,method='GET',data)=>{
    if(path.startsWith('/pulls/418/files?'))return changed.map(filename=>({filename}));
    if(path.startsWith('/pulls/'))return pr();
    if(path.startsWith('/issues/'))return [];
    if(path.includes('/actions/workflows/p1-windows.yml/runs?event=pull_request'))
      return {workflow_runs:[{id:7,head_sha:sha,event:'pull_request'}]};
    if(path==='/actions/runs/7/jobs?per_page=100')
      return {jobs:[{name:'Fast native contracts (no Windows / JUCE checkout)',
        conclusion:'success'}]};
    if(path.includes('/actions/workflows/p1-windows.yml/runs?event=workflow_dispatch'))
      return {workflow_runs:[]};
    if(path.endsWith('/dispatches')){writes.push({path,method,data});return null;}
    throw Error('MOCK_UNEXPECTED '+path);
  };
  const result=await performMilestone({event:req,repo,api:makeApi([
    'engine/device/DeviceSessionTransitionPolicy.h'])});
  assert.equal(result.kind,'dispatched');
  assert.equal(result.feature,'audio-io');
  assert.deepEqual(writes,[{path:'/actions/workflows/p1-windows.yml/dispatches',
    method:'POST',data:{ref:'main',inputs:{expected_sha:sha,
      request_pr_number:'418',package_fat:false,feature_scope:'audio-io'}}}]);
  for(const files of [
    ['src/Main.cpp'],
    ['engine/device/DeviceSessionTransitionPolicy.h','engine/builtin_fx/ChannelCompressor.h'],
    ['engine/builtin_fx/GuitarRigEditor.h']
  ])await assert.rejects(performMilestone({
    event:req,repo,api:makeApi(files)}),/CI_FEATURE_SCOPE_UNSUPPORTED_OR_MIXED/);
  assert.equal(writes.length,1);
});

test('ordinary FAT retains complete CTest and has no plugin-special payload',async()=>{
  const api=async(path,method='GET',data)=>{
    if(path.startsWith('/pulls/'))return pr();
    if(path.startsWith('/issues/'))return [];
    if(path.includes('/runs?event=pull_request'))return {workflow_runs:[{
      id:7,head_sha:sha,event:'pull_request'}]};
    if(path.includes('/actions/runs/7/jobs'))return {jobs:[{
      name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'success'}]};
    if(path.includes('event=workflow_dispatch'))return {workflow_runs:[]};
    if(path.endsWith('/dispatches')){
      assert.deepEqual(data,{ref:'main',inputs:{expected_sha:sha,
        package_fat:true,request_pr_number:'418'}});
      return null;
    }
    throw Error('MOCK_UNEXPECTED '+path);
  };
  assert.equal((await performMilestone({event:event(),repo,api})).kind,'dispatched');
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
    return {jobs:[{name:'Fast native contracts (no Windows / JUCE checkout)',
                   conclusion:'success'}]};
  if(path.includes('event=workflow_dispatch'))return {workflow_runs:runs};
  if(path.endsWith('/dispatches')||path.endsWith('/cancel'))return null;
  throw Error('Unexpected mocked endpoint: '+path);
};

test('Windows milestone identity uses verified PR and checked-out source SHA, not workflow-ref SHA',()=>{
  assert.deepEqual(parseWindowsMilestoneRun(windowsRun(1,'fat',418,sha)),
    {lane:'fat',number:418,sha});
  assert.deepEqual(parseWindowsMilestoneRun(windowsRun(1,'feature',418,sha)),
    {lane:'feature',number:418,sha});
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
  assert.equal(wf.match(/compression-level: 0/g)?.length,3);
  assert.match(wf,/Build executable and native RT tests/);
  assert.doesNotMatch(wf,/focused_harmony_fat|FOCUSED_HARMONY_FAT/);
  assert.match(wf,/Windows FEATURE scoped native CTest \(NOT full\)/);
  assert.match(wf,/ctest --test-dir build -C Release --output-on-failure/);
  assert.match(wf,/build_verify_windows_installer\.ps1/);
});


test('FAT upload is independently retryable without a second Windows compiler and rechecks all file digests',()=>{
  const wf=readFileSync('.github/workflows/p1-windows.yml','utf8');
  const build=wf.split(/\n  fat-upload:\n/)[0];
  const upload=wf.split(/\n  fat-upload:\n/)[1];
  assert.ok(upload,'dedicated artifact delivery job must exist');
  assert.match(build,/Seal exact-SHA FAT delivery payload after all installer tests/);
  assert.match(build,/Save completed FAT packages for retryable upload job/);
  assert.match(build,/actions\/cache\/save@[a-f0-9]{40}/);
  assert.equal((build.match(/actions\/upload-artifact@v4/g)||[]).length,1);
  assert.match(build,/Upload portable Windows field preview\n +if: \$\{\{ env\.FIELD_PREVIEW == 'true' \}\}\n +uses: actions\/upload-artifact@v4/);
  assert.match(upload,/needs: windows-build/);
  assert.match(upload,/runs-on: windows-2022/);
  assert.match(upload,/actions\/cache\/restore@[a-f0-9]{40}/);
  assert.match(upload,/fail-on-cache-miss: true/);
  assert.match(upload,/FAT_UPLOAD_HASH_MISMATCH/);
  assert.match(upload,/FAT_UPLOAD_RECEIPT_MISMATCH/);
  assert.match(upload,/sourceSha -ne \$actual/);
  assert.match(upload,/FAT_UPLOAD_EXE_OR_SETUP_INVALID/);
  assert.match(upload,/overwrite: true/);
  assert.equal((upload.match(/actions\/upload-artifact@v4/g)||[]).length,2);
  assert.equal((upload.match(/compression-level: 0/g)||[]).length,2);
  assert.match(build,/ctest --test-dir build -C Release --output-on-failure/);
  assert.match(build,/Prove two authentic historical Inno releases upgrade and roll back/);
  assert.match(build,/build_verify_windows_installer\.ps1/);
});


test('field preview dispatches exact source without cancelling native or another PR',async()=>{
  const request=event('/arsonmix-ci field '+sha);
  const calls=[];
  const active=[windowsRun(1,'native',418,sha),windowsRun(2,'field',419,sha)];
  assert.equal((await performMilestone({event:request,repo,api:checkedApi(active,calls)})).kind,'dispatched');
  assert.equal(calls.some(call=>call.path.endsWith('/cancel')),false);
  assert.deepEqual(calls.at(-1).data,{ref:'main',inputs:{
    expected_sha:sha,package_fat:false,request_pr_number:'418',field_preview:true
  }});
  for(const lane of ['field','fat']){
    const duplicate=[];
    assert.equal((await performMilestone({event:request,repo,api:checkedApi(
      [windowsRun(3,lane,418,sha)],duplicate)})).reason,'portable-already-available-or-scheduled');
    assert.equal(duplicate.some(call=>call.method==='POST'),false);
  }
});


test('smart CI respects trusted merge-gate scope and never marks FAST as FULL',()=>{
  const cases=[
    {files:['docs/README.md'],fast:[],full:[]},
    {files:['web/mixer-layout.js'],fast:[],full:['ui']},
    {files:['engine/builtin_fx/ImerSipP92VisualSkin.h'],fast:['field'],full:['native']},
    {files:['src/LiveAudioEngine.h','web/mixer-layout.js'],fast:['field'],
     full:['ui','native']},
    {files:['installer/ArSonMix.iss'],fast:['field'],full:['native']},
    {files:['scripts/chat_ci_dispatch.mjs','tests/chat_ci_dispatch_contract.test.mjs'],
     fast:[],full:[]}
  ];
  for(const sample of cases){
    assert.deepEqual(planSmartMilestone(sample.files,'check').targets,[]);
    assert.deepEqual(planSmartMilestone(sample.files,'fast').targets,sample.fast);
    assert.deepEqual(planSmartMilestone(sample.files,'full').targets,sample.full);
  }
  assert.throws(()=>planSmartMilestone(['../unsafe'],'full'),/INVALID_PR_FILES/);
  assert.throws(()=>planSmartMilestone([],'full'),/INVALID_PR_FILES/);
  assert.throws(()=>planSmartMilestone(['src/Main.cpp'],'overwrite'),/CI_SMART_MODE_INVALID/);
});

test('smart CHECK verifies exact-SHA fast jobs, never requests paid workflows',async()=>{
  const calls=[];
  const request=event('/arsonmix-ci check '+sha);
  const api=async(path,method,data)=>{
    calls.push({path,method,data});
    if(path.startsWith('/pulls/418/files'))return [{filename:'src/Main.cpp'}];
    if(path.startsWith('/pulls/418'))return pr();
    if(path.startsWith('/issues/418/comments'))return [];
    if(path.includes('/actions/workflows/p1-windows.yml/runs?'))return {workflow_runs:[
      {id:70,head_sha:sha,event:'pull_request',path:'.github/workflows/p1-windows.yml'}
    ]};
    if(path==='/actions/runs/70/jobs?per_page=100')return {jobs:[
      {name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'success'}
    ]};
    if(path.includes('/actions/workflows/release-contracts.yml/runs?'))return {
      workflow_runs:[{id:71,head_sha:sha,event:'pull_request',
        path:'.github/workflows/release-contracts.yml'}]
    };
    if(path==='/actions/runs/71/jobs?per_page=100')return {jobs:[{
      name:'No native compiler; owner authorization, catalog integrity and YAML shape',
      conclusion:'success'
    }]};
    throw Error('UNEXPECTED_API '+path);
  };
  const got=await performSmartMilestone({event:request,repo,api});
  assert.equal(got.kind,'checked');
  assert.equal(got.proof.length,2);
  assert.equal(calls.some(c=>c.method==='POST'),false);
});

test('smart FAST uses existing FIELD portable and FULL selects independent native + GUI',async()=>{
  for(const [mode,expected] of [['fast',['field']],['full',['ui','native']]]){
    const calls=[];
    const request=event('/arsonmix-ci '+mode+' '+sha);
    const api=async(path,method,data)=>{
      calls.push({path,method:method||'GET',data});
      if(path.startsWith('/pulls/418/files'))return [
        {filename:'src/LiveAudioEngine.h'},{filename:'web/mixer-layout.js'}];
      if(path.startsWith('/pulls/418'))return pr();
      if(path.startsWith('/issues/418/comments'))return [];
      if(path.includes('/actions/workflows/p1-windows.yml/runs?event=pull_request'))
        return {workflow_runs:[{id:101,head_sha:sha,event:'pull_request'}]};
      if(path.includes('/actions/workflows/ui-smoke.yml/runs?event=pull_request'))
        return {workflow_runs:[{id:102,head_sha:sha,event:'pull_request'}]};
      if(path==='/actions/runs/101/jobs?per_page=100')return {jobs:[{
        name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'success'
      }]};
      if(path==='/actions/runs/102/jobs?per_page=100')return {jobs:[{
        name:'Fast UI contracts (every relevant PR commit)',conclusion:'success'
      }]};
      if(path.includes('event=workflow_dispatch'))return {workflow_runs:[]};
      if(path.endsWith('/dispatches'))return null;
      throw Error('UNEXPECTED_API '+path);
    };
    const out=await performSmartMilestone({event:request,repo,api});
    assert.equal(out.kind,'selected');
    assert.deepEqual(out.results.map(r=>r.lane),expected);
    const writes=calls.filter(c=>c.method==='POST');
    assert.equal(writes.length,expected.length);
    if(mode==='fast'){
      assert.deepEqual(writes[0].data,{ref:'main',inputs:{
        expected_sha:sha,package_fat:false,request_pr_number:'418',
        field_preview:true
      }});
    }else{
      assert.deepEqual(writes.map(x=>x.path),[
        '/actions/workflows/ui-smoke.yml/dispatches',
        '/actions/workflows/p1-windows.yml/dispatches'
      ]);
      assert.equal(writes[1].data.inputs.package_fat,false);
    }
  }
});

test('smart FAST/FULL never spend Windows on red, missing or stale quick source',async()=>{
  const request=event('/arsonmix-ci fast '+sha);
  for(const state of ['missing','failure','stale']){
    const calls=[];
    let pulled=0, waits=0;
    const api=async(path,method,data)=>{
      calls.push({path,method:method||'GET'});
      if(path.startsWith('/pulls/418/files'))return [{filename:'src/Main.cpp'}];
      if(path.startsWith('/pulls/418'))return pr(state==='stale'&&pulled++>0?
        'b'.repeat(40):sha);
      if(path.startsWith('/issues/418/comments'))return [];
      if(path.includes('/actions/workflows/p1-windows.yml/runs?'))return {
        workflow_runs:state==='missing'?[]:[{
          id:9,head_sha:sha,event:'pull_request'
        }]
      };
      if(path==='/actions/runs/9/jobs?per_page=100')return {jobs:[{
        name:'Fast native contracts (no Windows / JUCE checkout)',
        conclusion:state==='failure'?'failure':null
      }]};
      if(path.endsWith('/dispatches'))throw Error('UNEXPECTED_PAID_DISPATCH');
      return {workflow_runs:[]};
    };
    const err=state==='missing'?/CI_SMART_FAST_MISSING/:
      state==='failure'?/CI_SMART_FAST_FAILED/:/CI_STALE_SHA/;
    await assert.rejects(performSmartMilestone({
      event:request,repo,api,waitForQuick:async()=>{waits++;}
    }),err);
    assert.equal(calls.some(c=>c.method==='POST'),false);
    assert.equal(waits,state==='stale'?1:0);
  }
});

test('smart owner duplicate command is idempotent, no new expensive work',async()=>{
  const req=event('/arsonmix-ci full '+sha);
  const api=async(path)=>{
    if(path.startsWith('/pulls/418'))return pr();
    if(path.startsWith('/issues/418/comments'))return [{
      id:47,body:req.comment.body,author_association:'OWNER',user:{login:'masarray'}
    }];
    throw Error('UNEXPECTED_DUPLICATE_API '+path);
  };
  assert.equal((await performSmartMilestone({event:req,repo,api})).reason,'duplicate');
});

test('ImmerSIB iteration has a strict changed-file allowlist and never includes mixer/installer',()=>{
  const isolated=[
    'engine/builtin_fx/ImerSipP92Editor.h',
    'engine/builtin_fx/ImerSipP92VisualSkin.h',
    'tests/imersip_p92_native_test.cpp',
    'tests/imersip_scene_assets_contract_test.py',
    'tests/imersip_p92_catalog_contract_test.py',
    'CMakeLists.txt',
    'tools/immersib_p102_cpu_profile.cpp',
    'docs/architecture/IMMERSIB_P102_CPU_OPTIMIZATION_PLAN_20261122.md',
    '.github/workflows/imersip-p92-single-engine.yml'
  ];
  assert.equal(focusedImmerSIBFileScope(isolated),true);
  for(const dangerous of ['src/Main.cpp','engine/builtin_fx/SonKuSpatialProcessor.h',
    'engine/device/AsioStartupPolicy.h','installer/ArSonMix.iss',
    'engine/builtin_fx/ChannelHarmonyProcessor.h','web/mixer-layout.js'])
    assert.equal(focusedImmerSIBFileScope([...isolated,dangerous]),false);
  assert.equal(focusedImmerSIBFileScope(['CMakeLists.txt']),false);
  assert.equal(focusedImmerSIBFileScope([]),false);
  const approved=readFileSync('.github/workflows/immersib-focused-native.yml','utf8');
  assert.match(approved,/run-name: "ArSonMix IMMERSIB PR/);
  assert.match(approved,/ref: \$\{\{ inputs.expected_sha \}\}/);
  assert.match(approved,/ARMixImerSipP92NativeTests ARMixImmerSIBP99NativeSonicGate/);
  assert.match(approved,/ctest --test-dir build -C Release --output-on-failure -R \$pattern/);
  assert.doesNotMatch(approved,/--target ARMixConsole|ctest --test-dir build -C Release --output-on-failure\s*$/m);
  assert.doesNotMatch(approved,/package_fat|focused_harmony_fat|build_verify_windows_installer/);
});

test('ImmerSIB owner command uses exact-source targeted workflow, no FAT/console',async()=>{
  const calls=[],req=event('/arsonmix-ci immersib '+sha);
  const p=pr();p.head.ref='perf/immersib-p102-editor';
  const isolated=['engine/builtin_fx/ImerSipP92Editor.h',
    'tests/imersip_p92_native_test.cpp'];
  const api=async(path,method='GET',data)=>{
    calls.push({path,method,data});
    if(path.startsWith('/pulls/418/files?'))return isolated.map(filename=>({filename}));
    if(path.startsWith('/pulls/418'))return p;
    if(path.startsWith('/issues/418/comments?'))return [];
    if(path.includes('/actions/workflows/p1-windows.yml/runs?event=pull_request'))
      return {workflow_runs:[{id:501,head_sha:sha,event:'pull_request'}]};
    if(path.includes('/actions/workflows/ui-smoke.yml/runs?event=pull_request'))
      return {workflow_runs:[{id:502,head_sha:sha,event:'pull_request'}]};
    if(path==='/actions/runs/501/jobs?per_page=100')return {jobs:[{
      name:'Fast native contracts (no Windows / JUCE checkout)',conclusion:'success'}]};
    if(path==='/actions/runs/502/jobs?per_page=100')return {jobs:[{
      name:'Fast UI contracts (every relevant PR commit)',conclusion:'success'}]};
    if(path.includes('/actions/workflows/p1-windows.yml/runs?event=workflow_dispatch'))
      return {workflow_runs:[]};
    if(path.includes('/actions/workflows/immersib-focused-native.yml/runs?'))
      return {workflow_runs:[]};
    if(path.endsWith('/dispatches'))return null;
    throw Error('unexpected '+path);
  };
  const res=await performMilestone({event:req,repo,api});
  assert.equal(res.kind,'dispatched');
  const posts=calls.filter(c=>c.method==='POST');
  assert.deepEqual(posts,[{path:'/actions/workflows/immersib-focused-native.yml/dispatches',
    method:'POST',data:{ref:'main',inputs:{expected_sha:sha,request_pr_number:'418'}}}]);
  assert.equal(calls.some(c=>c.path.includes('p1-windows.yml/dispatches')),false);
});

test('ImmerSIB scoped PR cannot accidentally request generic FAT; all other broad PRs retain explicit release FAT',async()=>{
  const req=event('/arsonmix-ci fat '+sha);
  const p=pr();p.head.ref='perf/immersib-p102-editor';
  const read=async(path)=>{
    if(path.startsWith('/pulls/418/files?'))return [
      {filename:'engine/builtin_fx/ImerSipP92Editor.h'},
      {filename:'tests/imersip_p92_native_test.cpp'},
      {filename:'CMakeLists.txt'}];
    if(path.startsWith('/pulls/418'))return p;
    if(path.startsWith('/issues/418/comments'))return [];
    throw Error('unexpected expensive API '+path);
  };
  await assert.rejects(performMilestone({event:req,repo,api:read}),
    /CI_IMMERSIB_ITERATION_USE_FOCUSED_NOT_FAT/);
});

test('STOP owner command cancels only active matching PR+exact-SHA, never creates a runner',async()=>{
  const calls=[], req=event('/arsonmix-ci stop '+sha);
  const api=async(path,method='GET')=>{
    calls.push({path,method});
    if(path.startsWith('/pulls/418'))return pr();
    if(path.startsWith('/issues/418/comments'))return [];
    if(path.includes('/p1-windows.yml/runs?event=workflow_dispatch'))return {
      workflow_runs:[
        windowsRun(51,'fat',418,sha,'in_progress'),
        windowsRun(52,'fat',419,sha,'in_progress'),
        windowsRun(53,'fat',418,'b'.repeat(40),'in_progress')]};
    if(path.includes('/immersib-focused-native.yml/runs?'))return {workflow_runs:[
      {id:54,event:'workflow_dispatch',status:'queued',
        display_title:'ArSonMix IMMERSIB PR418 '+sha},
      {id:55,event:'workflow_dispatch',status:'in_progress',
        display_title:'ArSonMix IMMERSIB PR419 '+sha}]};
    if(path.endsWith('/cancel'))return null;
    throw Error('UNEXPECTED_API '+path);
  };
  assert.deepEqual(parseImmerSIBFocusedRun({event:'workflow_dispatch',
    display_title:'ArSonMix IMMERSIB PR418 '+sha}),{number:418,sha});
  const got=await performMilestone({event:req,repo,api});
  assert.equal(got.kind,'cancelled');
  assert.deepEqual(calls.filter(c=>c.method==='POST').map(c=>c.path),
    ['/actions/runs/51/cancel','/actions/runs/54/cancel']);
});
