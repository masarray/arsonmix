// SPDX-License-Identifier: AGPL-3.0-only
import assert from 'node:assert/strict';
import {controller,defaults} from './editor.mjs';
let listener,view,sent;const t={subscribe(f){listener=f;return()=>{};},read(){listener({kind:'snapshot',settings:defaults});},send(c){sent=c;}};
const m=controller(t,v=>view=v);assert.equal(view.accepted.cleanDb,14);assert(!m.submit('settings',{cleanDb:99}));assert(m.submit('settings',{cleanDb:8}));assert(view.pending);assert.equal(view.accepted.cleanDb,14);assert(!m.submit('settings',{cleanDb:3}));listener({kind:'ack',id:sent.id+1,applied:true,settings:sent.settings});assert(view.pending);listener({kind:'ack',id:sent.id,applied:true,settings:sent.settings});assert.equal(view.accepted.cleanDb,8);assert(!view.pending);assert(m.submit('learn'));listener({kind:'ack',id:sent.id,applied:false,settings:view.accepted});assert(view.error);m.dispose();assert(!m.submit('learn'));console.log('Anti Noiz Web ACK contract PASS');
