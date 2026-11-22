// SPDX-License-Identifier: AGPL-3.0-only
// GitHub connector -> owner PR comment -> exact-SHA milestone. Runs on main.
import {readFileSync} from 'node:fs';
import {setTimeout as sleep} from 'node:timers/promises';
import {requiredLanes} from './required_pr_merge_gate.mjs';

export function parseMilestoneCommand(body) {
  if(typeof body!=='string')return null;
  const m=/^\/arsonmix-ci (check|fast|full|ui|native|fat|field|immersib|stop) ([0-9a-f]{40})$/.exec(body.trim());
  return m?{lane:m[1],sha:m[2]}:null;
}
export function isTrustedOwnerComment(event,repo) {
  const owner=typeof repo==='string'?repo.split('/')[0]:'';
  return !!(
    /^[a-zA-Z0-9_.-]+\/[a-zA-Z0-9_.-]+$/.test(repo||'') &&
    event?.repository?.full_name===repo && event?.issue?.pull_request &&
    Number.isSafeInteger(event?.issue?.number) &&
    event?.comment?.author_association==='OWNER' &&
    event?.comment?.user?.login===owner && Number.isSafeInteger(event?.comment?.id)
  );
}
export function validateMilestone(event,repo,pr) {
  if(!isTrustedOwnerComment(event,repo))throw Error('CI_OWNER_ONLY');
  const cmd=parseMilestoneCommand(event.comment.body);
  if(!cmd)throw Error('CI_COMMAND_INVALID');
  if(pr?.number!==event.issue.number || pr?.state!=='open' ||
     pr?.head?.repo?.full_name!==repo || pr?.base?.repo?.full_name!==repo ||
     pr?.base?.ref!=='main' || !/^[a-zA-Z0-9_.\/-]+$/.test(pr?.head?.ref||''))
    throw Error('CI_PR_INVALID');
  if(pr.head.sha!==cmd.sha)throw Error('CI_STALE_SHA');
  return {...cmd,number:pr.number,branch:pr.head.ref,
    workflow:cmd.lane==='ui'?'ui-smoke.yml':
      cmd.lane==='immersib'?'immersib-focused-native.yml':'p1-windows.yml'};
}
export function hadEarlierIdenticalRequest(comments,event,repo) {
  const cmd=parseMilestoneCommand(event.comment.body);
  if(!cmd)return false;
  return comments.some(c=>{
    const old=parseMilestoneCommand(c.body);
    return c.id<event.comment.id && c.user?.login===repo.split('/')[0] &&
      c.author_association==='OWNER' && old?.lane===cmd.lane && old?.sha===cmd.sha;
  });
}
// Only isolated Harmony source changes may use the shorter Windows FAT
// compiler graph. Keep native/full and generic FAT on the complete graph.
// Failure to fetch a complete changed-file list must fail closed.
export function focusedHarmonyFileScope(files) {
  if(!Array.isArray(files)||files.length===0)return false;
  const pluginOwned=[
    /^engine\/builtin_fx\/Harmony[^/]*\.h$/,
    /^engine\/builtin_fx\/ChannelHarmonyProcessor\.h$/,
    /^tests\/harmony[^/]*\.(cpp|py)$/,
    /^tests\/native_harmony_assembly_test\.py$/,
    /^tests\/approved_native_harmony_smoke\.py$/,
    /^tools\/harmony_offline_ab\.cpp$/,
    /^site\/preview-v2\/assets\/mixer-native-harmony\.js$/
  ];
  // CSS and the workflow can accompany isolated plugin edits, but neither
  // alone is proof that a PR warrants the focused Vocal Harmony FAT.
  const permitted=[
    ...pluginOwned,
    /^site\/preview-v2\/assets\/mixer-fx-rack\.css$/,
    /^\.github\/workflows\/harmony-fast-dsp\.yml$/
  ];
  return files.some(file=>typeof file==='string' &&
        pluginOwned.some(pattern=>pattern.test(file))) &&
    files.every(file=>typeof file==='string' &&
        permitted.some(pattern=>pattern.test(file)));
}

/**
 * A deliberately limited iteration/diagnostic scope, never a merge-gate
 * replacement. CMake is allowed only to compile real ImmerSIB sources:
 * the normal protected nativeFull gate STILL requires full Windows MSVC.
 * Shared mixer/device/ASIO, other FX or installer edits reject this lane.
 */
export function focusedImmerSIBFileScope(files) {
  if(!Array.isArray(files)||!files.length)return false;
  const owned=[
    /^engine\\/builtin_fx\\/ImerSipP92(?:Editor|VisualSkin|Processor|Scene)\\.h$/,
    /^tests\\/imersip(?:_p92_.*|_scene_assets_contract_test)\\.(?:cpp|py)$/,
    /^tests\\/immersib_.*\\.(?:cpp|py)$/,
    /^tools\\/immersib_.*\\.cpp$/,
    /^assets\\/imersip\\//
  ];
  const allowed=[
    ...owned,
    /^CMakeLists\\.txt$/,
    /^\\.github\\/workflows\\/(?:imersip-p92-single-engine|immersib-musical-eq|immersib-focused-native)\\.yml$/,
    /^docs\\/architecture\\/IMMERSIB_P102_[^/]+\\.md$/
  ];
  return files.some(f=>typeof f==='string'&&owned.some(re=>re.test(f)))&&
    files.every(f=>typeof f==='string'&&allowed.some(re=>re.test(f)));
}
export function parseImmerSIBFocusedRun(run) {
  if(run?.event!=='workflow_dispatch')return null;
  const m=/^ArSonMix IMMERSIB PR([1-9][0-9]*) ([0-9a-f]{40})$/
    .exec(run.display_title||'');
  return m?{number:Number(m[1]),sha:m[2]}:null;
}
async function recentImmerSIBFocusedRuns(api) {
  const all=[];
  for(let page=1;page<=5;page++){
    const response=await api('/actions/workflows/immersib-focused-native.yml/runs?'+
      'event=workflow_dispatch&per_page=100&page='+page);
    if(!Array.isArray(response?.workflow_runs))
      throw Error('CI_IMMERSIB_RUNS_INVALID');
    all.push(...response.workflow_runs);
    if(response.workflow_runs.length<100)return all;
  }
  throw Error('CI_IMMERSIB_RUNS_PAGINATION_LIMIT');
}

async function readChangedFiles(api,number) {
  const result=[];
  for(let page=1;page<=10;page++){
    const files=await api('/pulls/'+number+'/files?per_page=100&page='+page);
    if(!Array.isArray(files))throw Error('CI_FILES_INVALID');
    result.push(...files.map(file=>file.filename));
    if(files.length<100)return result;
  }
  // Never assume a partial list makes a multi-subsystem PR safe to focus.
  throw Error('CI_FILES_PAGINATION_LIMIT');
}
export function hasPriorDispatch(runs,sha) {
  return runs.some(r=>r.head_sha===sha && r.event==='workflow_dispatch' &&
    (r.status!=='completed'||r.conclusion==='success'));
}
// Workflow dispatch defaults to the workflow-ref SHA, NOT inputs.expected_sha.
// Therefore only a run-name containing the verified PR id + source SHA can
// deduplicate native/FAT builds dispatched from the protected main workflow.
// Never infer provenance from head_sha or an unstructured display title.
export function parseWindowsMilestoneRun(run) {
  if(run?.event!=='workflow_dispatch')return null;
  const m=/^ArSonMix (NATIVE|FAT|FIELD) PR([1-9][0-9]*) ([0-9a-f]{40})$/
    .exec(run.display_title||'');
  return m?{lane:m[1].toLowerCase(),number:Number(m[2]),sha:m[3]}:null;
}
export function isActiveWindowsRun(run) {
  return ['queued','in_progress','waiting','pending','requested'].includes(run?.status);
}
async function recentWindowsMilestones(api) {
  // Explicitly paginate: when several engineers work concurrently, one
  // page can hide an existing FAT and accidentally request another.
  const result=[];
  for(let page=1;page<=5;page++){
    const response=await api('/actions/workflows/p1-windows.yml/runs?event=workflow_dispatch&per_page=100&page='+page);
    if(!Array.isArray(response?.workflow_runs))
      throw Error('CI_DISPATCH_RUNS_INVALID');
    result.push(...response.workflow_runs);
    if(response.workflow_runs.length<100)return result;
  }
  throw Error('CI_DISPATCH_RUNS_PAGINATION_LIMIT');
}
export async function guardWindowsMilestone({target,api}) {
  const runs=(await recentWindowsMilestones(api)).map(run=>({
    run,identity:parseWindowsMilestoneRun(run)
  })).filter(x=>x.identity?.number===target.number);
  const same=runs.filter(x=>x.identity.sha===target.sha);
  if(target.lane==='field'){
    const existing=same.find(({run,identity})=>['fat','field'].includes(identity.lane) &&
      (isActiveWindowsRun(run)||run.conclusion==='success'));
    return existing?{kind:'skipped',reason:'portable-already-available-or-scheduled',target}:null;
  }
  const activeFat=runs.find(x=>x.identity.lane==='fat'&&isActiveWindowsRun(x.run));
  // Never cancel an RC package build even after later PR edits: it is
  // immutable field-test evidence. A new request can be made after it ends.
  if(activeFat)return {kind:'skipped',reason:'fat-already-running',target};
  const sameFat=same.find(x=>x.identity.lane==='fat');
  if(sameFat) {
    if(sameFat.run.conclusion==='success'||isActiveWindowsRun(sameFat.run))
      return {kind:'skipped',reason:'fat-already-qualified-or-scheduled',target};
    if(target.lane==='fat')throw Error('CI_FAT_FAILED_FIX_SOURCE_BEFORE_REDISPATCH');
  }
  if(target.lane==='native') {
    const native=same.find(x=>x.identity.lane==='native');
    if(native) {
      if(native.run.conclusion==='success'||isActiveWindowsRun(native.run))
        return {kind:'skipped',reason:'native-already-qualified-or-scheduled',target};
      throw Error('CI_NATIVE_FAILED_FIX_SOURCE_BEFORE_REDISPATCH');
    }
    return null;
  }
  // FAT supersedes *still running* native qualification of this PR.
  // The owner-signed dispatcher has actions:write permission; cancellation
  // uses the documented workflow-run API and costs no extra compile.
  // Never cancel anything belonging to another PR or a FAT run.
  for(const candidate of runs) {
    if(candidate.identity.lane!=='native'||!isActiveWindowsRun(candidate.run))
      continue;
    if(!Number.isSafeInteger(candidate.run.id))
      throw Error('CI_NATIVE_RUN_ID_INVALID');
    await api('/actions/runs/'+candidate.run.id+'/cancel','POST');
  }
  return null;
}

export function quickResult(jobs,lane) {
  const name=lane==='ui'?'Fast UI contracts (every relevant PR commit)':
    'Fast native contracts (no Windows / JUCE checkout)';
  return jobs.find(j=>j.name===name)?.conclusion??null;
}
export async function performMilestone({event,repo,api,waitForQuick=()=>sleep(5000)}) {
  if(!isTrustedOwnerComment(event,repo))throw Error('CI_OWNER_ONLY');
  const target=validateMilestone(event,repo,await api('/pulls/'+event.issue.number));
  const older=[];
  for(let page=1;page<=20;page++){
    const c=await api('/issues/'+target.number+'/comments?per_page=100&page='+page);
    older.push(...c);
    if(c.length<100)break;
    if(page===20)throw Error('CI_COMMENTS_PAGINATION_LIMIT');
  }
  if(hadEarlierIdenticalRequest(older,event,repo))
    return {kind:'skipped',reason:'duplicate',target};
  // STOP is an owner-authorized safety action. It NEVER dispatches a job
  // and cancels ONLY this exact PR and immutable HEAD; never another PR.
  if(target.lane==='stop') {
    const ordinary=(await recentWindowsMilestones(api)).filter(x=>{
      const id=parseWindowsMilestoneRun(x);
      return id?.number===target.number&&id.sha===target.sha&&
        isActiveWindowsRun(x);
    });
    const focused=(await recentImmerSIBFocusedRuns(api)).filter(x=>{
      const id=parseImmerSIBFocusedRun(x);
      return id?.number===target.number&&id.sha===target.sha&&
        isActiveWindowsRun(x);
    });
    const cancel=[...ordinary,...focused];
    for(const run of cancel){
      if(!Number.isSafeInteger(run.id))throw Error('CI_STOP_RUN_ID_INVALID');
      await api('/actions/runs/'+run.id+'/cancel','POST');
    }
    return {kind:cancel.length?'cancelled':'checked',
      reason:cancel.length?'stopped-exact-pr-head':'no-matching-active-runs',
      target,results:cancel.map(x=>({run:x.id}))};
  }
  // Ordinary FAT is for a REQUESTED installer, not editor iteration. The
  // previous ImmerSIB #865 consumed 30m building 113 unrelated CTests.
  // Explicit source-scoped denial makes repeating that mistake impossible.
  if(target.lane==='fat'&&/imersib|imersip/i.test(target.branch)&&
     focusedImmerSIBFileScope(await readChangedFiles(api,target.number)))
    throw Error('CI_IMMERSIB_ITERATION_USE_FOCUSED_NOT_FAT');
  if(target.lane==='immersib') {
    const files=await readChangedFiles(api,target.number);
    if(!focusedImmerSIBFileScope(files))
      throw Error('CI_IMMERSIB_FOCUSED_SCOPE_REQUIRES_ISOLATED_SOURCE');
    // Same PR exact-SHA quick preflight as SMART; no Windows on red UI,
    // native or release source checks and no trust in workflow-ref SHA.
    const proof=await verifySmartFast({target,scope:requiredLanes(files),
      api,waitForQuick});
    validateMilestone(event,repo,await api('/pulls/'+target.number));
    // Do not overlap a full generic FAT with the focused editor runner.
    const broad=(await recentWindowsMilestones(api)).filter(x=>{
      const id=parseWindowsMilestoneRun(x);
      return id?.number===target.number&&id.lane==='fat'&&
        isActiveWindowsRun(x);
    });
    if(broad.length)return {kind:'skipped',reason:'full-fat-already-running',
      target,proof};
    const candidates=(await recentImmerSIBFocusedRuns(api)).map(x=>({
      run:x,identity:parseImmerSIBFocusedRun(x)
    })).filter(x=>x.identity?.number===target.number);
    if(candidates.some(x=>isActiveWindowsRun(x.run)))
      return {kind:'skipped',reason:'immersib-focused-already-running',
        target,proof};
    const prior=candidates.find(x=>x.identity.sha===target.sha);
    if(prior?.run.conclusion==='success')
      return {kind:'skipped',reason:'immersib-focused-already-passed',
        target,proof};
    if(prior)throw Error('CI_IMMERSIB_FAILED_FIX_SOURCE_BEFORE_REDISPATCH');
    validateMilestone(event,repo,await api('/pulls/'+target.number));
    await api('/actions/workflows/immersib-focused-native.yml/dispatches','POST',
      {ref:'main',inputs:{expected_sha:target.sha,
        request_pr_number:String(target.number)}});
    return {kind:'dispatched',target,proof};
  }
  const qs='/actions/workflows/'+target.workflow+'/runs?event=pull_request&branch='+
    encodeURIComponent(target.branch)+'&per_page=100';
  const prior=(await api(qs)).workflow_runs.filter(r=>r.head_sha===target.sha)
    .sort((a,b)=>b.id-a.id)[0];
  if(prior){
    for(let attempt=0;;attempt++){
      const jobs=(await api('/actions/runs/'+prior.id+'/jobs?per_page=100')).jobs;
      const status=quickResult(jobs,target.lane);
      if(status==='success')break;
      if(status)throw Error('CI_QUICK_FAILED: '+status+'; fix lightweight regression before milestone');
      if(attempt===12)throw Error('CI_QUICK_PENDING: timed out waiting for lightweight checks');
      await waitForQuick();
      validateMilestone(event,repo,await api('/pulls/'+target.number));
    }
  }
  if(target.lane==='ui'){
    const q='/actions/workflows/'+target.workflow+
      '/runs?event=workflow_dispatch&branch='+encodeURIComponent(target.branch)+'&per_page=100';
    if(hasPriorDispatch((await api(q)).workflow_runs,target.sha))
      return {kind:'skipped',reason:'already-qualified-or-scheduled',target};
  }else{
    const gate=await guardWindowsMilestone({target,api});
    if(gate)return gate;
  }
  // Focused FAT saves compilation of unrelated CTest executables for
  // audibly-qualified Harmonice-only candidate PRs. A branch name alone
  // is NOT enough: inspect all PR files and deny on any shared engine,
  // mixer/ASIO or release workflow edit. Full native CTest stays available.
  let focusedHarmony=false;
  if(target.lane==='fat'&&/(^|\/)harmonice[-/]/.test(target.branch)) {
    focusedHarmony=focusedHarmonyFileScope(
      await readChangedFiles(api,target.number));
  }
  const inputs=target.lane==='ui'?{expected_sha:target.sha}:
    {expected_sha:target.sha,package_fat:target.lane==='fat',
     request_pr_number:String(target.number),
     ...(target.lane==='field'?{field_preview:true}:{}),
     ...(focusedHarmony?{focused_harmony_fat:true}:{})};
  // Windows always uses the reviewed CI definition from main, and checks
  // out the exact PR source SHA. Run-name also carries the verified PR/SHA
  // so a main-ref workflow_dispatch cannot conceal a duplicate compile.
  // Chromium continues to use the PR-specific UI workflow.
  const workflowRef=target.lane==='ui'?target.branch:'main';
  await api('/actions/workflows/'+target.workflow+'/dispatches','POST',
    {ref:workflowRef,inputs});
  return {kind:'dispatched',target};
}

// Smart authoring interface. CHECK proves existing fast contracts; FAST builds
// one exact-source Windows portable when native files changed; FULL selects
// the independently protected native and/or Chromium gates. Nothing here
// rewrites branch protection or silently upgrades a FIELD preview to a FAT.
export function planSmartMilestone(files,mode) {
  if(!['check','fast','full'].includes(mode))throw Error('CI_SMART_MODE_INVALID');
  const scope=requiredLanes(files); // same reviewed file policy as merge gate
  // The status publisher requests nativeFull only for actual C++/graph
  // changes. Package, native bridge, and installer changes need a Windows
  // binary as well, even when full status does not require it.
  const nativeBinary=files.some(f=>
    /^src\/|^engine\/|^CMakeLists\.txt$/.test(f) ||
    /^integration\/.*\.(?:h|hpp|cpp)$/.test(f) ||
    /^tests\/.*\.cpp$/.test(f) ||
    /^installer\//.test(f) ||
    /^web\/(?:approved-native-bridge|approved-native-contract|bridge|monitor)\.js$/.test(f) ||
    /^scripts\/(?:package_native_windows|build_verify_windows_installer|verify_native_windows_artifact|verify_windows_spatial_fx_packaging|verify_immersib_approved_pcm_sources)\./.test(f) ||
    /^config\/immersib_approved_pcm_sources\.json$/.test(f));
  const targets=mode==='check'?[]:
    mode==='fast'?(nativeBinary?['field']:[]):
    [...(scope.uiFull?['ui']:[]),...(nativeBinary?['native']:[])];
  return {scope,nativeBinary,targets};
}

const fastJobs=[
  ['nativeFast','p1-windows.yml','Fast native contracts (no Windows / JUCE checkout)'],
  ['uiFast','ui-smoke.yml','Fast UI contracts (every relevant PR commit)'],
  ['releaseFast','release-contracts.yml','No native compiler; owner authorization, catalog integrity and YAML shape']
];

// Fail closed if *any* required fast lane is missing, failed, cancelled or
// stale. Checking only the target Windows quick lane can miss UI/release reds
// on a mixed PR and burn 30+ minutes of CI unnecessarily.
export async function verifySmartFast({target,scope,api,waitForQuick=()=>sleep(5000)}) {
  const proof=[];
  for(const [key,workflow,jobName] of fastJobs) {
    if(!scope[key])continue;
    const url='/actions/workflows/'+workflow+'/runs?event=pull_request&branch='+
      encodeURIComponent(target.branch)+'&per_page=100';
    for(let attempt=0;attempt<=12;attempt++){
      const body=await api(url);
      if(!Array.isArray(body?.workflow_runs))throw Error('CI_SMART_FAST_RUNS_INVALID');
      const exact=body.workflow_runs.filter(r=>r.head_sha===target.sha &&
        r.event==='pull_request' &&
        (r.path===undefined ||
          r.path.split('@',1)[0]==='.github/workflows/'+workflow))
        .sort((a,b)=>b.id-a.id)[0];
      if(!exact)throw Error('CI_SMART_FAST_MISSING: '+key+' '+target.sha);
      const jobs=(await api('/actions/runs/'+exact.id+'/jobs?per_page=100'))?.jobs;
      if(!Array.isArray(jobs))throw Error('CI_SMART_FAST_JOBS_INVALID: '+key);
      const job=jobs.filter(j=>j.name===jobName);
      if(job.length!==1)throw Error('CI_SMART_FAST_JOB_MISSING: '+key);
      if(job[0].conclusion==='success'){
        proof.push({lane:key,run:exact.id,job:jobName});
        break;
      }
      if(job[0].conclusion)
        throw Error('CI_SMART_FAST_FAILED: '+key+' '+job[0].conclusion);
      if(attempt===12)throw Error('CI_SMART_FAST_PENDING: '+key);
      await waitForQuick();
      // A pushed commit during verification invalidates all earlier proof.
      const current=await api('/pulls/'+target.number);
      if(current?.head?.sha!==target.sha)throw Error('CI_STALE_SHA');
    }
  }
  return proof;
}

export async function performSmartMilestone({event,repo,api,
  waitForQuick=()=>sleep(5000)}) {
  if(!isTrustedOwnerComment(event,repo))throw Error('CI_OWNER_ONLY');
  const target=validateMilestone(event,repo,
    await api('/pulls/'+event.issue.number));
  if(!['check','fast','full'].includes(target.lane))
    throw Error('CI_SMART_MODE_INVALID');
  const earlier=[];
  for(let page=1;page<=20;page++) {
    const comments=await api('/issues/'+target.number+
      '/comments?per_page=100&page='+page);
    if(!Array.isArray(comments))throw Error('CI_COMMENTS_INVALID');
    earlier.push(...comments);
    if(comments.length<100)break;
    if(page===20)throw Error('CI_COMMENTS_PAGINATION_LIMIT');
  }
  if(hadEarlierIdenticalRequest(earlier,event,repo))
    return {kind:'skipped',reason:'duplicate',target};
  const files=await readChangedFiles(api,target.number);
  const {scope,targets}=planSmartMilestone(files,target.lane);
  const proof=await verifySmartFast({target,scope,api,waitForQuick});
  // One last current HEAD check immediately before any dispatch.
  validateMilestone(event,repo,await api('/pulls/'+target.number));
  if(targets.length===0)
    return {kind:'checked',target,proof,reason:'no-expensive-lane-for-scope'};
  const results=[];
  for(const lane of targets) {
    const request={...event,comment:{...event.comment,
      body:'/arsonmix-ci '+lane+' '+target.sha}};
    const outcome=await performMilestone({event:request,repo,api,waitForQuick});
    results.push({lane,kind:outcome.kind,reason:outcome.reason||null});
  }
  return {kind:'selected',target,proof,results};
}

async function main(){
  const repo=process.env.GITHUB_REPOSITORY||'';
  const token=process.env.GH_TOKEN||'';
  if(!token||!/^[a-zA-Z0-9_.-]+\/[a-zA-Z0-9_.-]+$/.test(repo))
    throw Error('CI_CONFIG_INVALID');
  const event=JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH,'utf8'));
  const api=async(path,method='GET',data)=>{
    const response=await fetch('https://api.github.com/repos/'+repo+path,{
      method,headers:{Accept:'application/vnd.github+json',Authorization:'Bearer '+token,
        'X-GitHub-Api-Version':'2022-11-28',...(data?{'Content-Type':'application/json'}:{})},
      ...(data?{body:JSON.stringify(data)}:{})
    });
    if(!response.ok)throw Error('GITHUB_API_'+response.status+' '+path+' '+
      (await response.text()).slice(0,400));
    return response.status===204?null:response.json();
  };
  const command=parseMilestoneCommand(event?.comment?.body);
  const result=command && ['check','fast','full'].includes(command.lane)
    ? await performSmartMilestone({event,repo,api})
    : await performMilestone({event,repo,api});
  console.log('CI_'+result.kind.toUpperCase()+' mode='+result.target.lane+
    ' sha='+result.target.sha+' pr='+result.target.number+' '+
    JSON.stringify({reason:result.reason||null,
      proof:result.proof||[],results:result.results||[]}));
}
if(process.argv[1]?.endsWith('chat_ci_dispatch.mjs'))
  main().catch(e=>{console.error(e.message);process.exitCode=1;});
