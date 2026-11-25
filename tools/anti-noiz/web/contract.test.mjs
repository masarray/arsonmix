// SPDX-License-Identifier: AGPL-3.0-only
import assert from 'node:assert/strict';
import {controller,defaults} from './editor.mjs';
let listener,view,sent;const t={subscribe(f){listener=f;return()=>{};},read(){listener({kind:'snapshot',settings:defaults});},send(c){sent=c;}};
const m=controller(t,v=>view=v);assert.equal(view.accepted.cleanDb,14);assert(!m.submit('settings',{cleanDb:99}));assert(m.submit('settings',{cleanDb:8}));assert(view.pending);assert.equal(view.accepted.cleanDb,14);assert(!m.submit('settings',{cleanDb:3}));listener({kind:'ack',id:sent.id+1,applied:true,settings:sent.settings});assert(view.pending);listener({kind:'ack',id:sent.id,applied:true,settings:sent.settings});assert.equal(view.accepted.cleanDb,8);assert(!view.pending);assert(m.submit('learn'));listener({kind:'ack',id:sent.id,applied:false,settings:view.accepted});assert(view.error);m.dispose();assert(!m.submit('learn'));console.log('Anti Noiz Web ACK contract PASS');

// Continuous adjustment is single flight; queued latest intent survives stale snapshots.
let l2,v2;const writes=[];const t2={subscribe(f){l2=f;return()=>{};},read(){l2({kind:'snapshot',settings:defaults});},send(c){writes.push(c);}};
const m2=controller(t2,v=>v2=v);
assert(m2.adjust('cleanDb',18));assert(m2.adjust('cleanDb',20));assert(m2.adjust('cleanDb',22));assert.equal(writes.length,1);assert.equal(v2.desired.cleanDb,22);assert.equal(v2.accepted.cleanDb,14);
l2({kind:'snapshot',settings:defaults});assert.equal(v2.desired.cleanDb,22);assert(!m2.submit('learn'));
l2({kind:'ack',id:writes[0].id,applied:true,settings:writes[0].settings});assert.equal(writes.length,2);assert.equal(writes[1].settings.cleanDb,22);assert.equal(v2.desired.cleanDb,22);
l2({kind:'ack',id:writes[1].id,applied:true,settings:writes[1].settings});assert.equal(v2.accepted.cleanDb,22);assert(!v2.pending);
assert(m2.adjust('cleanDb',10));assert(m2.adjust('cleanDb',9));l2({kind:'ack',id:writes[2].id,applied:false,settings:v2.accepted,error:'Rejected'});assert.equal(writes.length,3);assert.equal(v2.desired,null);assert.equal(v2.accepted.cleanDb,22);
assert(m2.adjust('cleanDb',12));assert(m2.adjust('cleanDb',13));l2({kind:'unavailable'});assert.equal(v2.accepted,null);assert.equal(v2.desired,null);assert.equal(writes.length,4);m2.dispose();console.log('Anti Noiz continuous single-flight/coalescing/reject/identity PASS');
