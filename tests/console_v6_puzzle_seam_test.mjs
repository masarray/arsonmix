// SPDX-License-Identifier: AGPL-3.0-only
// Cheap puzzle-socket gate. Synthetic fixtures are NOT engine audio proof.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {dirname,resolve} from 'node:path';
import {snapshot,intent,ack,uint64,requestToken} from '../integration/console-v6/puzzle-seam.mjs';
const root=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const data=JSON.parse(readFileSync(resolve(root,'integration/console-v6/fixtures.json'),'utf8'));
const copy=x=>structuredClone(x),bad=(fn,re)=>assert.throws(fn,re);
const schema=JSON.parse(readFileSync(resolve(root,'contracts/console-puzzle-v8.json'),'utf8'));
test('synthetic G0/G2 fixtures obey every machine-required inventory key',()=>{
 for(const [stage,s] of [['G0',data.g0],['G2',data.g2]]){
  for(const [section,keys] of Object.entries(schema.requiredSnapshotFields)){
   const obj=section==='root'?s:section==='capabilities'?s.graph.capabilities:s[section];
   for(const key of keys)
    assert.ok(Object.hasOwn(obj,key),stage+' missing '+section+'.'+key);
  }
  assert.doesNotThrow(()=>snapshot(s));
 }
 const absent=copy(data.g2);delete absent.telemetry.rawCapturePeak;
 bad(()=>snapshot(absent),/finite nonnegative peak/);
 const invalid=copy(data.g2);invalid.telemetry.rawCapturePeak=-0.01;
 bad(()=>snapshot(invalid),/finite nonnegative peak/);
});

test('G0 native without universal callback is honest and carries physical pin inventory',()=>{
 assert.equal(data.provenance,'SYNTHETIC_CONTRACT_ONLY_NOT_HARDWARE');
 assert.doesNotThrow(()=>snapshot(data.g0));
 assert.equal(data.g0.productionCallbackLive,false);
 assert.deepEqual(data.g0.graph.capabilities.supportedOps,[]);
});
test('G2 fixture uses actual stable numeric IDs, group serial and wet parallel topology',()=>{
 assert.doesNotThrow(()=>snapshot(data.g2));
 const find=id=>data.g2.graph.channels.find(ch=>ch.id===id);
 assert.equal(find('7').primaryOutId,'100');
 assert.equal(find('2').primaryOutId,'100');
 assert.equal(find('100').primaryOutId,'200');
 assert.equal(find('101').primaryOutId,'200');
 assert.equal(find('7').sends[0].targetId,'101');
 assert.equal(find('7').source.physicalPins[0],0);
 assert.equal(find('2').source.physicalPins[0],1);
});
test('native uint64 and unique editor request tokens cannot be approximated by JS Number',()=>{
 assert.equal(uint64('18446744073709551615','id'),(1n<<64n)-1n);
 for(const x of ['MAIN','BUS-FX','ch-07','07','18446744073709551616',7])
   bad(()=>uint64(x,'ChannelId'),/decimal-string|overflow/);
 assert.equal(requestToken(data.intent.requestId),data.intent.requestId);
 for(const x of ['1','v6','editor:0','editor:1\nmalicious'])
   bad(()=>requestToken(x),/unique-session/);
});
test('one-pin hardware cannot claim stereo from IN1/IN1 or invented IN2',()=>{
 const mono=copy(data.g2);
 mono.device.enabledInputs=[0];
 mono.device.inputEndpoints=[mono.device.inputEndpoints[0]];
 const ch2=mono.graph.channels.find(c=>c.id==='2');
 ch2.source={portId:'in:0',physicalPins:[0]};
 assert.doesNotThrow(()=>snapshot(mono));
 ch2.source={portId:'in:0',physicalPins:[0,0]};
 bad(()=>snapshot(mono),/duplicate/);
 ch2.source={portId:'in:1',physicalPins:[1]};
 bad(()=>snapshot(mono),/absent from driver mask/);
});
test('canonical operator intent + ACK require a refreshed matching native accepted snapshot',()=>{
 snapshot(data.g2);
 intent(data.intent,data.g2);
 const next=copy(data.g2);
 next.graph.revision='6';
 next.graph.channels.find(c=>c.id==='7').source={portId:'in:1',physicalPins:[1]};
 assert.doesNotThrow(()=>ack(data.ack,data.intent,data.g2,next));
 bad(()=>ack(data.ack,data.intent,data.g2,null),/NEW native snapshot/);
 bad(()=>ack({...data.ack,requestId:'other-editor:3'},data.intent,data.g2,next),/operator action/);
 const stale=copy(data.intent);stale.baseGraphRevision='4';
 bad(()=>intent(stale,data.g2),/stale accepted graph/);
 const denial={...data.ack,status:'REJECTED',errorCode:'INVALID_INPUT'};
 assert.doesNotThrow(()=>ack(denial,data.intent,data.g2,data.g2));
 bad(()=>ack(denial,data.intent,data.g2,next),/rejected request replaced/);
});
test('schema rejects fake LIVE, partial inventory, noncanonical and unknown roles',()=>{
 const cases=[
  [s=>{delete s.device.enabledInputs},/array required/],
  [s=>{s.graph.channels[0].id='MAIN'},/decimal-string/],
  [s=>{s.graph.channels[0].primaryOutId='NO_BUS'},/decimal-string/],
  [s=>{s.graph.channels[0].role='FX_RET'},/unrecognized role/],
  [s=>{s.graph.capabilities.fullVendorFxMigration=false},/cannot mark LIVE/],
  [s=>{s.device.outputArmed='true'},/boolean required/]
 ];
 for(const [edit,exp] of cases){const s=copy(data.g2);edit(s);bad(()=>snapshot(s),exp);}
});
