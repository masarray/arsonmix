// SPDX-License-Identifier: AGPL-3.0-only
// V6 presentation only: reuse the current Player controls and native ACK/readback.
// No second player, no audio element, no polling, no model or driver mutation.
(()=>{
'use strict';
const $=id=>document.getElementById(id);
const icon=(path,fill=false)=>'<svg viewBox="0 0 24 24" aria-hidden="true" '+(fill?'fill="currentColor"':'fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round"')+'>'+path+'</svg>';
const play=icon('<path d="M8 4.5a1 1 0 0 1 1.5-.85l12 7.5a1 1 0 0 1 0 1.7l-12 7.5A1 1 0 0 1 8 19.5z"/>',true);
const pause=icon('<rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/>',true);
const button=(id,label,glyph)=>'<button type="button" id="'+id+'" title="'+label+'" aria-label="'+label+'">'+glyph+'</button>';
function install(){
 const header=document.querySelector('.app > .window-title'),bar=document.querySelector('.app > .command-bar');
 const more=$('moreTools'),summary=more?.querySelector('summary');
 if(!header||!bar||!summary||bar.classList.contains('armix-ribbon'))return;
 header.classList.add('armix-ribbon-nav');bar.classList.add('armix-ribbon');
 const tabs=document.createElement('div');tabs.className='armix-ribbon-tabs';tabs.setAttribute('role','tablist');tabs.setAttribute('aria-label','Mixer workflows');
 tabs.innerHTML='<svg width="0" height="0" aria-hidden="true" style="position:absolute"><defs><linearGradient id="armix-ribbon-normal" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#516471"/><stop offset=".45" stop-color="#344651"/><stop offset="1" stop-color="#25343f"/></linearGradient><linearGradient id="armix-ribbon-active" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#36b8da"/><stop offset="1" stop-color="#087f9f"/></linearGradient></defs></svg>';
 const names=[['mixer','Mixer'],['player','Player'],['fx','FX Rack'],['routing','Routing'],['scenes','Scenes'],['io','Audio I/O']];
 for(const [id,name] of names){
   const tab=document.createElement('button');tab.type='button';tab.id='ribbon-tab-'+id;tab.className='armix-ribbon-tab';tab.dataset.workflow=id;tab.setAttribute('role','tab');tab.setAttribute('aria-controls','armix-ribbon-commands');tab.innerHTML='<svg viewBox="0 0 126 32" preserveAspectRatio="none" aria-hidden="true"><path fill="url(#armix-ribbon-normal)" d="M0 .5 H99 Q106 .5 112 12 L126 31.5 H0 Z"/></svg><span>'+name+'</span>';tabs.append(tab);
 }
 header.append(tabs);bar.id='armix-ribbon-commands';
 summary.innerHTML='<svg class="armix-ribbon-menu-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 6h16M4 12h16M4 18h16"/></svg><span>ArSon<span style="color:#51d4ef">Mix</span></span>';
 summary.setAttribute('aria-label','ArSonMix application menu');
 const player=document.createElement('div');player.className='armix-ribbon-player';player.hidden=true;player.setAttribute('role','group');player.setAttribute('aria-label','Player transport');
 player.innerHTML='<span class="armix-ribbon-track" id="ribbonTrack">No music loaded</span><input type="range" id="ribbonSeek" min="0" max="1000" step="1" value="0" aria-label="Seek through current song"><span class="armix-ribbon-time" id="ribbonTime">00:00 / 00:00</span><div class="armix-ribbon-transport">'+button('ribbonPrevious','Previous',icon('<rect x="4" y="5" width="3.5" height="14" rx="1"/><path d="M19 5 8 12l11 7z"/>',true))+button('ribbonPlay','Play',play)+button('ribbonNext','Next',icon('<rect x="16.5" y="5" width="3.5" height="14" rx="1"/><path d="m5 5 11 7-11 7z"/>',true))+button('ribbonStop','Stop',icon('<rect x="5" y="5" width="14" height="14" rx="2"/>',true))+'</div><div class="armix-ribbon-options">'+button('ribbonLoop','Loop A–B',icon('<path d="m17 3 4 4-4 4m4-4H8a5 5 0 0 0-5 5m4 9-4-4 4-4m-4 4h13a5 5 0 0 0 5-5"/>'))+button('ribbonMode','Playback mode / repeat',icon('<path d="m17 3 4 4-4 4m4-4H8a5 5 0 0 0-5 5m4 9-4-4 4-4m-4 4h13a5 5 0 0 0 5-5"/><path d="M10 12h4"/>'))+button('ribbonQueue','Queue',icon('<path d="M3 5h17M3 10h17M3 15h10m3 0 5 3-5 3z"/>'))+'</div><label class="armix-ribbon-volume">'+icon('<path d="M3 9v6h4l5 4V5L7 9H3zm13 0a4 4 0 0 1 0 6m3-9a8 8 0 0 1 0 12"/>')+'<input type="range" id="ribbonVolume" min="0" max="100" step="1" value="50" aria-label="Player level before channel FX"></label>';
 bar.insertBefore(player,$('playerToggle')||bar.firstElementChild);
 const scene=document.createElement('div');scene.className='armix-ribbon-scene';scene.hidden=true;scene.innerHTML='<button type="button" id="ribbonShowSave">Save Show</button><button type="button" id="ribbonShowBrowse">Browse Shows</button>';
 bar.insertBefore(scene,player);
 const native=Boolean(window.__JUCE__?.backend?.emitEvent);
 const source=id=>$(id);
 const click=id=>{const el=source(id);if(el&&!el.disabled)el.click();};
 const openPlayer=()=>{if($('arsonPlayer')?.hidden)click('playerToggle');};
 const mappings={ribbonPrevious:'playerPrevious',ribbonPlay:'playerPlay',ribbonNext:'playerNext',ribbonStop:'playerStop',ribbonLoop:'playerLoopAB'};
 Object.entries(mappings).forEach(([id,target])=>$(id).addEventListener('click',()=>click(target)));
 $('ribbonMode').addEventListener('click',()=>{openPlayer();click('playerAutoNext');});
 $('ribbonQueue').addEventListener('click',()=>{openPlayer();click('playerQueueView');});
 const findShow=label=>Array.from(more.querySelectorAll('button')).find(e=>e.textContent.trim()===label);
 for(const [id,label] of [['ribbonShowSave','Save Show'],['ribbonShowBrowse','Browse Shows']])$(id).addEventListener('click',()=>{const el=findShow(label);if(el&&!el.disabled){more.open=true;el.click();}});
 const controls=new Map();
 function range(proxyId,originalId,max){
   const proxy=$(proxyId),original=source(originalId);if(!original)return;
   let armed=false;
   const event=type=>original.dispatchEvent(new Event(type,{bubbles:true}));
   const paint=()=>proxy.style.setProperty('--ribbon-fill',(Number(proxy.value)/max*100)+'%');
   function cancel(){if(armed){armed=false;event('pointercancel');}sync();}
   function sync(){if(!armed){const v=original.value;if(proxy.value!==v)proxy.value=v;paint();}proxy.disabled=!native||original.disabled;}
   proxy.addEventListener('pointerdown',()=>{if(proxy.disabled)return;armed=true;event('pointerdown');});
   proxy.addEventListener('input',()=>{paint();if(originalId==='playerVolume'){original.value=proxy.value;event('input');}});
   proxy.addEventListener('keydown',()=>event('keydown'));
   proxy.addEventListener('change',()=>{if(proxy.disabled){cancel();return;}original.value=proxy.value;event('change');armed=false;sync();});
   proxy.addEventListener('pointerup',()=>{if(armed){armed=false;event('pointerup');}});
   proxy.addEventListener('pointercancel',cancel);proxy.addEventListener('lostpointercapture',cancel);
   controls.set(proxyId,{sync,cancel});
 }
 range('ribbonSeek','playerSeek',1000);range('ribbonVolume','playerVolume',100);
 let active='mixer',queued=false;
 const setText=(node,text)=>{if(node&&node.textContent!==text)node.textContent=text;};
 function syncPlayer(){
   if(active!=='player')return;
   const playing=source('playerPlay')?.dataset.playing==='true',p=$('ribbonPlay');
   if(p.getAttribute('aria-pressed')!==String(playing)){p.innerHTML=playing?pause:play;p.setAttribute('aria-pressed',String(playing));p.title=playing?'Pause':'Play';p.setAttribute('aria-label',p.title);}
   for(const [id,target] of Object.entries(mappings)){const el=source(target);$(id).disabled=!native||!el||el.disabled;}
   $('ribbonMode').disabled=!native||!source('playerAutoNext');
   $('ribbonQueue').disabled=!source('playerQueueView');
   $('ribbonLoop').setAttribute('aria-pressed',source('playerLoopAB')?.getAttribute('aria-pressed')||'false');
   $('ribbonMode').setAttribute('aria-pressed',source('playerAutoNext')?.getAttribute('aria-pressed')||'false');
   const title=source('playerNow')?.textContent||'No music loaded';setText($('ribbonTrack'),title);$('ribbonTrack').title=title;
   setText($('ribbonTime'),(source('playerElapsed')?.textContent||'00:00')+' / '+(source('playerDuration')?.textContent||'00:00'));
   controls.forEach(c=>c.sync());
 }
 function schedule(){if(active!=='player'||queued)return;queued=true;queueMicrotask(()=>{queued=false;syncPlayer();});}
 const observed=['playerPlay','playerPrevious','playerNext','playerStop','playerLoopAB','playerAutoNext','playerNow','playerElapsed','playerDuration','playerSeek','playerVolume'];
 const playerObserver=new MutationObserver(schedule);
 for(const id of observed){const el=source(id);if(el)playerObserver.observe(el,{attributes:true,attributeFilter:['disabled','aria-pressed','data-playing','style'],childList:true,characterData:true,subtree:true});}
 // Presentation follows the already accepted Player DOM; no second state stream.
 const group={
   bankPrev:'mixer',bankSelect:'mixer',bankNext:'mixer',densitySelect:'mixer',
   pluginsButton:'mixer fx',gainView:'mixer',fxView:'mixer fx',globalFxToggle:'fx',
   sofToggle:'mixer routing',sofTarget:'mixer routing',clearCue:'mixer routing',
   wireNavigatorToggle:'routing',wireMatrixToggle:'routing',wireConfigToggle:'routing',
   sceneSelect:'scenes',saveScene:'scenes',recallScene:'scenes',addTrack:'mixer routing',
   playerMini:'mixer',playerToggle:'all',moreTools:'all',mixContext:'all'
 };
 function apply(){
   for(const el of bar.children){
     if(el===player||el===scene)continue;
     const scope=el.classList.contains('strip-view-switch')?'mixer fx':el.matches('[data-native-io]')?'mixer io':group[el.id]||'all';
     el.dataset.ribbonHidden=String(scope!=='all'&&!scope.split(' ').includes(active));
   }
   player.hidden=active!=='player';scene.hidden=active!=='scenes'||!native;
   const trigger=$('playerToggle');if(trigger)trigger.textContent=active==='player'?'Open Player':'♫ Player';
   for(const [id,label] of [['ribbonShowSave','Save Show'],['ribbonShowBrowse','Browse Shows']]){const el=findShow(label);$(id).disabled=!el||el.disabled;}
 }
 function select(id,focus=false){
   if(active==='player'&&id!==active)controls.forEach(c=>c.cancel());
   active=id;bar.dataset.ribbonTab=id;
   tabs.querySelectorAll('[role=tab]').forEach(t=>{const on=t.dataset.workflow===id;t.setAttribute('aria-selected',String(on));t.tabIndex=on?0:-1;if(on&&focus)t.focus();});
   if(more.open)more.open=false;apply();syncPlayer();
 }
 tabs.addEventListener('click',e=>{const tab=e.target.closest('[role=tab]');if(tab)select(tab.dataset.workflow);});
 tabs.addEventListener('keydown',e=>{const i=names.findIndex(x=>x[0]===active);let n=i;if(e.key==='ArrowRight')n=(i+1)%names.length;else if(e.key==='ArrowLeft')n=(i+names.length-1)%names.length;else if(e.key==='Home')n=0;else if(e.key==='End')n=names.length-1;else return;e.preventDefault();select(names[n][0],true);});
 // Bridge creates a few real toolbar controls during native startup. Only
 // observe direct insertion/removal, never mixer strips/meters or all DOM.
 new MutationObserver(()=>{apply();syncPlayer();}).observe(bar,{childList:true});
 select('mixer');
}
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',install,{once:true});
else if(document.readyState==='interactive')document.addEventListener('DOMContentLoaded',install,{once:true});
else install();
})();
