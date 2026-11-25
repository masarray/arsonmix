/* SPDX-License-Identifier: AGPL-3.0-only */
const assert=require('node:assert/strict');
const {createModel,defaults,observationOf}=require(process.argv[2]||'../../site/preview-v2/assets/mixer-native-de-ezzer.js');
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
const o={inputDb:[-60,-35,-12,-20,-41],wetDb:[-61,-37,-20,-25,-42],essDb:-20,sidechainDb:-16,deltaDb:-24};
assert.equal(observationOf({observation:o}),o);
assert.equal(observationOf({observation:{...o,inputDb:[1,2,3]}}),null);
assert.equal(observationOf({observation:{...o,essDb:NaN}}),null);
assert.equal(observationOf({observation:{...o,wetDb:[0,0,0,0,121]}}),null);
assert.equal(observationOf({}),null);
console.log('PASS: measured observation schema and unavailable-data handling');

// Compare against the actual approved mixer source, not a second guessed timing curve.
const {meterSteps,advanceMeterPeak,meterMarkerRow}=require(process.argv[2]||'../../site/preview-v2/assets/mixer-native-de-ezzer.js');
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const mixer=fs.readFileSync(path.join(__dirname,'../../site/preview-v2/index.html'),'utf8');
const start=mixer.indexOf('const METER_STEP_PX='),end=mixer.indexOf('/* Safe read-only calibration probe',start);
assert(start>=0&&end>start);
const sandbox={};
vm.runInNewContext('function meterDbfsPercent(v){return Math.max(0,Math.min(100,(v+60)/60*100));}\n'+mixer.slice(start,end)+'\nthis.paint=displayMeterDbfs;',sandbox);
const styles={},marker={style:{setProperty:(k,v)=>styles[k]=v}},lane={clientHeight:126,querySelector:()=>marker},fill={parentElement:lane,style:{setProperty:(k,v)=>styles[k]=v}};
let peak=null;
for(const [ts,db] of [[0,-60],[20,-6],[50,-42],[869,-42],[870,-42],[989,-42],[990,-42],[1110,-42],[1600,-42],[1700,-3],[1800,-60],[2550,-60],[2700,-60],[4000,-60],[4100,6]]){
 const steps=meterSteps(db,-60,0,9);peak=advanceMeterPeak(peak,steps,ts);sandbox.paint(fill,db,ts,'test');
 assert(Math.abs(parseFloat(styles['--lit'])-steps/9*100)<1e-9);
 assert.equal(marker.style.opacity,peak.steps>0?'1':'0');
 assert.equal(parseFloat(styles['--peak-extent']),(meterMarkerRow(peak.steps,9)+1)*14-2);
}
assert.equal(meterSteps(NaN,-72,0,9),0);
assert.equal(meterSteps(-100,-72,0,9),0);
assert.equal(meterSteps(6,-72,0,9),9);
assert.equal(meterSteps(7.49,0,18,18),7);
assert.equal(meterSteps(7.5,0,18,18),8);
console.log('PASS: exact mixer peak-hold/release trace, stepped LCD normalization and bounds');

// Bounded chronological history: no fake samples from drag or repeat frames.
const {createMeterHistory}=require(process.argv[2]||'../../site/preview-v2/assets/mixer-native-de-ezzer.js');
const hist=createMeterHistory(4),steps=()=>Array.from({length:4},(_,i)=>hist.stepAt(i));
assert.deepEqual(steps(),[0,0,0,0]);
assert(hist.update(id,'0',-72));assert(hist.update(id,'1',-36));
assert.deepEqual(steps(),[0,0,0,3]);
assert(!hist.update(id,'1',0));assert.deepEqual(steps(),[0,0,0,3]);
assert(hist.update(id,'2',0));assert(hist.update(id,'3',-60));assert(hist.update(id,'4',-12));
assert.deepEqual(steps(),[3,5,1,4]);
assert(hist.update({...id,epoch:'2'},'4',-48));assert.deepEqual(steps(),[0,0,0,2]);
assert(hist.update(null,null,undefined));assert.deepEqual(steps(),[0,0,0,0]);
assert(!hist.update(null,null,undefined));
console.log('PASS: bounded history order, duplicate-frame dedupe, epoch fence and unavailable reset');
