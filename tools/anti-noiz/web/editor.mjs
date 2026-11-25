// SPDX-License-Identifier: AGPL-3.0-only
// Shared editor: isolated harness and approved native mixer use this exact
// renderer. Transport is the only authority for settings and observations.
export const defaults=Object.freeze({mode:'Auto',cleanDb:14,silenceDb:32,musicProtect:1,enabled:true});
export function valid(s){return !!s&&['Auto','Voice','Singing','Music'].includes(s.mode)&&typeof s.enabled==='boolean'&&[['cleanDb',24],['silenceDb',48],['musicProtect',1]].every(([k,max])=>Number.isFinite(s[k])&&s[k]>=0&&s[k]<=max);}
export function controller(transport,render){
 let accepted=null,pending=null,seq=0,closed=false,error='Waiting for DSP',observation=null,appliedId='';
 const state=()=>({accepted,pending:!!pending,error,observation,appliedId});
 const draw=()=>render(state());
 function receive(m){
  if(closed||!m)return;
  appliedId='';
  if(m.kind==='unavailable'){
   accepted=null;pending=null;observation=null;error=m.error||'Native instance unavailable';draw();return;
  }
  if(m.kind==='failure'&&pending&&m.id===pending.id){
   pending=null;error=m.error||'DSP request failed';draw();return;
  }
  if(!valid(m.settings))return;
  if(m.kind==='snapshot'){
   observation=m.observation||null;
   if(!pending){accepted={...m.settings};error='';}
  }else if(m.kind==='ack'&&pending&&m.id===pending.id&&typeof m.applied==='boolean'){
   accepted={...m.settings};pending=null;error=m.applied?'':(m.error||'DSP rejected request');
   if(m.applied)appliedId=m.requestId||'';
  }else return;
  draw();
 }
 const unsubscribe=transport.subscribe(receive);
 function submit(kind,patch={}){
  if(closed||pending||!accepted||!['settings','learn'].includes(kind))return false;
  const settings={...accepted,...patch};if(!valid(settings))return false;
  pending={kind,id:++seq,settings};error='';draw();
  try{transport.send(pending);}catch{pending=null;error='Transport unavailable';draw();return false;}
  return true;
 }
 draw();transport.read();
 return {submit,state,dispose(){closed=true;unsubscribe();},refresh(){if(!closed)transport.read();}};
}
export function mount(root,transport,options={}){
 root.innerHTML=`<section class="an-panel" aria-label="Anti Noiz"><header class="an-head"><div><h1>Anti Noiz</h1><small>INTELLIGENT MUSICAL DENOISER</small></div><div class="an-head-actions"><button data-power aria-label="DSP Power" aria-pressed="false">⏻</button><button data-close aria-label="Close Anti Noiz">×</button></div></header><div class="an-presets" hidden><select data-preset aria-label="Anti Noiz preset"><option value="CUSTOM">Custom</option><option value="DEFAULT">Default</option></select></div><div class="an-display"><div class="an-top"><span>NOISE CONTROL</span><span data-state>Waiting for DSP</span></div><div class="an-meter"><label>Deepest spectral cut <output data-spectral>—</output></label><div class="an-meter-track"><i data-spectral-bar></i></div></div><div class="an-meter"><label>Residual expander <output data-residual>—</output></label><div class="an-meter-track"><i data-residual-bar></i></div></div><div class="an-legend" data-observation>Waiting for native telemetry</div></div><div class="an-knobs"></div><div class="an-modes" role="group" aria-label="Source mode"></div><footer><button data-learn>Learn noise</button><span data-status role="status"></span><span>STEREO LINK</span></footer></section>`;
 const $=s=>root.querySelector(s), controls=[];
 const defs=[['cleanDb','Clean',24,.5,'dB'],['silenceDb','Silence',48,.5,'dB'],['musicProtect','Music Protect',1,.01,'%']];
 for(const [key,label,max,step,unit] of defs){const el=document.createElement('label');el.className='an-control';el.innerHTML=`<span class="an-dial" aria-hidden="true"><i></i></span><output>—</output><span>${label}</span><input type="range" min="0" max="${max}" step="${step}" aria-label="${label}">`;$('.an-knobs').append(el);controls.push({key,max,unit,el,input:el.querySelector('input'),out:el.querySelector('output')});}
 for(const mode of ['Auto','Voice','Singing','Music']){const b=document.createElement('button');b.textContent=mode;b.dataset.mode=mode;b.setAttribute('aria-pressed','false');$('.an-modes').append(b);}
 let current=null;const buttons=[...root.querySelectorAll('button:not([data-close])')];
 function meter(name,gain,live){
  const db=live&&Number.isFinite(gain)?Math.max(0,Math.min(48,-20*Math.log10(Math.max(1e-8,gain)))):null;
  $(name).textContent=db===null?'—':db.toFixed(1)+' dB';
  $(name.slice(0,-1)+'-bar]').style.width=db===null?'0%':(db/48*100)+'%';
 }
 const model=controller(transport,view=>{
  const {accepted,pending,error,observation}=view;current=accepted;
  for(const b of buttons)b.disabled=!accepted||pending;
  for(const c of controls){c.input.disabled=!accepted||pending;if(accepted){const v=accepted[c.key];c.input.value=v;c.out.textContent=c.unit==='%'?Math.round(v*100)+'%':v.toFixed(1)+' dB';c.el.style.setProperty('--angle',(-135+270*v/c.max)+'deg');}else c.out.textContent='—';}
  const live=observation?.live===true, rack=observation?.rackBypassed===true;
  if(accepted){$('[data-power]').setAttribute('aria-pressed',String(accepted.enabled));$('[data-learn]').disabled=pending||!accepted.enabled||accepted.mode==='Music'||!live||rack||observation?.learning===true||observation?.effectiveMode==='Music';for(const b of root.querySelectorAll('[data-mode]'))b.setAttribute('aria-pressed',String(b.dataset.mode===accepted.mode));}
  $('[data-state]').textContent=accepted?(rack?'Rack bypassed':accepted.enabled?(live?observation.effectiveMode:accepted.mode)+' policy':'DSP bypassed'):'Offline';
  meter('[data-spectral]',observation?.spectralGain,live&&!rack&&accepted?.enabled);
  meter('[data-residual]',observation?.residualGain,live&&!rack&&accepted?.enabled);
  $('[data-observation]').textContent=live?`${observation.profile?'Profile ready':'No learned profile'} · ${observation.learnStatus} · ${observation.latency} samples`:'No live telemetry · Learn requires active PCM';
  $('[data-status]').textContent=pending?'Applying…':error;
  options.onState?.(view);
 });
 for(const c of controls)c.input.addEventListener('change',()=>{if(!model.submit('settings',{[c.key]:Number(c.input.value)})&&current)c.input.value=current[c.key];});
 for(const b of root.querySelectorAll('[data-mode]'))b.onclick=()=>model.submit('settings',{mode:b.dataset.mode});
 $('[data-power]').onclick=()=>current&&model.submit('settings',{enabled:!current.enabled});$('[data-learn]').onclick=()=>model.submit('learn');
 $('[data-close]').onclick=()=>options.onClose?.();
 $('[data-close]').hidden=!options.onClose;
 return {model,dispose(){model.dispose();root.replaceChildren();}};
}
