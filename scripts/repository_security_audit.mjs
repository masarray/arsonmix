// SPDX-License-Identifier: AGPL-3.0-only
// Read-only, metadata-only guard. No privileged token or repository mutations.
import { pathToFileURL } from 'node:url';
export const REQUIRED_CONTEXT = 'ArSonMix / Required CI Merge Gate';
const REQUIRED_APP = 15368; // GitHub Actions integration in the audited ruleset
const MAIN = 'refs/heads/main';
const REPO = 'masarray/arsonmix';
const types = r => new Set((r.rules ?? []).map(x => x.type));
const exactTarget = (r, target, pattern) =>
  r.target === target && r.enforcement === 'active' &&
  Array.isArray(r.conditions?.ref_name?.include) &&
  r.conditions.ref_name.include.includes(pattern) &&
  Array.isArray(r.conditions.ref_name.exclude) &&
  r.conditions.ref_name.exclude.length === 0;
const noBypass = r => Array.isArray(r.bypass_actors) && r.bypass_actors.length === 0;

export function evaluate({repository, main, rulesets}) {
  if (!repository || !main || !Array.isArray(rulesets) ||
      rulesets.some(r => !r || !Array.isArray(r.rules))) throw Error('Invalid audit metadata');
  const checks = [];
  const add = (id, state, message) => checks.push({id,state,message});
  add('REPOSITORY', repository.full_name === REPO && repository.visibility === 'public' &&
    repository.default_branch === 'main' && repository.archived === false ? 'PASS' : 'FAIL',
    'Official public, non-archived repository and main default');
  add('MAIN_PROTECTED', main.protected === true ? 'PASS' : 'FAIL', 'GitHub main protected flag');
  // Require one active, exact-target ruleset containing the complete known main
  // protection contract. Do not infer arbitrary wildcard/exclusion semantics.
  const mains = rulesets.filter(r => exactTarget(r,'branch',MAIN) &&
    ['deletion','non_fast_forward','pull_request','required_status_checks'].every(x => types(r).has(x)) &&
    r.rules.some(x => x.type === 'required_status_checks' &&
      x.parameters?.strict_required_status_checks_policy === true &&
      x.parameters?.do_not_enforce_on_create === false &&
      x.parameters?.required_status_checks?.some(c => c.context === REQUIRED_CONTEXT && c.integration_id === REQUIRED_APP)));
  add('MAIN_RULESET',mains.length ? 'PASS' : 'FAIL','Active main PR / no deletion / no force push / strict exact named CI bound to GitHub Actions');
  if (mains.length) {
    add('MAIN_BYPASS',mains.some(noBypass) ? 'PASS' :
      mains.every(r => Array.isArray(r.bypass_actors)) ? 'FAIL' : 'UNKNOWN',
      'No bypass actors on a qualifying main ruleset; omitted API field needs admin review');
  }
  function releaseCheck(id,target,pattern,required) {
    const candidates = rulesets.filter(r => exactTarget(r,target,pattern) &&
      required.every(x => types(r).has(x)));
    const state = candidates.some(noBypass) ? 'PASS' : candidates.length &&
      candidates.some(r => !Array.isArray(r.bypass_actors)) ? 'UNKNOWN' : 'PENDING';
    add(id,state,'Expected active exact-pattern release rule with no bypass actors; admin setup/review if not PASS');
  }
  releaseCheck('RELEASE_TAGS','tag','refs/tags/v*',['deletion','update']);
  releaseCheck('RELEASE_BRANCHES','branch','refs/heads/releases/alpha/*',['deletion','non_fast_forward']);
  const settings = repository.security_and_analysis;
  for (const [key,id] of [['secret_scanning','SECRET_SCANNING'],['secret_scanning_push_protection','PUSH_PROTECTION']]) {
    const value=settings?.[key]?.status;
    add(id,value==='enabled'?'PASS':value==='disabled'?'PENDING':'UNKNOWN','Native GitHub setting; admin verification if not visible');
  }
  add('PRIVATE_REPORTING','UNKNOWN','Requires administrative verification; policy file is not proof of enablement');
  add('ACCOUNT_AND_BACKUP','UNKNOWN','Owner must privately verify account access, recovery and independent backups');
  return checks;
}

export async function collect(api) {
  const [repository,main] = await Promise.all([api(''),api('/branches/main')]);
  const summaries = [];
  for (let page=1;page<=5;page++) {
    const rows = await api('/rulesets?per_page=100&page='+page);
    if (!Array.isArray(rows)) throw Error('Invalid ruleset list');
    summaries.push(...rows);
    if (rows.length<100) break;
    if (page===5) throw Error('Ruleset page limit exceeded');
  }
  if (summaries.some(r=>!Number.isSafeInteger(r.id) || r.id<1)) throw Error('Invalid ruleset ID');
  // Sequential and bounded. Never read secret values or security alert details.
  const rulesets=[];
  for (const summary of summaries) rulesets.push(await api('/rulesets/'+summary.id));
  return {repository,main,rulesets};
}
async function run() {
  const token=process.env.GH_TOKEN;
  const api=async path=>{
    const headers={Accept:'application/vnd.github+json','X-GitHub-Api-Version':'2022-11-28'};
    if (token) headers.Authorization='Bearer '+token;
    const response=await fetch('https://api.github.com/repos/'+REPO+path,
      {headers,redirect:'error',signal:AbortSignal.timeout(10000)});
    if (!response.ok) throw Error('GitHub metadata request failed: HTTP '+response.status);
    return response.json();
  };
  const checks=evaluate(await collect(api));
  for (const c of checks) console.log(c.state+' '+c.id+': '+c.message);
  const failed=checks.some(c=>c.state==='FAIL');
  const pending=checks.some(c=>c.state!=='PASS');
  console.log(failed?'BASELINE DRIFT — investigate':pending?'MAIN BASELINE CHECKED — additional controls pending or unknown':'OBSERVED CONTROLS PASS');
  if (failed) process.exitCode=1;
}
if (process.argv[1] && import.meta.url===pathToFileURL(process.argv[1]).href)
  run().catch(e=>{console.error('Audit unavailable: '+e.message);process.exitCode=1;});
