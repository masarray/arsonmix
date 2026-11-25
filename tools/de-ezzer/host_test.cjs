/* SPDX-License-Identifier: AGPL-3.0-only */
const assert=require('node:assert/strict');
const {contextFor,createTransport}=require('../../site/preview-v2/assets/mixer-native-de-ezzer-host.js');
const {createModel,defaults}=require('../../site/preview-v2/assets/mixer-native-de-ezzer.js');
const id={instance:'9',epoch:'3'},s=(n='0',frames='0')=>({id,appliedSequence:n,frames,settings:defaults,audition:'Normal',gainReductionDb:0,lowerWeight:.5});
const view={admitted:true,device:{epoch:'9007199254740993'},graph:{revision:'11',channels:[{id:'42',inserts:[{id:'native-abc',catalogId:'armix.builtin.de-ezzer.v1'}]}]}};
const c=contextFor(view,'42',0);assert(c);assert(!contextFor({...view,logical:true},'42',0));assert(!contextFor(view,'43',0));
let ctx=c,time=0,events=[],faults=0;
const backend={emitEvent:(name,p)=>events.push({name,p})};
let model;const t=createTransport({backend,context:()=>ctx,initialContext:c,token:'dez-test',now:()=>time,onInitial:v=>model.attach(v),onFault:()=>faults++});
model=createModel({send:t.send,now:()=>time});t.subscribe({ack:model.ack,telemetry:model.ingest,disconnect:model.disconnect});t.tick();let req=events.at(-1).p;assert.equal(req.action,'nativeDeEzzerSnapshot');assert.equal(req.deviceEpoch,'9007199254740993');assert(!t.receive('snapshot',{...req,instanceId:'other',ok:true,snapshot:s()}));assert(t.receive('snapshot',{...req,ok:true,snapshot:s()}));assert(model.view().editable);
assert(model.request({amount:.7}));req=events.at(-1).p;assert.equal(req.action,'nativeDeEzzerSet');assert.equal(req.command.audition,'Normal');assert(t.receive('queue',{...req,ok:true}));assert(model.view().pending);assert(t.receive('ack',{...req,ok:true,ack:{requested:id,sequence:'1',code:'Applied',actual:{...s('1','64'),settings:{...defaults,amount:.7}}}}));assert.equal(model.view().actual.settings.amount,.7);
time=900;t.tick();req=events.at(-1).p;assert(t.receive('snapshot',{...req,ok:true,snapshot:{...s('1','64'),settings:{...defaults,amount:.7}}}));time=1001;model.tick();assert(!model.view().editable); // duplicate frame did not refresh freshness
ctx={...c,graphRevision:'12'};t.tick();assert.equal(faults,1);assert.equal(model.view().actual,null);
console.log('PASS: exact native lease envelope, correlated read/queue/ACK, graph fencing, no duplicate-frame freshness');

let cueEvents=[],cueStates=[];ctx=c;
const cueTransport=createTransport({backend:{emitEvent:(name,p)=>cueEvents.push(p)},context:()=>ctx,initialContext:c,token:'dez-12345678-1234-1234-1234-123456789abc',now:()=>time,onInitial:()=>{}});
cueTransport.subscribe({cue:x=>cueStates.push(x)});
cueTransport.tick();let cq=cueEvents.at(-1);
assert.equal(cq.editorToken,'dez-12345678-1234-1234-1234-123456789abc');
assert(cueTransport.receive('snapshot',{...cq,ok:true,snapshot:{...s(),cue:{available:true,requested:'Mix',applied:'Mix'}}}));
assert(cueTransport.setCue('Ess'));cq=cueEvents.at(-1);assert.equal(cq.action,'nativeDeEzzerCue');assert.equal(cq.mode,'Ess');
assert(!cueTransport.receive('cue',{...cq,requestId:'old',ok:true,cue:{available:true,requested:'Ess',applied:'Ess'}}));
assert(cueTransport.receive('cue',{...cq,ok:true,cue:{available:true,requested:'Ess',applied:'Mix'}}));
assert.equal(cueStates.at(-1).applied,'Mix');
cueTransport.tick();cq=cueEvents.at(-1);
assert(cueTransport.receive('snapshot',{...cq,ok:true,snapshot:{...s('0','64'),cue:{available:true,requested:'Ess',applied:'Ess'}}}));
assert.equal(cueStates.at(-1).applied,'Ess');
cueTransport.cancelAudition();assert.equal(cueEvents.at(-1).mode,'Mix');
ctx={...c,graphRevision:'12'};assert(!cueTransport.setCue('Delta'));
console.log('PASS: separate private cue request, correlated applied status, explicit cancellation, stale graph rejection');
