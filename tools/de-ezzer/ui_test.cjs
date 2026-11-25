/* SPDX-License-Identifier: AGPL-3.0-only */
const assert=require('node:assert/strict');
const {createModel,defaults}=require(process.argv[2]||'../../site/preview-v2/assets/mixer-native-de-ezzer.js');
const id={instance:'9007199254740993',epoch:'1'};
const snap=(n='0',frames='0',settings=defaults,extra={})=>({id,appliedSequence:n,frames,settings,audition:'Normal',gainReductionDb:0,lowerWeight:.5,...extra});
let time=0,commands=[];const make=(monitorSafe=false,send=c=>commands.push(c))=>createModel({send,now:()=>time,monitorSafe});
const m=make();assert(m.attach(snap('9007199254740993')));assert(!m.request({},'Delta'));assert(!m.request({amount:2}));assert(m.request({amount:.7}));assert.equal(commands.at(-1).sequence,'9007199254740994');assert.equal(m.view().actual.settings.amount,.5);assert(!m.request({amount:.9}));
const a={requested:id,sequence:commands.at(-1).sequence,code:'Applied',actual:snap(commands.at(-1).sequence,'64',{...defaults,amount:.7})};
assert(!m.ingest(a.actual));assert(!m.ack({...a,requested:{...id,epoch:'2'}}));assert(m.ack(a));assert.equal(m.view().actual.settings.amount,.7);assert(!m.ingest(snap('0','128')));assert(m.ingest(snap(a.sequence,'128',a.actual.settings)));assert(!m.ingest(snap(a.sequence,'64',a.actual.settings)));time=1001;m.tick();assert(!m.view().editable);assert.equal(m.view().phase,'Readback stale · reopen');assert(!m.attach(a.actual));assert(m.attach(snap('0','0',defaults,{id:{...id,epoch:'2'}})));assert(!m.ack(a));
const timeout=make();assert(timeout.attach(snap()));assert(timeout.request({amount:.6}));time+=1501;timeout.tick();assert.equal(timeout.view().phase,'ACK timeout · reopen');assert(!timeout.ack({...a,sequence:'1',actual:snap('1')}));
const full=make(false,()=>false);assert(full.attach(snap()));assert(!full.request({enabled:false}));assert(!full.view().editable);assert.equal(full.view().actual.settings.enabled,true);
const monitor=make(true);assert(monitor.attach(snap()));assert(monitor.request({},'Detect'));assert(monitor.ack({requested:id,sequence:'1',code:'Applied',actual:snap('1','64',defaults,{audition:'Detect'})}));assert(monitor.ingest(snap('1','144064')));assert.equal(monitor.view().actual.audition,'Normal');assert(monitor.request({enabled:false}));assert(monitor.ack({requested:id,sequence:'2',code:'Invalid',actual:snap('1','144064')}));assert(!monitor.view().editable);monitor.disconnect();assert.equal(monitor.view().actual,null);
const exhausted=make();assert(exhausted.attach(snap('18446744073709551615')));assert(!exhausted.request({amount:.9}));assert(!make().attach(snap('18446744073709551616')));
console.log('PASS: uint64 precision, authoritative ACK, sequence/epoch, stale/timeout, queue refusal, monitor safety, lease telemetry, rejection and disconnect');
