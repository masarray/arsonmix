// SPDX-License-Identifier: AGPL-3.0-only
// GitHub connector -> owner PR comment -> exact-SHA milestone. Runs on main.
import {readFileSync} from 'node:fs';

export function parseMilestoneCommand(body) {
  if(typeof body!=='string')return null;
  const m=/^\/arsonmix-ci (ui|native|fat) ([0-9a-f]{40})$/.exec(body.trim());
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
    workflow:cmd.lane==='ui'?'ui-smoke.yml':'p1-windows.yml'};
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
  const permitted=[
    /^engine\/builtin_fx\/Harmony[^/]*\.h$/,
    /^tests\/harmony[^/]*\.(cpp|py)$/,
    /^tests\/native_harmony_assembly_test\.py$/,
    /^tests\/approved_native_harmony_smoke\.py$/,
    /^tools\/harmony_offline_ab\.cpp$/,
    /^site\/preview-v2\/assets\/mixer-native-harmony\.js$/,
    /^site\/preview-v2\/assets\/mixer-fx-rack\.css$/,
    /^\.github\/workflows\/harmony-fast-dsp\.yml$/
  ];
  return files.every(file=>typeof file==='string'&&
    permitted.some(pattern=>pattern.test(file)));
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
  const m=/^ArSonMix (NATIVE|FAT) PR([1-9][0-9]*) ([0-9a-f]{40})$/
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
export async function performMilestone({event,repo,api}) {
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
  const qs='/actions/workflows/'+target.workflow+'/runs?event=pull_request&branch='+
    encodeURIComponent(target.branch)+'&per_page=100';
  const prior=(await api(qs)).workflow_runs.filter(r=>r.head_sha===target.sha)
    .sort((a,b)=>b.id-a.id)[0];
  if(prior){
    const jobs=(await api('/actions/runs/'+prior.id+'/jobs?per_page=100')).jobs;
    const status=quickResult(jobs,target.lane);
    if(['failure','cancelled','timed_out'].includes(status))
      throw Error('CI_QUICK_FAILED: fix lightweight regression before milestone');
    if(status!=='success')throw Error('CI_QUICK_PENDING');
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
  const result=await performMilestone({event,repo,api});
  console.log('CI_'+result.kind.toUpperCase()+' mode='+result.target.lane+
    ' sha='+result.target.sha+' pr='+result.target.number+' '+(result.reason||''));
}
if(process.argv[1]?.endsWith('chat_ci_dispatch.mjs'))
  main().catch(e=>{console.error(e.message);process.exitCode=1;});
