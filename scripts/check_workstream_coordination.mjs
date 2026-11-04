#!/usr/bin/env node
// SPDX-License-Identifier: AGPL-3.0-only
// One-owner PR preflight. Runs in EXISTING GUI/Windows workflows only.
// Cheap GitHub REST reads; NEVER changes a repo, builds audio or blocks push/main.
// New PRs fail closed on unowned production paths or active same-owner collisions.
import { pathToFileURL } from 'node:url';

export const POLICY_EPOCH_PR = 395; // Earlier PRs are reported, not retroactively failed.
export const WORKSTREAMS = Object.freeze(['GUI', 'ENGINE', 'INTEGRATOR', 'FAT']);

export function ownerOf(path) {
  if (path === 'AGENTS.md' ||
      path === '.github/PULL_REQUEST_TEMPLATE.md' ||
      path.startsWith('.github/workflows/') ||
      path === 'docs/workstreams/MULTI_AGENT_COORDINATION.md' ||
      path === 'scripts/check_workstream_coordination.mjs' ||
      path === 'tests/workstream_coordination_contract.test.mjs')
    return 'INTEGRATOR';
  if (path === 'CMakeLists.txt' || path === 'src/Main.cpp' ||
      path.startsWith('integration/') ||
      /^src\/(ApprovedConsole|ApprovedNativeGui|ConsoleNativeTransport)/.test(path) ||
      /^web\/approved-native/.test(path) ||
      path === 'web/bridge.js' || path === 'web/monitor.js' ||
      path === 'scripts/build_approved_native_gui.py')
    return 'INTEGRATOR';
  if (/^(scripts\/(package_native_windows|verify_native_windows_artifact)\.ps1)$/.test(path) ||
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
export function audit({pr, files, others = [], filesByPR = {}}) {
  const current = metadata(pr.body || '', pr.head?.ref || pr.headRef || '');
  const legacy = pr.number <= POLICY_EPOCH_PR && pr.draft === true;
  const errors = [], warnings = [];
  const changed = new Set(files);
  const owners = new Set([...changed].map(ownerOf).filter(Boolean));
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
    if (!shared.length) continue;
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
      warnings.push('PR #' + other.number + ' also edits ' + shared.length +
        ' path(s) outside its declared file owner; do not merge it raw.');
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
  return audit({pr,files:files.map(f=>f.filename),others,filesByPR});
}
async function main() {
  const args=process.argv.slice(2);
  if(args.includes('--help')) {
    console.log('Usage: node scripts/check_workstream_coordination.mjs --pr NUMBER');
    return;
  }
  const index=args.indexOf('--pr');
  const number=Number(index>=0?args[index+1]:0);
  const result=await auditGitHub({
    repo:process.env.GITHUB_REPOSITORY || '',
    number,token:process.env.GH_TOKEN || ''
  });
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
