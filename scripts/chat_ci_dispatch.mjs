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
export function hasPriorDispatch(runs,sha) {
  return runs.some(r=>r.head_sha===sha && r.event==='workflow_dispatch' &&
    (r.status!=='completed'||r.conclusion==='success'));
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
  if(target.lane!=='fat'){
    const q='/actions/workflows/'+target.workflow+
      '/runs?event=workflow_dispatch&branch='+encodeURIComponent(target.branch)+'&per_page=100';
    if(hasPriorDispatch((await api(q)).workflow_runs,target.sha))
      return {kind:'skipped',reason:'already-qualified-or-scheduled',target};
  }
  const inputs=target.lane==='ui'?{expected_sha:target.sha}:
    {expected_sha:target.sha,package_fat:target.lane==='fat'};
  await api('/actions/workflows/'+target.workflow+'/dispatches','POST',
    {ref:target.branch,inputs});
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
