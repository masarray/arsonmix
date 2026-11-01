// SPDX-License-Identifier: AGPL-3.0-only
// SYNTHETIC validator self-tests; these cannot qualify native callback or hardware.
import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {createHash} from 'node:crypto';
import {checkTrace,checkPcm,checkRelease,TRACE_SCHEMA} from '../scripts/console_v6_fat_gate.mjs';
const copy=x=>structuredClone(x);
const snap=n=>({type:'snapshot',device:{epoch:'e1',outputArmed:false},graph:{revision:n,capabilities:{groups:false,fxReturns:false,monitor:true},channels:[{id:'7',role:'INPUT'},{id:'2',role:'INPUT'},{id:'100',role:'MAIN'}]}});
const trace=()=>({schema:TRACE_SCHEMA,events:[snap(2),
 {type:'intent',requestId:'r1',baseRevision:2,deviceEpoch:'e1',op:'SET_FADER',targetChannelId:'7'},
 {type:'ack',requestId:'r1',status:'ACCEPTED',newRevision:3},snap(3),
 {type:'intent',requestId:'r2',baseRevision:2,deviceEpoch:'e1',op:'SET_SOURCE',targetChannelId:'7'},
 {type:'ack',requestId:'r2',status:'REJECTED',newRevision:3,errorCode:'STALE_REVISION'},snap(3)]});
const dry=[0.2,-0.1,0.4,-0.3],wet=[0.01,0.03,0.02,-0.01],grp=dry.map(x=>x*0.5),zero=a=>a.map(()=>0);
const pcm=()=>({schema:'arsonmix.console-v6.pcm-evidence.v1',cases:[
 {kind:'independent_inputs',samples:{input1:[0.1,-0.1,0.1,-0.1],input2:[0.05,0.15,-0.05,-0.15],ch7:[0.1,-0.1,0.1,-0.1],ch2:[0.05,0.15,-0.05,-0.15]}},
 {kind:'group_fx_return',groupGain:0.5,samples:{dry,group:grp,wetReturn:wet,main:grp.map((x,i)=>x+wet[i]),groupMutedMain:wet,returnMutedMain:grp}},
 {kind:'monitor_isolation',mainDestination:'hp0/1',monitorDestination:'hp2/3',samples:{main:dry,monitor:wet,monitorMuted:zero(wet),mainAfterMonitorMute:dry}},
 {kind:'physical_mute',samples:{mainBeforeMute:dry,mainAfterMute:zero(dry)}}
]});
test('serial native ACK requires next full accepted snapshot; rejects rollback',()=>{
 assert.equal(checkTrace(trace()).acceptedCommands,1);
 const a=trace();a.events.splice(3,1);assert.throws(()=>checkTrace(a),/full snapshot/);
 const b=trace();b.events[6].graph.channels[0].id='50';assert.throws(()=>checkTrace(b),/rejected command changed/);
});
test('reject stale revision, missing native capability and numeric ChannelId',()=>{
 const a=trace();a.events[1].deviceEpoch='old';assert.throws(()=>checkTrace(a),/stale revision\/epoch/);
 const b=trace();b.events[0].graph.channels.push({id:'4',role:'GROUP'});assert.throws(()=>checkTrace(b),/GROUP without native capability/);
 const c=trace();c.events[0].graph.channels[0].id=42;assert.throws(()=>checkTrace(c),/decimal uint64 string/);
});
test('duplicate request IDs do not reapply same mutation',()=>{
 const a=trace();a.events[4].requestId='r1';a.events[5].requestId='r1';assert.throws(()=>checkTrace(a),/duplicate requestId/);
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
test('exact SHA Windows+Chromium CI, EXE hash, manual 0FX 30-minute gate',()=>{
 const dir=mkdtempSync(join(tmpdir(),'arsonmix-fat-'));
 try{
 const exe=Buffer.from('SYNTHETIC EXE HASH FIXTURE');const h=createHash('sha256').update(exe).digest('hex'),sha='a'.repeat(40);
 writeFileSync(join(dir,'test.exe'),exe);
 writeFileSync(join(dir,'BUILD_PROVENANCE.json'),JSON.stringify({sourceCommit:sha,executable:'test.exe',exeSha256:h,fatStatus:'EXPERIMENTAL_NOT_HARDWARE_QUALIFIED',peMachine:'AMD64 (0x8664)',peSubsystem:'WINDOWS_GUI (2)',webView2LoaderMode:'JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1'}));
 const r={sourceSha:sha,nativeCI:{sourceSha:sha,conclusion:'success',url:'https://github.com/masarray/arsonmix/actions/runs/123'},chromiumCI:{sourceSha:sha,conclusion:'success',url:'https://github.com/masarray/arsonmix/actions/runs/124'},exeSha256:h,artifactSha256:h,traceSha256:h,pcmSha256:h,nativeCTest:'PASS',chromiumPlaywright:'PASS',windowsVerifier:'PASS',hardware:{issue:15,status:'PENDING_HUMAN_REVIEW',wired:true,backend:'ASIO',sampleRate:48000,bufferFrames:480,inputPins:[0],outputPins:[0,1],testDurationSeconds:1800,outputOnlyTone:'RECORDED',captureOnly:'RECORDED',dry0Fx:'RECORDED',fullReportSha256:h,verifiedHumanWitness:true,witnessId:'operator1'}};
 assert.equal(checkRelease(r,dir).certified,false);
 const stale=copy(r);stale.chromiumCI.sourceSha='b'.repeat(40);assert.throws(()=>checkRelease(stale,dir),/Chromium CI/);
 const tampered=copy(r);tampered.exeSha256='f'.repeat(64);assert.throws(()=>checkRelease(tampered,dir),/EXE SHA mismatch/);
 const short=copy(r);short.hardware.testDurationSeconds=1799;assert.throws(()=>checkRelease(short,dir),/30min/);
 const noOperator=copy(r);noOperator.hardware.verifiedHumanWitness=false;assert.throws(()=>checkRelease(noOperator,dir),/human witness/);
 }finally{rmSync(dir,{recursive:true,force:true})}
});
