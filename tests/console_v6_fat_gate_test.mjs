// SPDX-License-Identifier: AGPL-3.0-only
// SYNTHETIC validator self-tests; these cannot qualify native callback or hardware.
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync,readFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {checkTrace,checkPcm,checkRelease,TRACE_SCHEMA} from '../scripts/console_v6_fat_gate.mjs';
const copy=x=>structuredClone(x);
const snapshot=n=>({event:'arMixConsoleSnapshot',payload:{
 schemaVersion:6,productionCallbackLive:false,
 device:{epoch:'69',outputArmed:false,enabledInputs:[0,1],enabledOutputs:[0,1],
 inputEndpoints:[],outputEndpoints:[]},
 graph:{revision:String(n),rosterRevision:'5',
 capabilities:{callbackOwnsChannelIds:false,groups:false,fxReturns:false,
 monitor:true,supportedOps:['SET_FADER','SET_SOURCE']},
 channels:[{id:'7',role:'INPUT',source:{physicalPins:[0]}},
 {id:'2',role:'INPUT',source:{physicalPins:[1]}},{id:'100',role:'MAIN'}],
 destinations:[]},telemetry:{rawCapturePresent:false}}});
const evt=(event,payload)=>({event,payload});
const trace=()=>({schema:TRACE_SCHEMA,provenance:{kind:'SYNTHETIC'},events:[
 snapshot(2),
 evt('arMixConsoleIntent',{requestId:'webview-session:1',deviceEpoch:'69',
 baseGraphRevision:'2',baseRosterRevision:'5',operation:'SET_FADER',
 targetChannelId:'7',payload:{faderDb:-6}}),
 evt('arMixConsoleAck',{requestId:'webview-session:1',status:'ACCEPTED',
 graphRevision:'3',rosterRevision:'5'}),snapshot(3),
 evt('arMixConsoleIntent',{requestId:'webview-session:2',deviceEpoch:'69',
 baseGraphRevision:'2',baseRosterRevision:'5',operation:'SET_SOURCE',
 targetChannelId:'7',payload:{portId:'IN1'}}),
 evt('arMixConsoleAck',{requestId:'webview-session:2',status:'REJECTED',
 graphRevision:'3',rosterRevision:'5',errorCode:'STALE_GRAPH'}),snapshot(3)]});
const dry=[0.2,-0.1,0.4,-0.3],wet=[0.01,0.03,0.02,-0.01],grp=dry.map(x=>x*0.5),zero=a=>a.map(()=>0);
const pcm=()=>({schema:'arsonmix.console-v6.pcm-evidence.v1',cases:[
 {kind:'independent_inputs',samples:{input1:[0.1,-0.1,0.1,-0.1],input2:[0.05,0.15,-0.05,-0.15],ch7:[0.1,-0.1,0.1,-0.1],ch2:[0.05,0.15,-0.05,-0.15]}},
 {kind:'group_fx_return',groupGain:0.5,samples:{dry,group:grp,wetReturn:wet,main:grp.map((x,i)=>x+wet[i]),groupMutedMain:wet,returnMutedMain:grp}},
 {kind:'monitor_isolation',mainDestination:'hp0/1',monitorDestination:'hp2/3',samples:{main:dry,monitor:wet,monitorMuted:zero(wet),mainAfterMonitorMute:dry}},
 {kind:'physical_mute',samples:{mainBeforeMute:dry,mainAfterMute:zero(dry)}}
]});
test('canonical native WebView trace has exact ACK/full snapshot and no fake live',()=>{
 assert.equal(checkTrace(trace()).provenance,'SYNTHETIC');
 const a=trace();a.events[3]=a.events[4];
 assert.throws(()=>checkTrace(a),/fresh snapshot required/);
 const b=trace();b.events[6].payload.graph.channels[0].id='50';
 assert.throws(()=>checkTrace(b),/rejected intent mutated/);
 const c=trace();c.events[1].payload.requestId='1';
 assert.throws(()=>checkTrace(c),/session-scoped opaque requestId/);
 const armedM4=trace();
 for(const item of armedM4.events){
   if(item.event==='arMixConsoleSnapshot'){
     item.payload.device.outputArmed=true;
     item.payload.legacyM4={routeAccepted:true,routingRevision:'17'};
   }
 }
 const legacyResult=checkTrace(armedM4);
 assert.equal(legacyResult.hardwareCertified,false);
 assert.equal(legacyResult.productionCallbackCertified,false);
});
test('uint64 overflow, JS numeric revision, fake stereo pin and absent capability reject',()=>{
 const a=trace();a.events[0].payload.graph.channels[0].id='18446744073709551616';
 assert.throws(()=>checkTrace(a),/uint64 overflow/);
 const b=trace();b.events[1].payload.baseGraphRevision=2;
 assert.throws(()=>checkTrace(b),/decimal-string uint64/);
 const c=trace();c.events[0].payload.graph.channels[0].source.physicalPins=[0,0];
 assert.throws(()=>checkTrace(c),/duplicate L\/R physical stereo/);
 const d=trace();d.events[0].payload.graph.channels.push({id:'101',role:'GROUP'});
 assert.throws(()=>checkTrace(d),/GROUP without native capability/);
});
test('duplicate/replayed request, stale device, graph and roster, unsupported op',()=>{
 const a=trace();a.events[4].payload.requestId='webview-session:1';
 a.events[5].payload.requestId='webview-session:1';
 assert.throws(()=>checkTrace(a),/session-scoped opaque requestId/);
 const b=trace();b.events[1].payload.deviceEpoch='70';
 assert.throws(()=>checkTrace(b),/stale epoch\/graph\/roster/);
 const c=trace();c.events[1].payload.baseRosterRevision='4';
 assert.throws(()=>checkTrace(c),/stale epoch\/graph\/roster/);
 const d=trace();d.events[1].payload.operation='CREATE_CHANNEL';
 assert.throws(()=>checkTrace(d),/unsupported native operation/);
 const e=trace();e.events[2].payload.rosterRevision='6';
 assert.throws(()=>checkTrace(e),/ACK needs matching fresh snapshot/);
});
test('independent sparse native routing IN1->CH7 / IN2->CH2 and non-silent evidence',()=>{
 assert.equal(checkPcm(pcm()).pcmCases,4);
 const a=pcm();a.cases[0].samples.ch7=a.cases[0].samples.input2;assert.throws(()=>checkPcm(a),/IN1->CH7/);
 const b=pcm();b.cases[0].samples.input1=zero(b.cases[0].samples.input1);assert.throws(()=>checkPcm(b),/silent PCM/);
});
test('Group serial, FX Return wet parallel, no doubled dry and return mute',()=>{
 const a=pcm();a.cases[1].samples.main=dry.map((x,i)=>x+grp[i]+wet[i]);assert.throws(()=>checkPcm(a),/double-dry/);
 const b=pcm();b.cases[1].samples.wetReturn=dry;assert.throws(()=>checkPcm(b),/duplicate dry PCM/);
 const c=pcm();c.cases[1].samples.returnMutedMain=zero(grp);assert.throws(()=>checkPcm(c),/FX Return muted retains/);
});
test('MONITOR distinct device destination and independent mute; MASTER silence',()=>{
 const a=pcm();a.cases[2].monitorDestination='hp0/1';assert.throws(()=>checkPcm(a),/distinct destination/);
 const b=pcm();b.cases[2].samples.mainAfterMonitorMute=zero(dry);assert.throws(()=>checkPcm(b),/cannot change MAIN/);
 const c=pcm();c.cases[3].samples.mainAfterMute=dry;assert.throws(()=>checkPcm(c),/MASTER mute/);
});
test('exact-source full native evidence rejects forged digest, mock PCM, missing live roles and 0FX report',()=>{
 const dir=mkdtempSync(join(tmpdir(),'arsonmix-fat-'));
 try {
  const sha='a'.repeat(40);
  const hashBytes=data=>createHash('sha256').update(data).digest('hex');
  const persist=(file,data)=>{
   const bytes=Buffer.isBuffer(data)?data:Buffer.from(typeof data==='string'?data:JSON.stringify(data));
   writeFileSync(join(dir,file),bytes);
   return hashBytes(bytes);
  };
  const exeHash=persist('console.exe','EXE FIXTURE ONLY');
  persist('BUILD_PROVENANCE.json',{
   sourceCommit:sha,executable:'console.exe',exeSha256:exeHash,
   fatStatus:'EXPERIMENTAL_NOT_HARDWARE_QUALIFIED',
   peMachine:'AMD64 (0x8664)',peSubsystem:'WINDOWS_GUI (2)',
   webView2LoaderMode:'JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1'});
  const traceFixture=trace();
  traceFixture.provenance={kind:'NATIVE_JUCE',sourceSha:sha,producer:'JUCE_WEBVIEW'};
  const liveCaps={
   callbackOwnsChannelIds:true,fullVendorFxMigration:true,
   showAndOutputParity:true,physicalCaptureMapped:true,dynamicChannels:true,
   groups:true,fxReturns:true,monitor:true,
   supportedOps:['SET_FADER','SET_SOURCE']};
  const channels=Array.from({length:11},(_,i)=>({
   id:String(i+1),role:'INPUT',source:{physicalPins:[i%2]}}));
  channels.push(...[
   {id:'50',role:'GROUP'}, {id:'51',role:'FX_RETURN'},
   {id:'52',role:'MONITOR'}, {id:'53',role:'MAIN'}]);
  for(const item of traceFixture.events){
   if(item.event!=='arMixConsoleSnapshot')continue;
   item.payload.productionCallbackLive=true;
   item.payload.device.outputArmed=true;
   item.payload.graph.capabilities=liveCaps;
   item.payload.graph.channels=channels;
   item.payload.telemetry={
    rawCapturePresent:true,mainPcmPresent:true,monitorPcmPresent:true};
  }
  const pcmFixture=pcm();
  pcmFixture.provenance={kind:'NATIVE_JUCE',sourceSha:sha,producer:'JUCE_CALLBACK'};
  const archiveHash=persist('release.zip',Buffer.from('PK\x03\x04synthetic-zip-header-only'));
  const traceHash=persist('trace.json',traceFixture);
  const pcmHash=persist('pcm.json',pcmFixture);
  const reportHash=persist('audio-report.txt','SYNTHETIC HARDWARE QA TEST FIXTURE — NOT AUDIO CERTIFICATION');
  const record={
   sourceSha:sha,exeSha256:exeHash,
   nativeCI:{sourceSha:sha,conclusion:'success',url:'https://github.com/masarray/arsonmix/actions/runs/123'},
   chromiumCI:{sourceSha:sha,conclusion:'success',url:'https://github.com/masarray/arsonmix/actions/runs/124'},
   nativeCTest:'PASS',chromiumPlaywright:'PASS',windowsVerifier:'PASS',
   artifactSha256:archiveHash,traceSha256:traceHash,pcmSha256:pcmHash,
   artifacts:{zip:'release.zip',nativeTrace:'trace.json',nativePcm:'pcm.json',
    hardwareReport:'audio-report.txt'},
   hardware:{issue:15,status:'PENDING_HUMAN_REVIEW',wired:true,
    backend:'ASIO',sampleRate:48000,bufferFrames:480,inputPins:[0,1],
    outputPins:[0,1],testDurationSeconds:1800,outputOnlyTone:'RECORDED',
    captureOnly:'RECORDED',dry0Fx:'RECORDED',
    fullReportSha256:reportHash,verifiedHumanWitness:true,witnessId:'qa1'}
  };
  // Even a fully structural synthetic test fixture never sets certified=true.
  const accepted=checkRelease(record,dir);
  assert.equal(accepted.certified,false);
  assert.equal(accepted.evidenceFilesHashed,4);
  assert.equal(accepted.pcmCases,4);
  const wrongSha=copy(record);
  wrongSha.chromiumCI.sourceSha='b'.repeat(40);
  assert.throws(()=>checkRelease(wrongSha,dir),/Chromium CI/);
  const tampered=copy(record);
  tampered.artifactSha256='f'.repeat(64);
  assert.throws(()=>checkRelease(tampered,dir),/release ZIP: actual file SHA-256 mismatch/);
  const tamperedHardware=copy(record);
  tamperedHardware.hardware.fullReportSha256='f'.repeat(64);
  assert.throws(()=>checkRelease(tamperedHardware,dir),/release hardware report: actual file SHA-256 mismatch/);
  const syntheticTrace=copy(traceFixture);
  syntheticTrace.provenance.kind='SYNTHETIC';
  const syntheticTraceHash=persist('trace.json',syntheticTrace);
  const fromFake=copy(record);
  fromFake.traceSha256=syntheticTraceHash;
  assert.throws(()=>checkRelease(fromFake,dir),/native WebView producer evidence required/);
  persist('trace.json',traceFixture);
  const syntheticPcm=copy(pcmFixture);
  syntheticPcm.provenance.kind='SYNTHETIC';
  const syntheticPcmHash=persist('pcm.json',syntheticPcm);
  const fakePcm=copy(record);
  fakePcm.pcmSha256=syntheticPcmHash;
  assert.throws(()=>checkRelease(fakePcm,dir),/actual JUCE callback PCM provenance required/);
  persist('pcm.json',pcmFixture);
  const missingRole=copy(traceFixture);
  for(const e of missingRole.events)
   if(e.event==='arMixConsoleSnapshot')
    e.payload.graph.channels=e.payload.graph.channels.filter(ch=>ch.role!=='FX_RETURN');
  const missingRoleHash=persist('trace.json',missingRole);
  const noGroup=copy(record);
  noGroup.traceSha256=missingRoleHash;
  assert.throws(()=>checkRelease(noGroup,dir),/missing live FX_RETURN channel/);
  persist('trace.json',traceFixture);
  const shorter=copy(record);
  shorter.hardware.testDurationSeconds=1799;
  assert.throws(()=>checkRelease(shorter,dir),/30min/);
  const missingWitness=copy(record);
  missingWitness.hardware.verifiedHumanWitness=false;
  assert.throws(()=>checkRelease(missingWitness,dir),/human witness/);
  const pathTraversal=copy(record);
  pathTraversal.artifacts.zip='../release.zip';
  assert.throws(()=>checkRelease(pathTraversal,dir),/safe basename/);
 }finally {rmSync(dir,{recursive:true,force:true})}
});

// S0 unit fixture uses the ONE shared PR #279 synthetic file, never native evidence.
import {verifyNativeS0} from '../scripts/run_console_native_s0.mjs';
test('S0 shared v8 contract rejects absent native pins, fake ACK and mutated graph',()=>{
 const f=JSON.parse(readFileSync('integration/console-v6/fixtures.json','utf8'));
 assert.equal(f.provenance,'SYNTHETIC_CONTRACT_ONLY_NOT_HARDWARE');
 const s=copy(f.g0),i={...copy(f.intent),targetChannelId:null,
   deviceEpoch:s.device.epoch,baseGraphRevision:s.graph.revision,
   baseRosterRevision:s.graph.rosterRevision};
 const a={...copy(f.ack),requestId:i.requestId,deviceEpoch:s.device.epoch,
   graphRevision:s.graph.revision,rosterRevision:s.graph.rosterRevision,
   status:'REJECTED',errorCode:'UNIVERSAL_CALLBACK_NOT_READY'};
 const ev=[{event:'arMixConsoleSnapshot',payload:s},
   {event:'arMixConsoleIntent',payload:i},
   {event:'arMixConsoleAck',payload:a},
   {event:'arMixConsoleSnapshot',payload:copy(s)}];
 assert.equal(verifyNativeS0(ev).callbackPcmProven,false);
 const bad=copy(ev);delete bad[0].payload.device.enabledInputs;
 assert.throws(()=>verifyNativeS0(bad),/array required|enabledInputs/);
 const fake=copy(ev);fake[2].payload.status='ACCEPTED';
 assert.throws(()=>verifyNativeS0(fake),/native ACK must reject/);
 const rollback=copy(ev);rollback[3].payload.device.outputArmed=true;
 assert.throws(()=>verifyNativeS0(rollback),/changed accepted graph/);
});

// S0 regression: a portable fixture binary that ignores argv must NEVER
// masquerade as JUCE's native WebView producer or grant product acceptance.
import {assertExplicitS0Exporter} from '../scripts/run_console_native_s0.mjs';
test('native S0 exporter must deliberately reject unknown CLI, not crash/fail/ignore',()=>{
 assert.throws(()=>assertExplicitS0Exporter('/fake/fixture',
   ()=>Buffer.from('{"events":[]}')),
   /C\+\+ binary ignored unknown flag/);
 let calls=0;
 assert.doesNotThrow(()=>assertExplicitS0Exporter('/fake/native',()=>{
   calls++;
   const e=new Error('unsupported CLI');
   e.status=64;e.stderr=Buffer.from('ARSONMIX_S0_UNSUPPORTED_ARGUMENT');
   throw e;
 }));
 assert.equal(calls,1);
 for(const failure of [
   {status:3221226505,stderr:Buffer.from('Assertion failed')},
   {code:'ENOENT'},
   {status:2,stderr:Buffer.from('unrelated failed CTest')},
   {code:'ETIMEDOUT'}
 ]){
   assert.throws(()=>assertExplicitS0Exporter('/fake/crash',()=>{
     const e=new Error('failed native process');Object.assign(e,failure);throw e;
   }),/probe crashed, failed to execute, or lacked explicit rejection marker/);
 }
});



// Source-only phase gate must NEVER call portable golden/live M4 a universal
// callback or a product. Not an alternative schema implementation.
import {inspectPuzzleSources} from '../scripts/check_console_puzzle.mjs';
test('shared #279 release preflight blocks even a source with working M4 controls if universal callback remains UNREADY',()=>{
 const src={
  native:'PuzzleV8Wire::snapshot(w) PuzzleV8Wire::ack(ack) w.inputs.push_back(...) w.outputs.push_back(...) w.productionCallbackLive=false; engine.switchDryRoutingLabSource( engine.setDryRoutingLabParameter(',
  dispatcher:'arMixConsoleSnapshot arMixConsoleIntent arMixConsoleAck processProductionIntent',
  cxxFixture:'PuzzleV8Wire::fixture(snapshot,ack)',
  gui:'arMixConsoleAck arMixConsoleSnapshot arMixConsoleIntent baseGraphRevision baseRosterRevision enabledInputs enabledOutputs productionCallbackLive===true supportedOps',
  guiTest:'console-v8-native-unready.json '+ 'a'.repeat(64),
  portableGolden:'fixture-only',
  nativeRunner:'assertExplicitS0Exporter puzzle-seam.mjs',
  nativeProof:'runActualNativeS0 actual.equals(expected)',
  nativeSource:'bool process(',
  fat:'enabledInputs enabledOutputs',
  fatTest:'integration/console-v6/fixtures.json',
  machine:'"schemaVersion" "requiredSnapshotFields" "intentRequired" "ackRequired"'
 };
 const got=inspectPuzzleSources(src,'a'.repeat(64));
 assert.equal(got.portableFixtureMatch,true);
 assert.equal(got.stage,'S0_NATIVE_PRODUCER_BLOCKED');
 assert.equal(got.productCertified,false);
 assert.equal(got.actualCallbackPCMVerified,false);
 assert.ok(got.releaseSourceBlockers.includes('S0_EXPLICIT_JUCE_EXPORTER'));
 assert.ok(got.releaseSourceBlockers.includes('G2_FULL_UNIVERSAL_CALLBACK'));
 const real=structuredClone(src);
 real.nativeTest='if(argc==2 && std::string(argv[1])=="--emit-console-s0-json"){'+
   'std::cout<<armix::console::LiveConsoleBridgeV6::snapshot(engine.status());}';
 const after=inspectPuzzleSources(real,'a'.repeat(64));
 assert.equal(after.stage,'S1_OR_G0_ONLY_UNIVERSAL_BLOCKED');
 assert.ok(after.releaseSourceBlockers.includes('G2_FULL_UNIVERSAL_CALLBACK'));
 const fake=structuredClone(src);
 fake.nativeTest='if(argc==2 && std::string(argv[1])=="--emit-console-s0-json"){'+
   'std::cout<<armix::console::PuzzleV8Wire::fixture(s,ack);}';
 const pretend=inspectPuzzleSources(fake,'a'.repeat(64));
 assert.equal(pretend.stage,'S0_NATIVE_PRODUCER_BLOCKED');
 assert.ok(pretend.releaseSourceBlockers.includes('S0_EXPLICIT_JUCE_EXPORTER'));

 const bad=structuredClone(src);bad.guiTest='fake.json';
 assert.equal(inspectPuzzleSources(bad,'a'.repeat(64)).portableFixtureMatch,false);
});

// PR #278 visual authority is separate from real JUCE PCM/ACK qualification.
import {inspectPr278} from '../scripts/check_pr278_console_authority.mjs';
test('PR278 source contract blocks divergent universal chrome/identity/MAIN',()=>{
 const reference='<div id="tracks"></div><div id="master"></div><aside id="pluginBrowser"></aside>';
 const js="root.id='arV6Console' main.id='arV6Main'"+
  "selectField('IN','SET_SOURCE' selectField('OUT','SET_PRIMARY_OUT' "+
  "fxSection(ch) sendSection(ch) arMixConsoleAck arMixConsoleSnapshot "+
  "supportedOps productionCallbackLive===true paintPan( --v6-pan-start 'SET_CUE' "+
  "button('LINK FADERS' button('ROUTE TO GROUP BUS' arV6PluginPanel "+
  "arV6QuickPanel Search native scanned plugins";
 const css='#app{--am-metal-panel:#14232C}#app .arV6Strip{background:#123}'+
  '#app .arV6Main{width:178px}#app .arV6Top{background:#456}'+
  '#app .arV6StripHead{color:white}#app .arV6StripStatus{color:white}';
 const r=inspectPr278({reference,js,css,legacy:'channelIcons= channelPalette=',
  test:'console-v8-native-unready.json arMixConsoleAck'});
 for(const id of ['TOP_BOTTOM_COLOR_ICON','ONE_GRAPHITE_CHASSIS',
  'BTN180_HEADER','PINNED_MAIN_EQUAL_WIDTH'])
  assert.ok(r.blocked.some(c=>c.id===id),id+' must not pass');
 assert.equal(r.audioCertified,false);
 for(const id of ['REFERENCE_278','DIRECT_NATIVE_IO','LINK_VS_GROUP','NATIVE_ACK_NO_DEMO'])
  assert.ok(r.checks.some(c=>c.id===id&&c.pass),id+' valid path lost');
 assert.ok(inspectPr278({js,css}).blocked.some(c=>c.id==='REFERENCE_278'));
});
