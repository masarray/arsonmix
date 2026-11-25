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
