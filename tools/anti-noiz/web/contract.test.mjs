// SPDX-License-Identifier: AGPL-3.0-only
import assert from 'node:assert/strict';
import {controller,defaults,antiNoizSpectrumPresentation} from './editor.mjs';
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

// Synthetic numeric fixtures exercise presentation, never claim audio evidence.
const graph=antiNoizSpectrumPresentation();
const frame=(input,noise,binHz=187.5)=>({bins:input.length||noise.length||129,binHz,input,noise});
const flat=Array(129).fill(-60),quiet=Array(129).fill(-100),loud=Array(129).fill(-20);
const points=d=>[...d.matchAll(/[ML]([\d.]+) ([\d.]+)/g)].map(m=>m.slice(1).map(Number));
let p=graph.update(frame(flat,quiet),0);
assert.equal(points(p.input).length,128);
assert.deepEqual(points(p.input)[0],[0,44]);
assert.deepEqual(points(p.input).at(-1),[480,44]);
assert(points(p.input).every(([,y])=>y===44));
const stable=p.noise;
p=graph.update(frame(loud,quiet),40);
const attackY=points(p.input)[20][1];assert(attackY>14.6&&attackY<20);
p=graph.update(frame(quiet,quiet),80);
const releaseY=points(p.input)[20][1];assert(releaseY>attackY&&releaseY<25);
assert.equal(p.noise,stable);
assert.notEqual(graph.update(frame(quiet,flat),120).noise,stable);
graph.reset();
const peak=quiet.slice();peak[17]=-20;
p=graph.update(frame(peak,[]),0);
const pp=points(p.input);assert.equal(pp.length,128);
assert(pp[16][1]>=14.6&&pp[16][1]<16);
assert(pp[15][1]>70&&pp[17][1]>70);
assert(pp.every(([,y])=>Number.isFinite(y)&&y>=0&&y<=88));
assert.equal(p.noise,'');assert.equal(p.fill,'');
assert.equal(graph.update(null,50).input,'');
assert.equal(graph.update(frame(quiet,[]),55).input,graph.update(frame(quiet,[]),55).input);
const wide=Array(257).fill(-60);
p=graph.update(frame(wide,wide,172.265625),60);
assert.equal(points(p.input).length,256);assert.deepEqual(points(p.input).at(-1),[480,44]);
assert(p.fill.endsWith('L480 88L0 88Z'));
console.log('Anti Noiz UX10 presentation: actual bin count, log endpoints, bounded peak, temporal envelope, noise stability and reset PASS');
