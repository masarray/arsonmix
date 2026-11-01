// SPDX-License-Identifier: AGPL-3.0-only
// Evidence verifier; NOT a native PCM engine and NEVER a physical sound certificate.
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {resolve,join} from 'node:path';
import {pathToFileURL} from 'node:url';
export const TRACE_SCHEMA='arsonmix.console-v6.acceptance-trace.v2';
const fail=s=>{throw Error(s)};
const must=(ok,s)=>{if(!ok)fail(s)};
const json=p=>JSON.parse(readFileSync(p,'utf8'));
const hash=p=>createHash('sha256').update(readFileSync(p)).digest('hex');
const is40=s=>/^[0-9a-f]{40}$/.test(s||'');
const is64=s=>/^[0-9a-f]{64}$/.test(s||'');
const sample=(v,label)=>{must(Array.isArray(v)&&v.length>=4&&v.length<=65536,label+': bounded PCM sample array required');must(v.every(x=>typeof x==='number'&&Number.isFinite(x)&&Math.abs(x)<=8),label+': nonfinite/invalid PCM');return v};
const compare=(a,b,label)=>{must(a.length===b.length&&a.every((x,i)=>Math.abs(x-b[i])<=1e-5),label+': PCM mismatch')};
const audible=(a,label)=>must(a.reduce((s,x)=>s+x*x,0)/a.length>1e-9,label+': silent PCM cannot prove routing');
// G0 wire gate: actual WebView event envelopes, no parallel legacy trace shape.
// This validator checks claimed evidence structure, not producer authenticity.
const UINT64_MAX=18446744073709551615n;
function u64(v,where,positive=false){
 must(typeof v==='string'&&/^(0|[1-9][0-9]*)$/.test(v),
   where+': canonical decimal-string uint64 required');
 const n=BigInt(v);
 must(n<=UINT64_MAX&&(!positive||n>0n),where+': uint64 overflow/zero');
 return n;
}
function snapshot(s){
 must(s?.schemaVersion===6&&typeof s.productionCallbackLive==='boolean',
   'snapshot: schema v6 and explicit production callback state required');
 must(s.device&&s.graph&&s.telemetry,'snapshot: device/graph/telemetry required');
 u64(s.device.epoch,'device epoch',true);
 u64(s.graph.revision,'graph revision');
 u64(s.graph.rosterRevision,'roster revision');
 must(typeof s.device.outputArmed==='boolean','snapshot: outputArmed required');
 for(const k of ['enabledInputs','enabledOutputs','inputEndpoints','outputEndpoints'])
   must(Array.isArray(s.device[k]),'snapshot: native '+k+' inventory missing');
 for(const k of ['enabledInputs','enabledOutputs']){
   const a=s.device[k];
   must(a.every(x=>Number.isSafeInteger(x)&&x>=0&&x<=255)&&
     new Set(a).size===a.length,'snapshot: invalid physical '+k);
 }
 const g=s.graph,c=g.capabilities;
 must(Array.isArray(g.channels)&&Array.isArray(g.destinations)&&c&&
   typeof c.callbackOwnsChannelIds==='boolean'&&Array.isArray(c.supportedOps),
   'snapshot: native channels/destinations/capability/supportedOps missing');
 must(c.supportedOps.every(x=>typeof x==='string')&&
   new Set(c.supportedOps).size===c.supportedOps.length,'snapshot: invalid supportedOps');
 if(s.productionCallbackLive)must(c.callbackOwnsChannelIds===true,
   'snapshot: LIVE without installed native callback');
 // M4 can have ARMED physical outputs while universal ChannelId v6 is
 // NOT admitted. Do not collapse the legacy output state into a fake LIVE flag.
 // Separately, a v6 graph with productionCallbackLive=false is never audio proof.
 const ids=new Set();
 for(const x of g.channels){
   u64(x?.id,'ChannelId',true);
   must(!ids.has(x.id),'snapshot: duplicate ChannelId');ids.add(x.id);
   must(['INPUT','MAIN','MONITOR','GROUP','FX_RETURN'].includes(x.role),
     'snapshot: invalid channel role');
   if(x.role==='GROUP')must(c.groups===true,'GROUP without native capability');
   if(x.role==='FX_RETURN')must(c.fxReturns===true,'FX Return without native capability');
   if(x.role==='MONITOR')must(c.monitor===true,'MONITOR without native capability');
   const pins=x.source?.physicalPins;
   if(pins!==undefined){
     must(Array.isArray(pins)&&(pins.length===1||pins.length===2),
       'snapshot: invalid mono/stereo pin count');
     must(pins.every(p=>s.device.enabledInputs.includes(p)),
       'snapshot: source outside enabled native pins');
     if(pins.length===2)must(pins[0]!==pins[1],
       'snapshot: duplicate L/R physical stereo pin');
   }
 }
 return s;
}
export function checkTrace(t){
 must(t?.schema===TRACE_SCHEMA&&Array.isArray(t.events)&&t.events.length>=7,
   'trace v2 must use native WebView event envelopes');
 must(['SYNTHETIC','NATIVE_JUCE'].includes(t.provenance?.kind),
   'trace: synthetic/native provenance declaration required');
 if(t.provenance.kind==='NATIVE_JUCE')must(is40(t.provenance.sourceSha)&&
   t.provenance.producer==='JUCE_WEBVIEW','native provenance requires exact SHA and producer');
 let state=null,pending=null,previousAck=null,accepted=0,rejected=0;
 const ids=new Set();
 for(const [i,e] of t.events.entries()){
   const type=e?.event,p=e?.payload,where='event '+i;
   must(['arMixConsoleSnapshot','arMixConsoleIntent','arMixConsoleAck'].includes(type)&&
     p&&typeof p==='object',where+': unrecognized native WebView event');
   if(type==='arMixConsoleSnapshot'){
     snapshot(p);
     if(!state){must(i===0,'first event must be accepted native snapshot');state=p;continue}
     must(previousAck,where+': unsolicited snapshot');
     if(previousAck.status==='ACCEPTED'){
       must(p.device.epoch===state.device.epoch&&
         p.graph.revision===previousAck.graphRevision&&
         p.graph.rosterRevision===previousAck.rosterRevision,
         where+': accepted ACK needs matching fresh snapshot');
       must(u64(p.graph.revision,'next graph')>=u64(state.graph.revision,'prior graph'),
         where+': graph revision regressed');
     }else must(p.device.epoch===state.device.epoch&&
       JSON.stringify(p.graph)===JSON.stringify(state.graph)&&
       p.device.outputArmed===state.device.outputArmed,
       where+': rejected intent mutated accepted graph or ARM');
     state=p;previousAck=null;continue;
   }
   must(state&&!previousAck,where+': fresh snapshot required after ACK');
   if(type==='arMixConsoleIntent'){
     must(!pending&&typeof p.requestId==='string'&&
       p.requestId.length>=5&&p.requestId.length<=160&&
       /^[\x21-\x7e]+$/.test(p.requestId)&&p.requestId.includes(':')&&
       !ids.has(p.requestId),
       where+': unique session-scoped opaque requestId required');
     u64(p.deviceEpoch,'intent deviceEpoch',true);
     u64(p.baseGraphRevision,'intent baseGraphRevision');
     u64(p.baseRosterRevision,'intent baseRosterRevision');
     must(typeof p.operation==='string'&&p.operation.length>0,
       where+': operation required');
     if(p.targetChannelId!=null)u64(p.targetChannelId,'targetChannelId',true);
     must(p.payload&&typeof p.payload==='object'&&!Array.isArray(p.payload),
       where+': payload object required');
     ids.add(p.requestId);pending=p;continue;
   }
   must(pending&&p.requestId===pending.requestId,where+': ACK requestId mismatch');
   must(['ACCEPTED','REJECTED','REQUIRES_DEVICE_CHANGE'].includes(p.status),
     where+': unsupported native ACK status');
   const stale=pending.deviceEpoch!==state.device.epoch||
     pending.baseGraphRevision!==state.graph.revision||
     pending.baseRosterRevision!==state.graph.rosterRevision;
   must(!stale||p.status!=='ACCEPTED',where+': stale epoch/graph/roster accepted');
   must(state.graph.capabilities.supportedOps.includes(pending.operation)||
     p.status!=='ACCEPTED',where+': unsupported native operation accepted');
   if(p.status==='ACCEPTED'){
     must(u64(p.graphRevision,'ACK graph revision')>=u64(state.graph.revision,'accepted graph')&&
       u64(p.rosterRevision,'ACK roster revision')>=u64(state.graph.rosterRevision,'accepted roster'),
       where+': ACK revision regressed');accepted++;
   }else{
     must(typeof p.errorCode==='string'&&p.errorCode.length>0,
       where+': rejected ACK missing errorCode');
     if(p.graphRevision!==undefined)must(p.graphRevision===state.graph.revision,
       where+': rejected ACK changed graph revision');
     if(p.rosterRevision!==undefined)must(p.rosterRevision===state.graph.rosterRevision,
       where+': rejected ACK changed roster revision');rejected++;
   }
   previousAck=p;pending=null;
 }
 must(!pending&&!previousAck,'trace truncated without ACK + full snapshot');
 must(accepted>0&&rejected>0,'trace needs accepted and rejected rollback');
 return {acceptedCommands:accepted,rejectedCommands:rejected,
   finalGraphRevision:state.graph.revision,provenance:t.provenance.kind,
   productionCallbackCertified:false,hardwareCertified:false};
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
