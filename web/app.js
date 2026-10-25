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
 ['FX Return','◎',['Reverb'],-5.1,0],
 ['Monitor','◉',['EQ'],-2.8,15],
 ['Aux Out','◉',['Limiter'],-11.5,0]
];
const extraNames=['BGV 2','Guitar 2','FX Return 2','Talkback'];
const channels=raw.map((a,i)=>({id:i+1,name:a[0],icon:a[1],color:colors[i],inserts:[...a[2]],fader:a[3],pan:a[4],trim:0,mute:false,solo:false,pre:false,hpf:i%4!==0,a:true,b:false}));
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
function updateMeterVisual(key,level,now){
 const el=document.querySelector(`[data-meter="${key}"]`);if(!el)return;
 const value=clamp(level,0,99);
 el.style.setProperty('--level',value.toFixed(1)+'%');
 const slot=el.closest('.meter'),old=peakHold[key];
 if(!old||value>=old.value||now>=old.until)peakHold[key]={value,until:now+1600};
 const pk=slot?.querySelector('.meter-peak');if(pk)pk.style.bottom=peakHold[key].value.toFixed(1)+'%';
 if(value>=97){clipLatch[key]=true;slot?.querySelector('.clip-led')?.classList.add('latched')}
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
 $('channelList').innerHTML=labels.map((label,i)=>`<div class="list-item ${i===currentChannel?'selected':''}" data-choose="${i}"><span class="swatch" style="background:${channels[i].color||colors[i%colors.length]}"></span><span class="index">${i+1}</span><span>${label}</span></div>`).join('');
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
function openChannelIdentity(index){
 editingChannel=index;editingColor=channels[index].color||colors[index];editingIcon=channels[index].icon;
 const modal=$('channelIdentity');if(!modal)return;
 $('identityIndex').textContent=String(index+1).padStart(2,'0');
 $('identityName').value=channels[index].name;
 $('identityIcons').innerHTML=channelIcons.map(icon=>`<button type="button" class="identity-icon ${icon===editingIcon?'selected':''}" data-icon-choice="${safeText(icon)}" aria-pressed="${icon===editingIcon}">${safeText(icon)}</button>`).join('');
 $('identityColors').innerHTML=channelPalette.map(color=>`<button type="button" class="identity-color ${color===editingColor?'selected':''}" data-color-choice="${color}" style="--choice:${color}" aria-label="Channel color ${color}" aria-pressed="${color===editingColor}"></button>`).join('');
 modal.classList.remove('hidden');$('identityName').focus();$('identityName').select();
}
function closeChannelIdentity(){editingChannel=-1;$('channelIdentity')?.classList.add('hidden')}
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
function makeStrip(ch,i){let inserts=ch.inserts.slice(0,6);while(inserts.length<6) inserts.push('');return `<article class="strip ${i===currentChannel?'selected':''}" data-strip="${i}" style="--ch:${ch.color||colors[i]}">
 <div class="ch-id">${i+1}</div><div class="rack">${inserts.map((n,j)=>`<div class="insert-slot ${n&&bypassed[fxKey(i,j)]?'is-bypassed':''}"><button class="insert ${!n?'empty':''}" data-insert="${i}:${j}" title="${n?'Open or replace '+safeText(n):'Add VST effect'}">${n?'':'<span class="insert-add-glyph" aria-hidden="true">＋</span>'}<span class="insert-name">${n? safeText(n):'ADD FX'}</span></button>${n?`<button type="button" class="fx-power ${bypassed[fxKey(i,j)]?'is-bypassed':''}" data-fxpower="${i}:${j}" aria-pressed="${!bypassed[fxKey(i,j)]}" aria-label="${bypassed[fxKey(i,j)]?'Enable':'Bypass'} ${safeText(n)}" title="${bypassed[fxKey(i,j)]?'Enable':'Bypass'} ${safeText(n)}">⏻</button>`:''}</div>`).join('')}</div>
 <div class="strip-controls"><div class="trim-row"><span class="mini-label">TRIM<br>dB</span><div class="mini-lcd" data-trim-read="${i}">${fmt(ch.trim)}</div></div>${knobMarkup('trim',i,ch.trim,'Trim')}<div class="pan-control">${panMarkup(ch,i)}<div class="pan-readline"><span aria-hidden="true">L</span><strong data-pan-read="${i}">${panText(ch.pan)}</strong><span aria-hidden="true">R</span></div></div><div class="utility-row"><button data-pre="${i}" class="${ch.pre?'active':''}">Ø</button><button data-hpf="${i}" class="${ch.hpf?'active':''}">HPF</button></div></div>
 <div class="channel-actions"><button class="candy mute ${ch.mute?'active':''}" data-mute="${i}" aria-pressed="${ch.mute}">MUTE</button><button class="candy solo ${ch.solo?'active':''}" data-solo="${i}" aria-pressed="${ch.solo}">SOLO</button></div>
 <div class="db-display" data-read="${i}">${fmt(ch.fader)}</div><div class="fader-zone"><div class="scale"><span>10</span><span>0</span><span>−10</span><span>−20</span><span>−30</span><span>−40</span><span>−60</span></div><div class="fader" data-fader="${i}" role="slider" tabindex="0" aria-orientation="vertical" aria-label="${ch.name} fader" aria-valuemin="-60" aria-valuemax="10" aria-valuenow="${ch.fader}"><div class="cap" style="--pos:${levelToTop(ch.fader)}%"></div></div><div class="meter" data-meter-key="${i}"><button class="clip-led" type="button" data-clip="${i}" title="Clear latched clip — ${safeText(ch.name)}" aria-label="Clear ${safeText(ch.name)} clip"></button><div class="meter-fill" data-meter="${i}" style="--level:40%"></div><div class="meter-peak" aria-hidden="true"></div></div></div>
 <div class="fader-tools"><button data-send="${i}:a" class="${ch.a?'active':''}">A</button><button data-send="${i}:b" class="${ch.b?'active':''}">B</button></div>
 <div class="strip-name" data-editstrip="${i}" role="button" tabindex="0" aria-label="Edit ${safeText(ch.name)} name, icon and color" title="Edit channel identity"><span class="strip-caption">${safeText(ch.name)}</span><span class="track-icon">${safeText(ch.icon)}</span></div>
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
 <div class="fader-tools master-tools"><span class="master-route-label">MAIN L/R</span><button type="button" data-clear-all-clips="1" title="Reset latched clips">CLEAR CLIP</button></div>
 <div class="strip-name master-strip-name" role="group" aria-label="Master output"><span class="strip-caption">MASTER</span><span class="track-icon" aria-hidden="true"><svg viewBox="0 0 24 24" width="19" height="19" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="m11 5-5 4H3v6h3l5 4V5Z"/><path d="M15 9a5 5 0 0 1 0 6M18 6a9 9 0 0 1 0 12"/></svg></span></div>`;
 syncClipLeds();
}
let bands=[{freq:80,gain:2.1,q:1},{freq:300,gain:-3.2,q:1},{freq:2200,gain:-1.5,q:1.2},{freq:7800,gain:3.5,q:.8}];let activeBand=1;
const fx=x=>Math.log(x/20)/Math.log(1000)*1000;
const fy=y=>250-y*9;
function editorGraph(){let points=[];for(let i=0;i<=260;i++){const f=20*Math.pow(1000,i/260);let g=0;bands.forEach(b=>{const d=Math.log2(f/b.freq);g+=b.gain*Math.exp(-Math.pow(d*b.q*1.2,2))});points.push(`${i/260*1000},${fy(g).toFixed(1)}`)}return `<svg viewBox="0 0 1000 500" preserveAspectRatio="none" id="eqSvg" aria-label="Interactive parametric EQ"><line x1="0" y1="250" x2="1000" y2="250" stroke="#688d9b" stroke-width="1.4"/><polyline points="${points.join(' ')}" fill="none" stroke="#79e8ff" stroke-width="3" vector-effect="non-scaling-stroke"/>${bands.map((b,i)=>`<g data-band="${i}" style="cursor:move"><circle cx="${fx(b.freq)}" cy="${fy(b.gain)}" r="21" fill="${['#d6af40','#54cc7e','#b878e7','#27bfe5'][i]}" fill-opacity=".26" stroke="${['#d6af40','#54cc7e','#b878e7','#27bfe5'][i]}" stroke-width="2"/><circle cx="${fx(b.freq)}" cy="${fy(b.gain)}" r="11" fill="${['#d6af40','#54cc7e','#b878e7','#27bfe5'][i]}"/><text x="${fx(b.freq)}" y="${fy(b.gain)+4}" text-anchor="middle" fill="#071017" font-size="12" font-weight="700">${i+1}</text></g>`).join('')}</svg>`}
function renderEditor(){const c=channels[currentChannel];$('editor').innerHTML=`<div class="editor-top"><h2>CHANNEL ${currentChannel+1}　/　${c.name.toUpperCase()}</h2><span class="mini-lcd">PARAMETRIC EQ</span><button class="chrome-icon" id="editorClose">CLOSE ×</button></div><div class="editor-flex"><div class="eq-graph">${editorGraph()}</div><div class="eq-side"><div class="eq-title">FILTER PARAMETERS</div>${bands.map((b,i)=>`<div class="knob-row"><button class="right-button ${i===activeBand?'selected':''}" data-activeband="${i}">BAND ${i+1}</button><div class="knob-wrap"><span class="knob-value">${Math.round(b.freq)} Hz</span><span class="knob-value">${b.gain.toFixed(1)} dB</span></div></div>`).join('')}<div class="editor-help">Demo EQ surface only · Audio DSP is not connected.</div><button class="chrome-icon" id="resetEq">RESET EQ</button></div></div>`}
function setView(next){
 view=next;
 document.querySelectorAll('[data-view]').forEach(b=>b.classList.toggle('selected',b.dataset.view===next));
 $('mixer').classList.toggle('hidden',next!=='MIX');
 $('editor').classList.toggle('hidden',next!=='EDIT');
 $('rackWorkspace').classList.toggle('hidden',!['FX RACK','SIGNAL FLOW','SETUP'].includes(next));
 if(next==='EDIT')renderEditor();
 if(next==='FX RACK')renderRackWorkspace();
 if(next==='SIGNAL FLOW')renderSignalFlow();
 if(next==='SETUP')renderSetup();
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
function chooseChannel(i){if(i>=channels.length){flash('Additional bank channels are not populated in this UI study');return}currentChannel=i;renderMixer();if(view!=='MIX')renderEditor()}
function chooseScene(i){loadSnapshotByIndex((i+scenes.length)%scenes.length)}
function toggleState(i,key){channels[i][key]=!channels[i][key];const btn=document.querySelector(`[data-${key}="${i}"]`);if(btn){btn.classList.toggle('active',channels[i][key]);btn.setAttribute('aria-pressed',String(channels[i][key]))}send(key,channels[i][key],i)}
document.addEventListener('click',e=>{const b=e.target.closest('button,[data-choose],[data-scene],[data-editstrip],[data-group]');
 const icon=e.target.closest('[data-icon-choice]'),color=e.target.closest('[data-color-choice]');
 if(icon){editingIcon=icon.dataset.iconChoice;document.querySelectorAll('[data-icon-choice]').forEach(n=>{n.classList.toggle('selected',n===icon);n.setAttribute('aria-pressed',String(n===icon))});return}
 if(color){editingColor=color.dataset.colorChoice;document.querySelectorAll('[data-color-choice]').forEach(n=>{n.classList.toggle('selected',n===color);n.setAttribute('aria-pressed',String(n===color))});return}
 if(!b){const strip=e.target.closest('[data-strip]');if(strip&&view==='MIX')focusChannel(+strip.dataset.strip);else if(e.target.closest('#masterLeft')&&view==='MIX')focusMaster();return;}
 const originStrip=b.closest('[data-strip]');
 if(originStrip&&view==='MIX')focusChannel(+originStrip.dataset.strip);else if(b.closest('#masterLeft')&&view==='MIX')focusMaster();
 if(b.dataset.view){setView(b.dataset.view);return}if(b.dataset.choose!==undefined){chooseChannel(+b.dataset.choose);return}
 if(b.dataset.mute!==undefined){toggleState(+b.dataset.mute,'mute');return}if(b.dataset.solo!==undefined){toggleState(+b.dataset.solo,'solo');return}
 if(b.dataset.pre!==undefined){let i=+b.dataset.pre;channels[i].pre=!channels[i].pre;b.classList.toggle('active');send('phase',channels[i].pre,i);return}
 if(b.dataset.hpf!==undefined){let i=+b.dataset.hpf;channels[i].hpf=!channels[i].hpf;b.classList.toggle('active');send('hpf',channels[i].hpf,i);return}
 if(b.dataset.send!==undefined){let [i,k]=b.dataset.send.split(':');channels[+i][k]=!channels[+i][k];b.classList.toggle('active');send(`send${k.toUpperCase()}`,channels[+i][k],+i);return}
 if(b.dataset.masterInsert!==undefined){pendingMasterSlot=+b.dataset.masterInsert;openPluginBrowser(currentChannel,true);return}
 if(b.dataset.masterPower!==undefined){const slot=+b.dataset.masterPower;masterBypassed[slot]=!masterBypassed[slot];renderMaster();persistSession();send('masterFxBypass',{slot,bypass:masterBypassed[slot]},-1);return}
 if(b.dataset.clip!==undefined){resetClip(b.dataset.clip);return}
 if(b.dataset.clearAllClips!==undefined){Object.keys(clipLatch).forEach(resetClip);Object.keys(peakHold).forEach(k=>delete peakHold[k]);return}
 if(b.dataset.fxpower!==undefined){const [i,j]=b.dataset.fxpower.split(':').map(Number);setFxBypass(i,j,!bypassed[fxKey(i,j)]);return}
 if(b.dataset.insert){let [i,j]=b.dataset.insert.split(':').map(Number);currentChannel=i;pendingSlot=j;openPluginBrowser(i);return}
 if(b.dataset.editstrip!==undefined){openChannelIdentity(+b.dataset.editstrip);return}
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
 if(b.dataset.activeband!==undefined){activeBand=+b.dataset.activeband;renderEditor();return}
 if(b.dataset.side){document.querySelectorAll('[data-side]').forEach(x=>x.classList.toggle('selected',x===b));if(b.dataset.side==='fx')setView('FX RACK');else setView('MIX');return}
 switch(b.id){case'identityCancel':closeChannelIdentity();break;case'identitySave':commitChannelIdentity();break;case'prevScene':chooseScene(sceneIdx-1);break;case'nextScene':chooseScene(sceneIdx+1);break;case'editorClose':setView('MIX');break;case'masterMute':masterMute=!masterMute;b.textContent='MUTE';b.classList.toggle('active',masterMute);b.setAttribute('aria-pressed',String(masterMute));send('masterMute',masterMute,-1);break;case'resetEq':bands.forEach(b=>b.gain=0);renderEditor();break;case'addScene':saveMixerSnapshot();break;case'saveFxPreset':saveFxChainPreset();break;case'menuScene':exportSession();break;case'closeBrowser':closePluginBrowser();break;case'closeFxInspector':pluginEditorIndex=null;renderRackWorkspace();break;case'modeToggle':toggleMode();break;case'chooseFxFile':importSession();break;case'browse':importSession();break;case'settingsBtn':setView('SETUP');break;default:if(b.id)flash('UI study: '+b.textContent.trim().slice(0,28));}
});
document.addEventListener('dblclick',e=>{const pan=e.target.closest('[data-pan]');if(pan){e.preventDefault();e.stopPropagation();updatePan(pan.dataset.pan==='master'?'master':+pan.dataset.pan,0)}},true);
document.addEventListener('dblclick',e=>{const st=e.target.closest('[data-strip]');if(st){chooseChannel(+st.dataset.strip);setView('EDIT')}});
let drag=null;
document.addEventListener('pointerdown',e=>{const f=e.target.closest('[data-fader]'),p=e.target.closest('[data-pan]'),k=e.target.closest('[data-knob]'),node=e.target.closest('[data-band]');if(!f&&!p&&!k&&!node)return;const el=f||p||k||node;
 const strip=e.target.closest('[data-strip]');
 if(strip&&view==='MIX')focusChannel(+strip.dataset.strip);else if(e.target.closest('#masterLeft')&&view==='MIX')focusMaster();
 if(f){const idx=f.dataset.fader,fromCap=!!e.target.closest('.cap');
   drag={type:'fader',el:f,index:idx,fromCap,startY:e.clientY,
     startDb:idx==='master'?masterGain:channels[+idx].fader};
   // Motorized-console behavior: the rail is a focus/relative-drag surface.
   // Never teleport gain to pointer Y, either on pointerdown or on the first move.
 }
 else if(p){const index=p.dataset.pan==='master'?'master':+p.dataset.pan,fromThumb=!!e.target.closest('.pan-thumb');
 drag={type:'pan',el:p,index,fromThumb,startX:e.clientX,startPan:index==='master'?masterPan:channels[index].pan};
 if(!fromThumb){const r=p.getBoundingClientRect();updatePan(index,((e.clientX-r.left-9)/Math.max(1,r.width-18))*200-100,true)}}
 else if(k){drag={type:'knob',el,initial:e.clientY,original:k.dataset.knob==='phones'?phones:channels[+k.dataset.ch][k.dataset.knob],param:k.dataset.knob}}
 else if(node){drag={type:'eq',el,index:+node.dataset.band};activeBand=drag.index}
 if(drag&&(drag.type==='fader'||drag.type==='pan'))drag.el.classList.add('is-dragging');
 if(el.setPointerCapture)el.setPointerCapture(e.pointerId);e.preventDefault();});
document.addEventListener('pointermove',e=>{if(!drag)return;
 if(drag.type==='fader'){const r=drag.el.getBoundingClientRect();
   const distance=Math.max(24,r.height-66);
   updateFader(drag.index,drag.startDb+(drag.startY-e.clientY)*70/distance)}
 else if(drag.type==='pan'){const r=drag.el.getBoundingClientRect(),travel=Math.max(1,r.width-18);
 updatePan(drag.index,drag.fromThumb?drag.startPan+(e.clientX-drag.startX)*200/travel:((e.clientX-r.left-9)/travel)*200-100,true)}
 else if(drag.type==='knob'){const delta=(drag.initial-e.clientY);updateKnob(drag.el,drag.original+delta*.35)}
 else if(drag.type==='eq'){const svg=$('eqSvg');if(!svg)return;const r=svg.getBoundingClientRect();const x=clamp((e.clientX-r.left)/r.width,0,1),y=clamp((e.clientY-r.top)/r.height,0,1);bands[drag.index].freq=Math.round(20*Math.pow(1000,x));bands[drag.index].gain=+(clamp((.5-y)*500/9,-18,18)).toFixed(1);renderEditor();}
});
for(const event of ['pointerup','pointercancel'])document.addEventListener(event,()=>{
 if(drag?.el)drag.el.classList.remove('is-dragging');
 drag=null;flushUiParams()
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
let lastMeterFrame=0;function meters(t){
 // JUCE desktop reads actual audio meters; browser demo only animates a preview.
 if(window.__JUCE__?.backend?.emitEvent)return;
 requestAnimationFrame(meters);if(t-lastMeterFrame<33||document.hidden)return;lastMeterFrame=t;
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
let pendingSlot=-1, activeCategory='All', easy=true, userSnapshots={},savedChains={},bypassed={},fxParams={},pluginEditorIndex=null;
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
function renderSetup(){$('rackWorkspace').innerHTML=`<div class="feature-top"><div><span class="eyebrow">AUDIO DEVICE WIZARD</span><h2>Make your live setup simple</h2><p>Windows ASIO / WASAPI support is a core requirement for the native build.</p></div><button class="ghost-action" id="backMixer">BACK TO MIXER</button></div><div class="setup-grid"><div class="setup-section"><h3>1 · Audio Driver</h3><label>Driver API<select id="driverSelect"><option>ASIO — when available</option><option>WASAPI Shared</option><option>WASAPI Exclusive</option></select></label><label>Input Source<select><option>Demo microphone (not connected)</option></select></label><label>Output Device<select><option>Demo master output (not connected)</option></select></label><p style="color:#81b0c2;font-size:10px">Actual device enumeration and stream opening belong to the upcoming native engine.</p></div><div class="setup-section"><h3>2 · Live Latency</h3><label>Sample Rate<select id="rateSelect"><option>48000 Hz</option><option>44100 Hz</option><option>96000 Hz</option></select></label><label>ASIO/WASAPI Buffer<select id="bufferSelect"><option>64 samples</option><option>128 samples</option><option>256 samples</option></select></label><p style="font-size:11px;color:#8ed1df">At 48 kHz, 64 samples ≈ 1.33 ms buffer period. This is NOT the device round-trip latency.</p></div><div class="setup-section"><h3>3 · Your Voice</h3><button class="action" data-preset="Warm Karaoke">USE WARM KARAOKE PRESET</button><p style="font-size:11px;color:#85aab8">This changes the FX chain preview and can later recall stored VST3 states automatically.</p></div><div class="setup-section"><h3>4 · Live Safety</h3><p style="font-size:11px;color:#a1b9c5;line-height:1.8">Before livestream: verify your microphone level, output device, monitoring feedback, total latency and plugin stability. The future engine will display xruns, dropouts, latency and recovery state here.</p></div></div>`}
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
function applySession(session){if(!session||session.schemaVersion!==1||!Array.isArray(session.channels)||session.channels.length!==channels.length){flash('Unsupported session format');return}for(let i=0;i<channels.length;i++){const src=session.channels[i],c=channels[i];
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
function toggleMode(){easy=!easy;$('app').classList.toggle('easy',easy);$('modeToggle').classList.toggle('active',easy);$('modeToggle').textContent=easy?'EASY VIEW':'PRO VIEW';flash(easy?'Easy view: simplified controls':'Pro view: all controls visible')}
$('app').classList.add('easy');$('modeToggle').classList.add('active');
$('pluginSearch').addEventListener('input',renderPluginBrowser);
document.addEventListener('input',e=>{const el=e.target;if(!el.dataset.fxparam)return;const slot=+el.dataset.slot,key=el.dataset.fxparam;if(!['mix','amount','output'].includes(key))return;const v=getFxParams(currentChannel,slot);v[key]=+el.value;const out=el.closest('label')?.querySelector('output');if(out)out.textContent=key==='output'?`${v[key]>0?'+':''}${v[key]} dB`:`${v[key]}%`;send('fxDemoParam',{slot,key,value:v[key]},currentChannel)});
$('pluginModal').addEventListener('click',e=>{if(e.target===$('pluginModal'))closePluginBrowser()});
$('channelIdentity').addEventListener('click',e=>{if(e.target===$('channelIdentity'))closeChannelIdentity()});
$('identityName').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();commitChannelIdentity()}});
document.addEventListener('keydown',e=>{if(e.key==='Escape'){closePluginBrowser();closeChannelIdentity()}if((e.key==='Enter'||e.key===' ')&&e.target.matches('[data-editstrip]')){e.preventDefault();openChannelIdentity(+e.target.dataset.editstrip)}});
document.addEventListener('change',e=>{if(e.target.id==='rackChannel'){currentChannel=+e.target.value;renderRackWorkspace();renderMixer()}});
document.addEventListener('click',e=>{const id=e.target.closest('button')?.id;if(id==='saveFxFromRack')saveFxChainPreset();else if(id==='recallFxPreset')recallFxChain();else if(id==='openSetupFromFlow')setView('SETUP');else if(id==='flowFxButton')setView('FX RACK');else if(id==='backMixer')setView('MIX')});
let lastPersist=0;setInterval(()=>{if(Date.now()-lastPersist<2000)return;lastPersist=Date.now();persistSession()},5000);
try{userSnapshots=JSON.parse(localStorage.getItem('armix-v03-snapshots')||'{}');savedChains=JSON.parse(localStorage.getItem('armix-v03-chains')||'{}');for(const k of Object.keys(userSnapshots))if(!scenes.includes(k))scenes.push(k)}catch{userSnapshots={};savedChains={}}

createSide();renderMixer();renderMaster();try{let last=JSON.parse(localStorage.getItem('armix-v03-current'));if(!last)last=JSON.parse(localStorage.getItem('arMixStudioUI')); if(last&&last.channels?.length===channels.length){last.channels.forEach((s,i)=>{if(typeof s.name==='string'&&s.name.trim())channels[i].name=s.name.trim().slice(0,26);
 if(channelIcons.includes(s.icon))channels[i].icon=s.icon;
 if(channelPalette.includes(s.color))channels[i].color=s.color;
 for(const k of ['fader','pan','trim','mute','solo','hpf'])if(s[k]!==undefined)channels[i][k]=s[k];if(Array.isArray(s.inserts))channels[i].inserts=s.inserts.filter(x=>typeof x==='string').slice(0,8)});sceneIdx=last.sceneIdx??0;masterGain=last.masterGain??-.1;masterPan=Number.isFinite(last.masterPan)?clamp(last.masterPan,-100,100):0;masterMute=!!last.masterMute;
  if(Array.isArray(last.masterInserts))masterInserts=last.masterInserts.filter(x=>typeof x==='string').slice(0,6);
  if(last.masterBypassed&&typeof last.masterBypassed==='object')masterBypassed={...last.masterBypassed};
  if(last.bypassed&&typeof last.bypassed==='object')bypassed={...last.bypassed};
  if(last.fxParams&&typeof last.fxParams==='object')fxParams=structuredClone(last.fxParams);
  renderMixer();renderMaster();flash('Previous UI session restored')}}catch{}
requestAnimationFrame(meters);
