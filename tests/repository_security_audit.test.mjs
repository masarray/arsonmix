// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {evaluate,collect,REQUIRED_CONTEXT} from '../scripts/repository_security_audit.mjs';
const rule=(type,parameters)=>({type,...(parameters?{parameters}:{})});
function baseline(){
  return {repository:{full_name:'masarray/arsonmix',visibility:'public',default_branch:'main',archived:false},
    main:{protected:true},rulesets:[{id:26568033,target:'branch',enforcement:'active',
      conditions:{ref_name:{include:['refs/heads/main'],exclude:[]}},bypass_actors:[],
      rules:[rule('deletion'),rule('non_fast_forward'),rule('pull_request'),rule('required_status_checks',
        {strict_required_status_checks_policy:true,do_not_enforce_on_create:false,
          required_status_checks:[{context:REQUIRED_CONTEXT,integration_id:15368}]})]}]};
}
const state=(b,id)=>evaluate(b).find(x=>x.id===id)?.state;
test('observed baseline passes while unconfigured and private controls are explicit',()=>{
  const b=baseline();
  for(const id of ['REPOSITORY','MAIN_PROTECTED','MAIN_RULESET','MAIN_BYPASS']) assert.equal(state(b,id),'PASS');
  for(const id of ['RELEASE_TAGS','RELEASE_BRANCHES','BETA_RELEASE_BRANCHES']) assert.equal(state(b,id),'PENDING');
  for(const id of ['SECRET_SCANNING','PUSH_PROTECTION','PRIVATE_REPORTING','ACCOUNT_AND_BACKUP']) assert.equal(state(b,id),'UNKNOWN');
});
test('wrong identity and unprotected main fail',()=>{
  const b=baseline(); b.repository.visibility='private'; b.main.protected=false;
  assert.equal(state(b,'REPOSITORY'),'FAIL');assert.equal(state(b,'MAIN_PROTECTED'),'FAIL');
});
for(const kind of ['deletion','non_fast_forward','pull_request','required_status_checks'])
  test('missing main rule '+kind+' fails',()=>{
    const b=baseline();b.rulesets[0].rules=b.rulesets[0].rules.filter(r=>r.type!==kind);
    assert.equal(state(b,'MAIN_RULESET'),'FAIL');
  });
test('inactive or excluded ruleset fails',()=>{
  const b=baseline();b.rulesets[0].enforcement='evaluate';
  assert.equal(state(b,'MAIN_RULESET'),'FAIL');
  b.rulesets[0].enforcement='active';b.rulesets[0].conditions.ref_name.exclude=['refs/heads/main'];
  assert.equal(state(b,'MAIN_RULESET'),'FAIL');
});
test('forged status provider and non-strict status fail',()=>{
  const b=baseline();const p=b.rulesets[0].rules.at(-1).parameters;
  p.required_status_checks[0].integration_id=123;
  assert.equal(state(b,'MAIN_RULESET'),'FAIL');
  p.required_status_checks[0].integration_id=15368;p.strict_required_status_checks_policy=false;
  assert.equal(state(b,'MAIN_RULESET'),'FAIL');
});
test('main bypass is fail if present, unknown if API omits',()=>{
  const b=baseline();b.rulesets[0].bypass_actors=[{actor_id:1}];
  assert.equal(state(b,'MAIN_BYPASS'),'FAIL');
  delete b.rulesets[0].bypass_actors;assert.equal(state(b,'MAIN_BYPASS'),'UNKNOWN');
});
test('import recipes protect updates/deletes without blocking new release creation',()=>{
  const b=baseline();
  for(const file of ['release-tags','release-branches']){
    const r=JSON.parse(readFileSync('docs/security/rulesets/'+file+'.json','utf8'));
    assert.equal(r.rules.some(x=>x.type==='creation'),false);
    if(file==='release-branches')assert.equal(r.rules.some(x=>x.type==='update'),false);
    b.rulesets.push(r);
  }
  assert.equal(state(b,'RELEASE_TAGS'),'PASS');assert.equal(state(b,'RELEASE_BRANCHES'),'PASS');assert.equal(state(b,'BETA_RELEASE_BRANCHES'),'PASS');
  b.rulesets[1].bypass_actors=[{actor_id:1}];assert.equal(state(b,'RELEASE_TAGS'),'PENDING');
});
test('known secret scanning settings distinguish enabled, disabled and absent',()=>{
  const b=baseline();b.repository.security_and_analysis={secret_scanning:{status:'enabled'},secret_scanning_push_protection:{status:'disabled'}};
  assert.equal(state(b,'SECRET_SCANNING'),'PASS');assert.equal(state(b,'PUSH_PROTECTION'),'PENDING');
});
test('metadata transport errors cannot become success',async()=>{
  await assert.rejects(collect(async()=>{throw Error('denied')}),/denied/);
  assert.throws(()=>evaluate({repository:{},main:{},rulesets:[{}]}),/Invalid/);
});
test('collector uses read-only bounded endpoint paths',async()=>{
  const b=baseline(),calls=[];
  const data=await collect(async p=>{
    calls.push(p);
    if(p==='')return b.repository;
    if(p==='/branches/main')return b.main;
    if(p==='/rulesets?per_page=100&page=1')return [{id:26568033}];
    if(p==='/rulesets/26568033')return b.rulesets[0];
    throw Error('unexpected '+p);
  });
  assert.deepEqual(data,b);assert.equal(calls.length,4);
});
test('public intake routes to security policy and sensitive ownership includes itself',()=>{
  const read=p=>readFileSync(p,'utf8');
  assert.match(read('.github/ISSUE_TEMPLATE/config.yml'),/security\/policy/);
  for(const p of ['01_bug_report.yml','02_feature_request.yml'])assert.match(read('.github/ISSUE_TEMPLATE/'+p),/security\/policy/);
  assert.match(read('.github/CODEOWNERS'),/^\/\.github\/ @masarray$/m);
});
