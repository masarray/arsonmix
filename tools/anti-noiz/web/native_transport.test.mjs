// SPDX-License-Identifier: AGPL-3.0-only
import assert from 'node:assert/strict';
import {nativeTransport,validSpectrum} from './native_transport.mjs';
import {controller,defaults} from './editor.mjs';
const listeners=new Map(),sent=[];
const backend={addEventListener(name,fn){listeners.set(name,fn);return name;},
 removeEventListener(name){listeners.delete(name);},emitEvent(_,m){sent.push(m);}};
let now=1000,ctx={channelId:'9007199254740993',deviceEpoch:'8',graphRevision:'12',instanceId:'g5c-92',slot:1};
const tx=nativeTransport(backend,()=>ctx,()=>now);let view;
const model=controller(tx,v=>view=v);
const emit=(event,request,extra)=>listeners.get(event)?.({...request,...extra});
let req=sent.at(-1);
const obs={serial:'1',profile:true,learning:false,learnStatus:'Accepted',rackBypassed:false,effectiveMode:'Voice',latency:256,spectralGain:.5,residualGain:1};
emit('arMixBuiltinAntiNoizSnapshot',req,{status:'ACCEPTED',settings:defaults,observation:obs,instanceId:'g5c-foreign'});
assert.equal(view.accepted,null);
emit('arMixBuiltinAntiNoizSnapshot',req,{status:'ACCEPTED',settings:defaults,observation:obs});
assert.equal(view.accepted.cleanDb,14);assert(view.observation.live);
assert(model.submit('settings',{cleanDb:8}));req=sent.at(-1);
assert.equal(req.action,'nativeAntiNoizSet');assert.equal(view.accepted.cleanDb,14);
emit('arMixBuiltinAntiNoizQueue',req,{status:'QUEUED',settings:req.settings});assert(view.pending);
emit('arMixBuiltinAntiNoizAck',req,{status:'APPLIED',settings:req.settings,graphRevision:'11'});assert(view.pending);
emit('arMixBuiltinAntiNoizAck',req,{status:'APPLIED',settings:req.settings});assert.equal(view.accepted.cleanDb,8);assert(!view.pending);
req=sent.at(-1);now+=800;
emit('arMixBuiltinAntiNoizSnapshot',req,{status:'ACCEPTED',settings:{...defaults,cleanDb:8},observation:obs});assert(!view.observation.live);
assert(model.submit('learn'));req=sent.at(-1);assert.equal(req.action,'nativeAntiNoizLearn');
emit('arMixBuiltinAntiNoizQueue',req,{status:'REJECTED',settings:{...defaults,cleanDb:8},errorCode:'RACK_BYPASS'});assert(!view.pending);assert(view.error);
ctx={...ctx,instanceId:'g5c-93'};model.refresh();
req=sent.at(-1);emit('arMixBuiltinAntiNoizSnapshot',req,{status:'ACCEPTED',settings:defaults,observation:{...obs,serial:'2'}});
assert.equal(view.accepted.cleanDb,14);
assert(model.submit('settings',{cleanDb:6}));ctx=null;model.refresh();assert.equal(view.accepted,null);assert(!view.pending);
ctx={channelId:'9007199254740993',deviceEpoch:'8',graphRevision:'12',instanceId:'g5c-93',slot:1};
model.refresh();req=sent.at(-1);
emit('arMixBuiltinAntiNoizSnapshot',req,{status:'ACCEPTED',settings:defaults,observation:obs});
assert(view.accepted);
now+=1600;model.refresh();assert.equal(view.accepted,null);
model.dispose();assert.equal(listeners.size,0);console.log('ANTI_NOIZ_NATIVE_TRANSPORT identity/ACK/telemetry/stale/close PASS');

// Actual native transport reentrant ACK sends only the latest queued patch.
const listeners2=new Map(),sent2=[];const backend2={addEventListener(n,f){listeners2.set(n,f);return n;},removeEventListener(n){listeners2.delete(n);},emitEvent(_,m){sent2.push(m);}};
const target={channelId:'c',deviceEpoch:'8',graphRevision:'12',instanceId:'i',slot:1};let latest;
const model2=controller(nativeTransport(backend2,()=>target,()=>1000),v=>latest=v);
const emit2=(event,request,extra)=>listeners2.get(event)?.({...request,...extra});
emit2('arMixBuiltinAntiNoizSnapshot',sent2.at(-1),{status:'ACCEPTED',settings:defaults,observation:obs});
model2.adjust('cleanDb',16);const first=sent2.at(-1);model2.adjust('cleanDb',19);model2.adjust('silenceDb',12);assert.equal(sent2.filter(x=>x.action==='nativeAntiNoizSet').length,1);
emit2('arMixBuiltinAntiNoizAck',first,{status:'APPLIED',settings:first.settings});
const writes=sent2.filter(x=>x.action==='nativeAntiNoizSet');assert.equal(writes.length,2);assert.equal(writes[1].settings.cleanDb,19);assert.equal(writes[1].settings.silenceDb,12);assert(latest.pending);
emit2('arMixBuiltinAntiNoizAck',writes[1],{status:'APPLIED',settings:writes[1].settings});assert.equal(latest.accepted.cleanDb,19);assert(!latest.pending);
assert(model2.submit('clear'));const clear=sent2.at(-1);assert.equal(clear.action,'nativeAntiNoizClear');emit2('arMixBuiltinAntiNoizAck',clear,{status:'APPLIED',settings:latest.accepted});assert(!latest.pending);
assert(model2.submit('audition'));const audition=sent2.at(-1);assert.equal(audition.action,'nativeAntiNoizAudition');emit2('arMixBuiltinAntiNoizAck',audition,{status:'REJECTED',settings:latest.accepted,errorCode:'NO_PROFILE'});assert(latest.error);model2.dispose();console.log('ANTI_NOIZ_NATIVE_CONTINUOUS_ACK PASS');

// Spectrum extension is optional, bounded and rejects malformed data.
const spec={bins:129,binHz:187.5,candidate:true,progress:.4,input:Array(129).fill(-45),noise:Array(129).fill(-65)};
assert.equal(validSpectrum(spec,256),spec);assert.equal(validSpectrum({...spec,input:[0]},256),null);assert.equal(validSpectrum({...spec,noise:Array(129).fill(NaN)},256),null);assert.equal(validSpectrum({...spec,binHz:300},256),null);assert.equal(validSpectrum(spec,512),null);assert.equal(validSpectrum({...spec,input:[],noise:[]},256).bins,129);console.log('ANTI_NOIZ_SPECTRUM_SCHEMA PASS');
