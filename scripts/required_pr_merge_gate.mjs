// SPDX-License-Identifier: AGPL-3.0-only
// Trusted, event-driven PR merge status. NEVER checks out PR code in the
// privileged status publisher. Green CI means exact-head source checks, not FAT.
export const context="ArSonMix / Required CI Merge Gate";
export const workflowNames={
  native:"p1-windows.yml",ui:"ui-smoke.yml",release:"release-contracts.yml",security:"security-regression.yml"
};
const nativeFastName="Fast native contracts (no Windows / JUCE checkout)";
const nativeFullName="Windows MSVC / realtime CTest";
const uiFastName="Fast UI contracts (every relevant PR commit)";
const uiFullName="Full Chromium /preview/ visual and interaction qualification";
const securityName="Security regression / native ASan UBSan fuzz and release contracts";
const securityPaths=new Set([
  "src/BoundedDataInput.h",
  "src/StudioBackup.h",
  "tests/studio_backup_test.cpp",
  "scripts/disaster_recovery.py",
  "scripts/tests/test_disaster_recovery.py",
  "src/Vst3ScanIsolation.h",
  "src/Vst3DescriptionCache.h",
  "src/PluginCatalog.h",
  "src/PortableWorkspace.h",
  "src/NativeShowStore.h",
  "src/NativeShowSchema.h",
  "src/NativePresetArchive.h",
  "src/NativeMixerSessionStore.h",
  "src/NativeMixerSessionDocument.h",
  "src/BuiltInFxUserPresetStore.h",
  "src/RuntimeVendorCrashJournal.h",
  "src/StartupVst3RecoveryGuard.h",
  "src/LiveAudioEngine.h",
  "src/LiveDryRoutingLab.h",
  "src/NativeMixerSessionFxRestore.h",
  "src/Main.cpp",
  "engine/core/LiveChannelFxGraph.h",
  "installer/ArSonMixAutoUpdate.ps1",
  "scripts/release_trust_manifest.py",
  "scripts/supply_chain_audit.mjs",
  "scripts/run_native_memory_safety.sh",
  "scripts/run_security_regression.sh",
  "tests/security_adversarial_input_test.cpp",
  "tests/security_show_codec_fuzz.cpp",
  "tests/test_release_trust_manifest.py",
  "tests/native_preset_roundtrip_test.cpp",
  "tests/portable_workspace_test.cpp",
  "tests/native_show_schema_test.cpp",
  "tests/native_show_store_test.cpp",
  "tests/runtime_vendor_crash_journal_test.cpp",
  "tests/vst3_waves_identity_startup_test.cpp",
  "tests/supply_chain_audit.test.mjs",
  "config/supply-chain-baseline.json",
  "scripts/required_pr_merge_gate.mjs",
  "tests/required_pr_merge_gate.test.mjs",
  ".github/workflows/security-regression.yml",
  ".github/workflows/alpha-preview-publish.yml",
  ".github/workflows/crossplatform-native-qualification.yml",
  ".github/workflows/required-pr-merge-gate.yml"
]);
const releaseName="No native compiler; owner authorization, catalog integrity and YAML shape";

export function requiredLanes(files) {
  if(!Array.isArray(files)||!files.length||
      files.some(f=>typeof f!=="string"||!f||f.includes("..")))
    throw Error("INVALID_PR_FILES");
  const any=fn=>files.some(fn);
  const nativeCore=any(f=>/^src\/|^engine\/|^integration\/.*\.(?:cpp|hpp|h)$/.test(f)||
    f==="CMakeLists.txt"||/^tests\/.*\.cpp$/.test(f));
  // Must cover EVERY native fast-workflow trigger, including JS transport,
  // packaging, and release-source changes. A missing run is PENDING, not green.
  // Windows MSVC remains required separately only for actual C++/graph scope.
  const nativeFast=nativeCore||any(f=>
    /^integration\/console-v6\//.test(f) ||
    /^config\/factoryfx-(?:approved-releases\.json|notices\/)/.test(f) ||
    /^installer\//.test(f) ||
    /^scripts\/(?:package_native_windows|build_verify_windows_installer|verify_native_windows_artifact|collect_hardware_fat_windows|console_v6_fat_gate|verify_console_s0_native_fixture|run_console_native_s0|check_console_puzzle|check_approved_native_gui_release|check_pr278_console_authority|m3e_fat_evidence_gate|factoryfx_release_bundle|build_approved_native_gui)\./.test(f) ||
    /^scripts\/tests\/test_factoryfx_release_bundle\.py$/.test(f) ||
    /^tests\/(?:windows_installer_contract\.test\.mjs|fixtures\/installer_graceful_close\.cs|approved_native_gui_bundle_test\.py|live_graph_contract_test\.mjs|.*native.*contract.*\.mjs)$/.test(f) ||
    ["web/approved-native-bridge.js","web/approved-native-contract.js",
     "scripts/verify_windows_spatial_fx_packaging.py",
     "scripts/tests/test_windows_spatial_fx_packaging.py",
     "scripts/verify_immersib_approved_pcm_sources.py",
     "scripts/tests/test_immersib_approved_pcm_sources.py",
     "config/immersib_approved_pcm_sources.json",
     "web/bridge.js","web/monitor.js","tests/approved_native_m2_contract.test.cjs",
     "tests/live_routing_ui_contract_test.mjs",
     "scripts/windows_feature_scope.mjs",
     "tests/windows_feature_scope_contract.test.mjs",
     ".github/workflows/p1-windows.yml"].includes(f));
  const uiFast=any(f=>/^web\/|^site\/preview-v2\/|^site\/preview-buttons\/|^integration\/console-v6\//.test(f)||
    (f.startsWith("tests/")&&f.endsWith(".py")&&
     !["tests/test_platform_portability.py","tests/test_release_catalog.py",
       "tests/test_release_setup_zip.py"].includes(f))||
    /^tests\/(?:.*gui.*contract.*\.mjs|native_ui_contract_test\.mjs|live_routing_ui_contract_test\.mjs|wire_graph_canonical_model\.test\.cjs|wire_main_native_handoff_contract\.test\.cjs)$/.test(f) ||
    ["scripts/build_pages.py","scripts/build_ui_css.py",
     "tests/approved_native_m2_contract.test.cjs",
     "tests/chat_ci_dispatch_contract.test.mjs","scripts/chat_ci_dispatch.mjs",
     "tests/ci_workflow_efficiency_test.mjs",
     ".github/workflows/chat-ci-milestone.yml",
     ".github/workflows/pages-publish.yml",
     ".github/workflows/ui-smoke.yml"].includes(f));
  const uiFull=any(f=>/^web\/|^site\/preview-v2\/|^site\/preview-buttons\/|^integration\/console-v6\//.test(f)||
    (f.startsWith("tests/")&&f.endsWith(".py")&&
     !["tests/test_platform_portability.py","tests/test_release_catalog.py",
       "tests/test_release_setup_zip.py"].includes(f))||
    ["scripts/build_pages.py","scripts/build_ui_css.py",
     ".github/workflows/ui-smoke.yml"].includes(f));
  // Match release-contracts.yml path triggers (not arbitrary installer/
  // or factory lock files, which would wait forever for a workflow that
  // does not run). The independent native gate covers other factory assets.
  const releaseFast=any(f=>
    ["LICENSE","LICENSE-NOTICE.txt","src/Main.cpp","src/EmbeddedLegalResources.h",
     "CMakeLists.txt","site/index.html","site/downloads.js",
     "web/approved-native-legal.js","installer/ArSonMix.iss",
     "scripts/package_native_windows.ps1","scripts/package_native_linux.sh",
     "scripts/package_native_macos.sh","scripts/verify_native_windows_artifact.ps1",
     "scripts/verify_windows_spatial_fx_packaging.py",
     "scripts/tests/test_windows_spatial_fx_packaging.py",
     "scripts/verify_immersib_approved_pcm_sources.py",
     "scripts/tests/test_immersib_approved_pcm_sources.py",
     "config/immersib_approved_pcm_sources.json",
     "scripts/build_verify_windows_installer.ps1","scripts/test_inno_graceful_update_windows.ps1",
     "scripts/verify_embedded_legal.py","scripts/build_approved_native_gui.py",
     "scripts/build_release_catalog.py","scripts/verify_release_setup_zip.py",
     "scripts/release_ci_dispatch.mjs","scripts/check_platform_portability.py",
     "scripts/release_trust_manifest.py","tests/test_release_trust_manifest.py",
     ".github/workflows/release-attestation-probe.yml",
     "tests/license_distribution_contract.test.mjs",
     "tests/test_release_catalog.py","tests/test_release_setup_zip.py",
     "tests/test_platform_portability.py","tests/downloads_runtime_contract.test.mjs",
     "tests/landing_page_download_contract_test.py",
     "tests/release_ci_dispatch_contract.test.mjs",
     ".github/workflows/release-contracts.yml",
     ".github/workflows/release-control.yml",
     ".github/workflows/alpha-preview-publish.yml",
     ".github/workflows/crossplatform-native-qualification.yml"].includes(f));
  return {nativeFast,nativeFull:nativeCore,uiFast,uiFull,releaseFast,securityRegression:any(f=>securityPaths.has(f))};
}

export function workflowPath(run,workflow) {
  // GitHub Actions REST usually returns ".github/workflows/name.yml"
  // without an @ref, while some adapters return ".../name.yml@ref".
  const path=typeof run?.path==="string"?run.path.split("@",1)[0]:"";
  return path===".github/workflows/"+workflow ||
    path.endsWith("/.github/workflows/"+workflow);
}
export function sourceRun(run,workflow,sha,event) {
  return !!run && run.head_sha===sha && run.event===event &&
    workflowPath(run,workflow);
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
    workflowPath(r,workflowNames.native)&&
    [expectedWindowsTitle(number,sha),
      "ArSonMix FAT PR"+number+" "+sha].includes(r.display_title||r.name))
    .sort((a,b)=>b.id-a.id)[0];
  return {native:automatic(workflowNames.native),
    ui:automatic(workflowNames.ui),release:automatic(workflowNames.release),security:automatic(workflowNames.security),
    fullUi,fullWin};
}

export function evaluate(lanes,matched,jobs) {
  const checks=[
    ["nativeFast",matched.native,nativeFastName],
    ["nativeFull",matched.fullWin,nativeFullName],
    ["uiFast",matched.ui,uiFastName],
    ["uiFull",matched.fullUi,uiFullName],
    ["releaseFast",matched.release,releaseName],
    ["securityRegression",matched.security,securityName]
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

// The workflow_run payload for an owner-dispatched full Chromium run may have
// no pull_requests, even when GitHub's REST run readback associates a PR.
// Only the approved UI workflow may trigger commit->PR lookup in that case.
// Recompute ALL required lanes from verified jobs; never trust the event's
// success or a display title as a substitute for real qualification.
export function fallbackSourceForWorkflowRun(event) {
  const run=event?.workflow_run;
  const sha=run?.head_sha;
  if (!/^[0-9a-f]{40}$/.test(sha||"")) return null;
  const knownAutomatic=run.event==="pull_request" &&
    Object.values(workflowNames).some(workflow=>workflowPath(run,workflow));
  const approvedManualUi=run.event==="workflow_dispatch" &&
    workflowPath(run,workflowNames.ui);
  return knownAutomatic || approvedManualUi ? sha : null;
}

export function associatedCurrentPrNumbers(event,related,repo) {
  const sha=fallbackSourceForWorkflowRun(event);
  if (!sha || !Array.isArray(related)) return [];
  return [...new Set(related.filter(pr=>
    Number.isSafeInteger(pr?.number) && pr.number>0 &&
    pr.state==="open" && pr.head?.sha===sha &&
    pr.base?.ref==="main" && pr.base?.repo?.full_name===repo
  ).map(pr=>pr.number))];
}

export async function refreshOne(repo,number,api) {
  const pr=await api("/pulls/"+number);
  if(pr?.state!=="open"||pr?.base?.ref!=="main"||
     pr?.base?.repo?.full_name!==repo)
    return {kind:"ignored",number};
  const sha=pr.head.sha;
  if(!/^[a-f0-9]{40}$/.test(sha))throw Error("INVALID_PR_SHA");
  // GitHub exposes at most 3000 changed PR files. Read the complete
  // inventory (including the 31st empty page at exactly 3000), never
  // silently classify only the first 700 files of a large integration.
  const count=pr.changed_files;
  if(!Number.isSafeInteger(count)||count<1||count>3000)
    throw Error("INVALID_PR_FILE_COUNT");
  const files=(await allPages(api,"/pulls/"+number+"/files",null,31))
    .map(f=>f.filename);
  if(files.length!==count||new Set(files).size!==count)
    throw Error("INCOMPLETE_PR_FILE_INVENTORY");
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
  // A concurrent head/base change invalidates the paginated inventory.
  const current=await api("/pulls/"+number);
  if(current?.state!=="open"||current.head?.sha!==sha||
     current.base?.sha!==pr.base?.sha||current.base?.ref!=="main"||
     current.base?.repo?.full_name!==repo||current.changed_files!==count)
    throw Error("PR_CI_STALE_INVENTORY");
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
  // Completion events can omit pull_requests. Approved manual UI is the
  // only workflow_dispatch fallback; manual Windows already has a verified
  // PR+SHA title, and unrelated dispatches must not mutate PR statuses.
  const fallbackSha=prs.length ? null : fallbackSourceForWorkflowRun(event);
  if(fallbackSha) {
    const related=await api("/commits/"+fallbackSha+"/pulls?per_page=100");
    for(const n of associatedCurrentPrNumbers(event,related,repo))
      console.log(JSON.stringify(await refreshOne(repo,n,api)));
  }
}
if(process.argv[1]?.endsWith("required_pr_merge_gate.mjs"))
  main().catch(e=>{console.error(e.stack||e.message);process.exitCode=1;});
