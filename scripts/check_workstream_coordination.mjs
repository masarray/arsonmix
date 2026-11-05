#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-only
// One-owner PR preflight. Runs in EXISTING GUI/Windows workflows only.
// Cheap GitHub REST reads; NEVER changes a repo or builds audio.
// New PRs fail closed on unowned production paths or active same-owner collisions.
import { pathToFileURL } from 'node:url';
import { appendFileSync } from 'node:fs';

export const POLICY_EPOCH_PR = 399; // Existing #396/#398 are legacy; new #400+ are strict.
export const WORKSTREAMS = Object.freeze(['GUI', 'ENGINE', 'INTEGRATOR', 'FAT']);

export function ownerOf(path) {
  if (path === 'AGENTS.md' ||
      path === '.github/PULL_REQUEST_TEMPLATE.md' ||
      path.startsWith('.github/workflows/') ||
      path === 'docs/workstreams/MULTI_AGENT_COORDINATION.md' ||
      path === 'scripts/check_workstream_coordination.mjs' ||
      // Release admission is INTEGRATOR-owned; other lanes must not
      // silently weaken the approved GUI/native stop-ship evidence.
      path === 'scripts/check_console_puzzle.mjs' ||
      path === 'scripts/check_approved_native_gui_release.mjs' ||
      path === 'scripts/check_pr278_console_authority.mjs' ||
      path === 'tests/workstream_coordination_contract.test.mjs')
    return 'INTEGRATOR';
  if (path === 'CMakeLists.txt' || path === 'src/Main.cpp' ||
      path.startsWith('integration/') ||
      /^src\/(ApprovedConsole|ApprovedNativeGui|ConsoleNativeTransport)/.test(path) ||
      /^web\/approved-native/.test(path) ||
      path === 'web/bridge.js' || path === 'web/monitor.js' ||
      path === 'scripts/build_approved_native_gui.py')
    return 'INTEGRATOR';
  if (path === 'scripts/collect_hardware_fat_windows.ps1' ||
      path === 'scripts/console_v6_fat_gate.mjs' ||
      path === 'scripts/verify_console_s0_native_fixture.mjs' ||
      path === 'scripts/run_console_native_s0.mjs' ||
      /^(scripts\/(package_native_windows|verify_native_windows_artifact)\.ps1)$/.test(path) ||
      /^tests\/(hardware_fat|native_artifact|fat_|windows_fat)/.test(path))
    return 'FAT';
  if (path.startsWith('site/') || path.startsWith('web/') ||
      path === 'AR-MIX-Live-FX-Host-Standalone-v0.3.html' ||
      path === 'scripts/build_pages.py' ||
      path === 'scripts/build_ui_css.py')
    return 'GUI';
  if (path.startsWith('engine/') || path.startsWith('src/') ||
      path.startsWith('config/factoryfx-') ||
      path === 'scripts/factoryfx_release_bundle.py')
    return 'ENGINE';
  return null; // docs / tests / independent CI fixtures are shared, read-only contracts.
}
// Fine-grained subsystems within a workstream. Different filenames can
// still implement a SECOND audio callback or GUI-native reducer. The owning
// PR must coordinate an entire production domain, not merely one path.
export function runtimeDomain(path) {
  if (path.startsWith('engine/') ||
      /^src\/(?:LiveAudioEngine|LiveDryRoutingLab|ConsoleLiveBridgeV6|ApprovedNativeV8Projection|NativeVerifiedSourcePatch)\./.test(path))
    return 'engine-pcm';
  if (/^web\/approved-native-(?:bridge|contract|m2-state)\.js$/.test(path) ||
      path === 'scripts/build_approved_native_gui.py')
    return 'approved-native-bridge';
  if (path === 'src/Main.cpp' ||
      /^src\/ApprovedConsole.*\.(?:h|cpp)$/.test(path))
    return 'native-host-dispatch';
  if (path.startsWith('site/preview') || path === 'scripts/build_pages.py')
    return 'approved-visual';
  if (/^scripts\/(?:check_console_puzzle|check_approved_native_gui_release|check_pr278_console_authority)\.mjs$/.test(path))
    return 'release-admission';
  if (path.startsWith('.github/workflows/') ||
      path === 'AGENTS.md' || path === 'scripts/check_workstream_coordination.mjs')
    return 'coordination-policy';
  return null;
}

export function metadata(body = '', headRef = '') {
  const owner = body.match(/<!--\s*arsonmix-workstream:\s*(GUI|ENGINE|INTEGRATOR|FAT)\s*-->/i)?.[1]?.toUpperCase() || null;
  const issue = Number(body.match(/<!--\s*arsonmix-issue:\s*#([1-9][0-9]*)\s*-->/i)?.[1] || 0);
  const parked = /<!--\s*arsonmix-coordination:\s*parked\s*-->/i.test(body);
  let inferred = owner;
  if (!inferred && /^gui\//.test(headRef)) inferred = 'GUI';
  if (!inferred && /^(engine\/|milestone\/|fix\/p0-wasapi|feat\/factoryfx)/.test(headRef)) inferred = 'ENGINE';
  if (!inferred && /^(integration\/|coord\/)/.test(headRef)) inferred = 'INTEGRATOR';
  if (!inferred && /^work\/integration-fat/.test(headRef)) inferred = 'FAT';
  return {owner, inferred, issue, parked};
}
export function audit({pr, files, others = [], filesByPR = {}, fileStatuses = {}}) {
  const current = metadata(pr.body || '', pr.head?.ref || pr.headRef || '');
  const legacy = pr.number <= POLICY_EPOCH_PR && pr.draft === true;
  const errors = [], warnings = [];
  const changed = new Set(files);
  const owners = new Set([...changed].map(ownerOf).filter(Boolean));
  // Reject a NEW alternate mixer renderer; deleting its old archive is fine.
  for(const path of changed) {
    if(['web/console-native.html','web/console-native.js','web/console-native.css'].includes(path) &&
       fileStatuses[path] !== 'removed')
      errors.push('ALTERNATE GUI ' + path + ' is forbidden; only /preview/ is approved.');
  }
  if (!current.owner) errors.push('Declare <!-- arsonmix-workstream: GUI|ENGINE|INTEGRATOR|FAT --> in PR body');
  if (!current.issue) errors.push('Declare <!-- arsonmix-issue: #397 --> (or linked workstream issue) in PR body');
  if (current.parked) errors.push('PR is PARKED; remove parked marker only after owner reconciliation');
  if (current.owner) {
    for (const file of changed) {
      const owner = ownerOf(file);
      if (owner && owner !== current.owner)
        errors.push('CROSS-OWNER ' + file + ' belongs to ' + owner +
                    ', PR declares ' + current.owner + '. Request a handoff, do not edit the file.');
    }
  }
  for (const other of others) {
    if (other.number === pr.number || other.state === 'closed') continue;
    const peer = metadata(other.body || '', other.head?.ref || other.headRef || '');
    if (peer.parked) continue;
    const shared = [...changed].filter(f =>
      (filesByPR[other.number] || []).includes(f));
    // No exact overlap is NOT grounds to skip: two different files can
    // still create two native PCM/bridge implementations in one domain.
    // The sole authorized owner must be free to progress. Legacy branches
    // that edited somebody else's files are reported, not allowed to block.
    const activeShared = shared.filter(f => {
      const owner = ownerOf(f);
      return peer.inferred === current.owner && (!owner || owner === current.owner);
    });
    if (activeShared.length) {
      errors.push('ACTIVE COLLISION with PR #' + other.number + ' (' +
        (peer.inferred || 'UNKNOWN') + '): ' + activeShared.slice(0, 8).join(', ') +
        (activeShared.length > 8 ? ' + ' + (activeShared.length - 8) + ' more' : '') +
        '. Consolidate into ONE owner branch or park the superseded PR.');
    } else {
      if(shared.length)warnings.push('PR #' + other.number + ' also edits ' + shared.length +
        ' path(s) outside its declared file owner; do not merge it raw.');
      // Exact-path detection alone misses concurrent engine edits in
      // different files. Keep domains narrow so CMake/policy is not confused
      // with PCM/renderer ownership and existing legitimate owners can work.
      if(current.owner && current.owner===peer.inferred) {
        const myDomains=new Set([...changed].filter(f=>ownerOf(f)===current.owner)
          .map(runtimeDomain).filter(Boolean));
        const competing=(filesByPR[other.number]||[]).find(f=>
          ownerOf(f)===current.owner && myDomains.has(runtimeDomain(f)) &&
          !changed.has(f));
        if(competing)errors.push('ACTIVE DOMAIN COLLISION with PR #' + other.number +
          ': ' + runtimeDomain(competing) + ' (' + competing +
          '). Distinct files are still ONE runtime subsystem; join the owner PR.');
      }
    }
  }
  if (legacy && errors.length) {
    warnings.push('LEGACY migration (PR #' + pr.number + ' <= #' +
      POLICY_EPOCH_PR + '): ' + errors.length +
      ' ownership/collision finding(s), nonblocking until owner reconciliation');
    warnings.push(...errors);
  }
  return {ok: legacy || errors.length === 0, legacy, lane: current.owner || current.inferred,
    owners:[...owners].sort(), errors:legacy?[]:errors, warnings,
    changedFiles:changed.size, activeCollisions:errors.filter(x=>x.startsWith('ACTIVE COLLISION')).length};
}

async function apiJson(url, token) {
  const response = await fetch(url, {
    headers: {
      'Accept':'application/vnd.github+json',
      'Authorization':'Bearer ' + token,
      'X-GitHub-Api-Version':'2022-11-28',
      'User-Agent':'ArSonMix-workstream-preflight'
    },signal:AbortSignal.timeout(15000)
  });
  if (!response.ok) throw Error('GitHub REST ' + response.status + ' for ' +
                                new URL(url).pathname);
  return response.json();
}
async function allPages(base, token, limit = 10) {
  const rows=[];
  for (let page=1;page<=limit;page++) {
    const separator=base.includes('?')?'&':'?';
    const items=await apiJson(base+separator+'per_page=100&page='+page,token);
    if (!Array.isArray(items)) throw Error('GitHub API returned non-array');
    rows.push(...items);
    if(items.length<100) return rows;
  }
  throw Error('GitHub API pagination exceeded bounded limit: '+base);
}
// Scope expensive Chromium to the prior UI PR path policy, while every
// PR (docs-only / ENGINE-only included) receives fast ownership admission.
export function requiresUiSmoke(paths) {
  return paths.some(path =>
    path.startsWith('site/') ||
    path.startsWith('web/') ||
    path.startsWith('integration/console-v6/') ||
    path === 'AR-MIX-Live-FX-Host-Standalone-v0.3.html' ||
    path === 'scripts/build_pages.py' ||
    path === 'scripts/build_ui_css.py' ||
    /^tests\/[^/]+\.py$/.test(path) ||
    /^tests\/[^/]*gui[^/]*contract[^/]*\.mjs$/.test(path) ||
    [
      'tests/gui_workflow_contract_test.mjs',
      'tests/native_ui_contract_test.mjs',
      'tests/ci_workflow_efficiency_test.mjs',
      'tests/approved_native_m2_contract.test.cjs',
      'tests/wire_graph_canonical_model.test.cjs',
      'tests/wire_main_native_handoff_contract.test.cjs',
      '.github/workflows/ui-smoke.yml',
      '.github/workflows/pages-publish.yml'
    ].includes(path));
}


// Native qualification is intentionally narrower than "integration/**":
// C++ headers/translation units require a real MSVC+JUCE build, whereas
// isolated policy/docs/JS can finish without a paid Windows runner.
// This is the single PR file-scope authority for the reusable native job.
export function requiresNativeQualification(paths) {
  return paths.some(path =>
    path === 'CMakeLists.txt' ||
    path.startsWith('src/') ||
    path.startsWith('engine/') ||
    path.startsWith('integration/console-v6/') ||
    /^integration\/.*\.(?:h|hpp|hh|hxx|c|cc|cpp|cxx)$/i.test(path) ||
    path === '.github/workflows/p1-windows.yml' ||
    ['web/approved-native-bridge.js', 'web/approved-native-contract.js',
      'web/bridge.js', 'web/monitor.js',
      'tests/approved_native_m2_contract.test.cjs',
      'tests/approved_native_gui_bundle_test.py',
      'scripts/build_approved_native_gui.py',
      'scripts/package_native_windows.ps1',
      'scripts/verify_native_windows_artifact.ps1',
      'scripts/collect_hardware_fat_windows.ps1',
      'scripts/console_v6_fat_gate.mjs',
      'scripts/verify_console_s0_native_fixture.mjs',
      'scripts/run_console_native_s0.mjs',
      'scripts/check_console_puzzle.mjs',
      'scripts/check_approved_native_gui_release.mjs',
      'scripts/check_pr278_console_authority.mjs',
      'scripts/factoryfx_release_bundle.py',
      'scripts/m3e_fat_evidence_gate.mjs',
      'config/factoryfx-approved-releases.json',
      'tests/live_graph_contract_test.mjs'].includes(path) ||
    path.startsWith('config/factoryfx-notices/') ||
    /^tests\/[^/]+\.cpp$/i.test(path) ||
    /^tests\/[^/]*native[^/]*contract[^/]*\.mjs$/i.test(path) ||
    /^tests\/(?:m3e_fat_evidence_gate|console_v6_fat_gate)\.test\.mjs$/.test(path) ||
    path === 'scripts/tests/test_factoryfx_release_bundle.py');
}

export async function auditGitHub({repo,number,token}) {
  if (!/^[a-zA-Z0-9_.-]+\/[a-zA-Z0-9_.-]+$/.test(repo))
    throw Error('Invalid repository selector');
  if (!Number.isSafeInteger(number) || number<1 || !token)
    throw Error('A valid PR number and a GitHub read token are mandatory');
  const prefix='https://api.github.com/repos/'+repo;
  const [pr, others]=await Promise.all([
    apiJson(prefix+'/pulls/'+number,token),
    allPages(prefix+'/pulls?state=open',token,4)
  ]);
  const filesByPR={};
  const scoped=others.filter(p=>p.number!==number &&
    !metadata(p.body||'',p.head?.ref||'').parked);
  const files=await allPages(prefix+'/pulls/'+number+'/files',token,10);
  // Bounded concurrency prevents API spikes on frequent PR pushes.
  for (let i=0;i<scoped.length;i+=4) {
    await Promise.all(scoped.slice(i,i+4).map(async item=>{
      filesByPR[item.number]=(await allPages(prefix+'/pulls/'+item.number+'/files',token,10))
        .map(f=>f.filename);
    }));
  }
  const result = audit({pr,files:files.map(f=>f.filename),others,filesByPR,
    fileStatuses:Object.fromEntries(files.map(f=>[f.filename,f.status]))});
  const changedPaths = files.map(f=>f.filename);
  return {...result, runUi: requiresUiSmoke(changedPaths),
    runNative: requiresNativeQualification(changedPaths)};
}
// Single-owner product recovery: scope selected verification from the EXACT
// changed filenames. This path deliberately does not fetch competing PRs,
// enforce agent lanes, or gate integration on historic #397 ownership.
export async function qualificationScopeGitHub({repo,number,token}) {
  if(!/^[a-zA-Z0-9_.-]+\/[a-zA-Z0-9_.-]+$/.test(repo))
    throw Error('Invalid repository selector');
  if(!Number.isSafeInteger(number)||number<1||!token)
    throw Error('A valid PR number and a GitHub read token are mandatory');
  const changed=await allPages('https://api.github.com/repos/'+repo+
    '/pulls/'+number+'/files',token,10);
  const paths=changed.map(file=>file.filename);
  return {ok:true,lane:'SINGLE_INTEGRATION_OWNER',legacy:false,
    changedFiles:paths.length,activeCollisions:0,warnings:[],errors:[],
    runUi:requiresUiSmoke(paths),runNative:requiresNativeQualification(paths)};
}

async function main() {
  const args=process.argv.slice(2);
  if(args.includes('--help')) {
    console.log('Usage: node scripts/check_workstream_coordination.mjs --pr NUMBER --scope');
    return;
  }
  const index=args.indexOf('--pr');
  const number=Number(index>=0?args[index+1]:0);
  // --scope is the active lightweight product gate. Legacy --pr without
  // --scope remains available only for archived coordination diagnostics.
  const run= args.includes('--scope')?qualificationScopeGitHub:auditGitHub;
  const result=await run({
    repo:process.env.GITHUB_REPOSITORY || '',
    number,token:process.env.GH_TOKEN || ''
  });
  if (process.env.GITHUB_OUTPUT) {
    appendFileSync(process.env.GITHUB_OUTPUT, 'run_ui='+(result.runUi?'true':'false')+'\n'+
      'run_native='+(result.runNative?'true':'false')+'\n');
  }
  console.log('Qualification scope: ui='+(result.runUi?'yes':'no')+
    ' native='+(result.runNative?'yes':'no'));
  console.log('ArSonMix coordination: lane='+(result.lane||'UNDECLARED')+
    ' files='+result.changedFiles+' legacy='+result.legacy+
    ' authorized='+result.ok+' collision(s)='+result.activeCollisions);
  for(const w of result.warnings)console.warn('::warning::'+w);
  for(const e of result.errors)console.error('::error::'+e);
  if(!result.ok)process.exitCode=1;
}
if(process.argv[1] && import.meta.url===pathToFileURL(process.argv[1]).href) {
  main().catch(e=>{
    console.error('::error::Coordination API unavailable: '+e.message);
    process.exitCode=1; // No invisible PASS if the new PR cannot be checked.
  });
}
