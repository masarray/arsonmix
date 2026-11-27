// SPDX-License-Identifier: AGPL-3.0-only
// V6 presentation only: reuse the current Player controls and native ACK/readback.
// No second player, no audio element, no polling, no model or driver mutation.
(()=>{
'use strict';
const $=id=>document.getElementById(id);
const icon=(path,fill=false)=>'<svg viewBox="0 0 24 24" aria-hidden="true" '+(fill?'fill="currentColor"':'fill="none" stroke="currentColor" stroke-width="1.65" stroke-linecap="round" stroke-linejoin="round"')+'>'+path+'</svg>';
const play=icon('<path d="M6 5Q6 4 7 4.5L18 11Q19.5 12 18 13L7 19.5Q6 20 6 19Z"/>',true);
const pause=icon('<rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/>',true);
const button=(id,label,glyph)=>'<button type="button" id="'+id+'" title="'+label+'" aria-label="'+label+'">'+glyph+'</button>';
// One disclosure and one cascade per category. Original command nodes and
// native listeners are retained; category navigation never emits audio commands.
function installCompactMenu(more){
 const panel=more.querySelector('.more-tools-panel');
 if(!panel)return null;
 const glyphs={
 session:'<path d="M5 3h10l4 4v14H5zM14 3v5h5M8 12h8m-8 4h8"/>',
 channel:'<path d="M5 3v18M12 3v18M19 3v18M3 9h4m3 7h4m3-9h4"/>',
 view:'<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
 scenes:'<path d="m12 3 10 5-10 5L2 8zM2 12l10 5 10-5M2 16l10 5 10-5"/>',
 settings:'<circle cx="12" cy="12" r="3"/><path d="m12 2 2 3 3-.1 1.5 2.6-1.4 2.7 1.4 2.8 3 .9-1 3-3 .5-2 2.5-3-1-3 1-2-2.5-3-.5-1-3 3-.9L7 10 5.5 7.5 7 5l3 .1z"/>'
 };
 const groups=[['session','Session'],['channel','Channel'],['view','View'],['scenes','Scenes & Shows'],['settings','Settings']];
 const nav=new Map(),flyouts=new Map();
 let page='root',hoverTimer=0;
 const cancelHover=()=>{window.clearTimeout(hoverTimer);hoverTimer=0;};
 const channelIds=new Set(['addTrack','addBus','addFx','addGroupBus','addGroupSelected','convertSelectedReverb','copy','paste','group','ungroup']);
 const viewIds=new Set(['mode','viewFilter','ribbonFilterMenu','preview','reset']);
 const sceneIds=new Set(['sceneSafe','sceneSelect','saveScene','recallScene']);
 const headings=new Map([['PROJECT · MIXER SESSION','root'],['CHANNEL SETUP','channel'],['VIEW & SELECTION','channel'],['WIRE TOOLS','settings'],['REFERENCE TOOLS','view'],['AUDIO / SAFETY','settings'],['STUDIO / VERIFIED SHOWS','scenes']]);
 const summary=more.querySelector(':scope > summary');
 for(const [id,label] of groups){
   const row=document.createElement('button');row.type='button';row.className='armix-menu-category';row.dataset.menuCategory=id;
   row.innerHTML=icon(glyphs[id])+'<span>'+label+'</span>'+icon('<path d="m9 5 7 7-7 7"/>');
   const child=document.createElement('div');child.className='armix-menu-flyout';child.id='armix-menu-'+id;child.hidden=true;
   child.setAttribute('role','group');child.setAttribute('aria-label',label);
   row.setAttribute('aria-controls',child.id);row.setAttribute('aria-expanded','false');
   row.addEventListener('click',()=>show(id,true));
   row.addEventListener('pointerenter',e=>{
     if(e.pointerType!=='mouse'||!more.open||row.hidden)return;
     cancelHover();
     if(page==='root'||page===id)show(id);
     else hoverTimer=window.setTimeout(()=>{if(more.open)show(id);},120);
   });
   child.addEventListener('pointerenter',cancelHover);
   nav.set(id,row);flyouts.set(id,child);panel.append(row,child);
 }
 function classify(){
   let section='view';
   // Headings remain inert direct children as section markers for late
   // native insertions. Reparent each command at most once, never clone it.
   for(const el of Array.from(panel.children)){
     if(el.matches('.armix-menu-category,.armix-menu-flyout'))continue;
     const label=el.textContent.trim();
     if(el.matches('.more-tools-caption')&&headings.has(label)){
       section=headings.get(label);el.dataset.armixMenuHeading='true';el.dataset.armixMenuActive='false';continue;
     }
     let group=el.dataset.armixMenuPage||section;
     if(el.matches('.mixer-file-tool,.reference-file-menu,[data-native-emergency-output]'))group='root';
     else if(channelIds.has(el.id))group='channel';
     else if(viewIds.has(el.id))group='view';
     else if(sceneIds.has(el.id))group='scenes';
     else if(el.matches('[data-wire-open]'))group='settings';
     el.dataset.armixMenuPage=group;el.dataset.armixMenuActive='true';
     if(flyouts.has(group))flyouts.get(group).append(el);
   }
   for(const [id,row] of nav){
     const child=flyouts.get(id);
     row.hidden=!Array.from(child.children).some(el=>!el.hidden);
     if(row.hidden&&page===id)show('root');
   }
 }
 function position(){
   if(!more.open||page==='root')return;
   const child=flyouts.get(page),row=nav.get(page);
   if(!child||!row||child.hidden)return;
   const root=panel.getBoundingClientRect(),anchor=row.getBoundingClientRect();
   const top=Math.max(0,Math.min(anchor.top-root.top,window.innerHeight-root.top-child.offsetHeight-6));
   child.style.top=top+'px';
 }
 const focusables=host=>Array.from(host.querySelectorAll('button:not(:disabled),select:not(:disabled),input:not(:disabled),summary,a[href]')).filter(el=>el.getClientRects().length);
 function show(id,focus=false){
   cancelHover();
   const previous=page;page=flyouts.has(id)?id:'root';
   for(const [key,row] of nav){
     const active=key===page;row.setAttribute('aria-expanded',String(active));flyouts.get(key).hidden=!active;
   }
   panel.dataset.armixMenuPage=page;
   position();
   if(focus){
     if(page==='root')nav.get(previous)?.focus({preventScroll:true});
     else (focusables(flyouts.get(page))[0]||nav.get(page))?.focus({preventScroll:true});
   }
 }
 more.addEventListener('toggle',()=>{if(!more.open)show('root');else{position();if(!more.contains(document.activeElement))summary?.focus({preventScroll:true});}});
 document.addEventListener('keydown',e=>{
   if(!more.open)return;
   if(e.key!=='Escape'&&!more.contains(e.target))return;
   const child=e.target.closest?.('.armix-menu-flyout'),category=e.target.closest?.('.armix-menu-category');
   if(e.key==='Escape'||(e.key==='ArrowLeft'&&child&&!e.target.matches('select,input'))){
     e.preventDefault();e.stopPropagation();
     if(page!=='root')show('root',true);else{more.open=false;summary?.focus();}
   }else if(e.key==='ArrowRight'&&category){
     e.preventDefault();show(category.dataset.menuCategory,true);
   }else if((e.key==='ArrowDown'||e.key==='ArrowUp')&&!e.target.matches('select,input')){
     const items=child?focusables(child):focusables(panel).filter(el=>!el.closest('.armix-menu-flyout'));
     if(!items.length)return;
     const at=items.indexOf(e.target),next=at<0?(e.key==='ArrowDown'?0:items.length-1):(at+(e.key==='ArrowDown'?1:items.length-1))%items.length;
     e.preventDefault();items[next].focus({preventScroll:true});
   }
 },true);
 document.addEventListener('pointerdown',e=>{if(more.open&&!more.contains(e.target))more.open=false;},true);
 window.addEventListener('resize',position);
 // A Show list/readback may add rows after opening. Observe only the five
 // small popups (not the mixer), so the active flyout stays inside the window.
 if(typeof ResizeObserver==='function'){
   const sizes=new ResizeObserver(position);
   for(const child of flyouts.values())sizes.observe(child);
 }
 // Only direct insertions: native bridge installs commands at startup.
 // A second mutation after moving nodes is a no-op.
 new MutationObserver(classify).observe(panel,{childList:true});
 classify();show('root');
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
 summary.innerHTML=shape('menu',151,'armix-ribbon-menu-shape')+'<svg class="armix-ribbon-menu-icon" viewBox="0 0 24 24" aria-hidden="true"><path class="armix-menu-icon-closed" d="M4 6h16M4 12h16M4 18h16"/><path class="armix-menu-icon-open" d="m6 15 6-6 6 6" stroke-linecap="round" stroke-linejoin="round"/></svg><span>ArSon<span class="armix-menu-brand-accent">Mix</span></span>';
 summary.setAttribute('aria-label','ArSonMix application menu');
 const compactMenu=installCompactMenu(more);
 const filter=$('viewFilter');
 if(filter){
   const jump=document.createElement('button');jump.type='button';jump.id='ribbonFilterMenu';jump.className='tool';jump.textContent='Channel filter…';
   jump.addEventListener('click',()=>{more.open=false;select('mixer');filter.focus();});
   filter.before(jump);jump.dataset.armixMenuPage='view';bar.append(filter);
   delete filter.dataset.armixMenuActive;delete filter.dataset.armixMenuPage;
 }
 const navTools=document.createElement('div');navTools.className='armix-ribbon-nav-tools';
 navTools.innerHTML='<span class="armix-ribbon-nav-status" id="ribbonPlayerStatus"></span>'+button('ribbonQuickPlayer','Open Player',icon('<path d="M9 18V5l11-2v13M9 9l11-2"/><ellipse cx="6" cy="18" rx="3" ry="2"/><ellipse cx="17" cy="16" rx="3" ry="2"/>'));
 header.append(navTools);
 // Exact V6 right-hand caption: independent from the tablist, no web fullscreen
 // or browser close simulation. Only the native host can admit these actions.
 const caption=document.createElement('div');caption.className='armix-ribbon-window-controls';
 caption.setAttribute('role','group');caption.setAttribute('aria-label','Window controls');
 const maxGlyph=icon('<rect x="5" y="5" width="14" height="14" rx=".5"/>');
 const restoreGlyph=icon('<path d="M9 5h10v10h-4"/><rect x="5" y="9" width="10" height="10" rx=".5"/>');
 caption.innerHTML=button('ribbonWinMin','Minimize',icon('<path d="M5 12h14"/>'))+
   button('ribbonWinMax','Maximize',maxGlyph)+button('ribbonWinClose','Close',icon('<path d="m6 6 12 12M18 6 6 18"/>'));
 header.append(caption);
 const windowBackend=window.__JUCE__?.backend;
 const windowSend=operation=>windowBackend?.emitEvent?.('arMixRibbonWindow',{operation});
 let windowAdmitted=false,windowMaximised=false;
 for(const b of caption.querySelectorAll('button'))b.disabled=true;
 if(windowBackend?.emitEvent&&windowBackend?.addEventListener){
   windowBackend.addEventListener('arMixRibbonWindowState',state=>{
     if(state?.active!==true||typeof state.maximised!=='boolean')return;
     windowAdmitted=true;windowMaximised=state.maximised;
     for(const b of caption.querySelectorAll('button'))b.disabled=false;
     const max=$('ribbonWinMax'),label=windowMaximised?'Restore':'Maximize';
     if(max.title!==label){max.innerHTML=windowMaximised?restoreGlyph:maxGlyph;max.title=label;max.setAttribute('aria-label',label);}
   });
   for(const [id,operation] of [['ribbonWinMin','MINIMISE'],['ribbonWinMax','TOGGLE_MAXIMISE'],['ribbonWinClose','CLOSE']])
     $(id).addEventListener('click',()=>{if(windowAdmitted)windowSend(operation);});
   // Blank space only; tabs, quick Player and caption never initiate drag.
   let start=null;
   tabs.addEventListener('pointerdown',e=>{if(windowAdmitted&&!windowMaximised&&e.target===tabs&&e.button===0)start={x:e.clientX,y:e.clientY};});
   tabs.addEventListener('pointermove',e=>{if(!start)return;if(!(e.buttons&1)){start=null;return;}
     if(Math.hypot(e.clientX-start.x,e.clientY-start.y)>=5){start=null;windowSend('BEGIN_MOVE');}});
   for(const event of ['pointerup','pointercancel','pointerleave'])tabs.addEventListener(event,()=>{start=null;});
   tabs.addEventListener('dblclick',e=>{if(windowAdmitted&&e.target===tabs)windowSend('TOGGLE_MAXIMISE');});
   windowSend('READY');
 } else {
   for(const b of caption.querySelectorAll('button'))b.title+=' (desktop application only)';
 }
 const player=document.createElement('div');player.className='armix-ribbon-player';player.hidden=true;player.setAttribute('role','group');player.setAttribute('aria-label','Player transport');
 player.innerHTML='<span class="armix-ribbon-track" id="ribbonTrack">No music loaded</span><input type="range" id="ribbonSeek" min="0" max="1000" step="1" value="0" aria-label="Seek through current song"><span class="armix-ribbon-time" id="ribbonTime">00:00 / 00:00</span><div class="armix-ribbon-transport">'+button('ribbonPrevious','Previous',icon('<rect x="4" y="5" width="3" height="14" rx="1"/><path d="M20 5 9 12l11 7z"/>',true))+button('ribbonPlay','Play',play)+button('ribbonNext','Next',icon('<rect x="17" y="5" width="3" height="14" rx="1"/><path d="M4 5l11 7-11 7z"/>',true))+button('ribbonStop','Stop',icon('<rect x="5" y="5" width="14" height="14" rx="2"/>',true))+'</div><div class="armix-ribbon-options">'+button('ribbonLoop','Loop A–B',icon('<path d="m17 3 4 4-4 4m4-4H8a5 5 0 0 0-5 5m4 9-4-4 4-4m-4 4h13a5 5 0 0 0 5-5"/>'))+button('ribbonMode','Playback mode / repeat',icon('<path d="m17 3 4 4-4 4m4-4H8a5 5 0 0 0-5 5m4 9-4-4 4-4m-4 4h13a5 5 0 0 0 5-5"/><path d="M10 12h4"/>'))+button('ribbonQueue','Queue',icon('<path d="M3 5h17M3 10h17M3 15h10m3 0 5 3-5 3z"/>'))+'</div><label class="armix-ribbon-volume">'+icon('<path d="M3 9v6h4l5 4V5L7 9H3zm13 0a4 4 0 0 1 0 6m3-9a8 8 0 0 1 0 12"/>')+'<input type="range" id="ribbonVolume" min="0" max="100" step="1" value="50" aria-label="Player level before channel FX"></label>';
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
 // Exiting SEND MIX is local navigation and cannot require native SEND
 // admission. A temporarily disabled entry control must not trap the user.
 mainMix.addEventListener('click',()=>{
   const scope=window.__ARMIX_NATIVE_SCOPE__;
   if(scope?.sofEnabled&&typeof scope.toggleSof==='function')scope.toggleSof(false);
   else if($('sofToggle')?.getAttribute('aria-pressed')==='true')click('sofToggle');
 });
 function syncMix(){
   const sof=$('sofToggle'),scope=window.__ARMIX_NATIVE_SCOPE__;
   mainMix.hidden=sof?.getAttribute('aria-pressed')!=='true';
   mainMix.disabled=mainMix.hidden||(!scope?.toggleSof&&(!sof||sof.disabled));
 }
 if($('sofToggle'))new MutationObserver(syncMix).observe($('sofToggle'),{attributes:true,attributeFilter:['aria-pressed','disabled']});
 syncMix();
 const openPlayer=()=>click('playerToggle');
 $('ribbonQuickPlayer').addEventListener('click',openPlayer);
 function syncQuickPlayer(){
   const status=$('ribbonPlayerStatus'),playing=source('playerPlay')?.dataset.playing==='true';
   const value=playing?'Playing':'Player';
   if(status.textContent!==value)status.textContent=value;
   status.classList.toggle('is-playing',playing);
   $('ribbonQuickPlayer').disabled=!source('playerToggle')||source('playerToggle').disabled;
 }
 const panicShortcut=document.createElement('button');panicShortcut.type='button';panicShortcut.id='ribbonEmergency';panicShortcut.className='tool';panicShortcut.disabled=true;
 panicShortcut.innerHTML=icon('<circle cx="12" cy="12" r="9"/><path d="M12 7v6m0 4h.01"/>');
 panicShortcut.title='Emergency stop audio';panicShortcut.setAttribute('aria-label',panicShortcut.title);
 bar.append(panicShortcut);
 const emergency=()=>more.querySelector('[data-native-emergency-output]');
 function syncEmergency(){
   const action=emergency(),mode=action?.dataset.emergencyMode;
   panicShortcut.hidden=!native||!action;
   panicShortcut.disabled=!action||action.disabled||mode!=='stop';
   panicShortcut.title=mode==='resume'?'Output stopped · resume from ArSonMix menu':'Emergency stop audio';
   panicShortcut.setAttribute('aria-label',panicShortcut.title);
   panicShortcut.classList.toggle('is-stopped',mode==='resume');
 }
 panicShortcut.addEventListener('click',()=>{const action=emergency();if(action?.dataset.emergencyMode==='stop'&&!action.disabled)action.click();});
 const emergencyObserver=new MutationObserver(syncEmergency);
 let watchedEmergency=null;
 function bindEmergency(){const action=emergency();if(action!==watchedEmergency){emergencyObserver.disconnect();watchedEmergency=action;if(action)emergencyObserver.observe(action,{attributes:true,attributeFilter:['disabled','data-emergency-mode']});}syncEmergency();}
 new MutationObserver(bindEmergency).observe(more.querySelector('.more-tools-panel'),{childList:true});
 bindEmergency();
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
 function schedule(){syncQuickPlayer();if(active!=='player'||queued)return;queued=true;queueMicrotask(()=>{queued=false;syncPlayer();});}
 const observed=['playerPlay','playerPrevious','playerNext','playerStop','playerLoopAB','playerAutoNext','playerAdd','playerQueueView','playerNow','playerElapsed','playerDuration','playerSeek','playerVolume'];
 const playerObserver=new MutationObserver(schedule);
 for(const id of observed){const el=source(id);if(el)playerObserver.observe(el,{attributes:true,attributeFilter:['disabled','aria-pressed','data-playing','style'],childList:true,characterData:true,subtree:true});}
 // Presentation follows the already accepted Player DOM; no second state stream.
 const group={
   bankPrev:'mixer',bankSelect:'mixer',bankNext:'mixer',densitySelect:'mixer',viewFilter:'mixer',
   pluginsButton:'mixer fx',gainView:'mixer',fxView:'mixer',globalFxToggle:'mixer fx',
   sofToggle:'mixer routing',sofTarget:'mixer routing',clearCue:'mixer routing',
   wireNavigatorToggle:'routing',wireMatrixToggle:'routing',wireConfigToggle:'routing',
   sceneSelect:'scenes',saveScene:'scenes',recallScene:'scenes',addTrack:'mixer routing',
   playerMini:'mixer',playerToggle:'player',moreTools:'all',mixContext:'all',ribbonEmergency:'all'
 };
 function orderCommands(){
   // Maintain a single native command per action. DOM order also defines keyboard order.
   const io=bar.querySelector(':scope > [data-native-io]');
   const preferred=[more,$('bankPrev'),$('bankSelect'),$('bankNext'),$('densitySelect'),
     bar.querySelector(':scope > .strip-view-switch'),$('globalFxToggle'),$('wireNavigatorToggle'),
     $('wireMatrixToggle'),$('wireConfigToggle'),$('sofToggle'),$('sofTarget'),$('clearCue'),$('viewFilter'),
     $('sceneSelect'),$('recallScene'),$('saveScene'),scene,$('addTrack'),$('pluginsButton'),
     player,$('playerToggle'),$('playerMini'),bar.querySelector(':scope > [data-ribbon-audio-summary]'),io,$('ribbonMainMix')].filter(el=>el?.parentElement===bar);
   const known=new Set([...preferred,panicShortcut]),context=$('mixContext');
   const wanted=[...preferred,...Array.from(bar.children).filter(el=>!known.has(el)&&el!==context)];
   if(context?.parentElement===bar)wanted.push(context);
   wanted.push(panicShortcut);
   // Move only when needed; a second child-list callback is a no-op.
   let cursor=bar.firstElementChild;
   for(const el of wanted){if(el===cursor)cursor=cursor.nextElementSibling;else bar.insertBefore(el,cursor);}
 }
 function apply(){
   orderCommands();
   for(const el of bar.children){
     if(el===player||el===scene)continue;
     const scope=el.classList.contains('strip-view-switch')?'mixer':el.matches('[data-native-io],[data-ribbon-audio-summary]')?'io':group[el.id]||'all';
     el.dataset.ribbonHidden=String(scope!=='all'&&!scope.split(' ').includes(active));
   }
   player.hidden=active!=='player';scene.hidden=active!=='scenes'||!native;
   syncQuickPlayer();bindEmergency();
   const trigger=$('playerToggle');if(trigger){const label='Open Player';if(trigger.dataset.ribbonLabel!==label){trigger.dataset.ribbonLabel=label;trigger.replaceChildren();{trigger.insertAdjacentHTML('beforeend',icon('<path d="M14 3h7v7m0-7-11 11M21 14v7H3V3h7"/>'));trigger.firstElementChild.classList.add('armix-ribbon-open-icon');}trigger.append(document.createTextNode(label));}}
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
