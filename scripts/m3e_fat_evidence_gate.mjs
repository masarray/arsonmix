// SPDX-License-Identifier: AGPL-3.0-only
// M3-E: independent field-evidence intake; NEVER a sound generator or DSP.
// Exact EXE/collector byte checks do NOT certify a native callback or DAC.
// No device access, output arming, audio capture, or automatic upload.
import {readFileSync,existsSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {join,resolve,basename} from 'node:path';
import {fileURLToPath} from 'node:url';

export const M3E_RATES=Object.freeze([44100,48000]);
export const M3E_MODES=Object.freeze([
  'OUTPUT_ONLY_TONE','CAPTURE_ONLY','DRY_THROUGH_0FX','LIVE_THROUGH_1FX'
]);
export const M3E_SCHEMA='arsonmix.hardware-fat-field-journal.v1';
const HEX40=/^[a-f0-9]{40}$/,HEX64=/^[a-f0-9]{64}$/;
const hash=x=>createHash('sha256').update(x).digest('hex');
const nonempty=v=>typeof v==='string'&&v.trim().length>0;
const uniqueInts=v=>Array.isArray(v)&&v.every(n=>Number.isSafeInteger(n)&&n>=0&&n<=255)
  &&new Set(v).size===v.length;
const safeName=v=>typeof v==='string'&&/^[A-Za-z0-9][A-Za-z0-9_.-]{0,179}$/.test(v)&&
  basename(v)===v&&v!=='.'&&v!=='..';
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const time=v=>typeof v==='string'?Date.parse(v):NaN;
// Windows PowerShell 5.1 -Encoding UTF8 emits a BOM; retain strict JSON+SHA checks.
const readJsonFile=path=>{
  const s=readFileSync(path,'utf8');
  return JSON.parse(s.charCodeAt(0)===0xfeff?s.slice(1):s);
};
function checkPe64Gui(bytes){
  if(bytes.length<4*1024*1024||bytes.toString('ascii',0,2)!=='MZ')
    throw Error('Native EXE missing / under 4 MiB / invalid MZ');
  const pos=bytes.readUInt32LE(0x3c);
  if(pos<64||pos>bytes.length-128||
     bytes.toString('ascii',pos,pos+4)!=='PE\0\0'||
     bytes.readUInt16LE(pos+4)!==0x8664||
     bytes.readUInt16LE(pos+24)!==0x20b||
     bytes.readUInt16LE(pos+24+68)!==2)
    throw Error('Package target is not an AMD64 PE32+ WINDOWS_GUI EXE');
}
function readExact(root,filename,expectedHash){
  if(!safeName(filename)||!HEX64.test(expectedHash||''))
    throw Error('Unsafe or non-attested FAT evidence file');
  const absolute=join(root,filename);
  if(!existsSync(absolute))throw Error('Missing expected FAT file: '+filename);
  const bytes=readFileSync(absolute);
  if(hash(bytes)!==expectedHash)
    throw Error('SHA256 mismatch: '+filename);
  return bytes;
}
export function inspectM3ePackage(root){
  const p=resolve(root),manifest=readJsonFile(join(p,'BUILD_PROVENANCE.json'));
  if(manifest.schema!=='arsonmix.native-artifact-provenance.v1'||
     !HEX40.test(manifest.sourceCommit||'')||
     manifest.executable!=='ArSonMix-GUI10CH-P012-Windows-x64.exe'||
     manifest.approvedGuiRoute!=='/preview/'||
     manifest.approvedGuiOutput!=='preview/index.html'||
     !HEX64.test(manifest.approvedGuiPagesSha256||'')||
     !HEX64.test(manifest.approvedGuiNativeHtmlSha256||'')||
     !HEX64.test(manifest.approvedGuiManifestSha256||'')||
     manifest.fatStatus!=='EXPERIMENTAL_NOT_HARDWARE_QUALIFIED')
    throw Error('Source/GUI/qualification manifest is not approved and fail-closed');
  const exe=readExact(p,manifest.executable,manifest.exeSha256);
  checkPe64Gui(exe);
  if(manifest.guiExeBytes!==exe.length)throw Error('Native EXE length not attested');
  readExact(p,'collect_hardware_fat_windows.ps1',manifest.fatCollectorSha256);
  readExact(p,'m3e_fat_evidence_gate.mjs',manifest.fatGateSha256);
  // The separate Windows verifier performs independent ZIP entry/digest
  // inspection. This tool does not claim compressed ZIP verification.
  return {sourceSha:manifest.sourceCommit,exeSha256:manifest.exeSha256,
    build:'EXE_AND_FAT_BYTES_VERIFIED',zip:'EXTERNAL_WINDOWS_VERIFIER_REQUIRED',
    gui:'APPROVED_PREVIEW_PROVENANCE_MANIFEST',manifest};
}
function verifySession(run,mode,rate,root,blockers){
  const tag=rate+' '+mode;
  if(!run||typeof run!=='object'){blockers.push(tag+': missing');return false;}
  if(run.mode!==mode||run.sampleRate!==rate)blockers.push(tag+': mode/rate mismatch');
  if(!nonempty(run.backend)||!Number.isInteger(run.bufferFrames)||
     run.bufferFrames<16||run.bufferFrames>16384)
    blockers.push(tag+': unverified backend or device buffer');
  if(!uniqueInts(run.capturePins)||!uniqueInts(run.renderPins))
    blockers.push(tag+': invalid actual physical pin inventory');
  const inCount=Array.isArray(run.capturePins)?run.capturePins.length:0;
  const outCount=Array.isArray(run.renderPins)?run.renderPins.length:0;
  // A manual port ordinal without the actual capture/render endpoint identity
  // cannot establish that 0FX and 1FX used the same wired device.
  if((mode!=='OUTPUT_ONLY_TONE'&&!nonempty(run.captureEndpoint))||
     (mode!=='CAPTURE_ONLY'&&!nonempty(run.renderEndpoint)))
    blockers.push(tag+': actual capture/render endpoint identity missing');
  if((mode==='OUTPUT_ONLY_TONE'&&(inCount!==0||outCount===0))||
     (mode==='CAPTURE_ONLY'&&(outCount!==0||inCount===0))||
     (mode.includes('THROUGH')&&(inCount===0||outCount===0||run.wired!==true)))
    blockers.push(tag+': physical I/O configuration is not valid for mode');
  const start=time(run.startedUtc),end=time(run.endedUtc);
  const elapsed=run.elapsedSeconds;
  if(!Number.isFinite(start)||!Number.isFinite(end)||end<start||
     !Number.isInteger(elapsed)||elapsed<0||
     Math.abs((end-start)/1000-elapsed)>5)
    blockers.push(tag+': elapsed seconds not consistent with UTC start/end');
  if(mode.includes('THROUGH')&&elapsed<1800)
    blockers.push(tag+': 30-minute wired soak not completed');
  if(run.operatorObservation!=='CLEAR')
    blockers.push(tag+': operator audio result is not CLEAR');
  if(!nonempty(run.witness))blockers.push(tag+': human witness missing');
  if(!safeName(run.reportFile)||!HEX64.test(run.reportSha256||''))
    blockers.push(tag+': full native report and SHA256 missing');
  else {
    try{const bytes=readExact(root,run.reportFile,run.reportSha256);
      if(bytes.length<16)throw Error('report too short');
    }catch(error){blockers.push(tag+': report integrity '+error.message);}
  }
  if(mode==='LIVE_THROUGH_1FX'){
    if(run.operatorReportedFxCount!==1||!nonempty(run.operatorReportedVst3Name))
      blockers.push(tag+': one real native VST3 identity missing');
  }else if(run.operatorReportedFxCount!==0||
    nonempty(run.operatorReportedVst3Name))
    blockers.push(tag+': baseline must have zero active VST3');
  // Capture flags are backend-dependent; neither callback gaps nor WASAPI
  // capture DATA_DISCONTINUITY are hardware render starvation counters.
  return true;
}
function comparable(a,b){
  return ['backend','sampleRate','bufferFrames','captureEndpoint','renderEndpoint']
    .every(k=>a?.[k]===b?.[k])&&
    same(a?.capturePins,b?.capturePins)&&same(a?.renderPins,b?.renderPins);
}
export function inspectM3eJournal(journal,{root='.'}={}){
  const blockers=[],warnings=[];
  if(!journal||journal.schema!==M3E_SCHEMA||
     !HEX40.test(journal.sourceSha||'')||
     !HEX64.test(journal.exeSha256||'')||
     !Array.isArray(journal.sessions)){
    return {physical:'PENDING',blockers:['Invalid or missing FAT field journal'],
      warnings,reviewReady:false};
  }
  if(journal.active!==null&&journal.active!==undefined)
    blockers.push('A running FAT session must be stopped');
  const selected=[];
  for(const rate of M3E_RATES){
    const byMode={};
    for(const mode of M3E_MODES){
      const matches=journal.sessions.filter(s=>s?.sampleRate===rate&&s.mode===mode)
        .sort((a,b)=>time(a.endedUtc)-time(b.endedUtc));
      const run=matches.at(-1);
      if(run){byMode[mode]=run;selected.push(run);}
      verifySession(run,mode,rate,root,blockers);
    }
    const a=byMode.DRY_THROUGH_0FX,b=byMode.LIVE_THROUGH_1FX;
    if(a&&b&&!comparable(a,b))
      blockers.push(rate+': 0FX/1FX must use identical backend, buffer, endpoint and physical pins');
    if(a&&b&&time(b.startedUtc)<time(a.endedUtc))
      blockers.push(rate+': 1FX run must occur after its completed 0FX baseline');
  }
  if(journal.sessions.some(s=>s?.operatorObservation==='CRACKLE'))
    warnings.push('Recorded crackle history exists; #15 requires independent investigation');
  if(journal.sessions.some(s=>![44100,48000].includes(s?.sampleRate)))
    warnings.push('Additional unqualified sample-rate sessions were recorded');
  const reviewReady=blockers.length===0;
  return {physical:reviewReady?'EVIDENCE_READY_FOR_HUMAN_REVIEW':'PENDING',
    reviewReady,blockers,warnings,selectedSessions:selected.length,
    actualRenderXrunEvidence:'UNAVAILABLE_UNLESS_SEPARATELY_MEASURED',
    operatorObservations:'SELF_REPORTED_NOT_AUTOMATICALLY_CERTIFIED',
    hardwareCertified:false};
}
export function assessM3eFat(root){
  let packageResult;
  try{packageResult=inspectM3ePackage(root);}
  catch(error){return {build:'BLOCKED',nativePcm:'PENDING_NATIVE_CALLBACK_PROOF',
    physical:'PENDING',releaseReady:false,hardwareCertified:false,
    blockers:['Package integrity: '+error.message]};}
  const journalPath=join(resolve(root),'FAT_RUNS.json');
  if(!existsSync(journalPath))
    return {sourceSha:packageResult.sourceSha,build:packageResult.build,
      zip:packageResult.zip,nativePcm:'PENDING_NATIVE_CALLBACK_PROOF',
      physical:'PENDING',releaseReady:false,hardwareCertified:false,
      blockers:['No physical FAT_RUNS.json: no 44.1/48-kHz field evidence']};
  try{
    const journal=readJsonFile(journalPath);
    if(journal.sourceSha!==packageResult.sourceSha||
       journal.exeSha256!==packageResult.exeSha256)
      throw Error('Journal source/executable SHA differs from packaged native EXE');
    const physical=inspectM3eJournal(journal,{root:resolve(root)});
    return {sourceSha:packageResult.sourceSha,build:packageResult.build,
      zip:packageResult.zip,nativePcm:'PENDING_NATIVE_CALLBACK_PROOF',
      physical:physical.physical,releaseReady:false,hardwareCertified:false,
      selectedSessions:physical.selectedSessions,blockers:physical.blockers,
      warnings:physical.warnings,
      renderHardwareXrun:physical.actualRenderXrunEvidence};
  }catch(error){
    return {sourceSha:packageResult.sourceSha,build:packageResult.build,
      zip:packageResult.zip,nativePcm:'PENDING_NATIVE_CALLBACK_PROOF',
      physical:'PENDING',releaseReady:false,hardwareCertified:false,
      blockers:['FAT journal integrity: '+error.message]};
  }
}
// CLI is read-only. Exit 2 only on invalid package; missing hardware logs
// are an explicit PENDING result, not a CI packaging failure and never PASS.
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const output=assessM3eFat(process.argv[2]||'.');
  console.log(JSON.stringify(output,null,2));
  if(output.build==='BLOCKED')process.exitCode=2;
}
