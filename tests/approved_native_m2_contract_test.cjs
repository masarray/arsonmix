// SPDX-License-Identifier: AGPL-3.0-only
// M2 production handshake has no speculative/optimistic graph promotion.
// Same JS used by the ONLY /preview/ WebView2 native resource provider.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {readFileSync}=require('node:fs');
const {createController,validateSnapshot,uint64}=require('../web/approved-native-m2-state.js');
const read=path=>readFileSync(path,'utf8');
const fullCaps=()=>({
 callbackOwnsChannelIds:true,fullVendorFxMigration:true,
 showAndOutputParity:true,physicalCaptureMapped:true,
 dynamicChannels:true,groups:true,fxReturns:true,monitor:true,
 sendsOnFaders:true,supportedOps:['SET_FADER','SET_SOURCE']
});
const status=(epoch='69',revision='5',live=true)=>({
 schemaVersion:6,productionCallbackLive:live,
 device:{epoch,backend:'ASIO',captureDevice:'real',playbackDevice:'real',
   inputEndpoints:[{id:'in:4',port:4,callback:0,label:'verified four',active:true}],
   outputEndpoints:[{id:'out:2',port:2,callback:0,label:'verified out',active:true}],
   enabledInputs:[4],enabledOutputs:[2],sampleRate:48000,
   bufferFrames:128,outputArmed:false},
 graph:{revision,rosterRevision:'2',
   channels:[{id:'18446744073709551614',name:'actual native ID',role:'INPUT',
     processingFormat:'MONO',inserts:[],sends:[]}],
   destinations:[],capabilities:fullCaps()},
 telemetry:{rawCapturePresent:false,rawCapturePeak:0,
   mainPcmPresent:false,monitorPcmPresent:false}
});
function session() {
 const events=[],updates=[];
 const ctl=createController({emit:(event,payload)=>events.push({event,payload}),
   session:'native-m2-proof',notify:(status,detail)=>updates.push([status,detail])});
 return {ctl,events,updates};
}
test('uint64 ChannelIds are lossless and JS never compares them as Number',()=>{
 assert.equal(uint64('18446744073709551615'),(1n<<64n)-1n);
 assert.throws(()=>uint64(18446744073709551615),/M2_BAD_UINT64/);
 for(const n of ['01','-1','+2','18446744073709551616','3.3'])
   assert.throws(()=>uint64(n));
});
test('real driver pin mask is mandatory; inactive or imaginary pins fail closed',()=>{
 const good=status(); assert.deepEqual(validateSnapshot(good),good);
 const bad=status();bad.device.inputEndpoints[0].port=7;
 assert.throws(()=>validateSnapshot(bad),/M2_ENDPOINT/);
 const bad2=status();bad2.device.enabledInputs=[4,4];
 assert.throws(()=>validateSnapshot(bad2),/M2_DUPLICATE_PIN/);
 const bad3=status();bad3.device.inputEndpoints[0].id='in:9';
 assert.throws(()=>validateSnapshot(bad3),/M2_ENDPOINT/);
});
test('no producer LIVE parity => snapshot may exist but ALL operator edits are refused',()=>{
 const {ctl,events}=session();
 const g=status('69','0',false); g.graph.channels=[];
 g.graph.destinations=[];g.graph.capabilities.supportedOps=[];
 Object.keys(g.graph.capabilities).forEach(k=>{
   if(k!=='supportedOps')g.graph.capabilities[k]=false;
 });
 assert.equal(ctl.receiveSnapshot(g),true);
 assert.equal(ctl.status(),'M2_NATIVE_READBACK_ONLY');
 assert.equal(ctl.request('SET_FADER',null,{value:-5}),null);
 assert.equal(events.length,0);
});
test('intent emits canonical native request with same accepted base revisions and stable ChannelId',()=>{
 const {ctl,events}=session();ctl.receiveSnapshot(status());
 const id=ctl.request('SET_FADER','18446744073709551614',{db:-5});
 assert.equal(id,'native-m2-proof:1');
 assert.equal(events[0].event,'arMixConsoleIntent');
 assert.deepEqual(events[0].payload,{
  requestId:id,deviceEpoch:'69',baseGraphRevision:'5',baseRosterRevision:'2',
  operation:'SET_FADER',targetChannelId:'18446744073709551614',payload:{db:-5}
 });
 assert.equal(ctl.request('SET_FADER','18446744073709551614',{db:0}),null);
 assert.equal(ctl.status(),'PENDING_NATIVE_ACK');
});
test('matching ACK alone does NOT commit; commit after exact later native snapshot',()=>{
 const {ctl}=session();ctl.receiveSnapshot(status());
 const id=ctl.request('SET_FADER','18446744073709551614',{db:-12});
 assert.equal(ctl.receiveAck({requestId:id,status:'ACCEPTED',deviceEpoch:'69',
   graphRevision:'6',rosterRevision:'2',errorCode:''}),true);
 assert.equal(ctl.status(),'AWAITING_MATCHED_NATIVE_SNAPSHOT');
 assert.equal(ctl.snapshot().graph.revision,'5');
 assert.equal(ctl.receiveSnapshot(status('69','7')),false);
 assert.equal(ctl.snapshot().graph.revision,'5');
 assert.equal(ctl.receiveSnapshot(status('69','6')),true);
 assert.equal(ctl.snapshot().graph.revision,'6');
 assert.equal(ctl.status(),'ACCEPTED_NATIVE_CALLBACK');
});
test('out-of-order snapshot is quarantined until ACK; stale/foreign ACK ignored',()=>{
 const {ctl}=session();ctl.receiveSnapshot(status());
 const id=ctl.request('SET_FADER','18446744073709551614',{db:-6});
 assert.equal(ctl.receiveSnapshot(status('69','6')),false);
 assert.equal(ctl.snapshot().graph.revision,'5');
 assert.equal(ctl.receiveAck({requestId:'other:99',status:'ACCEPTED',deviceEpoch:'69',
   graphRevision:'6',rosterRevision:'2',errorCode:''}),false);
 assert.equal(ctl.receiveAck({requestId:id,status:'ACCEPTED',deviceEpoch:'69',
   graphRevision:'6',rosterRevision:'2',errorCode:''}),true);
 assert.equal(ctl.snapshot().graph.revision,'6');
});
test('REJECTED/stale leaves accepted graph unchanged, never optimistic PCM',()=>{
 const {ctl}=session();ctl.receiveSnapshot(status());
 const id=ctl.request('SET_FADER','18446744073709551614',{db:-6});
 assert.equal(ctl.receiveAck({requestId:id,status:'REJECTED',
   deviceEpoch:'69',graphRevision:'5',rosterRevision:'2',errorCode:'STALE_GRAPH'}),true);
 assert.equal(ctl.snapshot().graph.revision,'5');
 assert.equal(ctl.status(),'NATIVE_REJECTED');
 assert.equal(ctl.pendingRequest(),null);
});
test('device epoch change cancels in-flight edit and accepts new hardware truth',()=>{
 const {ctl}=session();ctl.receiveSnapshot(status());
 const id=ctl.request('SET_FADER','18446744073709551614',{db:-9});
 assert.equal(ctl.receiveSnapshot(status('70','0',false)),true);
 assert.equal(ctl.pendingRequest(),null);
 assert.equal(ctl.snapshot().device.epoch,'70');
 assert.equal(ctl.receiveAck({requestId:id,status:'ACCEPTED',deviceEpoch:'69',
  graphRevision:'6',rosterRevision:'2',errorCode:''}),false);
 assert.equal(ctl.status(),'M2_NATIVE_READBACK_ONLY');
});
test('cannot send source outside actual enabled endpoints or unknown channel ids',()=>{
 const {ctl,events}=session();ctl.receiveSnapshot(status());
 assert.equal(ctl.request('SET_SOURCE','18446744073709551614',{portId:'in:0'}),null);
 assert.equal(ctl.request('SET_FADER','1',{db:-5}),null);
 assert.equal(events.length,0);
});
test('native EXE exposes ONE approved GUI + truthful producer with no JS DSP',()=>{
 const cpp=read('src/Main.cpp'),header=read('src/ApprovedConsoleM2Snapshot.h');
 const adapter=read('web/approved-native-bridge.js');
 const generator=read('scripts/build_approved_native_gui.py');
 assert.match(cpp,/withEventListener \("arMixConsoleSubscribe"/);
 assert.match(cpp,/withEventListener \("arMixConsoleIntent"/);
 assert.match(cpp,/emitApprovedM2Snapshot/);
 assert.match(cpp,/NATIVE_CHANNELID_CALLBACK_NOT_ADMITTED/);
 assert.match(cpp,/withResourceProvider \(assetFor\)/);
 assert.match(header,/enabledInputs/);
 assert.match(header,/enabledOutputs/);
 assert.match(header,/productionCallbackLive",false/);
 assert.match(header,/mainPcmPresent",false/);
 assert.match(header,/callbackOwnsChannelIds/);
 assert.match(adapter,/arMixConsoleSnapshot/);
 assert.match(adapter,/arMixConsoleAck/);
 assert.match(adapter,/arMixConsoleSubscribe/);
 assert.match(adapter,/APPROVED_PAGES_PREVIEW_BUTTONS/);
 assert.match(adapter,/stopImmediatePropagation/); // block fake demo audio edits
 assert.doesNotMatch(adapter,/AudioContext|AudioWorklet|createOscillator|setMasterMuted/);
 assert.match(generator,/approved-native-m2-state\.js/);
 assert.match(generator,/scripts" \/ "build_pages\.py"/);
});
