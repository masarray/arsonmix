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
const window={__JUCE__:{backend},addEventListener(){}};
const $=id=>document.getElementById(id);
const player=readFileSync('site/preview-v2/assets/arson-player.js','utf8');
runInNewContext(player,{document,window,console});
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
  ((2000-songRows().length)*34)+'px',
  'full virtual scrollbar must cover every native song');
assert.equal(queueRows().length,128,'all 128 queued entries are accessible');
assert.equal($('playerQueueCount').textContent,'128');
assert.equal(queueRows()[0].children[0].textContent,'Duplicate Song');
assert.equal(queueRows()[3].children[0].textContent,'Duplicate Song');
assert.ok(queueRows()[5].draggable);
assert.equal(queueRows()[0].children[1].children[0].disabled,true,
  'first queue entry cannot move above zero');

const list=$('playerList');
list.scrollTop=34*1400;
list.fire('scroll');
assert.ok(songRows().length<=80);
const visibleIds=songRows().map(row=>row.children[0].children[0].textContent);
assert.ok(visibleIds.includes('1401'), 'song 1401 must be in virtual viewport');
const preserved=list.children;
const firstButton=songRows()[0].children[0];
actions.length=0;
publish({queue,queueRevision:7,setlists:initial.setlists,pending:-1,
         activeSetlist:0,current:0,selected:0});
assert.strictEqual(list.children,preserved,
  'queue-only snapshot must not rebuild the 2,000-track catalogue');
publish({selected:1400,current:1399,pending:-1});
assert.strictEqual(songRows()[0].children[0],firstButton,
  'CURRENT/NEXT ACK must preserve row/button identity for double-click');
const selectedRow=songRows().find(row=>row.dataset.songId==='1400');
assert.equal(selectedRow.children[0].dataset.selected,'true');
assert.equal($('playerPitchTarget').dataset.next,'true');
assert.match($('playerPitchTarget').textContent,/NEXT · Song 1400/);
assert.equal($('playerUpNext').textContent,'Duplicate Song');
assert.equal(selectedRow.children[1].tagName,'BUTTON');
actions.length=0;
selectedRow.children[1].click();
assert.equal($('playerSongMenu').hidden,false);
$('playerSongMenu').children[2].click();
assert.equal(actions.at(-1).detail.action,'playerQueueAppend');
assert.equal(actions.at(-1).detail.value,1400);
assert.equal($('playerSongMenu').hidden,true);
actions.length=0;selectedRow.fire('contextmenu',{clientX:100,clientY:200,preventDefault(){},stopPropagation(){}});
assert.equal($('playerSongMenu').hidden,false);assert.equal(actions.length,0,'right click does not select or play');
$('playerSongMenu').fire('keydown',{key:'End',preventDefault(){},stopPropagation(){}});
assert.equal(document.activeElement.textContent,'Remove from setlist');
$('playerSongMenu').fire('keydown',{key:'Escape',preventDefault(){},stopPropagation(){}});
assert.equal($('playerSongMenu').hidden,true);assert.equal(document.activeElement,selectedRow.children[1]);
$('playerQueueView').click();assert.equal($('playerList').hidden,true);assert.equal($('playerQueuePanel').hidden,false);
$('playerSetlistView').click();assert.equal($('playerList').hidden,false);assert.equal($('playerQueuePanel').hidden,true);
const rename=$('playerSetlistName');rename.tagName='INPUT';
actions.length=0;
$('arsonPlayer').fire('keydown',{key:' ',target:rename,preventDefault(){throw Error('space in text field must not be intercepted');}});
assert.equal(actions.length,0);
$('arsonPlayer').fire('keydown',{key:' ',target:$('arsonPlayer'),preventDefault(){}});
assert.equal(actions.at(-1).detail.action,'playerPlay');
const freshRow=songRows().find(row=>row.dataset.songId==='1400');
freshRow.children[1].click();const staleMenu=$('playerSongMenu').children.find(x=>x.textContent==='Remove from setlist');
publish({tracks:songs,selected:1400,current:1399});
actions.length=0;staleMenu.click();
assert.equal(actions.length,0,'stale contextual action cannot remove a different song');
actions.length=0;
selectedRow.children[0].fire('click');
selectedRow.children[0].fire('dblclick');
assert.equal(actions.length,0,'detached song cannot select/play a replacement index');
const acceptedRow=songRows().find(row=>row.dataset.songId==='1400');
acceptedRow.children[0].fire('click');acceptedRow.children[0].fire('dblclick');
assert.equal(actions[0].detail.action,'playerSelect');assert.equal(actions[1].detail.action,'playerPlayNow');
actions.length=0; // isolate the next gesture, not previous click evidence

const data={types:['application/x-arsonmix-queue'],setData(){},getData(){}};
const prevent=()=>{};
const trackTarget=songRows().find(row=>row.dataset.songId==='1401');
trackTarget.fire('drop',{dataTransfer:{getData(){return '0';}},preventDefault:prevent});
assert.equal(actions.length,0,'external text drag must not reorder music');
acceptedRow.fire('dragstart',{dataTransfer:data,preventDefault:prevent});
publish({tracks:songs,selected:1400,current:1399,pending:-1});
songRows().find(row=>row.dataset.songId==='1401').fire('drop',{dataTransfer:data,preventDefault:prevent});
assert.equal(actions.length,0,'library revision invalidates drag origin');
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
const staleRow=queueRows()[0];const staleQueueRemove=staleRow.children[1].children[2];
staleRow.fire('contextmenu',{preventDefault(){},stopPropagation(){}});
assert.equal($('playerSongMenu').children[4].textContent,'Remove from queue');const staleQueueMenu=$('playerSongMenu').children[4];
publish({queue,queueRevision:8,setlists:initial.setlists,pending:-1,
         activeSetlist:0,current:0,selected:0});
staleRow.fire('drop',{dataTransfer:data,preventDefault:prevent});
assert.equal(actions.length,0,
  'native queue revision change invalidates pending old browser drag');
staleQueueRemove.click();staleQueueMenu.click();assert.equal(actions.length,0,'stale queue button/menu cannot remove a different entry');
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
search.value='';search.fire('input');
assert.ok(songRows().length<=80);
$('playerToggle').fire('click'); // opening hidden panel remeasures viewport
assert.ok(songRows().length<=80);
assert.doesNotMatch(player,/new Audio\s*\(|AudioContext\s*\(/,
  'WebView may never become a competing audio player');
assert.equal($('playerRemaining').textContent,'−04:00');
publish({queue:[],current:0,selected:0,pending:-1});assert.equal($('playerUpNext').textContent,'Song 0001');
publish({queue:[],current:1999,selected:1999,pending:-1});assert.equal($('playerUpNext').textContent,'End of setlist');
publish({queue:[],current:0,selected:1,pending:2});assert.equal($('playerNextLabel').textContent,'PREPARING');assert.equal($('playerUpNext').textContent,'Song 0002');
publish({queue:[],current:0,selected:0,pending:-1,transport:{state:'PLAYING',title:'Song 0000',currentSeconds:102,durationSeconds:240}});
$('playerClose').click();assert.equal($('playerMini').hidden,false);
assert.match($('playerMiniTime').textContent,/01:42 \/ 04:00/);
actions.length=0;$('playerMiniPlay').click();assert.equal(actions.at(-1).detail.action,'playerPause');
$('playerMiniExpand').click();assert.equal($('arsonPlayer').hidden,false);assert.equal($('playerMini').hidden,true);
$('playerLibraryView').click();assert.equal($('playerLibraryNote').hidden,false);assert.ok(songRows().every(row=>!row.draggable));
$('playerSetlistView').click();assert.equal($('playerLibraryNote').hidden,true);
const seek=$('playerSeek');seek.fire('pointerdown');seek.value='800';
publish({current:1,selected:1,pending:-1,transport:{title:'Song 0001',durationSeconds:180}});
actions.length=0;seek.fire('change');assert.equal(actions.length,0,'seek gesture cannot cross song identity');
seek.fire('keydown');seek.value='500';seek.fire('change');assert.equal(actions.at(-1).detail.value,90);
const current=songRows().find(row=>row.dataset.songId==='1');
if(current){current.children[1].click();assert.equal($('playerSongMenu').children.find(x=>x.textContent==='Remove from setlist').disabled,true);}

console.log('V4.2 PASS: right click, keyboard menu, virtual rows, queue epochs, native MiniPlayer and library view');

const modified=songs.map(s=>s.id===1999?{...s,title:"Café Session 1999",artist:"Acoustic"}:s);
publish({tracks:modified,queue:[],current:1,selected:1,pending:-1,transport:{state:"PLAYING",title:"Song 0001"}});
search.value="1999 cafe";search.fire("input");
assert.equal(songRows().length,1);assert.equal($("playerCount").textContent,"1 / 2000 songs");
actions.length=0;search.fire("keydown",{key:"ArrowDown",preventDefault(){}});assert.equal(actions.length,0);
assert.equal(document.activeElement,songRows()[0].children[0]);
search.fire("keydown",{key:"Enter",preventDefault(){}});assert.equal(actions.at(-1).detail.action,"playerSelect");assert.equal(actions.at(-1).detail.value,1999);
search.fire("keydown",{key:"Enter",ctrlKey:true,preventDefault(){}});assert.equal(actions.at(-1).detail.action,"playerQueueAppend");
search.fire("keydown",{key:"Escape",preventDefault(){}});assert.equal(search.value,"");
actions.length=0;assert.equal($("playerSetlistSelect").disabled,true);$("playerNewSetlist").click();assert.equal(actions.length,0);
publish({transport:{state:"STOPPED"}});assert.equal($("playerSetlistSelect").disabled,false);
$("playerOptions").click();$("playerSongMenu").children.find(x=>x.textContent==="Show current song").click();
assert.equal(document.activeElement,songRows().find(row=>row.dataset.songId==="1").children[0]);assert.equal(actions.length,0);
console.log("V4.4 PASS: search, keyboard focus, live setlist guard");

// P0 metadata readback never uses catalogue id as active deck index.
publish({tracks:[{...songs[0],catalogId:4}],queue:[],current:0,selected:0,pending:-1});
listeners.get("arMixPlayerMedia")({schema:"arsonmix.media.v1",rows:[{catalogId:4,title:"Real title",artist:"Artist",durationSeconds:123,format:"FLAC",sampleRate:48000,channels:2}]});
assert.equal($("playerNow").textContent,"Real title");assert.match($("playerNowMeta").textContent,/FLAC/);
assert.equal($("playerCount").textContent,"1 songs · 02:03");
listeners.get("arMixPlayerCover")({catalogId:4,uri:"data:image/png;base64,AAAA"});assert.equal($("playerArtwork").hidden,false);
listeners.get("arMixPlayerCover")({catalogId:3,uri:"data:image/png;base64,AAAA"});assert.equal($("playerArtwork").hidden,true);
$("playerLibraryView").click();
listeners.get("arMixPlayerCatalogue")({schema:"arsonmix.catalogue.v1",rows:Array.from({length:5},(_,catalogId)=>({catalogId,title:"Library "+catalogId}))});
search.value="Library 1";search.fire("input");actions.length=0;
songRows()[0].children[1].click();$("playerSongMenu").children[1].click();
assert.deepEqual(JSON.parse(JSON.stringify(actions.at(-1).detail)),{action:"playerCatalogueAdd",value:{id:1,set:0}});
assert.equal(actions.length,1);console.log("P0 UI metadata, duration and global catalogue intent PASS");

// Original art precedence and fallback remain presentation-only.
publish({tracks:[{...songs[0],catalogId:4},{...songs[1],catalogId:5}],current:0,selected:0});
const initialArt=$("playerArtworkFallback").dataset.art;
assert.ok(["blue-hour","prism","tide","dune","bloom"].includes(initialArt));
assert.equal($("playerMiniArtworkFallback").dataset.art,initialArt);
listeners.get("arMixPlayerCover")({catalogId:4,uri:"data:image/png;base64,AAAA"});
assert.equal($("playerArtwork").hidden,false);assert.equal($("playerMiniArtwork").hidden,false);
$("playerArtwork").fire("error",{currentTarget:$("playerArtwork")});
assert.equal($("playerArtwork").hidden,true);assert.equal($("playerMiniArtwork").hidden,true);
publish({current:0,selected:0});assert.equal($("playerArtworkFallback").dataset.art,initialArt);assert.equal($("playerArtwork").hidden,true);
publish({current:1,selected:1});assert.equal($("playerArtwork").hidden,true);
listeners.get("arMixPlayerCover")({catalogId:4,uri:"data:image/png;base64,BBBB"});assert.equal($("playerArtwork").hidden,true);
publish({tracks:[{...songs[0],catalogId:4},{...songs[1],catalogId:5,missing:true}],current:1,selected:1});
assert.equal($("playerArtworkFallback").dataset.art,"unavailable");
publish({current:-1,selected:-1,transport:{state:"EMPTY",title:""}});
assert.equal($("playerArtworkFallback").dataset.art,"still");
console.log("Artwork priority, stable fallback, missing and MiniPlayer PASS");
