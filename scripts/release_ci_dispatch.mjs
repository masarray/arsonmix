// Owner-only release controller. A PR/branch push can NEVER start paid release builders.
// One validated issue comment -> one exact-SHA workflow_dispatch; duplicates fail closed.
import { readFileSync } from 'node:fs';

const REPO='masarray/arsonmix';
const CONTROL_ISSUE=450;
export function parseReleaseCommand(body) {
  const m=/^\/arsonmix-release (v\d+\.\d+\.\d+-alpha\.\d+) ([0-9a-f]{40})$/.exec(
    typeof body==='string'?body.trim():'');
  return m?{tag:m[1],sha:m[2],branch:'releases/alpha/'+m[1]}:null;
}
// One OS per explicitly requested milestone; no automatic platform matrix.
export function parsePortCommand(body) {
  const m=/^\/arsonmix-port (linux|macos) ([a-zA-Z0-9][a-zA-Z0-9._/-]{0,94}) ([0-9a-f]{40})$/.exec(
    typeof body==='string'?body.trim():'');
  if (!m || m[2].includes('..') || m[2].includes('//') || m[2].endsWith('/')) return null;
  return {platform:m[1],branch:m[2],sha:m[3]};
}
export function trustedReleaseComment(event) {
  return event?.repository?.full_name===REPO && event?.issue?.number===CONTROL_ISSUE
    && !event.issue.pull_request
    && event?.comment?.author_association==='OWNER'
    && event?.comment?.user?.login==='masarray'
    && Number.isSafeInteger(event.comment.id);
}
export async function releaseDispatch({event,api}) {
  if (!trustedReleaseComment(event)) return {kind:'ignored'};
  const cmd=parseReleaseCommand(event.comment.body);
  if (!cmd) return {kind:'ignored'};
  const ref=await api('/git/ref/heads/'+cmd.branch);
  if (ref?.ref!=='refs/heads/'+cmd.branch || ref?.object?.sha!==cmd.sha)
    throw Error('RELEASE_STALE_SHA_OR_BRANCH');
  const older=[];
  for(let page=1;page<=10;page++) {
    const chunk=await api('/issues/'+CONTROL_ISSUE+'/comments?per_page=100&page='+page);
    older.push(...chunk);
    if(chunk.length<100)break;
    if(page===10)throw Error('RELEASE_COMMENT_LIMIT');
  }
  if(older.some(c=>c.id<event.comment.id && c.user?.login==='masarray'
      && c.author_association==='OWNER'
      && parseReleaseCommand(c.body)?.tag===cmd.tag))
    return {kind:'skipped',reason:'earlier owner request for tag',...cmd};
  try {
    await api('/releases/tags/'+cmd.tag);
    return {kind:'skipped',reason:'release already exists',...cmd};
  } catch(err) {
    if(!String(err).includes('GITHUB_API_404'))throw err;
  }
  const query='/actions/workflows/alpha-preview-publish.yml/runs?event=workflow_dispatch&branch='+
    encodeURIComponent(cmd.branch)+'&per_page=100';
  const old=(await api(query)).workflow_runs||[];
  if(old.some(r=>r.head_sha===cmd.sha && r.conclusion==='success'))
    return {kind:'skipped',reason:'previously qualified',...cmd};
  if(old.some(r=>r.head_sha===cmd.sha && r.status!=='completed'))
    return {kind:'skipped',reason:'qualification already active',...cmd};
  if(old.some(r=>r.head_sha===cmd.sha && r.status==='completed'))
    throw Error('RELEASE_FAILED_SAME_SHA_REQUIRES_FIX_NOT_RETRY');
  // Reject incomplete release candidates before allocating a Windows runner.
  // Both checks read from the exact immutable SHA, never moving main.
  let notes;
  try {
    notes=await api('/contents/docs/releases/'+cmd.tag+'.md?ref='+cmd.sha);
  } catch(err) {
    if(String(err).includes('GITHUB_API_404'))
      throw Error('RELEASE_NOTES_MISSING_PRECHECK');
    throw err;
  }
  if(notes?.type!=='file'||!Number.isSafeInteger(notes.size)||notes.size<100)
    throw Error('RELEASE_NOTES_EMPTY_PRECHECK');
  const cmake=await api('/contents/CMakeLists.txt?ref='+cmd.sha);
  const version=cmd.tag.match(/^v(\d+\.\d+\.\d+)-alpha\./)?.[1];
  if(cmake?.type!=='file'||cmake.encoding!=='base64'||!version||
      !Buffer.from(cmake.content||'','base64').toString('utf8')
        .includes('project(ARMixConsole VERSION '+version+' LANGUAGES'))
    throw Error('RELEASE_CMAKE_VERSION_PRECHECK');
  await api('/actions/workflows/alpha-preview-publish.yml/dispatches','POST',
    {ref:cmd.branch, inputs:{release_tag:cmd.tag, expected_sha:cmd.sha}});
  return {kind:'dispatched',...cmd};
}

export async function portDispatch({event,api}) {
  if (!trustedReleaseComment(event)) return {kind:'ignored'};
  const cmd=parsePortCommand(event.comment.body);
  if (!cmd) return {kind:'ignored'};
  const ref=await api('/git/ref/heads/'+cmd.branch);
  if (ref?.ref!=='refs/heads/'+cmd.branch || ref?.object?.sha!==cmd.sha)
    throw Error('PORT_STALE_SHA_OR_BRANCH');
  const older=[];
  for(let page=1;page<=10;page++) {
    const chunk=await api('/issues/'+CONTROL_ISSUE+'/comments?per_page=100&page='+page);
    older.push(...chunk);
    if(chunk.length<100)break;
    if(page===10)throw Error('PORT_COMMENT_LIMIT');
  }
  if(older.some(c=>c.id<event.comment.id && c.user?.login==='masarray'
      && c.author_association==='OWNER'
      && parsePortCommand(c.body)?.platform===cmd.platform
      && parsePortCommand(c.body)?.sha===cmd.sha))
    return {kind:'skipped',reason:'same platform and SHA already requested',...cmd};
  // Separate workflow is deliberately manual-dispatch-only: its preflight
  // fails cheaply on current Windows-only source before any macOS runner.
  await api('/actions/workflows/crossplatform-native-qualification.yml/dispatches','POST',
    {ref:cmd.branch,inputs:{platform:cmd.platform,source_branch:cmd.branch,expected_sha:cmd.sha}});
  return {kind:'dispatched',...cmd};
}

async function main() {
  const event=JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH,'utf8'));
  if(!trustedReleaseComment(event))return;
  const token=process.env.GH_TOKEN;
  if(!token)throw Error('RELEASE_TOKEN_MISSING');
  const api=async(path,method='GET',body)=>{
    const res=await fetch('https://api.github.com/repos/'+REPO+path,{
      method,
      headers:{
        Authorization:'Bearer '+token,Accept:'application/vnd.github+json',
        'X-GitHub-Api-Version':'2022-11-28',
        ...(body?{'Content-Type':'application/json'}:{})
      },
      ...(body?{body:JSON.stringify(body)}:{})
    });
    if(!res.ok)throw Error('GITHUB_API_'+res.status+' '+path);
    return res.status===204?null:res.json();
  };
  const isPort=!!parsePortCommand(event.comment.body);
  const result=isPort?await portDispatch({event,api}):
    await releaseDispatch({event,api});
  console.log('SMART_'+(isPort?'PORT_':'RELEASE_')+result.kind.toUpperCase()+
    ' tag='+(result.tag||'')+' platform='+(result.platform||'')+
    ' sha='+(result.sha||'')+' '+(result.reason||''));
}
if(process.argv[1]?.endsWith('release_ci_dispatch.mjs'))
  main().catch(e=>{console.error(e.message);process.exitCode=1;});
