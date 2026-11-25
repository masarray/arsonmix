// SPDX-License-Identifier: AGPL-3.0-only
// Shared editor: isolated harness and approved native mixer use this exact
// renderer. Transport is the only authority for settings and observations.
export const defaults=Object.freeze({mode:'Auto',cleanDb:14,silenceDb:32,musicProtect:1,enabled:true});
export function valid(s){return !!s&&['Auto','Voice','Singing','Music'].includes(s.mode)&&typeof s.enabled==='boolean'&&[['cleanDb',24],['silenceDb',48],['musicProtect',1]].every(([k,max])=>Number.isFinite(s[k])&&s[k]>=0&&s[k]<=max);}
export function controller(transport,render){
 let accepted=null,pending=null,queued=null,seq=0,closed=false,error='Waiting for DSP',observation=null,appliedId='';
 const state=()=>({accepted,pending:!!pending,adjusting:!!pending?.continuous,desired:pending?.continuous?{...pending.settings,...queued}:null,error,observation,appliedId});
 const draw=()=>render(state());
 function receive(m){
  if(closed||!m)return;
  appliedId='';
  if(m.kind==='unavailable'){
   accepted=null;pending=null;queued=null;observation=null;error=m.error||'Native instance unavailable';draw();return;
  }
  if(m.kind==='failure'&&pending&&m.id===pending.id){
   pending=null;queued=null;error=m.error||'DSP request failed';draw();return;
  }
  if(!valid(m.settings))return;
  if(m.kind==='snapshot'){
   observation=m.observation||null;
   if(!pending){if(!accepted)error='';accepted={...m.settings};}
  }else if(m.kind==='ack'&&pending&&m.id===pending.id&&typeof m.applied==='boolean'){
   const next=queued;queued=null;accepted={...m.settings};pending=null;error=m.applied?'':(m.error||'DSP rejected request');
   if(m.applied&&next&&Object.keys(next).some(k=>next[k]!==accepted[k])){
    submit('settings',next,true);return;
   }
   if(m.applied)appliedId=m.requestId||'';
  }else return;
  draw();
 }
 const unsubscribe=transport.subscribe(receive);
 function submit(kind,patch={},continuous=false){
  if(closed||pending||!accepted||!['settings','learn'].includes(kind))return false;
  const settings={...accepted,...patch};if(!valid(settings))return false;
  pending={kind,id:++seq,settings,continuous};error='';draw();
  try{transport.send(pending);}catch{pending=null;queued=null;error='Transport unavailable';draw();return false;}
  return true;
 }
 draw();transport.read();
 function adjust(key,value){
  if(closed||!accepted||!['cleanDb','silenceDb','musicProtect'].includes(key)||!valid({...accepted,[key]:value}))return false;
  if(pending){
   if(!pending.continuous)return false;
   queued={...queued,[key]:value};draw();return true;
  }
  return accepted[key]===value||submit('settings',{[key]:value},true);
 }
 return {submit:(kind,patch)=>submit(kind,patch),adjust,state,dispose(){closed=true;queued=null;unsubscribe();},refresh(){if(!closed)transport.read();}};
}
export function mount(root,transport,options={}){
 root.innerHTML=`<section class="an-panel" aria-label="Anti Noiz"><header class="an-head"><div><h1>Anti Noiz</h1><small>LIVE NOISE CONTROL</small></div><div class="an-head-actions"><button data-power aria-label="DSP Power" aria-pressed="false">⏻</button><button data-close aria-label="Close Anti Noiz">×</button></div></header><div class="an-presets" hidden><select data-preset aria-label="Anti Noiz preset"><option value="CUSTOM">Custom</option><option value="DEFAULT">Default</option></select></div><div class="an-display"><div class="an-top"><span>NOISE CONTROL</span><span data-state>Waiting for DSP</span></div><div class="an-meter"><label>Deepest spectral cut <output data-spectral>—</output></label><div class="an-meter-track"><i data-spectral-bar></i></div></div><div class="an-meter"><label>Residual expander <output data-residual>—</output></label><div class="an-meter-track"><i data-residual-bar></i></div></div><div class="an-legend" data-observation>Waiting for native telemetry</div></div><div class="an-knobs"></div><div class="an-modes" role="group" aria-label="Source mode"></div><footer><button data-learn>Learn noise</button><span data-status role="status"></span><span>STEREO LINK</span></footer></section>`;
 const $=s=>root.querySelector(s), controls=[];
 const defs=[['cleanDb','Clean',24,.5,'dB'],['silenceDb','Silence',48,.5,'dB'],['musicProtect','Music Protect',1,.01,'%']];
 for(const [key,label,max,step,unit] of defs){const el=document.createElement('label');el.className='an-control';el.innerHTML=`<span class="an-dial"><i aria-hidden="true"></i><input type="range" min="0" max="${max}" step="${step}" aria-label="${label}" title="Drag up/down · Shift for fine control · Double-click to reset"></span><output>—</output><span>${label}</span>`;$('.an-knobs').append(el);controls.push({key,max,step,unit,el,input:el.querySelector('input'),out:el.querySelector('output'),value:null,gesture:null,timer:null,draft:null});}
 for(const mode of ['Auto','Voice','Singing','Music']){const b=document.createElement('button');b.textContent=mode;b.dataset.mode=mode;b.setAttribute('aria-pressed','false');$('.an-modes').append(b);}
 let current=null;const buttons=[...root.querySelectorAll('button:not([data-close])')];
 const setText=(el,value)=>{if(el.textContent!==value)el.textContent=value;};
 const setPressed=(el,value)=>{const v=String(value);if(el.getAttribute('aria-pressed')!==v)el.setAttribute('aria-pressed',v);};
 const paintDial=(c,v)=>{if(c.value===v)return;c.value=v;setText(c.out,c.unit==='%'?Math.round(v*100)+'%':v.toFixed(1)+' dB');c.el.style.setProperty('--angle',(-135+270*v/c.max)+'deg');};
 const cancel=c=>{clearTimeout(c.timer);c.timer=null;c.draft=null;const g=c.gesture;c.gesture=null;if(g&&c.input.hasPointerCapture(g.id))c.input.releasePointerCapture(g.id);};
 const meterEls=new Map();
 function meter(name,gain,live){
  const db=live&&Number.isFinite(gain)?Math.max(0,Math.min(48,-20*Math.log10(Math.max(1e-8,gain)))):null;
  let els=meterEls.get(name);if(!els){els={out:$(name),bar:$(name.slice(0,-1)+'-bar]'),value:undefined};meterEls.set(name,els);}
  const v=db===null?null:Math.round(db*10)/10;if(els.value===v)return;els.value=v;
  setText(els.out,v===null?'—':v.toFixed(1)+' dB');
  els.bar.style.transform='scaleX('+(v===null?0:v/48)+')';
 }
 let lastView=null;
 function paintHost(view){
  const {accepted,pending,adjusting,observation}=view;
  const power=options.hostPower?.()||'ON';
  const live=observation?.live===true;
  const hostOff=power!=='ON'||observation?.rackBypassed===true;
  const unavailable=power==='UNAVAILABLE';
  $('[data-learn]').disabled=!accepted||pending||!accepted.enabled||accepted.mode==='Music'||!live||hostOff||observation?.learning===true||observation?.effectiveMode==='Music';
  const label={GLOBAL:'Global FX bypassed',RACK:'Rack bypassed',SLOT:'Slot bypassed',LOGICAL:'Offline settings',UNAVAILABLE:'FX unavailable'}[power];
  setText($('[data-state]'),label||(accepted?(observation?.rackBypassed?'Rack bypassed':accepted.enabled?(live?observation.effectiveMode:accepted.mode)+' policy':'DSP bypassed'):'Offline'));
  meter('[data-spectral]',observation?.spectralGain,live&&!hostOff&&accepted?.enabled);
  meter('[data-residual]',observation?.residualGain,live&&!hostOff&&accepted?.enabled);
  setText($('[data-observation]'),live&&!unavailable?`${observation.profile?'Profile ready':'No learned profile'} · ${observation.learnStatus} · ${observation.latency} samples`:'No live telemetry · Learn requires active PCM');
  for(const b of buttons)if(!b.hasAttribute('data-learn'))b.disabled=!accepted||pending||unavailable;
  for(const c of controls){c.input.disabled=!accepted||(pending&&!adjusting)||unavailable;if(c.input.disabled){cancel(c);if(accepted){c.input.value=accepted[c.key];paintDial(c,accepted[c.key]);}}}
 }
 const model=controller(transport,view=>{
  lastView=view;
  const {accepted,pending,desired,error}=view;current=accepted;
  for(const c of controls){
   if(!accepted||error){cancel(c);}
   if(accepted){
    const v=c.draft??desired?.[c.key]??accepted[c.key];
    if(Number(c.input.value)!==v)c.input.value=v;paintDial(c,v);
   }else{c.value=null;setText(c.out,'—');}
  }
  setPressed($('[data-power]'),!!accepted?.enabled);
  for(const b of root.querySelectorAll('[data-mode]'))setPressed(b,b.dataset.mode===accepted?.mode);
  paintHost(view);
  setText($('[data-status]'),pending?'Applying…':error);
  options.onState?.(view);
 });
 for(const c of controls){
  const quantize=v=>Math.max(0,Math.min(c.max,Math.round(v/c.step)*c.step));
  const flush=()=>{clearTimeout(c.timer);c.timer=null;if(c.draft===null)return;const value=c.draft;if(!model.adjust(c.key,value)){cancel(c);if(lastView&&current){c.input.value=current[c.key];paintDial(c,current[c.key]);}}};
  const preview=v=>{c.draft=quantize(v);c.input.value=c.draft;paintDial(c,c.draft);if(c.timer===null)c.timer=setTimeout(flush,40);};
  c.input.addEventListener('pointerdown',e=>{
   if(c.input.disabled||e.button!==0||c.gesture)return;
   e.preventDefault();c.input.focus({preventScroll:true});c.input.setPointerCapture(e.pointerId);
   c.gesture={id:e.pointerId,y:e.clientY,x:e.clientX,raw:Number(c.input.value)};
  });
  c.input.addEventListener('pointermove',e=>{
   const g=c.gesture;if(!g||e.pointerId!==g.id)return;
   const delta=(g.y-e.clientY)+(e.clientX-g.x)*.5;g.x=e.clientX;g.y=e.clientY;
   g.raw=Math.max(0,Math.min(c.max,g.raw+delta*c.max/(e.shiftKey?900:180)));preview(g.raw);
  });
  const release=e=>{
   const g=c.gesture;if(!g||e.pointerId!==g.id)return;
   c.gesture=null;flush();c.draft=null;if(c.input.hasPointerCapture(e.pointerId))c.input.releasePointerCapture(e.pointerId);
   if(lastView&&current){const v=lastView.desired?.[c.key]??current[c.key];c.input.value=v;paintDial(c,v);}
  };
  c.input.addEventListener('pointerup',release);
  c.input.addEventListener('pointercancel',release);
  c.input.addEventListener('lostpointercapture',release);
  c.input.addEventListener('input',()=>{if(!c.gesture){const v=Number(c.input.value);preview(v);flush();c.draft=null;}});
  c.input.addEventListener('dblclick',e=>{e.preventDefault();if(!c.input.disabled){preview(defaults[c.key]);flush();c.draft=null;}});
 }
 for(const b of root.querySelectorAll('[data-mode]'))b.onclick=()=>model.submit('settings',{mode:b.dataset.mode});
 $('[data-power]').onclick=()=>current&&model.submit('settings',{enabled:!current.enabled});$('[data-learn]').onclick=()=>{if((!options.hostPower||options.hostPower()==='ON')&&!$('[data-learn]').disabled)model.submit('learn');};
 $('[data-close]').onclick=()=>options.onClose?.();
 $('[data-close]').hidden=!options.onClose;
 return {model,refreshHostPower(){if(lastView)paintHost(lastView);},dispose(){for(const c of controls)cancel(c);model.dispose();root.replaceChildren();}};
}
