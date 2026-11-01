// SPDX-License-Identifier: AGPL-3.0-only
// Evidence verifier; NOT a native PCM engine and NEVER a physical sound certificate.
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
export const TRACE_SCHEMA='arsonmix.console-v6.acceptance-trace.v1';
const fail=s=>{throw Error(s)};
const must=(ok,s)=>{if(!ok)fail(s)};
const json=p=>JSON.parse(readFileSync(p,'utf8'));
const hash=p=>createHash('sha256').update(readFileSync(p)).digest('hex');
const is40=s=>/^[0-9a-f]{40}$/.test(s||'');
const is64=s=>/^[0-9a-f]{64}$/.test(s||'');
const sample=(v,label)=>{must(Array.isArray(v)&&v.length>=4&&v.length<=65536,label+': bounded PCM sample array required');must(v.every(x=>typeof x==='number'&&Number.isFinite(x)&&Math.abs(x)<=8),label+': nonfinite/invalid PCM');return v};
const compare=(a,b,label)=>{must(a.length===b.length&&a.every((x,i)=>Math.abs(x-b[i])<=1e-5),label+': PCM mismatch')};
const audible=(a,label)=>must(a.reduce((s,x)=>s+x*x,0)/a.length>1e-9,label+': silent PCM cannot prove routing');
function checkSnapshot(s){
 must(s?.device&&s.graph&&typeof s.device.epoch==='string'&&s.device.epoch.length>0,'snapshot: native device epoch missing');
 must(Number.isSafeInteger(s.graph.revision)&&s.graph.revision>=0,'snapshot: accepted revision missing');
 must(typeof s.device.outputArmed==='boolean','snapshot: actual outputArmed missing');
 must(Array.isArray(s.graph.channels)&&s.graph.capabilities,'snapshot: channels/capabilities missing');
 const ids=new Set();
 for(const c of s.graph.channels){
   must(typeof c.id==='string'&&/^(0|[1-9][0-9]*)$/.test(c.id)&&BigInt(c.id)<=18446744073709551615n,'snapshot: ChannelId must be decimal uint64 string');
   must(!ids.has(c.id),'snapshot: duplicate ChannelId');ids.add(c.id);
   must(['INPUT','GROUP','FX_RETURN','MONITOR','MAIN'].includes(c.role),'snapshot: unknown role');
   if(c.role==='GROUP')must(s.graph.capabilities.groups===true,'snapshot: GROUP without native capability');
   if(c.role==='FX_RETURN')must(s.graph.capabilities.fxReturns===true,'snapshot: FX RETURN without native capability');
   if(c.role==='MONITOR')must(s.graph.capabilities.monitor===true,'snapshot: MONITOR without native capability');
 }
 return s;
}
export function checkTrace(t){
 must(t?.schema===TRACE_SCHEMA&&Array.isArray(t.events)&&t.events.length>=3,'trace schema/events missing');
 let current=null,pending=null,awaiting=null,accepts=0,rejects=0;
 const requestIds=new Set();
 for(const [i,e] of t.events.entries()){
  const tag='event '+i;
  must(['snapshot','intent','ack'].includes(e?.type),tag+': invalid event');
  if(e.type==='snapshot'){
    const s=checkSnapshot(e);
    if(!current){must(i===0,'first event must be full accepted snapshot');current=s;continue}
    must(awaiting,tag+': unsolicited accepted graph');
    if(awaiting.status==='ACCEPTED'){
      must(s.device.epoch===current.device.epoch,tag+': device epoch unexpectedly changed');
      must(s.graph.revision===awaiting.newRevision,tag+': ACK needs exact full accepted snapshot');
    }else{
      must(s.device.epoch===current.device.epoch&&JSON.stringify(s.graph)===JSON.stringify(current.graph),tag+': rejected command changed accepted graph');
      must(s.device.outputArmed===current.device.outputArmed,tag+': rejected command changed output ARM');
    }
    current=s;awaiting=null;continue;
  }
  must(current&&!awaiting,tag+': full snapshot required after previous ACK');
  if(e.type==='intent'){
   must(!pending&&typeof e.requestId==='string'&&e.requestId.length>0,tag+': serialized requestId required');
   must(Number.isSafeInteger(e.baseRevision)&&typeof e.deviceEpoch==='string',tag+': baseRevision/deviceEpoch required');
   must(typeof e.op==='string'&&e.op.length>0,tag+': operation required');
   if(e.targetChannelId!=null)must(typeof e.targetChannelId==='string',tag+': ChannelId cannot be JS number');
   must(!requestIds.has(e.requestId),tag+': duplicate requestId; replay cached ACK without reapplying');
   requestIds.add(e.requestId);pending=e;continue;
  }
  must(pending&&e.requestId===pending.requestId,tag+': ACK unmatched');
  must(['ACCEPTED','REJECTED','REQUIRES_DEVICE_CHANGE'].includes(e.status),tag+': unknown ACK status');
  const stale=pending.baseRevision!==current.graph.revision||pending.deviceEpoch!==current.device.epoch;
  must(!stale||e.status!=='ACCEPTED',tag+': stale revision/epoch must reject');
  if(e.status==='ACCEPTED'){
    must(Number.isSafeInteger(e.newRevision)&&e.newRevision>=current.graph.revision,tag+': accepted revision regressed');
    accepts++;
  }else{
    must(typeof e.errorCode==='string'&&e.errorCode.length>0,tag+': reject must explain errorCode');
    must(e.newRevision==null||e.newRevision===current.graph.revision,tag+': reject advanced graph revision');
    rejects++;
  }
  awaiting=e;pending=null;
 }
 must(!pending&&!awaiting,'trace truncated: ACK/full snapshot required');
 must(accepts>0&&rejects>0,'trace must prove positive ACK plus negative ACK rollback');
 return {acceptedCommands:accepts,rejectedCommands:rejects,finalRevision:current.graph.revision};
}
export function checkPcm(p){
 must(p?.schema==='arsonmix.console-v6.pcm-evidence.v1'&&Array.isArray(p.cases),'PCM schema missing');
 const required=['independent_inputs','group_fx_return','monitor_isolation','physical_mute'],seen=new Set();
 for(const c of p.cases){
   must(required.includes(c.kind)&&!seen.has(c.kind),'PCM duplicate/unrecognized case');
   seen.add(c.kind);
   const s={};
   for(const [k,v] of Object.entries(c.samples||{}))s[k]=sample(v,c.kind+'.'+k);
   const get=k=>{must(s[k],c.kind+': missing '+k);return s[k]};
   if(c.kind==='independent_inputs'){
      const a=get('input1'),b=get('input2');
      audible(a,'IN1');audible(b,'IN2');
      must(a.length===b.length&&a.some((x,i)=>Math.abs(x-b[i])>1e-3),'sources must be distinguishable');
      compare(get('ch7'),a,'IN1->CH7');compare(get('ch2'),b,'IN2->CH2');
   }else if(c.kind==='group_fx_return'){
      const dry=get('dry'),grp=get('group'),wet=get('wetReturn');
      must(typeof c.groupGain==='number'&&c.groupGain>0&&c.groupGain<1,'Group must measurably attenuate dry PCM');
      audible(dry,'GROUP dry');audible(wet,'FX RETURN wet');
      must(wet.length===dry.length&&wet.some((x,i)=>Math.abs(x-dry[i])>1e-3),'FX Return cannot duplicate dry PCM');
      compare(grp,dry.map(x=>x*c.groupGain),'serial GROUP');
      compare(get('main'),grp.map((x,i)=>x+wet[i]),'MAIN must contain GROUP+wet without double-dry');
      compare(get('groupMutedMain'),wet,'Group muted retains wet ONLY');
      compare(get('returnMutedMain'),grp,'FX Return muted retains Group dry');
   }else if(c.kind==='monitor_isolation'){
      must(typeof c.mainDestination==='string'&&typeof c.monitorDestination==='string'&&c.mainDestination!==c.monitorDestination,'MONITOR needs distinct destination');
      const main=get('main'),mon=get('monitor');audible(main,'MAIN');audible(mon,'MONITOR');
      compare(get('monitorMuted'),mon.map(()=>0),'MONITOR mute');
      compare(get('mainAfterMonitorMute'),main,'MONITOR mute cannot change MAIN');
   }else{
      const main=get('mainBeforeMute');audible(main,'pre-mute MAIN');
      compare(get('mainAfterMute'),main.map(()=>0),'MASTER mute');
   }
 }
 must(required.every(k=>seen.has(k)),'missing PCM cases: '+required.filter(k=>!seen.has(k)).join(','));
 return {pcmCases:seen.size,note:'numerical evidence only; synthetic fixtures are not live hardware proof'};
}
export function checkRelease(r,dist){
 must(is40(r?.sourceSha),'release: exact source SHA required');
 must(r.nativeCI?.sourceSha===r.sourceSha&&r.nativeCI?.conclusion==='success','release: same-source Windows CI PASS required');
 must(r.chromiumCI?.sourceSha===r.sourceSha&&r.chromiumCI?.conclusion==='success','release: same-source Chromium CI PASS required');
 for(const ci of [r.nativeCI,r.chromiumCI])must(/^https:\/\/github\.com\/masarray\/arsonmix\/actions\/runs\/[0-9]+$/.test(ci.url||''),'release: GitHub CI run URL required');
 const m=json(join(dist,'BUILD_PROVENANCE.json'));
 must(m.sourceCommit===r.sourceSha,'release: package sourceCommit mismatches exact CI SHA');
 must(m.fatStatus==='EXPERIMENTAL_NOT_HARDWARE_QUALIFIED','release: package cannot self-certify hardware');
 must(m.peMachine==='AMD64 (0x8664)'&&m.peSubsystem==='WINDOWS_GUI (2)'&&m.webView2LoaderMode==='JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1','release: Windows x64 GUI/WebView2 provenance missing');
 must(is64(m.exeSha256)&&m.exeSha256===hash(join(dist,m.executable)),'release: EXE SHA-256 does not match actual packaged bytes');
 must(r.exeSha256===m.exeSha256,'release: evidence EXE SHA mismatch');
 must(r.nativeCTest==='PASS'&&r.chromiumPlaywright==='PASS'&&r.windowsVerifier==='PASS','release: existing CTest/Chromium/package verifier proof missing');
 must(is64(r.artifactSha256)&&is64(r.traceSha256)&&is64(r.pcmSha256),'release: ZIP/trace/PCM evidence digests required');
 const h=r.hardware;
 must(h&&h.issue===15&&h.status==='PENDING_HUMAN_REVIEW','release: issue #15 physical review must remain open');
 must(h.backend&&h.sampleRate>0&&h.bufferFrames>0&&Array.isArray(h.inputPins)&&h.inputPins.length>0&&Array.isArray(h.outputPins)&&h.outputPins.length>0&&h.wired===true&&h.testDurationSeconds>=1800,'release: wired 0FX physical 30min evidence required');
 must(h.outputOnlyTone==='RECORDED'&&h.captureOnly==='RECORDED'&&h.dry0Fx==='RECORDED'&&is64(h.fullReportSha256),'release: OUTPUT_ONLY_TONE, CAPTURE_ONLY, 0FX and full report required');
 must(h.verifiedHumanWitness===true&&typeof h.witnessId==='string'&&h.witnessId.length>=3,'release: human witness review required');
 return {stage:'EVIDENCE_INTAKE_COMPLETE__ACOUSTIC_REVIEW_REQUIRED',certified:false,sourceSha:r.sourceSha,exeSha256:m.exeSha256};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(resolve(process.argv[1])).href){
 try{
  const [mode,evidence,dist]=process.argv.slice(2);
  must(['trace','pcm','release'].includes(mode)&&evidence,'usage: node scripts/console_v6_fat_gate.mjs trace|pcm|release EVIDENCE.json [dist]');
  const result=mode==='trace'?checkTrace(json(evidence)):mode==='pcm'?checkPcm(json(evidence)):checkRelease(json(evidence),dist||'dist');
  console.log(JSON.stringify({ok:true,mode,...result},null,2));
 }catch(e){console.error('FAT GATE REJECT: '+e.message);process.exitCode=1}
}
