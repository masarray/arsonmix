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
// Keep original command nodes and listeners. Only their presentation page changes.
function installCompactMenu(more){
 const panel=more.querySelector('.more-tools-panel');
 if(!panel)return null;
 const glyphs={
 channel:'<path d="M5 3v18M12 3v18M19 3v18M3 9h4m3 7h4m3-9h4"/>',
 view:'<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
 scenes:'<path d="m12 3 10 5-10 5L2 8zM2 12l10 5 10-5M2 16l10 5 10-5"/>',
 settings:'<circle cx="12" cy="12" r="3"/><path d="m12 2 2 3 3-.1 1.5 2.6-1.4 2.7 1.4 2.8 3 .9-1 3-3 .5-2 2.5-3-1-3 1-2-2.5-3-.5-1-3 3-.9L7 10 5.5 7.5 7 5l3 .1z"/>',
 diagnostics:'<path d="M2 12h4l3-7 4 14 3-7h6"/>'
 };
 const groups=[['channel','Channel'],['view','View'],['scenes','Scenes & Shows'],['settings','Settings'],['diagnostics','Diagnostics']];
 const nav=new Map();
 const back=document.createElement('button');back.type='button';back.className='armix-menu-back';
 back.innerHTML=icon('<path d="m15 5-7 7 7 7"/>')+'<span>Back</span>';
 panel.prepend(back);
 for(const [id,label] of groups){
   const row=document.createElement('button');row.type='button';row.className='armix-menu-category';row.dataset.menuCategory=id;
   row.innerHTML=icon(glyphs[id])+'<span>'+label+'</span>'+icon('<path d="m9 5 7 7-7 7"/>');
   row.addEventListener('click',()=>show(id,true));nav.set(id,row);panel.append(row);
 }
 let page='root';
 const channelIds=new Set(['addTrack','addBus','addFx','addGroupBus','addGroupSelected','convertSelectedReverb','copy','paste','group','ungroup']);
 const viewIds=new Set(['mode','viewFilter']);
 const sceneIds=new Set(['sceneSafe','sceneSelect','saveScene','recallScene']);
 const headings=new Map([['PROJECT · MIXER SESSION','root'],['CHANNEL SETUP','channel'],['VIEW & SELECTION','channel'],['WIRE TOOLS','settings'],['REFERENCE TOOLS','diagnostics'],['AUDIO / SAFETY','settings'],['AUDIO HEALTH','diagnostics'],['STUDIO / VERIFIED SHOWS','scenes']]);
 function classify(){
   let section='diagnostics';
   for(const el of panel.children){
     if(el===back||el.classList.contains('armix-menu-category'))continue;
     const label=el.textContent.trim();
     if(el.matches('.more-tools-caption')&&headings.has(label)){
       section=headings.get(label);el.dataset.armixMenuHeading='true';el.dataset.armixMenuActive='false';continue;
     }
     let group=section;
     if(el.matches('.mixer-file-tool,.reference-file-menu,[data-native-emergency-output]'))group='root';
     else if(channelIds.has(el.id))group='channel';
     else if(viewIds.has(el.id))group='view';
     else if(sceneIds.has(el.id))group='scenes';
     else if(el.matches('[data-wire-open],.arNativeAdvancedRecovery'))group='settings';
     // Unknown commands are retained in their original section (or Diagnostics).
     el.dataset.armixMenuPage=group;el.dataset.armixMenuActive=String(group===page);
   }
   for(const [id,row] of nav){
     row.hidden=page!=='root'||!Array.from(panel.children).some(el=>el.dataset.armixMenuPage===id&&!el.hidden);
   }
   back.hidden=page==='root';
   back.querySelector('span').textContent=page==='root'?'Back':groups.find(g=>g[0]===page)[1];
   panel.dataset.armixMenuPage=page;
 }
 function show(id,focus=false){
   page=id;classify();panel.scrollTop=0;
   if(focus)(id==='root'?nav.get(lastPage):back)?.focus({preventScroll:true});
 }
 let lastPage='channel';
 for(const [id,row] of nav)row.addEventListener('click',()=>{lastPage=id;});
 back.addEventListener('click',()=>show('root',true));
 more.addEventListener('toggle',()=>{if(!more.open)show('root');});
 more.addEventListener('keydown',e=>{
   if(e.key!=='Escape'||!more.open)return;
   e.preventDefault();e.stopPropagation();
   if(page!=='root')show('root',true);else{more.open=false;more.querySelector('summary').focus();}
 });
 // Direct insertions only: native bridge installs the real commands at startup.
 new MutationObserver(classify).observe(panel,{childList:true});
 classify();
 return {show};
}

function install(){
 const header=document.querySelector('.app > .window-title'),bar=document.querySelector('.app > .command-bar');
 const more=$('moreTools'),summary=more?.querySelector('summary');
 if(!header||!bar||!summary||bar.classList.contains('armix-ribbon'))return;
 header.classList.add('armix-ribbon-nav');bar.classList.add('armix-ribbon');
 const tabs=document.createElement('div');tabs.className='armix-ribbon-tabs';tabs.setAttribute('role','tablist');tabs.setAttribute('aria-label','Mixer workflows');
 const shape=(id,w,cls='')=>{const end=w-24;return '<svg class="'+cls+'" viewBox="0 0 '+w+' 32" preserveAspectRatio="none" aria-hidden="true"><defs><linearGradient id="armix-shape-'+id+'" x1="0" y1="0" x2="0" y2="1"><stop style="stop-color:var(--tab-top)"/><stop offset=".45" style="stop-color:var(--tab-mid)"/><stop offset="1" style="stop-color:var(--tab-bottom)"/></linearGradient></defs><path d="M0 0 H'+end+' Q'+(end+7)+' 0 '+(end+13)+' 12 L'+w+' 32 H0 Z" fill="url(#armix-shape-'+id+')"/></svg>';};
 const names=[['mixer','Mixer'],['player','Player'],['fx','FX Rack'],['routing','Routing'],['scenes','Scenes'],['io','Audio I/O']];
 for(const [id,name] of names){
   const tab=document.createElement('button');tab.type='button';tab.id='ribbon-tab-'+id;tab.className='armix-ribbon-tab';tab.dataset.workflow=id;tab.setAttribute('role','tab');tab.setAttribute('aria-controls','armix-ribbon-commands');tab.style.zIndex=String(6-names.findIndex(n=>n[0]===id));tab.innerHTML=shape(id,126)+'<span>'+name+'</span>';tabs.append(tab);
 }
 header.append(tabs);bar.id='armix-ribbon-commands';
 summary.innerHTML=shape('menu',151,'armix-ribbon-menu-shape')+'<svg class="armix-ribbon-menu-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 6h16M4 12h16M4 18h16"/></svg><span>ArSon<span style="color:#51d4ef">Mix</span></span>';
 summary.setAttribute('aria-label','ArSonMix application menu');
 const compactMenu=installCompactMenu(more);
 const player=document.createElement('div');player.className='armix-ribbon-player';player.hidden=true;player.setAttribute('role','group');player.setAttribute('aria-label','Player transport');
 player.innerHTML='<span class="armix-ribbon-track" id="ribbonTrack">No music loaded</span><input type="range" id="ribbonSeek" min="0" max="1000" step="1" value="0" aria-label="Seek through current song"><span class="armix-ribbon-time" id="ribbonTime">00:00 / 00:00</span><div class="armix-ribbon-transport">'+button('ribbonPrevious','Previous',icon('<rect x="4" y="5" width="3.5" height="14" rx="1"/><path d="M19 5 8 12l11 7z"/>',true))+button('ribbonPlay','Play',play)+button('ribbonNext','Next',icon('<rect x="16.5" y="5" width="3.5" height="14" rx="1"/><path d="m5 5 11 7-11 7z"/>',true))+button('ribbonStop','Stop',icon('<rect x="5" y="5" width="14" height="14" rx="2"/>',true))+'</div><div class="armix-ribbon-options">'+button('ribbonLoop','Loop A–B',icon('<path d="m17 3 4 4-4 4m4-4H8a5 5 0 0 0-5 5m4 9-4-4 4-4m-4 4h13a5 5 0 0 0 5-5"/>'))+button('ribbonMode','Playback mode / repeat',icon('<path d="m17 3 4 4-4 4m4-4H8a5 5 0 0 0-5 5m4 9-4-4 4-4m-4 4h13a5 5 0 0 0 5-5"/><path d="M10 12h4"/>'))+button('ribbonQueue','Queue',icon('<path d="M3 5h17M3 10h17M3 15h10m3 0 5 3-5 3z"/>'))+'</div><label class="armix-ribbon-volume">'+icon('<path d="M3 9v6h4l5 4V5L7 9H3zm13 0a4 4 0 0 1 0 6m3-9a8 8 0 0 1 0 12"/>')+'<input type="range" id="ribbonVolume" min="0" max="100" step="1" value="50" aria-label="Player level before channel FX"></label>';
 // Restore local V6 order with the same native action controls.
 const sep=()=>{const el=document.createElement('span');el.className='armix-ribbon-separator';el.setAttribute('aria-hidden','true');return el;};
 const readout=document.createElement('div');readout.className='armix-ribbon-readout';
 readout.innerHTML=icon('<path d="M9 18V5l12-2v13M9 18c0 2-3 3-4.5 2S3 17 5 16s4-1 4 2zm12-2c0 2-3 3-4.5 2S15 15 17 14s4-1 4 2z"/>');
 const transport=player.querySelector('.armix-ribbon-transport'),options=player.querySelector('.armix-ribbon-options'),volume=player.querySelector('.armix-ribbon-volume');
 const track=player.querySelector('#ribbonTrack'),seek=player.querySelector('#ribbonSeek'),time=player.querySelector('#ribbonTime');
 readout.append(track,time);
 const divider=document.createElement('span');divider.className='armix-ribbon-option-divider';divider.setAttribute('aria-hidden','true');
 options.insertBefore(divider,options.querySelector('#ribbonQueue'));
 options.insertAdjacentHTML('beforeend',button('ribbonAdd','Open audio files',icon('<path d="M3 6h7l2 2h9v11H3z"/>')));
 player.replaceChildren(transport,sep(),readout,seek,sep(),options,sep(),volume,sep());
 bar.insertBefore(player,$('playerToggle')||bar.firstElementChild);
 const scene=document.createElement('div');scene.className='armix-ribbon-scene';scene.hidden=true;scene.innerHTML='<button type="button" id="ribbonShowSave">Save Show</button><button type="button" id="ribbonShowBrowse">Browse Shows</button>';
 bar.insertBefore(scene,player);
 const native=Boolean(window.__JUCE__?.backend?.emitEvent&&window.__JUCE__?.backend?.addEventListener);
 const playerReady=()=>source('playerPlay')?.dataset.playing!==undefined;
 const source=id=>$(id);
 const click=id=>{const el=source(id);if(el&&!el.disabled)el.click();};
 const mainMix=document.createElement('button');mainMix.type='button';mainMix.className='tool';mainMix.id='ribbonMainMix';mainMix.textContent='Return to Main';mainMix.hidden=true;
 bar.insertBefore(mainMix,$('mixContext')||null);
 mainMix.addEventListener('click',()=>{if($('sofToggle')?.getAttribute('aria-pressed')==='true')click('sofToggle');});
 function syncMix(){const sof=$('sofToggle');mainMix.hidden=sof?.getAttribute('aria-pressed')!=='true';mainMix.disabled=!sof||sof.disabled;}
 if($('sofToggle'))new MutationObserver(syncMix).observe($('sofToggle'),{attributes:true,attributeFilter:['aria-pressed','disabled']});
 syncMix();
 const openPlayer=()=>{if($('arsonPlayer')?.hidden)click('playerToggle');};
 const mappings={ribbonPrevious:'playerPrevious',ribbonPlay:'playerPlay',ribbonNext:'playerNext',ribbonStop:'playerStop',ribbonLoop:'playerLoopAB'};
 Object.entries(mappings).forEach(([id,target])=>$(id).addEventListener('click',()=>click(target)));
 $('ribbonMode').addEventListener('click',()=>{openPlayer();click('playerAutoNext');});
 $('ribbonQueue').addEventListener('click',()=>{openPlayer();click('playerQueueView');});
 $('ribbonAdd').addEventListener('click',()=>click('playerAdd'));
 const findShow=label=>Array.from(more.querySelectorAll('button')).find(e=>e.textContent.trim()===label);
 for(const [id,label] of [['ribbonShowSave','Save Show'],['ribbonShowBrowse','Browse Shows']])$(id).addEventListener('click',()=>{const el=findShow(label);if(el&&!el.disabled){more.open=true;compactMenu?.show('scenes');el.click();}});
 const controls=new Map();
 function range(proxyId,originalId,max){
   const proxy=$(proxyId),original=source(originalId);if(!original)return;
   let armed=false;
   const event=type=>original.dispatchEvent(new Event(type,{bubbles:true}));
   const paint=()=>proxy.style.setProperty('--ribbon-fill',(Number(proxy.value)/max*100)+'%');
   function cancel(){if(armed){armed=false;event('pointercancel');}sync();}
   function sync(){if(!armed){const v=original.value;if(proxy.value!==v)proxy.value=v;paint();}proxy.disabled=!native||!playerReady()||original.disabled;}
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
   for(const [id,target] of Object.entries(mappings)){const el=source(target);$(id).disabled=!native||!playerReady()||!el||el.disabled;}
   $('ribbonMode').disabled=!native||!playerReady()||!source('playerAutoNext')||source('playerAutoNext').disabled;
   $('ribbonQueue').disabled=!playerReady()||!source('playerQueueView')||source('playerQueueView').disabled;
   $('ribbonAdd').disabled=!native||!source('playerAdd')||source('playerAdd').disabled;
   $('ribbonLoop').setAttribute('aria-pressed',source('playerLoopAB')?.getAttribute('aria-pressed')||'false');
   $('ribbonMode').setAttribute('aria-pressed',source('playerAutoNext')?.getAttribute('aria-pressed')||'false');
   const title=source('playerNow')?.textContent||'No music loaded';setText($('ribbonTrack'),title);$('ribbonTrack').title=title;
   setText($('ribbonTime'),(source('playerElapsed')?.textContent||'00:00')+' / '+(source('playerDuration')?.textContent||'00:00'));
   controls.forEach(c=>c.sync());
 }
 function schedule(){if(active!=='player'||queued)return;queued=true;queueMicrotask(()=>{queued=false;syncPlayer();});}
 const observed=['playerPlay','playerPrevious','playerNext','playerStop','playerLoopAB','playerAutoNext','playerAdd','playerQueueView','playerNow','playerElapsed','playerDuration','playerSeek','playerVolume'];
 const playerObserver=new MutationObserver(schedule);
 for(const id of observed){const el=source(id);if(el)playerObserver.observe(el,{attributes:true,attributeFilter:['disabled','aria-pressed','data-playing','style'],childList:true,characterData:true,subtree:true});}
 // Presentation follows the already accepted Player DOM; no second state stream.
 const group={
   bankPrev:'mixer',bankSelect:'mixer',bankNext:'mixer',densitySelect:'mixer',
   pluginsButton:'mixer fx',gainView:'mixer',fxView:'mixer fx',globalFxToggle:'mixer fx',
   sofToggle:'mixer routing',sofTarget:'mixer routing',clearCue:'mixer routing',
   wireNavigatorToggle:'routing',wireMatrixToggle:'routing',wireConfigToggle:'routing',
   sceneSelect:'scenes',saveScene:'scenes',recallScene:'scenes',addTrack:'mixer routing',
   playerMini:'mixer',playerToggle:'all',moreTools:'all',mixContext:'all'
 };
 function orderCommands(){
   // Maintain a single native command per action. DOM order also defines keyboard order.
   const io=bar.querySelector(':scope > [data-native-io]');
   const preferred=[more,$('bankPrev'),$('bankSelect'),$('bankNext'),$('densitySelect'),
     bar.querySelector(':scope > .strip-view-switch'),$('globalFxToggle'),$('wireNavigatorToggle'),
     $('wireMatrixToggle'),$('wireConfigToggle'),$('sofToggle'),$('sofTarget'),$('clearCue'),
     $('sceneSelect'),$('recallScene'),$('saveScene'),scene,$('addTrack'),$('pluginsButton'),
     player,$('playerToggle'),$('playerMini'),io,$('ribbonMainMix')].filter(el=>el?.parentElement===bar);
   const known=new Set(preferred),context=$('mixContext');
   const wanted=[...preferred,...Array.from(bar.children).filter(el=>!known.has(el)&&el!==context)];
   if(context?.parentElement===bar)wanted.push(context);
   // Move only when needed; a second child-list callback is a no-op.
   let cursor=bar.firstElementChild;
   for(const el of wanted){if(el===cursor)cursor=cursor.nextElementSibling;else bar.insertBefore(el,cursor);}
 }
 function apply(){
   orderCommands();
   for(const el of bar.children){
     if(el===player||el===scene)continue;
     const scope=el.classList.contains('strip-view-switch')?'mixer fx':el.matches('[data-native-io]')?'mixer io':group[el.id]||'all';
     el.dataset.ribbonHidden=String(scope!=='all'&&!scope.split(' ').includes(active));
   }
   player.hidden=active!=='player';scene.hidden=active!=='scenes'||!native;
   const trigger=$('playerToggle');if(trigger){const label=active==='player'?'Open Player':'♫ Player';if(trigger.dataset.ribbonLabel!==label){trigger.dataset.ribbonLabel=label;trigger.replaceChildren();if(active==='player'){trigger.insertAdjacentHTML('beforeend',icon('<path d="M14 3h7v7m0-7-11 11M21 14v7H3V3h7"/>'));trigger.firstElementChild.classList.add('armix-ribbon-open-icon');}trigger.append(document.createTextNode(label));}}
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
