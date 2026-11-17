// SPDX-License-Identifier: AGPL-3.0-only
// Trusted, event-driven PR merge status. NEVER checks out PR code in the
// privileged status publisher. Green CI means exact-head source checks, not FAT.
export const context="ArSonMix / Required CI Merge Gate";
export const workflowNames={
  native:"p1-windows.yml",ui:"ui-smoke.yml",release:"release-contracts.yml"
};
const nativeFastName="Fast native contracts (no Windows / JUCE checkout)";
const nativeFullName="Windows MSVC / realtime CTest";
const uiFastName="Fast UI contracts (every relevant PR commit)";
const uiFullName="Full Chromium /preview/ visual and interaction qualification";
const releaseName="No native compiler; owner authorization, catalog integrity and YAML shape";

export function requiredLanes(files) {
  if(!Array.isArray(files)||!files.length||
      files.some(f=>typeof f!=="string"||!f||f.includes("..")))
    throw Error("INVALID_PR_FILES");
  const any=fn=>files.some(fn);
  const nativeCore=any(f=>/^src\/|^engine\/|^integration\/.*\.(?:cpp|hpp|h)$/.test(f)||
    f==="CMakeLists.txt"||/^tests\/.*\.cpp$/.test(f));
  const nativeFast=nativeCore||any(f=>
    ["web/approved-native-bridge.js","web/approved-native-contract.js",
     "tests/approved_native_m2_contract.test.cjs",
     ".github/workflows/p1-windows.yml"].includes(f));
  const uiFast=any(f=>/^web\/|^site\/preview-v2\/|^site\/preview-buttons\/|^integration\/console-v6\//.test(f)||
    (f.startsWith("tests/")&&f.endsWith(".py")&&
     !["tests/test_platform_portability.py","tests/test_release_catalog.py",
       "tests/test_release_setup_zip.py"].includes(f))||
    ["scripts/build_pages.py","tests/approved_native_m2_contract.test.cjs",
     ".github/workflows/ui-smoke.yml"].includes(f));
  const uiFull=any(f=>/^web\/|^site\/preview-v2\/|^site\/preview-buttons\/|^integration\/console-v6\//.test(f)||
    (f.startsWith("tests/")&&f.endsWith(".py")&&
     !["tests/test_platform_portability.py","tests/test_release_catalog.py",
       "tests/test_release_setup_zip.py"].includes(f))||
    ["scripts/build_pages.py",".github/workflows/ui-smoke.yml"].includes(f));
  const releaseFast=any(f=>/^(?:installer\/|config\/factoryfx-)/.test(f) ||
    /^scripts\/(?:package_native_|build_verify_windows_installer|verify_native_windows_artifact|test_inno_graceful_update_windows|verify_embedded_legal|build_approved_native_gui|build_release_catalog|verify_release_setup_zip|release_ci_dispatch|check_platform_portability)/.test(f)||
    /^tests\/(?:license_distribution_contract|test_release_catalog|test_release_setup_zip|test_platform_portability|downloads_runtime_contract|landing_page_download_contract|release_ci_dispatch_contract)/.test(f)||
    ["LICENSE","LICENSE-NOTICE.txt","src/Main.cpp","src/EmbeddedLegalResources.h",
     "CMakeLists.txt","site/index.html","site/downloads.js",
     "web/approved-native-legal.js",
     ".github/workflows/release-contracts.yml",
     ".github/workflows/release-control.yml",
     ".github/workflows/alpha-preview-publish.yml",
     ".github/workflows/crossplatform-native-qualification.yml"].includes(f));
  return {nativeFast,nativeFull:nativeCore,uiFast,uiFull,releaseFast};
}

export function sourceRun(run,workflow,sha,event) {
  return !!run && run.head_sha===sha && run.event===event &&
    typeof run.path==="string"&&run.path.includes("/"+workflow+"@");
}
export function expectedWindowsTitle(prNumber,sha) {
  return "ArSonMix NATIVE PR"+prNumber+" "+sha;
}
export function findRuns(runs,number,sha) {
  const automatic=(workflow)=>runs.filter(r=>
    sourceRun(r,workflow,sha,"pull_request")&&
    (!Array.isArray(r.pull_requests)||r.pull_requests.length===0||
      r.pull_requests.some(p=>p.number===number)))
    .sort((a,b)=>b.id-a.id)[0];
  const fullUi=runs.filter(r=>sourceRun(r,workflowNames.ui,sha,"workflow_dispatch"))
    .sort((a,b)=>b.id-a.id)[0];
  const fullWin=runs.filter(r=>r.event==="workflow_dispatch"&&
    typeof r.path==="string"&&r.path.includes("/"+workflowNames.native+"@")&&
    (r.display_title||r.name)===expectedWindowsTitle(number,sha))
    .sort((a,b)=>b.id-a.id)[0];
  return {native:automatic(workflowNames.native),
    ui:automatic(workflowNames.ui),release:automatic(workflowNames.release),
    fullUi,fullWin};
}

export function evaluate(lanes,matched,jobs) {
  const checks=[
    ["nativeFast",matched.native,nativeFastName],
    ["nativeFull",matched.fullWin,nativeFullName],
    ["uiFast",matched.ui,uiFastName],
    ["uiFull",matched.fullUi,uiFullName],
    ["releaseFast",matched.release,releaseName]
  ];
  const waiting=[],failed=[],passed=[];
  for(const [key,run,jobName] of checks) {
    if(!lanes[key])continue;
    if(!run){waiting.push(key+": not dispatched");continue;}
    const list=jobs[run.id];
    if(!Array.isArray(list)){waiting.push(key+": no job readback");continue;}
    const matching=list.filter(j=>j.name===jobName);
    if(matching.length!==1){failed.push(key+": missing/duplicate job");continue;}
    const j=matching[0];
    if(run.status!=="completed"||j.status!=="completed") {
      waiting.push(key+": running");continue;
    }
    if(run.conclusion!=="success"||j.conclusion!=="success")
      failed.push(key+": "+(j.conclusion||run.conclusion||"unknown"));
    else passed.push(key);
  }
  return {state:failed.length?"failure":waiting.length?"pending":"success",
    description:(failed.length?failed:waiting.length?waiting:["All scoped checks passed"])
      .slice(0,2).join("; ").slice(0,135),waiting,failed,passed};
}

async function request(repo,path,token,method="GET",payload) {
  const r=await fetch("https://api.github.com/repos/"+repo+path,{
    method,headers:{"Accept":"application/vnd.github+json",
      "Authorization":"Bearer "+token,"X-GitHub-Api-Version":"2022-11-28",
      ...(payload?{"Content-Type":"application/json"}:{})},
    ...(payload?{body:JSON.stringify(payload)}:{})
  });
  if(!r.ok)throw Error("GITHUB_HTTP_"+r.status+" "+path+" "+
    (await r.text()).slice(0,200));
  return r.status===204?null:r.json();
}
async function allPages(api,path,key,max=7) {
  const all=[];
  for(let page=1;page<=max;page++){
    const j=await api(path+(path.includes("?")?"&":"?")+
      "per_page=100&page="+page);
    const part=key?j?.[key]:j;
    if(!Array.isArray(part))throw Error("INVALID_PAGINATED_API_"+path);
    all.push(...part);
    if(part.length<100)return all;
  }
  throw Error("PR_CI_PAGINATION_LIMIT_"+path);
}
export function extractPrNumbers(event) {
  if(event.pull_request?.number)return [event.pull_request.number];
  const run=event.workflow_run;
  if(!run)return [];
  const title=run.display_title||run.name||"";
  const m=/^ArSonMix (?:NATIVE|FAT|FIELD) PR([1-9][0-9]*) [0-9a-f]{40}$/.exec(title);
  if(m)return [Number(m[1])];
  return [...new Set((run.pull_requests||[]).map(p=>p.number)
    .filter(n=>Number.isSafeInteger(n)&&n>0))];
}

export async function refreshOne(repo,number,api) {
  const pr=await api("/pulls/"+number);
  if(pr?.state!=="open"||pr?.base?.ref!=="main"||
     pr?.base?.repo?.full_name!==repo||pr?.head?.repo?.full_name!==repo)
    return {kind:"ignored",number};
  const sha=pr.head.sha;
  if(!/^[a-f0-9]{40}$/.test(sha))throw Error("INVALID_PR_SHA");
  const files=(await allPages(api,"/pulls/"+number+"/files",null))
    .map(f=>f.filename);
  const lanes=requiredLanes(files);
  const allRuns=await allPages(api,"/actions/runs?head_sha="+sha,
    "workflow_runs",6);
  // Manual Windows runs have main's head_sha. Their trusted run title
  // includes the validated exact PR head SHA; never infer it from main.
  const winRuns=lanes.nativeFull?await allPages(api,
    "/actions/workflows/p1-windows.yml/runs?event=workflow_dispatch",
    "workflow_runs",6):[];
  const matched=findRuns([...allRuns,...winRuns],number,sha);
  const jobs={};
  for(const run of new Set(Object.values(matched).filter(Boolean)))
    jobs[run.id]=(await allPages(api,"/actions/runs/"+run.id+"/jobs","jobs",2));
  const result=evaluate(lanes,matched,jobs);
  await api("/statuses/"+sha,"POST",{
    state:result.state,context,description:result.description,
    target_url:"https://github.com/"+repo+"/pull/"+number+"/checks"
  });
  return {kind:"published",number,sha,lanes,...result};
}

async function main(){
  const repo=process.env.GITHUB_REPOSITORY||"";
  const token=process.env.GH_TOKEN||"";
  if(!/^[a-zA-Z0-9_.-]+\/[a-zA-Z0-9_.-]+$/.test(repo)||!token)
    throw Error("INVALID_GITHUB_ENV");
  const event=JSON.parse(await (await import("node:fs/promises"))
    .readFile(process.env.GITHUB_EVENT_PATH,"utf8"));
  const api=(path,method="GET",payload)=>request(repo,path,token,method,payload);
  const prs=extractPrNumbers(event);
  for(const n of prs)console.log(JSON.stringify(await refreshOne(repo,n,api)));
  // Some workflow_run events omit pull_requests. Reconcile their source SHA
  // from GitHub's verified commit->PR association, never guess a PR number.
  if(!prs.length && event.workflow_run?.event==="pull_request" &&
     /^[a-f0-9]{40}$/.test(event.workflow_run.head_sha||"")) {
    const related=await api("/commits/"+event.workflow_run.head_sha+"/pulls?per_page=100");
    for(const pr of related.filter(p=>p.state==="open"))
      console.log(JSON.stringify(await refreshOne(repo,pr.number,api)));
  }
}
if(process.argv[1]?.endsWith("required_pr_merge_gate.mjs"))
  main().catch(e=>{console.error(e.stack||e.message);process.exitCode=1;});
