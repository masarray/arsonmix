// SPDX-License-Identifier: AGPL-3.0-only
// Execute the approved Player WebView source against a minimal DOM; no mock
// audio renderer. Verify bounded rows, full 128-entry queue and queue epochs.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {runInNewContext} from 'node:vm';

class Element {
  constructor(tag='div'){
    this.tagName=tag.toUpperCase();this.children=[];this.listeners={};
    this.dataset={};this.style={};this.attributes={};
    this.hidden=false;this.disabled=false;this.value='';
    this.textContent='';this.scrollTop=0;this.clientHeight=266;
    this.width=256;this.height=42;
  }
  addEventListener(name,fn){(this.listeners[name]??=[]).push(fn);}
  fire(name,event={}){for(const fn of this.listeners[name]||[])fn(event);}
  append(...nodes){this.children.push(...nodes);}
  replaceChildren(...nodes){this.children=[...nodes];}
  setAttribute(name,value){this.attributes[name]=value;}
  getContext(){return {clearRect(){},fillRect(){},fillStyle:''};}
  focus(){document.activeElement=this;}
  querySelector(){return new Element('div');}
  click(){this.fire('click');}
}
const elements=new Map();
const document={
  activeElement:null,
  getElementById(id){
    if(!elements.has(id))elements.set(id,new Element('div'));
    return elements.get(id);
  },
  createElement(tag){return new Element(tag);}
};
const listeners=new Map(),actions=[];
const backend={
  addEventListener(name,fn){listeners.set(name,fn);},
  emitEvent(name,detail){actions.push({name,detail});}
};
const $=id=>document.getElementById(id);
const player=readFileSync('site/preview-v2/assets/arson-player.js','utf8');
runInNewContext(player,{document,window:{__JUCE__:{backend}},console});
const publish=state=>listeners.get('arMixPlayerState')({
  schema:'arsonmix.player.v1',
  ...state,
  transport:{state:'READY',title:'Song 0000',currentSeconds:0,
             durationSeconds:240,...state.transport}
});
const songs=Array.from({length:2000},(_,id)=>({
  id,title:'Song '+String(id).padStart(4,'0'),missing:false,
  keySemitones:0,tempoPercent:100,cueIn:0,cueOut:0
}));
const queue=Array.from({length:128},(_,id)=>({
  id,title:id%3===0?'Duplicate Song':'Next '+id
}));
const initial={tracks:songs,queue,setlists:[{id:0,name:'Show',count:2000}],
  activeSetlist:0,current:0,selected:0,pending:-1,queueRevision:7,
  autoNext:false,canUndoRemove:false,crossfadeMs:180,error:''};
publish(initial);
const songRows=()=> $('playerList').children.filter(x=>x.className==='ap-songrow');
const queueRows=()=> $('playerQueueList').children.filter(x=>x.className==='ap-queue-row');
assert.ok(songRows().length>0 && songRows().length<=80,
  '2,000-song library must never allocate 2,000 WebView rows');
assert.equal($('playerList').children.at(-1).style.height,
  ((2000-songRows().length)*38)+'px',
  'full virtual scrollbar must cover every native song');
assert.equal(queueRows().length,128,'all 128 queued entries are accessible');
assert.equal($('playerQueueCount').textContent,'128');
assert.equal(queueRows()[0].children[0].textContent,'Duplicate Song');
assert.equal(queueRows()[3].children[0].textContent,'Duplicate Song');
assert.ok(queueRows()[5].draggable);
assert.ok(!queueRows()[0].children[1].children[0].disabled===false ||
          queueRows()[0].children[1].children[0].disabled,
          'first queue entry cannot move above zero');

const list=$('playerList');
list.scrollTop=38*1400;
list.fire('scroll');
assert.ok(songRows().length<=80);
const visibleIds=songRows().map(row=>row.children[0].children[0].textContent);
assert.ok(visibleIds.includes('1401'), 'song 1401 must be in virtual viewport');
const preserved=list.children;
actions.length=0;
publish({queue,queueRevision:7,setlists:initial.setlists,pending:-1,
         activeSetlist:0,current:0,selected:0});
assert.strictEqual(list.children,preserved,
  'queue-only snapshot must not rebuild the 2,000-track catalogue');

const data={types:['application/x-arsonmix-queue'],setData(){},getData(){}};
const prevent=()=>{};
const rows=queueRows();
rows[5].fire('dragstart',{dataTransfer:data,preventDefault:prevent});
rows[1].fire('dragover',{dataTransfer:data,preventDefault:prevent});
rows[1].fire('drop',{dataTransfer:data,preventDefault:prevent});
assert.equal(actions.length,1);
assert.equal(actions[0].detail.action,'playerQueueMove');
assert.equal(actions[0].detail.value.from,5);
assert.equal(actions[0].detail.value.to,1);
assert.equal(actions[0].detail.value.revision,7);
assert.equal(queueRows().length,128);

actions.length=0;
queueRows()[8].fire('dragstart',{dataTransfer:data,preventDefault:prevent});
const staleRow=queueRows()[0];
publish({queue,queueRevision:8,setlists:initial.setlists,pending:-1,
         activeSetlist:0,current:0,selected:0});
staleRow.fire('drop',{dataTransfer:data,preventDefault:prevent});
assert.equal(actions.length,0,
  'native queue revision change invalidates pending old browser drag');
queueRows()[2].children[1].children[2].click();
assert.deepEqual(
  JSON.parse(JSON.stringify(actions.at(-1).detail)),
  {action:'playerQueueRemove',value:{from:2,revision:8}});
actions.length=0;
publish({queue,queueRevision:9,setlists:initial.setlists,pending:4,
         activeSetlist:0,current:0,selected:0});
assert.ok(queueRows().every(row=>!row.draggable),
  'in-flight NEXT must freeze queue drag UI');
queueRows()[4].fire('dragstart',{dataTransfer:data,preventDefault:prevent});
assert.equal(actions.length,0);
publish({queue,queueRevision:10,setlists:initial.setlists,pending:-1,
         activeSetlist:0,current:0,selected:0});
const search=$('playerSearch');
search.value='song 1999';search.fire('input');
assert.equal(songRows().length,1,'search must reach song 2000');
assert.equal(songRows()[0].children[0].children[1].textContent,'Song 1999');
assert.equal(list.scrollTop,0,'changing search returns to first match');
assert.doesNotMatch(player,/new Audio\s*\(|AudioContext\s*\(/,
  'WebView may never become a competing audio player');
console.log('M6 PASS: 2000-song virtualization, 128 queue, duplicates, revision-locked drag, search and native-only playback');
