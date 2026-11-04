// SPDX-License-Identifier: AGPL-3.0-only
// Synthetic package and field-ledger fixtures. Tests MUST NOT certify sound.
import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
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
    fatStatus:'EXPERIMENTAL_NOT_HARDWARE_QUALIFIED',
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
