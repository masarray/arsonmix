// SPDX-License-Identifier: AGPL-3.0-only
// Transport injected by existing native editor owner or isolated dev harness.
export const defaults=Object.freeze({mode:'Auto',cleanDb:14,silenceDb:32,musicProtect:1,enabled:true});
export function valid(s){return !!s&&['Auto','Voice','Singing','Music'].includes(s.mode)&&typeof s.enabled==='boolean'&&[['cleanDb',24],['silenceDb',48],['musicProtect',1]].every(([k,max])=>Number.isFinite(s[k])&&s[k]>=0&&s[k]<=max);}
export function controller(transport,render){
 let accepted=null,pending=null,seq=0,closed=false,error='Waiting for DSP';
 const draw=()=>render({accepted,pending:!!pending,error});
 function receive(m){
  if(closed||!m||!valid(m.settings))return;
  if(m.kind==='snapshot'&&!pending){accepted={...m.settings};error='';}
  else if(m.kind==='ack'&&pending&&m.id===pending.id&&typeof m.applied==='boolean'){
   accepted={...m.settings};pending=null;error=m.applied?'':'DSP rejected request';
  }else return;
  draw();
 }
 const unsubscribe=transport.subscribe(receive);
 function submit(kind,patch={}){
  if(closed||pending||!accepted)return false;
  const settings={...accepted,...patch};if(!valid(settings))return false;
  pending={kind,id:++seq,settings};error='';draw();
  try{transport.send(pending);}catch{pending=null;error='Transport unavailable';draw();return false;}
  return true;
 }
 draw();transport.read();
 return {submit,dispose(){closed=true;unsubscribe();},refresh(){if(!closed&&!pending)transport.read();}};
}
export function mount(root,transport){
 root.innerHTML=`<section class="an-panel" aria-label="Anti Noiz"><header><div><h1>Anti Noiz</h1><small>INTELLIGENT MUSICAL DENOISER</small></div><button data-power aria-label="Power" aria-pressed="false">⏻</button></header><div class="an-display"><div class="an-top"><span>SPECTRAL ANALYSIS</span><span data-state>Waiting for DSP</span></div><svg viewBox="0 0 340 114" role="img" aria-label="Spectrum unavailable until native telemetry is connected"><g stroke="#34404a" stroke-width=".7"><path d="M30 8H333 M30 32H333 M30 56H333 M30 80H333 M30 104H333 M30 8V104 M90 8V104 M150 8V104 M210 8V104 M270 8V104 M333 8V104"/></g><g fill="#83909b" font-size="8"><text x="4" y="11">0</text><text x="4" y="59">−40</text><text x="4" y="107">−80</text><text x="110" y="60" data-spectrum>Awaiting spectrum telemetry</text></g></svg><div class="an-legend">Input · Output · Noise profile</div></div><div class="an-knobs"></div><div class="an-modes" role="group" aria-label="Source mode"></div><footer><button data-learn>Learn noise</button><span data-status role="status"></span><span>STEREO LINK</span></footer></section>`;
 const $=s=>root.querySelector(s), controls=[];
 const defs=[['cleanDb','Clean',24,.5,'dB'],['silenceDb','Silence',48,.5,'dB'],['musicProtect','Music Protect',1,.01,'%']];
 for(const [key,label,max,step,unit] of defs){const el=document.createElement('label');el.className='an-control';el.innerHTML=`<span class="an-dial" aria-hidden="true"><i></i></span><output>—</output><span>${label}</span><input type="range" min="0" max="${max}" step="${step}" aria-label="${label}">`;$('.an-knobs').append(el);controls.push({key,max,unit,el,input:el.querySelector('input'),out:el.querySelector('output')});}
 for(const mode of ['Auto','Voice','Singing','Music']){const b=document.createElement('button');b.textContent=mode;b.dataset.mode=mode;b.setAttribute('aria-pressed','false');$('.an-modes').append(b);}
 let current=null;const buttons=[...root.querySelectorAll('button')];
 const model=controller(transport,({accepted,pending,error})=>{current=accepted;for(const b of buttons)b.disabled=!accepted||pending;for(const c of controls){c.input.disabled=!accepted||pending;if(accepted){const v=accepted[c.key];c.input.value=v;c.out.textContent=c.unit==='%'?Math.round(v*100)+'%':v.toFixed(1)+' dB';c.el.style.setProperty('--angle',(-135+270*v/c.max)+'deg');}}if(accepted){$('[data-power]').setAttribute('aria-pressed',String(accepted.enabled));$('[data-learn]').disabled=pending||!accepted.enabled||accepted.mode==='Music';for(const b of root.querySelectorAll('[data-mode]'))b.setAttribute('aria-pressed',String(b.dataset.mode===accepted.mode));}$('[data-state]').textContent=accepted?(accepted.enabled?accepted.mode+' policy':'Bypassed'):'Offline';$('[data-status]').textContent=pending?'Applying…':error;});
 for(const c of controls)c.input.addEventListener('change',()=>{if(!model.submit('settings',{[c.key]:Number(c.input.value)})&&current)c.input.value=current[c.key];});
 for(const b of root.querySelectorAll('[data-mode]'))b.onclick=()=>model.submit('settings',{mode:b.dataset.mode});
 $('[data-power]').onclick=()=>current&&model.submit('settings',{enabled:!current.enabled});$('[data-learn]').onclick=()=>model.submit('learn');
 return ()=>{model.dispose();root.replaceChildren();};
}
