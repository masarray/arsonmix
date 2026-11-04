// SPDX-License-Identifier: AGPL-3.0-only
// M2.5: fail *new* PRs that duplicate the LIVE implementation owner.
// This is a cheap control-plane check, never a DSP implementation.
// Existing PRs <= #399 are advisory until their unique work is absorbed.
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';

export const POLICY_ISSUE=399;
export const STRICT_FROM_PR=400;

// Shared junction files (CMake/workflows) require review, but are not an
// automatic rejection: GUI, engine and FAT genuinely have to meet here.
const SHARED=/^(?:\.github\/workflows\/|CMakeLists\.txt$|scripts\/(?:package_native_windows|verify_native_windows_artifact)\.ps1$)/;
const REGIONS=[
  ['engine', /^(?:engine\/|src\/(?:LiveAudioEngine|LiveDryRoutingLab|ConsoleLiveBridgeV6|ApprovedNativeV8Projection|NativeVerifiedSourcePatch|NativeDryShowRecall|NativeLiveShowCapture|NativeShowSaveWorker)\.(?:h|cpp)$)/],
  ['host', /^src\/(?:Main|ApprovedConsoleNativeTransportV6|ApprovedConsoleM2Snapshot)\.(?:h|cpp)$/],
  ['native-gui-bridge', /^(?:web\/approved-native-(?:bridge|contract|m2-state)\.js|scripts\/build_approved_native_gui\.py)$/],
  ['approved-gui', /^(?:site\/preview(?:\/|-v2\/|-buttons\/)|scripts\/build_pages\.py$)/],
  ['release-admission', /^scripts\/(?:check_console_puzzle|check_approved_native_gui_release)\.mjs$/],
  ['hardware-fat', /^scripts\/(?:collect_hardware_fat_windows\.ps1|console_v6_fat_gate\.mjs)$/],
];
const FORBIDDEN_NEW=/^web\/(?:console-native\.(?:html|css|js)|index\.html)$/;

export function region(path) {
  for(const [label,re] of REGIONS)if(re.test(path))return label;
  return null;
}
function filesOf(pr) {
  return (pr.files||[]).map(v=>typeof v==='string'?{filename:v,status:'modified'}:v)
    .filter(v=>typeof v?.filename==='string');
}
export function auditWorkstreams(current, openPrs) {
  const files=filesOf(current), ours=new Set(files.map(f=>f.filename));
  const mine=new Map();
  for(const f of files) {
    const label=region(f.filename);
    if(label) {
      if(!mine.has(label))mine.set(label,[]);
      mine.get(label).push(f.filename);
    }
  }
  const hard=[], advisory=[];
  for(const f of files) {
    if(FORBIDDEN_NEW.test(f.filename) && f.status!=='removed')
      hard.push({reason:'rejected-alternative-GUI',file:f.filename,other:null});
  }
  for(const other of openPrs) {
    if(other.number===current.number || other.state==='closed')continue;
    const theirs=filesOf(other),otherRegions=new Map();
    for(const f of theirs) {
      const label=region(f.filename);
      if(label) {
        if(!otherRegions.has(label))otherRegions.set(label,[]);
        otherRegions.get(label).push(f.filename);
      }
      // Renames can be a hidden transfer of a protected path.
      if(typeof f.previous_filename==='string') {
        const previous=region(f.previous_filename);
        if(previous) {
          if(!otherRegions.has(previous))otherRegions.set(previous,[]);
          otherRegions.get(previous).push(f.previous_filename);
        }
      }
    }
    for(const [label,paths] of mine) {
      if(otherRegions.has(label))hard.push({
        reason:'duplicate-'+label,file:paths[0],other:other.number,
        overlapping:otherRegions.get(label)[0],
      });
    }
    const otherFiles=new Set(theirs.flatMap(f=>[f.filename,f.previous_filename].filter(Boolean)));
    for(const path of ours) {
      if(SHARED.test(path)&&otherFiles.has(path))
        advisory.push({reason:'shared-merge-junction',file:path,other:other.number});
    }
  }
  return {hard,advisory,enforced:Number(current.number)>=STRICT_FROM_PR};
}
async function json(url,token) {
  const response=await fetch(url,{headers:{
    'Accept':'application/vnd.github+json',
    'User-Agent':'ArSonMix-M2.5-ownership-check',
    ...(token?{'Authorization':'Bearer '+token}:{})
  },signal:AbortSignal.timeout(18000)});
  if(!response.ok)throw Error('GitHub PR metadata unavailable: HTTP '+response.status+
    ' / '+url.split('?')[0]);
  return response.json();
}
async function remotePRs(repo,token) {
  const root='https://api.github.com/repos/'+repo;
  const prs=await json(root+'/pulls?state=open&per_page=100',token);
  if(prs.length===100)throw Error('Cannot certify overlap: >99 open PRs (pagination required)');
  return Promise.all(prs.map(async pr=>{
    const files=[];
    for(let page=1;page<=5;page++) {
      const a=await json(root+'/pulls/'+pr.number+'/files?per_page=100&page='+page,token);
      files.push(...a.map(f=>({filename:f.filename,status:f.status,
        previous_filename:f.previous_filename})));
      if(a.length<100)break;
      if(page===5)throw Error('PR #'+pr.number+' exceeds 500 changed files');
    }
    return {number:pr.number,title:pr.title,state:pr.state,files};
  }));
}
export async function main() {
  if(process.argv.includes('--test-fixture')) {
    const at=process.argv.indexOf('--test-fixture');
    const bundle=JSON.parse(readFileSync(process.argv[at+1],'utf8'));
    const out=auditWorkstreams(bundle.current,bundle.open);
    console.log(JSON.stringify(out,null,2));
    if(out.enforced&&out.hard.length)process.exitCode=1;
    return;
  }
  const eventPath=process.env.GITHUB_EVENT_PATH;
  if(!eventPath){console.log('M2.5 ownership check: no PR event, skipping.');return;}
  const event=JSON.parse(readFileSync(eventPath,'utf8'));
  const number=event.pull_request?.number;
  if(!Number.isInteger(number)){console.log('M2.5: non-PR event; skip.');return;}
  const repo=process.env.GITHUB_REPOSITORY;
  if(!/^[\w.-]+\/[\w.-]+$/.test(repo||''))throw Error('Invalid GITHUB_REPOSITORY');
  const prs=await remotePRs(repo,process.env.GH_TOKEN||process.env.GITHUB_TOKEN);
  const current=prs.find(x=>x.number===number);
  if(!current)throw Error('Cannot certify overlap: current PR #'+number+' not in open PRs');
  const report=auditWorkstreams(current,prs);
  const notify=(severity,entry)=>{
    const text='M2.5 '+entry.reason+' '+entry.file+
      (entry.other?' vs PR #'+entry.other+' ('+entry.overlapping+')':'')+
      ' · see #'+POLICY_ISSUE;
    console.log('::'+severity+'::'+text);
  };
  for(const item of report.advisory)notify('warning',item);
  for(const item of report.hard)notify(report.enforced?'error':'warning',item);
  console.log('M2.5 #'+number+': '+report.hard.length+' production ownership conflicts, '+
    report.advisory.length+' shared junctions; '+
    (report.enforced?'STRICT':'ADVISORY (legacy PR)')+
    '; no new workflow, no extra runner.');
  if(report.enforced&&report.hard.length)process.exitCode=1;
}
if(process.argv[1]&&fileURLToPath(import.meta.url)===process.argv[1]) {
  main().catch(err=>{console.error('::error::M2.5 ownership check: '+err.message);process.exitCode=1;});
}
