'use strict';
// AR MIX Studio UI-only demonstration. No audio is mixed or routed here.
const colors=['#1699cd','#b92e41','#cf6b1e','#b28c27','#34a544','#168762','#0e95c9','#066fa9','#7140b5','#b52893','#c32974','#586889'];
const raw=[
 ['Lead Vocal','♩',['Gate','EQ','Compressor','Reverb'],-.6,0],
 ['Mic 2','♩',['EQ','Compressor','De-Esser'],-4.7,-15],
 ['Music L','♫',['EQ','Compressor'],-10.3,0],
 ['Music R','♫',['EQ','Compressor'],-6.1,0],
 ['Guest Mic','♩',['Gate','EQ','Compressor'],-8.8,10],
 ['Stream PC','▥',['EQ','Limiter'],-12.4,0],
 ['Backing','♫',['EQ','Reverb'],-3.6,-25],
 ['Guitar','♬',['EQ','Compressor','Delay'],-7.9,30],
 ['Keyboard','▥',['EQ','Reverb'],-14.2,0],
 ['FX Return','◎',['Reverb'],-5.1,0]
];
const channels=raw.map((a,i)=>({id:i+1,name:a[0],icon:a[1],color:colors[i],inserts:[...a[2]],fader:a[3],pan:a[4],trim:0,mute:false,solo:false,pre:false,hpf:i%4!==0}));
let currentChannel=0, masterSelected=false, view='MIX', sceneIdx=0, timer=0;
const scenes=['Vocal Live','Karaoke Warm','Clean Voice','Studio Session'];
const $=id=>document.getElementById(id);
const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
const levelToTop=db=>+(clamp((10-db)/70,0,1)*100).toFixed(3);
const fmt=db=>db===-Infinity?'−∞':`${db>0?'+':''}${db.toFixed(1)} dB`;
// UI side throttling is distinct from the native realtime control plane.
const pendingUiParams=new Map();let uiFlushScheduled=false;
const peakHold=Object.create(null),clipLatch=Object.create(null);
function resetClip(key){clipLatch[key]=false;document.querySelector(`[data-clip="${key}"]`)?.classList.remove('latched')}
function syncClipLeds(){document.querySelectorAll('[data-clip]').forEach(e=>e.classList.toggle('latched',!!clipLatch[e.dataset.clip]))}
// Cache per-meter nodes across preview frames, rebuilding on mixer re-render.
// Styling a full-height bar with a compositor transform avoids 14 cyclic height
// reflows per meter tick. No native meter or DSP data path is affected.
const meterNodes=new Map();
// Shared preview/native painter: preserve fixed colors; light only whole 7px LEDs.
const LED_PITCH_PX=7,ledMeterCache=new WeakMap();
function setLedMeterLevel(el,rawLevel){
 if(!el)return;
 const level=clamp(Number(rawLevel)||0,0,100);
 let state=ledMeterCache.get(el);
 if(!state){state={height:el.getBoundingClientRect().height,level:-1,lit:-1};ledMeterCache.set(el,state)}
 if(state.level!==level){state.level=level;el.style.setProperty('--level',level+'%')}
 const lit=level>=100?state.height:Math.floor((level*state.height/100+1e-7)/LED_PITCH_PX)*LED_PITCH_PX;
 if(state.lit!==lit){state.lit=lit;el.style.setProperty('--led-lit',lit+'px')}
}
window.__arMixSetLedMeterLevel=setLedMeterLevel;
// Observe only the stable mixer root, never disposable strips; no hot-path reflow.
if(typeof ResizeObserver==='function'){
 const ledResizeObserver=new ResizeObserver(()=>{
  document.querySelectorAll('#mixer [data-meter]').forEach(el=>{
   const state=ledMeterCache.get(el);if(!state)return;
   const height=el.getBoundingClientRect().height;
   if(height===state.height)return;
   state.height=height;state.lit=-1;setLedMeterLevel(el,state.level);
  });
 });
 const mixerRoot=document.getElementById('mixer');
 if(mixerRoot)ledResizeObserver.observe(mixerRoot);
}
function meterNode(key){
 const cached=meterNodes.get(key);
 if(cached?.fill.isConnected)return cached;
 const fill=document.querySelector(`[data-meter="${key}"]`);
 if(!fill)return null;
 const slot=fill.closest('.meter');
 const entry={fill,peak:slot?.querySelector('.meter-peak'),
              clip:slot?.querySelector('.clip-led'),level:-1,peakValue:-1};
 // DOM replacement must not erase a held peak or a latched CLIP lamp.
 const held=peakHold[key];
 if(held&&entry.peak){
   entry.peakValue=Math.round(held.value*10);
   entry.peak.style.bottom=(entry.peakValue/10)+'%';
 }
 if(clipLatch[key])entry.clip?.classList.add('latched');
 meterNodes.set(key,entry);
 return entry;
}
function updateMeterVisual(key,level,now){
 const meter=meterNode(key);if(!meter)return;
 const value=clamp(level,0,99);
 const levelStep=Math.round(value*10);
 if(meter.level!==levelStep){
   meter.level=levelStep;
   setLedMeterLevel(meter.fill,levelStep/10);
 }
 const previous=peakHold[key];
 if(!previous||value>=previous.value||now>=previous.until){
   peakHold[key]={value,until:now+1600};
   const step=Math.round(value*10);
   if(meter.peak&&meter.peakValue!==step){
     meter.peakValue=step;
     meter.peak.style.bottom=(step/10)+'%';
   }
 }
 if(value>=97&&!clipLatch[key]){
   clipLatch[key]=true;
   meter.clip?.classList.add('latched');
 }
}

function flushUiParams(){uiFlushScheduled=false;const bridge=window.__JUCE__?.backend;if(bridge?.emitEvent)for(const event of pendingUiParams.values())bridge.emitEvent('arMixParameter',event);pendingUiParams.clear()}
function send(param,value,ch=currentChannel){
 const event={channel:ch+1,parameter:param,value};
 if(['faderDb','pan','trimDb','phonesDb','masterGain'].includes(param)){
   pendingUiParams.set(`${ch}:${param}`,event);
   if(!uiFlushScheduled){uiFlushScheduled=true;requestAnimationFrame(flushUiParams)}
 }else{flushUiParams();const bridge=window.__JUCE__?.backend;if(bridge?.emitEvent)bridge.emitEvent('arMixParameter',event)}
}
function flash(message){const x=$('toast');x.textContent=message;x.classList.add('show');clearTimeout(timer);timer=setTimeout(()=>x.classList.remove('show'),1500)}
function createSide(){
 const labels=channels.map(c=>c.name);
 $('channelList').innerHTML=labels.map((label,i)=>`<div class="list-item ${i===currentChannel?'selected':''}" data-choose="${i}"><span class="swatch" style="background:${channels[i].color||colors[i%colors.length]}"></span><span class="index">${i+1}</span><span>${safeText(label)}</span></div>`).join('');
 $('groupsList').innerHTML=['Drums','Band','Vocals','FX','All Music','All Vocals'].map((x,i)=>`<div class="list-item" data-group="${i}"><span class="swatch" style="background:${colors[(i+4)%12]}"></span><span class="index">${i+1}</span><span>${x}</span></div>`).join('');
 $('scenesList').innerHTML=scenes.map((x,i)=>`<div class="list-item ${i===sceneIdx?'selected':''}" data-scene="${i}"><span class="swatch" style="background:${i===sceneIdx?'#23bce9':'#283d4a'};width:8px;height:14px"></span>${x}</div>`).join('');
 $('currentScene').innerHTML=`Snapshot: <b>${scenes[sceneIdx]}</b><span>▾</span>`;
}
function knobMarkup(param,ch,value,label,small=false){let angle=(param==='pan'?value/100*130:value/24*130);return `<button class="knob ${small?'small':''}" data-knob="${param}" data-ch="${ch}" data-value="${value}" style="--angle:${angle}deg" title="Drag vertically or scroll to change ${label}" aria-label="${label}"></button>`}
function panText(v){return v===0?'C':(v<0?'L':'R')+' '+Math.abs(v)}
function panMarkup(ch,i){return `<div class="pan-track" data-pan="${i}" role="slider" tabindex="0" aria-orientation="horizontal" aria-label="${ch.name} pan" aria-valuemin="-100" aria-valuemax="100" aria-valuenow="${ch.pan}" aria-valuetext="${panText(ch.pan)}" title="Drag left/right · double-click to center" style="--pan-frac:${(ch.pan+100)/200};--pan-left:${Math.min(.5,(ch.pan+100)/200)};--pan-span:${Math.abs(ch.pan)/200}"><span class="pan-fill" aria-hidden="true"></span><span class="pan-thumb" aria-hidden="true"></span></div>`}

const channelIcons=['♩','♫','♬','🎙','🎸','🎹','🥁','🎧','▥','◉','◎','⊞'];
const channelPalette=['#1699cd','#b92e41','#cf6b1e','#b28c27','#34a544','#168762','#0e95c9','#066fa9','#7140b5','#b52893','#c32974','#586889'];
let editingChannel=-1,editingColor='',editingIcon='';
// Position the lightweight popover at the strip name the user actually clicked.
// Clamp the panel to the visible viewport without breaking its anchor/arrow.
function positionChannelIdentity(){
 const modal=$('channelIdentity');if(editingChannel<0||!modal||modal.classList.contains('hidden'))return;
 const anchor=document.querySelector('[data-editstrip="'+editingChannel+'"]');
 const panel=modal.querySelector('.identity-panel');if(!anchor||!panel)return;
 const target=anchor.getBoundingClientRect(),pop=panel.getBoundingClientRect();
 const left=clamp(target.left+target.width/2-pop.width/2,10,Math.max(10,innerWidth-pop.width-10));
 const top=Math.max(10,target.top-pop.height-12);
 panel.style.left=left+'px';panel.style.top=top+'px';
 panel.style.setProperty('--identity-pointer-x',clamp(target.left+target.width/2-left,18,pop.width-18)+'px');
}
function openChannelIdentity(index){
 if(!Number.isInteger(index)||index<0||index>=channels.length)return;
 editingChannel=index;editingColor=channels[index].color||colors[index];editingIcon=channels[index].icon;
 const modal=$('channelIdentity');if(!modal)return;
 $('identityIndex').textContent=String(index+1).padStart(2,'0');
 $('identityName').value=channels[index].name;
 $('identityIcons').innerHTML=channelIcons.map(icon=>`<button type="button" class="identity-icon ${icon===editingIcon?'selected':''}" data-icon-choice="${safeText(icon)}" aria-pressed="${icon===editingIcon}">${safeText(icon)}</button>`).join('');
 $('identityColors').innerHTML=channelPalette.map(color=>`<button type="button" class="identity-color ${color===editingColor?'selected':''}" data-color-choice="${color}" style="--choice:${color}" aria-label="Channel color ${color}" aria-pressed="${color===editingColor}"></button>`).join('');
 modal.classList.remove('hidden');
 document.querySelectorAll('.strip.identity-editing').forEach(el=>el.classList.remove('identity-editing'));
 document.querySelector('[data-strip="'+index+'"]')?.classList.add('identity-editing');
 positionChannelIdentity();
 $('identityName').focus();$('identityName').select();
}
function closeChannelIdentity(){
 editingChannel=-1;$('channelIdentity')?.classList.add('hidden');
 document.querySelectorAll('.strip.identity-editing').forEach(el=>el.classList.remove('identity-editing'));
}
function commitChannelIdentity(){
 if(editingChannel<0)return;const index=editingChannel;
 const name=$('identityName').value.trim().replace(/\s+/g,' ').slice(0,26);
 if(!name){flash('Enter a channel name');$('identityName').focus();return}
 channels[index].name=name;channels[index].icon=channelIcons.includes(editingIcon)?editingIcon:'♩';
 channels[index].color=channelPalette.includes(editingColor)?editingColor:colors[index];
 closeChannelIdentity();currentChannel=index;renderMixer();
 if(view==='FX RACK')renderRackWorkspace();
 persistSession();flash('Channel identity updated');
}
window.addEventListener('resize',positionChannelIdentity);
$('mixer').addEventListener('scroll',positionChannelIdentity,{passive:true});
function makeStrip(ch,i){let inserts=ch.inserts.slice(0,6);while(inserts.length<6) inserts.push('');return `<article class="strip ${i===currentChannel?'selected':''}" data-strip="${i}" style="--ch:${ch.color||colors[i]}">
 <div class="ch-id">${i+1}</div><div class="rack">${inserts.map((n,j)=>`<div class="insert-slot ${n&&bypassed[fxKey(i,j)]?'is-bypassed':''}"><button class="insert ${!n?'empty':''}" data-insert="${i}:${j}" title="${n?'Open or replace '+safeText(n):'Add VST effect'}">${n?'':'<span class="insert-add-glyph" aria-hidden="true">＋</span>'}<span class="insert-name">${n? safeText(n):'ADD FX'}</span></button>${n?`<button type="button" class="fx-power ${bypassed[fxKey(i,j)]?'is-bypassed':''}" data-fxpower="${i}:${j}" aria-pressed="${!bypassed[fxKey(i,j)]}" aria-label="${bypassed[fxKey(i,j)]?'Enable':'Bypass'} ${safeText(n)}" title="${bypassed[fxKey(i,j)]?'Enable':'Bypass'} ${safeText(n)}">⏻</button>`:''}</div>`).join('')}</div>
 <div class="strip-controls"><div class="trim-row"><span class="mini-label">TRIM<br>dB</span><div class="mini-lcd" data-trim-read="${i}">${fmt(ch.trim)}</div></div>${knobMarkup('trim',i,ch.trim,'Trim')}<div class="pan-control">${panMarkup(ch,i)}<div class="pan-readline"><span aria-hidden="true">L</span><strong data-pan-read="${i}">${panText(ch.pan)}</strong><span aria-hidden="true">R</span></div></div><div class="utility-row"><button data-pre="${i}" class="btn-180 ${ch.pre?'active':''}">Ø</button><button data-hpf="${i}" class="btn-180 ${ch.hpf?'active':''}">HPF</button></div></div>
 <div class="channel-actions"><button class="btn-180 candy mute ${ch.mute?'active':''}" data-mute="${i}" aria-pressed="${ch.mute}">MUTE</button><button class="btn-180 candy solo ${ch.solo?'active':''}" data-solo="${i}" aria-pressed="${ch.solo}">SOLO</button></div>
 <div class="db-display" data-read="${i}">${fmt(ch.fader)}</div><div class="fader-zone"><div class="scale"><span>10</span><span>0</span><span>−10</span><span>−20</span><span>−30</span><span>−40</span><span>−60</span></div><div class="fader" data-fader="${i}" role="slider" tabindex="0" aria-orientation="vertical" aria-label="${ch.name} fader" aria-valuemin="-60" aria-valuemax="10" aria-valuenow="${ch.fader}"><div class="cap" style="--pos:${levelToTop(ch.fader)}%"></div></div><div class="meter" data-meter-key="${i}"><button class="clip-led" type="button" data-clip="${i}" title="Clear latched clip — ${safeText(ch.name)}" aria-label="Clear ${safeText(ch.name)} clip"></button><div class="meter-fill" data-meter="${i}" style="--level:40%"></div><div class="meter-peak" aria-hidden="true"></div></div></div>
 <div class="strip-name" data-editstrip="${i}" role="button" tabindex="0" aria-label="Double-click to edit ${safeText(ch.name)} name, icon and color" title="Double-click to edit channel identity"><span class="strip-caption">${safeText(ch.name)}</span><span class="track-icon">${safeText(ch.icon)}</span></div>
 </article>`}
function renderMixer(){
 const x=$('mixer'),scroll=x.scrollLeft;
 x.innerHTML=channels.map(makeStrip).join('')+'<article id="masterLeft" class="strip master-strip '+(masterSelected?'selected':'')+'" aria-label="Stereo master channel"></article>';
 renderMaster();x.scrollLeft=scroll;createSide();
}
let masterGain=-.1,masterPan=0,masterMute=false,phones=-12,masterInserts=['Studio EQ','Brickwall Limiter'],masterBypassed={},pendingMasterSlot=null;
function masterFxRows(){
 const slots=masterInserts.slice(0,6);while(slots.length<6)slots.push('');
 return slots.map((name,i)=>`<div class="master-fx-slot ${masterBypassed[i]?'is-bypassed':''}">
 <button class="master-insert ${name?'':'empty'}" data-master-insert="${i}" title="${name?'Replace '+safeText(name):'Add master FX'}">${name?'':'<span class="master-insert-add" aria-hidden="true">＋</span>'}<span class="insert-name">${name?safeText(name):'ADD FX'}</span></button>
 ${name?`<button class="master-fx-power ${masterBypassed[i]?'is-bypassed':''}" data-master-power="${i}" title="${masterBypassed[i]?'Enable':'Bypass'} ${safeText(name)}" aria-label="${masterBypassed[i]?'Enable':'Bypass'} ${safeText(name)}" aria-pressed="${!masterBypassed[i]}">⏻</button>`:''}</div>`).join('');
}
function meterUnit(key,label){return `<div class="meter-unit"><div class="meter" data-meter-key="${key}"><button type="button" class="clip-led" data-clip="${key}" title="Clear latched clip — ${label}" aria-label="Clear ${label} clip indication"></button><div class="meter-fill" data-meter="${key}" style="--level:6%"></div><div class="meter-peak" aria-hidden="true"></div></div>${label?'<span class="meter-channel-label">'+label+'</span>':''}</div>`}
function renderMaster(){
 const host=$('masterLeft');if(!host)return;
 host.innerHTML=`<div class="ch-id master-ch-id">MASTER</div>
 <div class="master-fx-rack">${masterFxRows()}</div>
 <div class="strip-controls master-controls-zone">
  <div class="master-analysis" role="group" aria-label="Livestreaming loudness and true peak (simulated)">
   <div class="master-info-kicker">STREAM LEVELS <span>SIM</span></div>
   <div class="master-info-row"><span>LUFS-I</span><strong id="masterLufs" aria-label="Integrated loudness">--</strong></div>
   <div class="master-info-row"><span>dBTP</span><strong id="masterDbtp" aria-label="True peak">--</strong></div>
  </div>
 <div class="master-pan-wrap">
    <div class="pan-control"><div class="pan-track ${masterPan===0?'is-centered':''}" data-pan="master"
       role="slider" tabindex="0" aria-orientation="horizontal"
       aria-label="Master pan" aria-valuemin="-100" aria-valuemax="100"
       aria-valuenow="${masterPan}" aria-valuetext="${panText(masterPan)}"
       title="Master pan · drag · center snap" style="--pan-frac:${(masterPan+100)/200};--pan-left:${Math.min(.5,(masterPan+100)/200)};--pan-span:${Math.abs(masterPan)/200}">
       <span class="pan-fill" aria-hidden="true"></span><span class="pan-thumb" aria-hidden="true"></span></div>
       <div class="pan-readline"><span aria-hidden="true">L</span><strong data-pan-read="master">${panText(masterPan)}</strong><span aria-hidden="true">R</span></div>
    </div>
  </div>
  <div class="master-monitor-label">STEREO MAIN L / R</div>
 </div>
 <div class="channel-actions master-actions">
  <button class="candy mute ${masterMute?'active':''}" type="button" id="masterMute" aria-pressed="${masterMute}">MUTE</button>
  <span class="master-stereo-badge" aria-label="Stereo master channel">STEREO</span>
 </div>
 <div class="db-display master-db" data-master-db>${fmt(masterGain)}</div>
 <div class="fader-zone master-fader-zone">
  <div class="scale"><span>10</span><span>0</span><span>−10</span><span>−20</span><span>−30</span><span>−40</span><span>−60</span></div>
  ${meterUnit('masterL','L')}
  <div class="fader" data-fader="master" role="slider" tabindex="0" aria-orientation="vertical"
       aria-label="Main master fader" aria-valuemin="-60" aria-valuemax="10" aria-valuenow="${masterGain}">
    <div class="cap master-cap" style="--pos:${levelToTop(masterGain)}%"></div>
  </div>
  ${meterUnit('masterR','R')}
   </div>
 <div class="fader-tools master-tools"><span class="master-route-label">MAIN L/R</span><button type="button" class="btn-180" data-clear-all-clips="1" title="Reset latched clips">CLEAR CLIP</button></div>
 <div class="strip-name master-strip-name" role="group" aria-label="Master output"><span class="strip-caption">MASTER</span><span class="track-icon" aria-hidden="true"><svg viewBox="0 0 24 24" width="19" height="19" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="m11 5-5 4H3v6h3l5 4V5Z"/><path d="M15 9a5 5 0 0 1 0 6M18 6a9 9 0 0 1 0 12"/></svg></span></div>`;
 syncClipLeds();
}
function setView(next){
 if(next!=='MIX'&&next!=='FX RACK'&&next!=='SIGNAL FLOW')return;
 view=next;
 document.querySelectorAll('[data-view]').forEach(btn=>btn.classList.toggle('selected',btn.dataset.view===next));
 $('mixer').classList.toggle('hidden',next!=='MIX');
 $('rackWorkspace').classList.toggle('hidden',!['FX RACK','SIGNAL FLOW'].includes(next));
 if(next==='FX RACK')renderRackWorkspace();
 if(next==='SIGNAL FLOW')renderSignalFlow();
}
function updateFader(idx,value){if(idx==='master'){masterGain=clamp(value,-60,10);const host=document.querySelector('[data-fader="master"]');host.querySelector('.cap').style.setProperty('--pos',levelToTop(masterGain)+'%');host.setAttribute('aria-valuenow',masterGain.toFixed(1));$('masterLeft').querySelectorAll('.master-db').forEach(e=>e.textContent=fmt(masterGain));send('masterGain',masterGain,-1);return}const ch=channels[+idx];ch.fader=clamp(value,-60,10);const el=document.querySelector(`[data-fader="${idx}"]`);if(el){el.querySelector('.cap').style.setProperty('--pos',levelToTop(ch.fader)+'%');el.setAttribute('aria-valuenow',ch.fader.toFixed(1));document.querySelector(`[data-read="${idx}"]`).textContent=fmt(ch.fader)}send('faderDb',ch.fader,+idx)}
function updatePan(idx,value,pointer=false){
 const master=idx==='master';
 const actual=master?masterPan:channels[idx].pan;
 // A tactile 10% center detent, with 3% release hysteresis during a drag.
 const oldCenter=actual===0;
 const threshold=pointer?(oldCenter?13:10):0;
 const next=Math.round(clamp(pointer&&Math.abs(value)<=threshold?0:value,-100,100));
 if(master)masterPan=next;else channels[idx].pan=next;
 const c={pan:next};
 const el=document.querySelector(`[data-pan="${idx}"]`);
 if(el){el.classList.toggle('is-centered',c.pan===0);el.style.setProperty('--pan-frac',(c.pan+100)/200);el.style.setProperty('--pan-left',Math.min(.5,(c.pan+100)/200));el.style.setProperty('--pan-span',Math.abs(c.pan)/200);el.setAttribute('aria-valuenow',String(c.pan));el.setAttribute('aria-valuetext',panText(c.pan))}
 const display=document.querySelector(`[data-pan-read="${idx}"]`);
 if(display)display.textContent=panText(c.pan);
 send(master?'masterPan':'pan',c.pan,master?-1:idx);
}
function updateKnob(el,value){let idx=+el.dataset.ch,param=el.dataset.knob;
 if(param==='phones'){phones=clamp(value,-60,10);el.style.setProperty('--angle',phones/24*130+'deg');const rd=document.querySelector('[data-phones-read]');if(rd)rd.textContent=fmt(phones);send('phonesDb',phones,-1);return}
 const c=channels[idx];if(param==='pan'){updatePan(idx,value)}else{c.trim=+clamp(value,-24,24).toFixed(1);el.style.setProperty('--angle',c.trim/24*130+'deg');document.querySelector(`[data-trim-read="${idx}"]`).textContent=fmt(c.trim);send('trimDb',c.trim,idx)}}
function focusChannel(i){
 if(i<0||i>=channels.length||(i===currentChannel&&!masterSelected))return;
 masterSelected=false;currentChannel=i;
 document.querySelectorAll('.strip').forEach(strip=>strip.classList.toggle('selected',strip.dataset.strip!==undefined&&+strip.dataset.strip===i));
 document.querySelectorAll('#channelList [data-choose]').forEach(row=>row.classList.toggle('selected',+row.dataset.choose===i));
}
function focusMaster(){
 if(masterSelected)return;
 masterSelected=true;
 document.querySelectorAll('.strip').forEach(strip=>strip.classList.toggle('selected',strip.id==='masterLeft'));
 document.querySelectorAll('#channelList [data-choose]').forEach(row=>row.classList.remove('selected'));
}
function chooseChannel(i){if(i<0||i>=channels.length){flash('Additional bank channels are not populated in this UI study');return}currentChannel=i;renderMixer();if(view==='FX RACK')renderRackWorkspace()}
function toggleState(i,key){channels[i][key]=!channels[i][key];const btn=document.querySelector(`[data-${key}="${i}"]`);if(btn){btn.classList.toggle('active',channels[i][key]);btn.setAttribute('aria-pressed',String(channels[i][key]))}send(key,channels[i][key],i)}
document.addEventListener('click',e=>{const b=e.target.closest('button,[data-choose],[data-scene],[data-group]');
 const icon=e.target.closest('[data-icon-choice]'),color=e.target.closest('[data-color-choice]');
 if(icon){editingIcon=icon.dataset.iconChoice;document.querySelectorAll('[data-icon-choice]').forEach(n=>{n.classList.toggle('selected',n===icon);n.setAttribute('aria-pressed',String(n===icon))});return}
 if(color){editingColor=color.dataset.colorChoice;document.querySelectorAll('[data-color-choice]').forEach(n=>{n.classList.toggle('selected',n===color);n.setAttribute('aria-pressed',String(n===color))});return}
 if(!b){const strip=e.target.closest('[data-strip]');if(strip&&view==='MIX')focusChannel(+strip.dataset.strip);else if(e.target.closest('#masterLeft')&&view==='MIX')focusMaster();return;}
 const originStrip=b.closest('[data-strip]');
 if(originStrip&&view==='MIX')focusChannel(+originStrip.dataset.strip);else if(b.closest('#masterLeft')&&view==='MIX')focusMaster();
 if(b.dataset.view){setView(b.dataset.view);return}
 if(b.dataset.nativeAction==='audioSetup'){flash('Audio Setup is available in the Windows desktop app');return}
 if(b.dataset.pluginManager!==undefined){flash('Plugin Manager is available in the Windows desktop app');return}
 if(b.dataset.mySounds!==undefined){flash('Native VST3 presets are available in the Windows desktop app');return}
 if(b.dataset.choose!==undefined){chooseChannel(+b.dataset.choose);return}
 if(b.dataset.mute!==undefined){toggleState(+b.dataset.mute,'mute');return}if(b.dataset.solo!==undefined){toggleState(+b.dataset.solo,'solo');return}
 if(b.dataset.pre!==undefined){let i=+b.dataset.pre;channels[i].pre=!channels[i].pre;b.classList.toggle('active');send('phase',channels[i].pre,i);return}
 if(b.dataset.hpf!==undefined){let i=+b.dataset.hpf;channels[i].hpf=!channels[i].hpf;b.classList.toggle('active');send('hpf',channels[i].hpf,i);return}
 if(b.dataset.masterInsert!==undefined){pendingMasterSlot=+b.dataset.masterInsert;openPluginBrowser(currentChannel,true);return}
 if(b.dataset.masterPower!==undefined){const slot=+b.dataset.masterPower;masterBypassed[slot]=!masterBypassed[slot];renderMaster();persistSession();send('masterFxBypass',{slot,bypass:masterBypassed[slot]},-1);return}
 if(b.dataset.clip!==undefined){resetClip(b.dataset.clip);return}
 if(b.dataset.clearAllClips!==undefined){Object.keys(clipLatch).forEach(resetClip);Object.keys(peakHold).forEach(k=>delete peakHold[k]);return}
 if(b.dataset.fxpower!==undefined){const [i,j]=b.dataset.fxpower.split(':').map(Number);setFxBypass(i,j,!bypassed[fxKey(i,j)]);return}
 if(b.dataset.insert){let [i,j]=b.dataset.insert.split(':').map(Number);currentChannel=i;pendingSlot=j;openPluginBrowser(i);return}
 if(b.dataset.scene!==undefined){loadSnapshotByIndex(+b.dataset.scene);return}
 if(b.dataset.preset){applyQuickPreset(b.dataset.preset);return}
 if(b.dataset.addplugin!==undefined){pendingSlot=channels[currentChannel].inserts.length;openPluginBrowser(currentChannel);return}
 if(b.dataset.chainmove){movePlugin(+b.dataset.index,+b.dataset.chainmove);return}
 if(b.dataset.chainremove!==undefined){removePlugin(+b.dataset.chainremove);return}
 if(b.dataset.chainbypass!==undefined){toggleBypass(+b.dataset.chainbypass);return}
 if(b.dataset.chainedit!==undefined){pluginEditorIndex=+b.dataset.chainedit;renderRackWorkspace();return}
 if(b.dataset.chanselect!==undefined){chooseChannel(+b.dataset.chanselect);renderRackWorkspace();return}
 if(b.dataset.pluginname){insertPlugin(b.dataset.pluginname);return}
 if(b.dataset.category){activeCategory=b.dataset.category;renderPluginBrowser();return}
 if(b.dataset.group!==undefined){flash('Group selection — interface placeholder');return}
 if(b.dataset.side){document.querySelectorAll('[data-side]').forEach(x=>x.classList.toggle('selected',x===b));if(b.dataset.side==='fx')setView('FX RACK');else setView('MIX');return}
 switch(b.id){case'identityCancel':closeChannelIdentity();break;case'identitySave':commitChannelIdentity();break;case'masterMute':masterMute=!masterMute;b.textContent='MUTE';b.classList.toggle('active',masterMute);b.setAttribute('aria-pressed',String(masterMute));send('masterMute',masterMute,-1);break;case'addScene':saveMixerSnapshot();break;case'saveFxPreset':saveFxChainPreset();break;case'menuScene':exportSession();break;case'closeBrowser':closePluginBrowser();break;case'closeFxInspector':pluginEditorIndex=null;renderRackWorkspace();break;case'chooseFxFile':importSession();break;case'arSignalOpen':if(!window.__JUCE__?.backend?.emitEvent)flash('Signal Check is available in the Windows desktop app');break;default:if(b.id)flash('UI study: '+b.textContent.trim().slice(0,28));}
});
// Deliberate double-click to edit a strip. Single-click only changes selection.
document.addEventListener('dblclick',e=>{const el=e.target.closest('[data-editstrip]');if(!el)return;
 const i=Number(el.dataset.editstrip);if(!Number.isInteger(i)||i<0||i>=channels.length)return;
 e.preventDefault();e.stopPropagation();openChannelIdentity(i);
},true);
document.addEventListener('dblclick',e=>{const pan=e.target.closest('[data-pan]');if(pan){e.preventDefault();e.stopPropagation();updatePan(pan.dataset.pan==='master'?'master':+pan.dataset.pan,0)}},true);
// Double-click a fader for 0 dB unity gain, without changing workspace.
document.addEventListener('dblclick',e=>{const f=e.target.closest('[data-fader]');if(!f)return;e.preventDefault();e.stopPropagation();updateFader(f.dataset.fader,0)},true);
// Idle pointer movement has no handler. Acquire geometry only once per drag
// rather than forcing a layout read on every input event.
let drag=null;
function moveDraggedControl(e){
 const d=drag;
 if(!d||d.pointerId!==e.pointerId)return;
 if(d.type==='fader'){
   updateFader(d.index,d.startDb+(d.startY-e.clientY)*70/d.distance);
 }else if(d.type==='pan'){
   updatePan(d.index,d.fromThumb?d.startPan+(e.clientX-d.startX)*200/d.travel:
      ((e.clientX-d.left-9)/d.travel)*200-100,true);
 }else if(d.type==='knob'){
   updateKnob(d.el,d.original+(d.initial-e.clientY)*.35);
 }
}
document.addEventListener('pointerdown',e=>{
 if(drag)return;
 const f=e.target.closest('[data-fader]'),p=e.target.closest('[data-pan]'),k=e.target.closest('[data-knob]');
 if(!f&&!p&&!k)return;
 const el=f||p||k,strip=e.target.closest('[data-strip]');
 if(strip&&view==='MIX')focusChannel(+strip.dataset.strip);
 else if(e.target.closest('#masterLeft')&&view==='MIX')focusMaster();
 if(f){
   const idx=f.dataset.fader,r=f.getBoundingClientRect();
   drag={type:'fader',el:f,pointerId:e.pointerId,index:idx,
     startY:e.clientY,distance:Math.max(24,r.height-77),
     startDb:idx==='master'?masterGain:channels[+idx].fader};
   // Relative drag: clicking track never teleports the fader.
 }else if(p){
   const index=p.dataset.pan==='master'?'master':+p.dataset.pan,
     fromThumb=!!e.target.closest('.pan-thumb'),r=p.getBoundingClientRect(),
     travel=Math.max(1,r.width-18);
   drag={type:'pan',el:p,pointerId:e.pointerId,index,fromThumb,
     startX:e.clientX,startPan:index==='master'?masterPan:channels[index].pan,
     travel,left:r.left};
   if(!fromThumb)updatePan(index,((e.clientX-r.left-9)/travel)*200-100,true);
 }else{
   drag={type:'knob',el:k,pointerId:e.pointerId,initial:e.clientY,
     original:k.dataset.knob==='phones'?phones:channels[+k.dataset.ch][k.dataset.knob]};
 }
 if(drag.type==='fader'||drag.type==='pan')el.classList.add('is-dragging');
 document.addEventListener('pointermove',moveDraggedControl,{passive:true});
 if(el.setPointerCapture)el.setPointerCapture(e.pointerId);
 e.preventDefault();
});
for(const type of ['pointerup','pointercancel'])document.addEventListener(type,e=>{
 if(!drag||e.pointerId!==drag.pointerId)return;
 document.removeEventListener('pointermove',moveDraggedControl);
 drag.el.classList.remove('is-dragging');
 drag=null;
 flushUiParams();
});
document.addEventListener('wheel',e=>{const pan=e.target.closest('[data-pan]');if(pan){e.preventDefault();const idx=pan.dataset.pan==='master'?'master':+pan.dataset.pan;updatePan(idx,(idx==='master'?masterPan:channels[idx].pan)-Math.sign(e.deltaY)*2);return}
 const k=e.target.closest('[data-knob]');if(!k)return;e.preventDefault();const p=k.dataset.knob,i=+k.dataset.ch,v=p==='phones'?phones:channels[i][p];updateKnob(k,v-Math.sign(e.deltaY)*.5)},{passive:false});
document.addEventListener('keydown',e=>{const pan=e.target.closest('[data-pan]');if(pan){const k=e.key,idx=pan.dataset.pan==='master'?'master':+pan.dataset.pan;
 if(!['ArrowLeft','ArrowRight','PageUp','PageDown','Home','End'].includes(k))return;
 e.preventDefault();const value=idx==='master'?masterPan:channels[idx].pan,step=e.shiftKey?10:1;
 updatePan(idx,k==='Home'?0:k==='End'?100:k==='PageUp'?value+10:k==='PageDown'?value-10:value+(k==='ArrowRight'?step:-step));return}
 const f=e.target.closest('[data-fader]');if(!f)return;
 const steps={ArrowUp:.5,ArrowDown:-.5,PageUp:5,PageDown:-5};
 if(!(e.key in steps)&&e.key!=='Home'&&e.key!=='End')return;
 e.preventDefault();const idx=f.dataset.fader,db=idx==='master'?masterGain:channels[+idx].fader;
 updateFader(idx,e.key==='Home'?-60:e.key==='End'?10:db+steps[e.key]);
});
document.addEventListener('keydown',e=>{if(e.ctrlKey&&e.key.toLowerCase()==='s'){e.preventDefault();if(persistSession())flash('UI state saved locally');else flash('Browser storage unavailable')}});
// Issue #56: no pointer profiler/event listener runs in normal preview or native.
const mixperfParam=new URLSearchParams(location.search).get('mixperf');
const mixperfModes=['optimized','baseline','hover-off','meters-off','layers-off','all-off'];
const mixperfMode=mixperfModes.includes(mixperfParam)?mixperfParam:'optimized';
document.documentElement.dataset.mixperf=mixperfMode;
if(mixperfParam&&mixperfModes.includes(mixperfParam)){
 window.__arMixPerfProbe={mode:mixperfMode,run(durationMs=10000){
  const ms=Math.max(1000,Math.min(20000,Number(durationMs)||10000));
  const buckets={mixer:[],outside:[]},moves={mixer:0,outside:0};
  let area='',pointerAt=-Infinity,lastFrame=0,frameId=0,observer=null,longTasks=0,longTaskMs=0;
  // Long Task entries are browser-dependent. Unavailable must never read as zero.
  let longTaskAvailable=false;
  function pointer(e){area=e.target.closest?.('#mixer')?'mixer':'outside';pointerAt=performance.now();moves[area]++}
  function visibility(){lastFrame=0;pointerAt=-Infinity}
  function frame(t){
   if(lastFrame&&!document.hidden&&t-pointerAt<350&&t-lastFrame>0)buckets[area].push(t-lastFrame);
   lastFrame=t;frameId=requestAnimationFrame(frame);
  }
  function stats(a){const s=a.slice().sort((x,y)=>x-y),n=s.length;
   const over33=s.filter(x=>x>33.3).length,over50=s.filter(x=>x>50).length;
   return {samples:n,medianMs:n?+s[Math.floor((n-1)*.5)].toFixed(2):null,
    p95Ms:n?+s[Math.ceil(n*.95)-1].toFixed(2):null,
    p99Ms:n?+s[Math.ceil(n*.99)-1].toFixed(2):null,maxMs:n?+s[n-1].toFixed(2):null,
    over33Ms:over33,over50Ms:over50,over33Pct:n?+(over33*100/n).toFixed(2):null};
  }
  document.addEventListener('pointermove',pointer,{passive:true});
  document.addEventListener('visibilitychange',visibility);
  if(typeof PerformanceObserver==='function' &&
     PerformanceObserver.supportedEntryTypes?.includes('longtask'))try{
   observer=new PerformanceObserver(l=>{
    for(const e of l.getEntries()){longTasks++;longTaskMs+=e.duration}
   });observer.observe({entryTypes:['longtask']});longTaskAvailable=true;
  }catch{observer?.disconnect();observer=null}
  frameId=requestAnimationFrame(frame);
  return new Promise(done=>setTimeout(()=>{
   cancelAnimationFrame(frameId);document.removeEventListener('pointermove',pointer);
   document.removeEventListener('visibilitychange',visibility);observer?.disconnect();
   done({mode:mixperfMode,durationMs:ms,environment:{userAgent:navigator.userAgent,dpr:devicePixelRatio,
    width:innerWidth,height:innerHeight,iframe:top!==self},mixer:stats(buckets.mixer),
    outside:stats(buckets.outside),pointerMoves:moves,longTaskAvailable,
    longTasks:longTaskAvailable?longTasks:null,
    longTaskMs:longTaskAvailable?+longTaskMs.toFixed(1):null,
    note:'rAF proxy, not measured GPU paint/input latency; unsupported longtask is null'});
  },ms));
 }};
}
// Browser-only meter scheduler: do not enqueue a 60/120/144Hz rAF just to
// reject 3/4 of its callbacks. Timer wakes shortly before the next visual tick;
// a single rAF aligns the paint with the browser. Native JUCE owns live metering.
const browserMeterStats={ticks:0,skipped:0,slowWakeups:0,intervalMs:40};
let meterTimer=0,meterRAF=0,meterExpected=0,meterLate=0,meterHealthy=0;
function scheduleDemoMeter(){
 if(window.__JUCE__?.backend?.emitEvent||document.hidden||
    mixperfMode==='meters-off'||mixperfMode==='all-off')return;
 const interval=browserMeterStats.intervalMs;
 meterExpected=performance.now()+interval;
 meterTimer=setTimeout(()=>{
   meterTimer=0;
   const late=Math.max(0,performance.now()-meterExpected);
   if(late>25){
     browserMeterStats.slowWakeups++;
     meterLate++;meterHealthy=0;
     if(meterLate>=3){browserMeterStats.intervalMs=60;meterLate=0}
   }else{
     meterHealthy++;meterLate=0;
     if(meterHealthy>=30){browserMeterStats.intervalMs=40;meterHealthy=0}
   }
   if(document.hidden){browserMeterStats.skipped++;return}
   meterRAF=requestAnimationFrame(t=>{meterRAF=0;meters(t)});
 },interval);
}
function meters(t){
 // JUCE desktop reads actual audio meters; browser demo only animates a preview.
 if(window.__JUCE__?.backend?.emitEvent||document.hidden||
    mixperfMode==='meters-off'||mixperfMode==='all-off')return;
 browserMeterStats.ticks++;
 scheduleDemoMeter();
 channels.forEach((ch,i)=>{const wave=(Math.sin(t*.0022+i*1.92)+Math.sin(t*.0043+i*.6))*.5;
 let v=clamp(45+wave*24+ch.fader*.48,4,98);if(ch.mute)v=2;updateMeterVisual(String(i),v,t)});
 const base=clamp(67+Math.sin(t*.002)*15,8,95);
 const l=clamp(base+Math.sin(t*.003)*6,3,99),r=clamp(base-5+Math.sin(t*.003+1)*7,3,99);
 updateMeterVisual('masterL',masterMute?2:l,t);updateMeterVisual('masterR',masterMute?2:r,t);
 // UI demonstration only. These values are never from a true-peak or loudness meter.
 const lufs=$('masterLufs'),tp=$('masterDbtp');
 if(lufs)lufs.textContent=(-31+(l+r)/11).toFixed(1);
 if(tp)tp.textContent=(-18+Math.max(l,r)/7).toFixed(1);
}
// v0.3: demo-only FX catalog. Items represent mock plugins, not installed VST3 binaries.
const pluginCatalog=[
 {name:'Studio EQ',type:'EQ',badge:'EQ',hint:'Four-band tonal shaping'},
 {name:'Dynamic EQ',type:'EQ',badge:'D',hint:'Adaptive tonal control'},
 {name:'Vocal Compressor',type:'Dynamics',badge:'C',hint:'Smooth vocal dynamics'},
 {name:'Noise Gate',type:'Dynamics',badge:'G',hint:'Reduce background noise'},
 {name:'De-Esser',type:'Dynamics',badge:'S',hint:'Sibilance control'},
 {name:'Brickwall Limiter',type:'Dynamics',badge:'L',hint:'Prevent excessive peaks'},
 {name:'Studio Reverb',type:'Space',badge:'R',hint:'Natural vocal ambience'},
 {name:'Tempo Delay',type:'Space',badge:'D',hint:'Synced echo'},
 {name:'Stereo Chorus',type:'Creative',badge:'CH',hint:'Gentle stereo width'},
 {name:'Saturator',type:'Creative',badge:'SAT',hint:'Analog-style harmonic color'}
];
let pendingSlot=-1, activeCategory='All', userSnapshots={},savedChains={},bypassed={},fxParams={},pluginEditorIndex=null;
const initialFactoryChannels=channels.map(c=>({...c,inserts:[...c.inserts]}));
const safeText=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function normalChain(ch){return ch.inserts.filter(Boolean)}
function fxKey(ch,slot){return `${ch}:${slot}`}
function getFxParams(ch,slot){const k=fxKey(ch,slot);return fxParams[k]??(fxParams[k]={mix:60,amount:45,output:0})}
function moveFxMeta(ch,from,to){const a=fxKey(ch,from),b=fxKey(ch,to),tmp=fxParams[a];fxParams[a]=fxParams[b];fxParams[b]=tmp;const bo=bypassed[a];bypassed[a]=bypassed[b];bypassed[b]=bo}
function shiftFxMetaRight(ch,at,len){for(let i=len;i>at;i--){fxParams[fxKey(ch,i)]=fxParams[fxKey(ch,i-1)];bypassed[fxKey(ch,i)]=bypassed[fxKey(ch,i-1)]}delete fxParams[fxKey(ch,at)];delete bypassed[fxKey(ch,at)]}
function shiftFxMetaLeft(ch,at,len){for(let i=at;i<len-1;i++){fxParams[fxKey(ch,i)]=fxParams[fxKey(ch,i+1)];bypassed[fxKey(ch,i)]=bypassed[fxKey(ch,i+1)]}delete fxParams[fxKey(ch,len-1)];delete bypassed[fxKey(ch,len-1)]}
function setChain(ch,chain){ch.inserts=chain.slice(0,8);renderMixer();if(view==='FX RACK')renderRackWorkspace()}
function focusOptions(){return channels.map((c,i)=>`<option value="${i}" ${i===currentChannel?'selected':''}>${i+1} · ${safeText(c.name)}</option>`).join('')}
function renderRackWorkspace(){let c=channels[currentChannel];let chain=normalChain(c);$('rackWorkspace').innerHTML=`<div class="feature-top"><div><span class="eyebrow">YOUR SOUND · CORE WORKFLOW</span><h2>FX Rack — ${safeText(c.name)}</h2><p>Build your signature vocal sound. Add, reorder, bypass, and store your VST chain.</p></div><div style="display:flex;gap:7px"><select id="rackChannel" class="ghost-action" aria-label="Choose channel">${focusOptions()}</select><button class="ghost-action" id="recallFxPreset">LOAD CHAIN</button><button class="action" id="saveFxFromRack">SAVE FX CHAIN</button></div></div><div class="rack-layout"><div class="chain-list"><div class="chain-end">● AUDIO INPUT · ${safeText(c.name)}</div><div class="chain-path">↓</div>${chain.map((name,i)=>`<div class="chain-node ${bypassed[currentChannel+':'+i]?'dimmed':''}"><span class="chain-index">${String(i+1).padStart(2,'0')}</span><span class="chain-name"><b>${safeText(name)}</b><small>${safeText(pluginCatalog.find(p=>p.name===name)?.hint||'VST3 plugin · mock chain state')}</small></span><button data-chainedit="${i}" title="Open plugin controls">EDIT</button><button data-chainbypass="${i}" class="${bypassed[fxKey(currentChannel,i)]?'is-bypassed':'power-on'}" aria-pressed="${!bypassed[fxKey(currentChannel,i)]}" aria-label="${bypassed[fxKey(currentChannel,i)]?'Enable':'Bypass'} ${safeText(name)}" title="${bypassed[fxKey(currentChannel,i)]?'Enable':'Bypass'} ${safeText(name)}">⏻</button><button data-chainmove="-1" data-index="${i}" title="Move up" ${i===0?'disabled':''}>↑</button><button data-chainmove="1" data-index="${i}" title="Move down" ${i===chain.length-1?'disabled':''}>↓</button><button data-chainremove="${i}" title="Remove FX">×</button></div><div class="chain-path">↓</div>`).join('')}<button class="chain-add" data-addplugin="1">＋ ADD VST3 EFFECT</button><div class="chain-path">↓</div><div class="chain-end">◎ OUTPUT · MASTER / STREAM</div></div>${pluginEditorIndex!==null&&pluginEditorIndex<chain.length?pluginInspector(currentChannel,pluginEditorIndex):`<div class="guide-panel"><h3>Ready-to-use Sound</h3><p>Start with a preset, then personalize your FX chain. No complex routing required.</p><div>${['Clean Vocal','Warm Karaoke','Live Broadcast'].map(s=>`<button class="guide-chip" data-preset="${s}">${s}</button>`).join('')}</div><h3 style="margin-top:20px">Make it sound better</h3><p>Recommended signal path:</p><p>Noise Gate → EQ → Compressor → De-Esser → Reverb</p><h3 style="margin-top:20px">Demo status</h3><p>Plugin names and bypass buttons currently change visual state only. Installed VST3 scanning and DSP are scheduled for the native engine phase.</p></div>`}</div>`}
function pluginInspector(ch,slot){const name=normalChain(channels[ch])[slot],v=getFxParams(ch,slot);return `<div class="guide-panel plugin-inspector"><span class="eyebrow">FX EDITOR · DEMO PARAMETERS</span><h3>${safeText(name)}</h3><p>These generic controls demonstrate the editing and preset workflow. The real VST3 editor will replace them when connected.</p><label>Mix <output data-fxread="mix">${v.mix}%</output><input type="range" min="0" max="100" value="${v.mix}" data-fxparam="mix" data-slot="${slot}"></label><label>Amount <output data-fxread="amount">${v.amount}%</output><input type="range" min="0" max="100" value="${v.amount}" data-fxparam="amount" data-slot="${slot}"></label><label>Output <output data-fxread="output">${v.output>0?'+':''}${v.output} dB</output><input type="range" min="-24" max="12" value="${v.output}" data-fxparam="output" data-slot="${slot}"></label><button id="closeFxInspector" class="ghost-action" style="margin-top:20px;width:100%">CLOSE EDITOR</button></div>`}
function renderSignalFlow(){$('rackWorkspace').innerHTML=`<div class="feature-top"><div><span class="eyebrow">NO TECHNICAL PATCHBAY REQUIRED</span><h2>Signal Flow — See where your voice goes</h2><p>A beginner-first view of the real-time audio path. Select your audio device in Setup.</p></div><button class="action" id="openSetupFromFlow">CHOOSE AUDIO DEVICE</button></div><div class="workflow"><div class="flow-node"><span class="eyebrow">01 · INPUT</span><b>Microphone</b><small>Audio Interface<br>ASIO or WASAPI</small></div><span class="flow-arrow">➜</span><div class="flow-node"><span class="eyebrow">02 · FX CHAIN</span><b>VST3 Plugins</b><small>EQ · Dynamics<br>Reverb · Delay</small></div><span class="flow-arrow">➜</span><div class="flow-node"><span class="eyebrow">03 · MIXER</span><b>Mix & Monitor</b><small>Fader · Pan<br>Master · Meters</small></div><span class="flow-arrow">➜</span><div class="flow-node"><span class="eyebrow">04 · OUTPUT</span><b>Speakers / Stream</b><small>Audio Device<br>Streaming bridge later</small></div></div><div class="flow-summary"><span>⚠ In this trial the audio flow is a visual concept; no microphone is captured and no audio is sent to a livestream.</span><button class="ghost-action" id="flowFxButton">OPEN FX RACK</button></div>`}
function openPluginBrowser(idx,isMaster=false){if(!isMaster){currentChannel=idx;pendingMasterSlot=null}activeCategory='All';$('pluginModal').classList.remove('hidden');$('pluginTarget').textContent=isMaster?'Adding to MASTER bus · demo FX catalog':'Adding to '+channels[idx].name+' · virtual effects catalog';$('pluginSearch').value='';renderPluginBrowser();$('pluginSearch').focus()}
function closePluginBrowser(){$('pluginModal').classList.add('hidden')}
function renderPluginBrowser(){document.querySelectorAll('[data-category]').forEach(b=>b.classList.toggle('active',b.dataset.category===activeCategory));const q=$('pluginSearch').value.trim().toLowerCase();let pool=pluginCatalog.filter(p=>(activeCategory==='All'||p.type===activeCategory)&&`${p.name} ${p.type} ${p.hint}`.toLowerCase().includes(q));$('pluginResults').innerHTML=pool.map(p=>`<button class="plugin-card" data-pluginname="${safeText(p.name)}"><span class="plugin-glyph">${safeText(p.badge)}</span><span><b>${safeText(p.name)}</b><small>${safeText(p.type)} · ${safeText(p.hint)}</small></span></button>`).join('')||'<p>No matching demo effects.</p>'}
function insertPlugin(name){
 if(pendingMasterSlot!==null){
   const slot=pendingMasterSlot;
   if(slot>=6)return;
   masterInserts[slot]=name;delete masterBypassed[slot];pendingMasterSlot=null;
   closePluginBrowser();renderMaster();persistSession();
   send('masterChainChanged',{chain:[...masterInserts]},-1);
   flash('Master FX updated (GUI preview)');return;
 }
 const c=channels[currentChannel];const chain=normalChain(c);if(chain.length>=8){flash('Maximum 8 FX slots in this UI trial');return}const index=clamp(pendingSlot,0,chain.length);shiftFxMetaRight(currentChannel,index,chain.length);chain.splice(index,0,name);pluginEditorIndex=index;setChain(c,chain);closePluginBrowser();flash(name+' added to preview chain');send('chainChanged',{chain},currentChannel)}
function movePlugin(idx,dir){const c=channels[currentChannel],ch=normalChain(c),n=idx+dir;if(n<0||n>=ch.length)return;[ch[idx],ch[n]]=[ch[n],ch[idx]];moveFxMeta(currentChannel,idx,n);pluginEditorIndex=n;setChain(c,ch);send('chainChanged',{chain:ch},currentChannel)}
function removePlugin(idx){const c=channels[currentChannel],ch=normalChain(c);shiftFxMetaLeft(currentChannel,idx,ch.length);ch.splice(idx,1);pluginEditorIndex=null;setChain(c,ch);send('chainChanged',{chain:ch},currentChannel)}
function setFxBypass(ch,slot,disabled){
 const chain=normalChain(channels[ch]);
 if(!chain[slot])return;
 const k=fxKey(ch,slot);
 bypassed[k]=!!disabled;
 const power=document.querySelector(`[data-fxpower="${k}"]`);
 const cell=power?.closest('.insert-slot');
 if(cell)cell.classList.toggle('is-bypassed',disabled);
 if(power){
   power.classList.toggle('is-bypassed',disabled);
   power.setAttribute('aria-pressed',String(!disabled));
   power.setAttribute('aria-label',(disabled?'Enable ':'Bypass ')+chain[slot]);
   power.title=(disabled?'Enable ':'Bypass ')+chain[slot];
 }
 if(ch===currentChannel){
   const rackButton=document.querySelector(`[data-chainbypass="${slot}"]`);
   if(rackButton){
     rackButton.classList.toggle('power-on',!disabled);
     rackButton.classList.toggle('is-bypassed',disabled);
     rackButton.setAttribute('aria-pressed',String(!disabled));
     rackButton.setAttribute('aria-label',(disabled?'Enable ':'Bypass ')+chain[slot]);
     rackButton.title=(disabled?'Enable ':'Bypass ')+chain[slot];
     rackButton.closest('.chain-node')?.classList.toggle('dimmed',disabled);
   }
 }
 send('chainBypass',{slot,bypass:!!disabled},ch);
 persistSession();
}
function toggleBypass(idx){setFxBypass(currentChannel,idx,!bypassed[fxKey(currentChannel,idx)])}
const quickPresets={'Clean Vocal':['Noise Gate','Studio EQ','Vocal Compressor'],'Warm Karaoke':['Noise Gate','Studio EQ','Vocal Compressor','Studio Reverb'],'Live Broadcast':['Noise Gate','Dynamic EQ','De-Esser','Brickwall Limiter']};
function applyQuickPreset(name){const c=channels[currentChannel];for(let i=0;i<8;i++){delete fxParams[fxKey(currentChannel,i)];delete bypassed[fxKey(currentChannel,i)]}pluginEditorIndex=null;setChain(c,quickPresets[name]||[]);document.querySelectorAll('[data-preset]').forEach(b=>b.classList.toggle('active',b.dataset.preset===name));flash('FX chain preset selected: '+name+' (UI only)');send('chainChanged',{chain:normalChain(c)},currentChannel)}
function durableSession(){return {schemaVersion:1,version:'0.3-ui',channels:channels.map(c=>({id:c.id,name:c.name,icon:c.icon,color:c.color,inserts:[...c.inserts],fader:c.fader,pan:c.pan,trim:c.trim,mute:c.mute,solo:c.solo,hpf:c.hpf})),masterGain,masterPan,masterMute,masterInserts:[...masterInserts],masterBypassed:{...masterBypassed},sceneIdx,selected:currentChannel,fxParams:structuredClone(fxParams),bypassed:{...bypassed}};}
function persistSession(){try{localStorage.setItem('armix-v03-current',JSON.stringify(durableSession()));localStorage.setItem('armix-v03-snapshots',JSON.stringify(userSnapshots));localStorage.setItem('armix-v03-chains',JSON.stringify(savedChains));return true}catch{return false}}
function saveFxChainPreset(){const name=prompt('Name this FX chain preset:','My Vocal Chain');if(!name)return;const key=name.trim().slice(0,60);if(!key)return;savedChains[key]={schemaVersion:1,inserts:[...normalChain(channels[currentChannel])],states:normalChain(channels[currentChannel]).map((_,i)=>({...getFxParams(currentChannel,i),bypass:!!bypassed[fxKey(currentChannel,i)]}))};persistSession();flash('FX chain preset saved: '+key)}
function recallFxChain(){const keys=Object.keys(savedChains);if(!keys.length){flash('No saved FX chains yet');return}const key=prompt('Enter FX chain preset name:\n'+keys.join('\n'),keys[0]);if(!key||!savedChains[key])return;setChain(channels[currentChannel],savedChains[key].inserts);savedChains[key].states?.forEach((v,i)=>{fxParams[fxKey(currentChannel,i)]={mix:v.mix,amount:v.amount,output:v.output};bypassed[fxKey(currentChannel,i)]=!!v.bypass});pluginEditorIndex=null;renderRackWorkspace();flash('FX chain recalled: '+key)}
function saveMixerSnapshot(){const name=prompt('Name this mixer snapshot:','My Live Sound');if(!name)return;const key=name.trim().slice(0,60);if(!key)return;userSnapshots[key]=durableSession();if(!scenes.includes(key))scenes.push(key);persistSession();createSide();flash('Snapshot saved: '+key)}
function applySession(session){if(!session||session.schemaVersion!==1||!Array.isArray(session.channels)||session.channels.length<channels.length){flash('Unsupported session format');return}for(let i=0;i<channels.length;i++){const src=session.channels[i],c=channels[i];
 if(typeof src.name==='string'&&src.name.trim())c.name=src.name.trim().slice(0,26);
 if(channelIcons.includes(src.icon))c.icon=src.icon;
 if(channelPalette.includes(src.color))c.color=src.color;
 for(const k of ['fader','pan','trim'])if(Number.isFinite(src[k]))c[k]=src[k];for(const k of ['mute','solo','hpf'])if(typeof src[k]==='boolean')c[k]=src[k];if(Array.isArray(src.inserts))c.inserts=src.inserts.filter(x=>typeof x==='string').slice(0,8)}masterGain=Number.isFinite(session.masterGain)?session.masterGain:-.1;masterPan=Number.isFinite(session.masterPan)?clamp(session.masterPan,-100,100):0;masterMute=!!session.masterMute;
 if(Array.isArray(session.masterInserts))masterInserts=session.masterInserts.filter(x=>typeof x==='string').slice(0,6);
 masterBypassed=(session.masterBypassed&&typeof session.masterBypassed==='object')?{...session.masterBypassed}:{};
 fxParams=(session.fxParams&&typeof session.fxParams==='object')?structuredClone(session.fxParams):{};bypassed=(session.bypassed&&typeof session.bypassed==='object')?{...session.bypassed}:{};pluginEditorIndex=null;renderMixer();renderMaster();if(view==='FX RACK')renderRackWorkspace()}
function loadSnapshotByIndex(i){sceneIdx=(i+scenes.length)%scenes.length;const name=scenes[sceneIdx];
 if(userSnapshots[name])applySession(userSnapshots[name]);
 else if(sceneIdx<4){const state=durableSession();state.channels=initialFactoryChannels.map(c=>({...c,inserts:[...c.inserts]}));state.masterGain=-.1;
  const voicePresets=[quickPresets['Clean Vocal'],quickPresets['Warm Karaoke'],quickPresets['Live Broadcast'],quickPresets['Clean Vocal']];state.channels[0].inserts=[...voicePresets[sceneIdx]];applySession(state);flash('Factory snapshot recalled: '+name+' (UI only)')}
 createSide();persistSession()}
function exportSession(){const data=JSON.stringify({...durableSession(),savedChains,userSnapshots},null,2);const blob=new Blob([data],{type:'application/json'}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='ar-mix-live-session.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1200);flash('Session JSON exported')}
function importSession(){const f=document.createElement('input');f.type='file';f.accept='application/json,.json';f.onchange=async()=>{const file=f.files?.[0];if(!file)return;if(file.size>2_000_000){flash('Session too large');return}try{const obj=JSON.parse(await file.text());applySession(obj);persistSession();flash('Session loaded')}catch{flash('Invalid JSON session')}};f.click()}
// Single optimized console layout. The unused PRO/EASY switch is retired.
$('app').classList.add('easy');
$('pluginSearch').addEventListener('input',renderPluginBrowser);
document.addEventListener('input',e=>{const el=e.target;if(!el.dataset.fxparam)return;const slot=+el.dataset.slot,key=el.dataset.fxparam;if(!['mix','amount','output'].includes(key))return;const v=getFxParams(currentChannel,slot);v[key]=+el.value;const out=el.closest('label')?.querySelector('output');if(out)out.textContent=key==='output'?`${v[key]>0?'+':''}${v[key]} dB`:`${v[key]}%`;send('fxDemoParam',{slot,key,value:v[key]},currentChannel)});
$('pluginModal').addEventListener('click',e=>{if(e.target===$('pluginModal'))closePluginBrowser()});
$('channelIdentity').addEventListener('click',e=>{if(e.target===$('channelIdentity'))closeChannelIdentity()});
$('identityName').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();commitChannelIdentity()}});
document.addEventListener('keydown',e=>{if(e.key==='Escape'){closePluginBrowser();closeChannelIdentity()}if((e.key==='Enter'||e.key===' ')&&e.target.matches('[data-editstrip]')){e.preventDefault();openChannelIdentity(+e.target.dataset.editstrip)}});
document.addEventListener('change',e=>{if(e.target.id==='rackChannel'){currentChannel=+e.target.value;renderRackWorkspace();renderMixer()}});
document.addEventListener('click',e=>{const id=e.target.closest('button')?.id;if(id==='saveFxFromRack')saveFxChainPreset();else if(id==='recallFxPreset')recallFxChain();else if(id==='openSetupFromFlow')$('audioSetupBtn')?.click();else if(id==='flowFxButton')setView('FX RACK');else if(id==='backMixer')setView('MIX')});
let lastPersist=0;setInterval(()=>{if(Date.now()-lastPersist<2000)return;lastPersist=Date.now();persistSession()},5000);
try{userSnapshots=JSON.parse(localStorage.getItem('armix-v03-snapshots')||'{}');savedChains=JSON.parse(localStorage.getItem('armix-v03-chains')||'{}');for(const k of Object.keys(userSnapshots))if(!scenes.includes(k))scenes.push(k)}catch{userSnapshots={};savedChains={}}

createSide();renderMixer();renderMaster();try{let last=JSON.parse(localStorage.getItem('armix-v03-current'));if(!last)last=JSON.parse(localStorage.getItem('arMixStudioUI')); if(last&&last.channels?.length>=channels.length){last.channels.slice(0,channels.length).forEach((s,i)=>{if(typeof s.name==='string'&&s.name.trim())channels[i].name=s.name.trim().slice(0,26);
 if(channelIcons.includes(s.icon))channels[i].icon=s.icon;
 if(channelPalette.includes(s.color))channels[i].color=s.color;
 for(const k of ['fader','pan','trim','mute','solo','hpf'])if(s[k]!==undefined)channels[i][k]=s[k];if(Array.isArray(s.inserts))channels[i].inserts=s.inserts.filter(x=>typeof x==='string').slice(0,8)});sceneIdx=last.sceneIdx??0;masterGain=last.masterGain??-.1;masterPan=Number.isFinite(last.masterPan)?clamp(last.masterPan,-100,100):0;masterMute=!!last.masterMute;
  if(Array.isArray(last.masterInserts))masterInserts=last.masterInserts.filter(x=>typeof x==='string').slice(0,6);
  if(last.masterBypassed&&typeof last.masterBypassed==='object')masterBypassed={...last.masterBypassed};
  if(last.bypassed&&typeof last.bypassed==='object')bypassed={...last.bypassed};
  if(last.fxParams&&typeof last.fxParams==='object')fxParams=structuredClone(last.fxParams);
  renderMixer();renderMaster();flash('Previous UI session restored')}}catch{}
// Stop all simulated animation work while the tab is hidden, and restart on
// foregrounding. One owner per timer / RAF; never double-schedule on restore.
document.addEventListener('visibilitychange',()=>{
 if(document.hidden){
   if(meterTimer){clearTimeout(meterTimer);meterTimer=0}
   if(meterRAF){cancelAnimationFrame(meterRAF);meterRAF=0}
 }else if(!meterTimer&&!meterRAF)scheduleDemoMeter();
});
scheduleDemoMeter();
