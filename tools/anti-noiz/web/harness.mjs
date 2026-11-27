// SPDX-License-Identifier: AGPL-3.0-only
import {mount,defaults} from './editor.mjs';
let settings={...defaults},listener=()=>{};
const transport={subscribe(fn){listener=fn;return()=>{listener=()=>{};};},read(){listener({kind:'snapshot',settings});},send(c){setTimeout(()=>{if(c.kind==='settings')settings={...c.settings};listener({kind:'ack',id:c.id,applied:c.kind==='settings',settings});},60);}};
mount(document.querySelector('#app'),transport);
