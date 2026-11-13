// SPDX-License-Identifier: AGPL-3.0-only
// Synthetic package and field-ledger fixtures. Tests MUST NOT certify sound.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {mkdtempSync,writeFileSync,readFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {inspectM3eJournal,inspectM3ePackage,assessM3eFat,M3E_MODES,M3E_RATES} from
  '../scripts/m3e_fat_evidence_gate.mjs';

const sha=b=>createHash('sha256').update(b).digest('hex');
const exeName='ArSonMix-GUI10CH-P012-Windows-x64.exe';
const commit='a'.repeat(40);
const makeTemp=()=>mkdtempSync(join(tmpdir(),'m3e-fat-'));
const clean=(path)=>rmSync(path,{recursive:true,force:true});
function fakePe(){
  const b=Buffer.alloc(4*1024*1024+16);
  b.write('MZ',0,'ascii');b.writeUInt32LE(0x80,0x3c);
  b.write('PE\0\0',0x80,'binary');
  b.writeUInt16LE(0x8664,0x84);b.writeUInt16LE(0x20b,0x80+24);
  b.writeUInt16LE(2,0x80+24+68);
  return b;
}
function makePackage(dir){
  const exe=fakePe(),collector=Buffer.from('synthetic-collector-fixture'),
    gate=Buffer.from('synthetic-node-gate-fixture');
  writeFileSync(join(dir,exeName),exe);
  writeFileSync(join(dir,'collect_hardware_fat_windows.ps1'),collector);
  writeFileSync(join(dir,'m3e_fat_evidence_gate.mjs'),gate);
  const m={schema:'arsonmix.native-artifact-provenance.v1',
    sourceCommit:commit,executable:exeName,exeSha256:sha(exe),guiExeBytes:exe.length,
    approvedGuiRoute:'/preview/',approvedGuiOutput:'preview/index.html',
    approvedGuiPagesSha256:'1'.repeat(64),approvedGuiNativeHtmlSha256:'2'.repeat(64),
    approvedGuiManifestSha256:'3'.repeat(64),
    fatStatus:'SOFTWARE_VERIFIED_PREVIEW_CANDIDATE',
    fatCollectorSha256:sha(collector),fatGateSha256:sha(gate)};
  const save=()=>writeFileSync(join(dir,'BUILD_PROVENANCE.json'),JSON.stringify(m));
  save();return {m,save,exe,collector};
}
function makeSession(mode,rate,index,dir){
  const start=Date.parse('2026-11-05T00:00:00.000Z')+index*3600*1000;
  const input=mode==='OUTPUT_ONLY_TONE'?[]:[0,1];
  const output=mode==='CAPTURE_ONLY'?[]:[0,1];
  const name='FAT_REPORT_'+rate+'_'+mode+'.txt',report=Buffer.from(
    'SIMULATED REPORT ONLY -- NEVER HARDWARE EVIDENCE '+rate+' '+mode);
  writeFileSync(join(dir,name),report);
  return {mode,sampleRate:rate,startedUtc:new Date(start).toISOString(),
    endedUtc:new Date(start+1800e3).toISOString(),elapsedSeconds:1800,
    backend:'ASIO_MATCHED',bufferFrames:256,capturePins:input,renderPins:output,
    captureEndpoint:'verified-input',renderEndpoint:'verified-output',
    operatorReportedFxCount:mode==='LIVE_THROUGH_1FX'?1:0,
    operatorReportedVst3Name:mode==='LIVE_THROUGH_1FX'?'Vendor Compressor':'',
    wired:mode.includes('THROUGH'),operatorObservation:'CLEAR',
    witness:'human-operator-manual-only',reportFile:name,reportSha256:sha(report)};
}
function journal(dir){
  let i=0;const sessions=[];
  for(const rate of M3E_RATES)
    for(const mode of M3E_MODES)
      sessions.push(makeSession(mode,rate,i++,dir));
  return {schema:'arsonmix.hardware-fat-field-journal.v1',
    sourceSha:commit,exeSha256:sha(fakePe()),active:null,sessions};
}
test('no captured hardware record is PENDING, even if package hashes/PE match',()=>{
 const d=makeTemp();try{
  makePackage(d);
  assert.equal(inspectM3ePackage(d).build,'EXE_AND_FAT_BYTES_VERIFIED');
  const r=assessM3eFat(d);
  assert.equal(r.physical,'PENDING');assert.equal(r.nativePcm,'PENDING_NATIVE_CALLBACK_PROOF');
  assert.equal(r.hardwareCertified,false);assert.equal(r.releaseReady,false);
 }finally{clean(d);}
});
test('exact field preview package is integrity-verified but NEVER a public FAT approval',()=>{
 const d=makeTemp();try{
  const p=makePackage(d);
  p.m.fatStatus='FIELD_PREVIEW_FULL_CTEST_NOT_RUN';p.save();
  const verified=inspectM3ePackage(d);
  assert.equal(verified.build,'EXE_AND_FAT_BYTES_VERIFIED');
  assert.equal(verified.softwareQualification,'FIELD_PREVIEW_FULL_CTEST_NOT_RUN');
  const assessment=assessM3eFat(d);
  assert.equal(assessment.build,'EXE_AND_FAT_BYTES_VERIFIED');
  assert.equal(assessment.softwareQualification,'FIELD_PREVIEW_FULL_CTEST_NOT_RUN');
  assert.equal(assessment.nativePcm,'PENDING_NATIVE_CALLBACK_PROOF');
  assert.equal(assessment.physical,'PENDING');
  assert.equal(assessment.releaseReady,false);
  assert.equal(assessment.hardwareCertified,false);
  assert.match(assessment.blockers.join(' '),/No physical FAT_RUNS.json/);
  // Both package modes must independently attest the same EXE and tools.
  writeFileSync(join(d,'collect_hardware_fat_windows.ps1'),'tampered');
  assert.equal(assessM3eFat(d).build,'BLOCKED');
  assert.match(assessM3eFat(d).blockers.join(' '),/SHA256 mismatch/);
 }finally{clean(d);}
});

test('unknown, blank or forged software qualification claims remain fail-closed',()=>{
 const d=makeTemp();try{
  const p=makePackage(d);
  for(const value of ['','FULL_CTEST_PASS','HARDWARE_CERTIFIED',
                      'FIELD_PREVIEW_FULL_CTEST_NOT_RUN_EXTRA',null]){
    p.m.fatStatus=value;p.save();
    assert.equal(assessM3eFat(d).build,'BLOCKED',String(value));
  }
  p.m.fatStatus='SOFTWARE_VERIFIED_PREVIEW_CANDIDATE';p.save();
  assert.equal(inspectM3ePackage(d).softwareQualification,
               'SOFTWARE_VERIFIED_PREVIEW_CANDIDATE');
 }finally{clean(d);}
});

test('native PE target, original package SHA and approved GUI are required',()=>{
 const d=makeTemp();try{
  const p=makePackage(d);
  p.m.approvedGuiRoute='/preview-v2/';p.save();
  assert.equal(assessM3eFat(d).build,'BLOCKED');
  p.m.approvedGuiRoute='/preview/';p.save();
  writeFileSync(join(d,exeName),Buffer.from('not a Windows application'));
  assert.equal(assessM3eFat(d).build,'BLOCKED');
 }finally{clean(d);}
});
test('FAT executable and offline verifier cannot be replaced without hash detection',()=>{
 const d=makeTemp();try{
  makePackage(d);
  writeFileSync(join(d,'collect_hardware_fat_windows.ps1'),'tampered');
  assert.match(assessM3eFat(d).blockers.join(' '),/SHA256 mismatch/);
 }finally{clean(d);}
});
test('44.1 and 48 kHz matched 0FX/1FX and source-only modes are review-ready, NEVER PASS',()=>{
 const d=makeTemp();try{
  makePackage(d);const j=journal(d);
  const a=inspectM3eJournal(j,{root:d});
  assert.equal(a.physical,'EVIDENCE_READY_FOR_HUMAN_REVIEW');
  assert.equal(a.selectedSessions,8);
  writeFileSync(join(d,'FAT_RUNS.json'),JSON.stringify(j));
  const b=assessM3eFat(d);
  assert.equal(b.physical,'EVIDENCE_READY_FOR_HUMAN_REVIEW');
  assert.equal(b.releaseReady,false);assert.equal(b.hardwareCertified,false);
  assert.equal(b.nativePcm,'PENDING_NATIVE_CALLBACK_PROOF');
  assert.equal(b.renderHardwareXrun,'UNAVAILABLE_UNLESS_SEPARATELY_MEASURED');
 }finally{clean(d);}
});
test('0FX and 1FX must be matched: device/pin/clock and chronological baseline',()=>{
 const d=makeTemp();try{
  const j=journal(d),one=j.sessions.find(s=>s.sampleRate===48000&&s.mode==='LIVE_THROUGH_1FX');
  one.bufferFrames=512;
  assert.match(inspectM3eJournal(j,{root:d}).blockers.join(' '),/identical backend/);
  one.bufferFrames=256;
  one.startedUtc=new Date('2026-11-05T00:00:00.000Z').toISOString();
  assert.match(inspectM3eJournal(j,{root:d}).blockers.join(' '),/after its completed 0FX baseline/);
 }finally{clean(d);}
});
test('missing 48 kHz, 0FX too short and forged elapsed time all fail closed',()=>{
 const d=makeTemp();try{
  const j=journal(d);
  j.sessions=j.sessions.filter(s=>s.sampleRate!==48000);
  assert.equal(inspectM3eJournal(j,{root:d}).physical,'PENDING');
  const a=journal(d),dry=a.sessions.find(s=>s.mode==='DRY_THROUGH_0FX');
  dry.elapsedSeconds=60;
  assert.match(inspectM3eJournal(a,{root:d}).blockers.join(' '),/30-minute wired soak/);
  const b=journal(d);b.sessions[0].elapsedSeconds=1800+60;
  assert.match(inspectM3eJournal(b,{root:d}).blockers.join(' '),/not consistent/);
 }finally{clean(d);}
});
test('crackle is never renamed to PASS and prior incident remains visible',()=>{
 const d=makeTemp();try{
  const j=journal(d);j.sessions.find(s=>s.mode==='DRY_THROUGH_0FX').operatorObservation='CRACKLE';
  const r=inspectM3eJournal(j,{root:d});
  assert.equal(r.physical,'PENDING');
  assert.match(r.blockers.join(' '),/not CLEAR/);
  assert.match(r.warnings.join(' '),/crackle history/);
 }finally{clean(d);}
});
test('tampered reports and path escapes never become hardware evidence',()=>{
 const d=makeTemp();try{
  const j=journal(d);j.sessions[0].reportSha256='1'.repeat(64);
  assert.match(inspectM3eJournal(j,{root:d}).blockers.join(' '),/report integrity SHA256 mismatch/);
  const k=journal(d);k.sessions[0].reportFile='../outside.txt';
  assert.match(inspectM3eJournal(k,{root:d}).blockers.join(' '),/report and SHA256 missing/);
 }finally{clean(d);}
});
test('hardware journal binds exact artifact and source SHA, rejects next-build reuse',()=>{
 const d=makeTemp();try{
  makePackage(d);const j=journal(d);j.sourceSha='b'.repeat(40);
  writeFileSync(join(d,'FAT_RUNS.json'),JSON.stringify(j));
  const a=assessM3eFat(d);
  assert.equal(a.physical,'PENDING');assert.match(a.blockers.join(' '),/source\/executable SHA differs/);
 }finally{clean(d);}
});
test('an active FAT timer, missing native vendor identity or fake VST in 0FX block review',()=>{
 const d=makeTemp();try{
  const j=journal(d);j.active={mode:'DRY_THROUGH_0FX'};
  assert.match(inspectM3eJournal(j,{root:d}).blockers.join(' '),/running FAT session/);
  j.active=null;
  j.sessions.find(s=>s.mode==='LIVE_THROUGH_1FX').operatorReportedVst3Name='';
  assert.match(inspectM3eJournal(j,{root:d}).blockers.join(' '),/native VST3 identity missing/);
  j.sessions.find(s=>s.mode==='DRY_THROUGH_0FX').operatorReportedFxCount=1;
  assert.match(inspectM3eJournal(j,{root:d}).blockers.join(' '),/zero active VST3/);
 }finally{clean(d);}
});

test('physical endpoint IDs are mandatory for wired 0FX/1FX evidence, not just pin ordinals',()=>{
 const d=makeTemp();try{
  const j=journal(d);
  for(const s of j.sessions)if(s.mode==='DRY_THROUGH_0FX')s.renderEndpoint='';
  assert.equal(inspectM3eJournal(j,{root:d}).physical,'PENDING');
  assert.match(inspectM3eJournal(j,{root:d}).blockers.join(' '),/actual capture\/render endpoint identity missing/);
 }finally{clean(d);}
});

test('UTF-8 BOM written by Windows PowerShell 5.1 preserves exact source and report SHA verification',()=>{
  const d=makeTemp();try{
    const p=makePackage(d),bom=Buffer.from([0xef,0xbb,0xbf]);
    writeFileSync(join(d,'BUILD_PROVENANCE.json'),Buffer.concat([bom,Buffer.from(JSON.stringify(p.m))]));
    assert.equal(inspectM3ePackage(d).build,'EXE_AND_FAT_BYTES_VERIFIED');
    const j=journal(d);
    writeFileSync(join(d,'FAT_RUNS.json'),Buffer.concat([bom,Buffer.from(JSON.stringify(j))]));
    const r=assessM3eFat(d);
    assert.equal(r.physical,'EVIDENCE_READY_FOR_HUMAN_REVIEW');
    assert.equal(r.hardwareCertified,false);
    assert.equal(r.releaseReady,false);
  }finally{clean(d);}
});
test('BOM does not weaken malformed-journal rejection or source SHA pinning',()=>{
  const d=makeTemp();try{
    makePackage(d);
    writeFileSync(join(d,'FAT_RUNS.json'), '\uFEFF{"schema":bad}');
    const malformed=assessM3eFat(d);
    assert.equal(malformed.physical,'PENDING');
    assert.match(malformed.blockers.join(' '),/FAT journal integrity/);
    const j=journal(d);j.sourceSha='b'.repeat(40);
    writeFileSync(join(d,'FAT_RUNS.json'), '\uFEFF'+JSON.stringify(j));
    const altered=assessM3eFat(d);
    assert.equal(altered.physical,'PENDING');
    assert.equal(altered.hardwareCertified,false);
    assert.match(altered.blockers.join(' '),/source\/executable SHA differs/);
  }finally{clean(d);}
});

test('Windows offline FAT collector accepts attested FIELD preview, rejects invented qualification',()=>{
 if(process.platform!=='win32')return;
 const d=makeTemp();try{
  const p=makePackage(d);
  const collector=readFileSync('scripts/collect_hardware_fat_windows.ps1');
  const collectorPath=join(d,'collect_hardware_fat_windows.ps1');
  writeFileSync(collectorPath,collector);
  p.m.fatCollectorSha256=sha(collector);
  const verify=()=>execFileSync('powershell.exe',[
    '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',
    '-File',collectorPath,'-Action','Verify'
  ],{cwd:d,timeout:30000,encoding:'utf8'});
  p.m.fatStatus='FIELD_PREVIEW_FULL_CTEST_NOT_RUN';p.save();
  assert.doesNotThrow(verify);
  p.m.fatStatus='HARDWARE_CERTIFIED';p.save();
  assert.throws(verify,/Unexpected or unapproved software qualification status/);
  p.m.fatStatus='SOFTWARE_VERIFIED_PREVIEW_CANDIDATE';p.save();
  assert.doesNotThrow(verify);
 }finally{clean(d);}
});

test('PowerShell Summary validates 44.1/48 kHz real start/end duration rather than a forged 30-minute field',()=>{
 if(process.platform!=='win32')return;
 const d=makeTemp();try{
  const p=makePackage(d);
  // This is a synthetic source-verified test-only FAT kit, NOT live hardware.
  const actualCollector=readFileSync('scripts/collect_hardware_fat_windows.ps1');
  writeFileSync(join(d,'collect_hardware_fat_windows.ps1'),actualCollector);
  p.m.fatCollectorSha256=sha(actualCollector);p.save();
  const j=journal(d),journalPath=join(d,'FAT_RUNS.json');
  const bom='\uFEFF';
  const runSummary=()=>{
    writeFileSync(journalPath,bom+JSON.stringify(j));
    execFileSync('powershell.exe',[
      '-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass',
      '-File',join(d,'collect_hardware_fat_windows.ps1'),
      '-Action','Summary'
    ],{cwd:d,timeout:30000,encoding:'utf8'});
    return JSON.parse(readFileSync(join(d,'FAT_SUMMARY.json'),'utf8').replace(/^\uFEFF/,''));
  };
  const good=runSummary();
  assert.equal(good.status,'EVIDENCE_READY_FOR_HUMAN_REVIEW');
  assert.equal(good.hardwareCertified,false);
  const forged=j.sessions.find(s=>s.sampleRate===48000&&s.mode==='DRY_THROUGH_0FX');
  forged.endedUtc=new Date(Date.parse(forged.startedUtc)+1000).toISOString();
  const bad=runSummary();
  assert.equal(bad.status,'EVIDENCE_INCOMPLETE');
  assert.equal(bad.hardwareCertified,false);
  assert.match(bad.blockers.join(' '),/48000 DRY_THROUGH_0FX: recorded duration inconsistent with UTC start\/end/);
  forged.endedUtc=new Date(Date.parse(forged.startedUtc)+1800e3).toISOString();
  for(const mode of ['DRY_THROUGH_0FX','LIVE_THROUGH_1FX']){
    const s=j.sessions.find(x=>x.sampleRate===48000&&x.mode===mode);
    s.capturePins=[];s.renderPins=[];
  }
  const missingPins=runSummary();
  assert.equal(missingPins.status,'EVIDENCE_INCOMPLETE');
  assert.match(missingPins.blockers.join(' '),/wired through-mode requires actual capture and render pins/);
  const dry=j.sessions.find(x=>x.sampleRate===44100&&x.mode==='DRY_THROUGH_0FX');
  dry.capturePins=[0,0];
  const duplicatePins=runSummary();
  assert.equal(duplicatePins.status,'EVIDENCE_INCOMPLETE');
  assert.match(duplicatePins.blockers.join(' '),/duplicate physical capturePins ordinal/);
  dry.capturePins=[-1,257];
  const outOfRange=runSummary();
  assert.equal(outOfRange.status,'EVIDENCE_INCOMPLETE');
  assert.match(outOfRange.blockers.join(' '),/invalid physical capturePins ordinal/);
 }finally{clean(d);}
});

test('field collector uses module-independent streaming SHA256 and preserves exact package provenance',()=>{
 const ps=readFileSync('scripts/collect_hardware_fat_windows.ps1','utf8');
 assert.match(ps,/function Get-FatSha256/);
 assert.match(ps,/System\.Security\.Cryptography\.SHA256/);
 assert.match(ps,/filePath=\(Resolve-Path -LiteralPath \$Path/);
 assert.match(ps,/\$stream\.Dispose\(\)/);
 assert.match(ps,/\$hasher\.Dispose\(\)/);
 assert.doesNotMatch(ps,/^\s*\$[a-zA-Z]\w*=\(Get-FileHash\b/m);
 assert.match(ps,/FAT collector was modified after packaging/);
 assert.match(ps,/GUI EXE SHA-256 mismatch/);
 assert.match(ps,/native report SHA256 mismatch/);
});
