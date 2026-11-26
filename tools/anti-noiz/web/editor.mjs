// SPDX-License-Identifier: AGPL-3.0-only
// Shared editor: isolated harness and approved native mixer use this exact
// renderer. Transport is the only authority for settings and observations.
export const defaults=Object.freeze({mode:'Auto',cleanDb:14,silenceDb:32,musicProtect:1,enabled:true,spectralManual:false,noiseOffsetDb:0,manualExpander:false,gate:false,thresholdDb:-40,ratio:4,rangeDb:60,attackMs:1,holdMs:100,releaseMs:120,kneeDb:0,sidechainHpHz:120,sidechainLpHz:12000,spectralEnabled:true,artifactControl:.5,vocalProtection:.5});
const proDefs=[['noiseOffsetDb','Noise threshold',-20,20,.5,'dB'],['artifactControl','Artifact control',0,1,.01,'%'],['vocalProtection','Vocal protection',0,1,.01,'%'],['thresholdDb','Threshold',-80,0,1,'dBFS'],['ratio','Ratio',1,20,.5,':1'],['rangeDb','Range',0,96,1,'dB'],['attackMs','Attack',.5,20,.5,'ms'],['holdMs','Hold',0,500,5,'ms'],['releaseMs','Release',10,1000,10,'ms'],['kneeDb','Knee',0,18,1,'dB'],['sidechainHpHz','Detector HPF',20,500,10,'Hz'],['sidechainLpHz','Detector LPF',1000,20000,100,'Hz']];
const bounds=[['cleanDb','Clean',0,40],['silenceDb','Silence',0,48],['musicProtect','Music Protect',0,1],...proDefs];
export const normalize=s=>({...defaults,...s});
export function valid(s){if(!s||!['Auto','Voice','Singing','Music'].includes(s.mode)||typeof s.enabled!=='boolean')return false;const n=Object.keys(s).length;if(n!==5&&n!==18&&n!==21)return false;if(n===5&&s.cleanDb>24)return false;const v=normalize(s);return Object.keys(defaults).every(k=>Object.hasOwn(v,k))&&Object.keys(s).every(k=>Object.hasOwn(defaults,k))&&['spectralManual','manualExpander','gate','spectralEnabled'].every(k=>typeof v[k]==='boolean')&&bounds.every(([k,_,min,max])=>Number.isFinite(v[k])&&v[k]>=min&&v[k]<=max)&&['cleanDb','silenceDb','musicProtect'].every(k=>Object.hasOwn(s,k))&&(n===5||Object.keys(defaults).filter(k=>n===21||!['spectralEnabled','artifactControl','vocalProtection'].includes(k)).every(k=>Object.hasOwn(s,k)));}
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
   if(!pending){if(!accepted)error='';accepted=normalize(m.settings);}
  }else if(m.kind==='ack'&&pending&&m.id===pending.id&&typeof m.applied==='boolean'){
   const next=queued;queued=null;accepted=normalize(m.settings);pending=null;error=m.applied?'':(m.error||'DSP rejected request');
   if(m.applied&&next&&Object.keys(next).some(k=>next[k]!==accepted[k])){
    submit('settings',next,true);return;
   }
   if(m.applied)appliedId=m.requestId||'';
  }else return;
  draw();
 }
 const unsubscribe=transport.subscribe(receive);
 function submit(kind,patch={},continuous=false){
  if(closed||pending||!accepted||!['settings','learn','clear','audition'].includes(kind))return false;
  const settings={...accepted,...patch};if(!valid(settings))return false;
  pending={kind,id:++seq,settings,continuous};error='';draw();
  try{transport.send(pending);}catch{pending=null;queued=null;error='Transport unavailable';draw();return false;}
  return true;
 }
 draw();transport.read();
 function adjust(key,value){
  if(closed||!accepted||!bounds.some(d=>d[0]===key)||!valid({...accepted,[key]:value}))return false;
  if(pending){
   if(!pending.continuous)return false;
   queued={...queued,[key]:value};draw();return true;
  }
  return accepted[key]===value||submit('settings',{[key]:value},true);
 }
 return {submit:(kind,patch)=>submit(kind,patch),adjust,state,dispose(){closed=true;queued=null;unsubscribe();},refresh(){if(!closed)transport.read();}};
}
// Presentation-only: one value per measured non-DC FFT bin. Never infer
// sub-bin detail or insert synthetic points. Work in linear mean-square power.
export function antiNoizSpectrumPresentation(){
 let previous=null,stamp=0,gridKey='',x=[],noiseRaw=null,noisePath='';
 const same=(a,b)=>a!==null&&a.length===b.length&&b.every((v,i)=>v===a[i]);
 const smooth=a=>{
  const power=a.map(v=>10**(v/10));
  // Mild 3-bin convex filter; preserve endpoints and limit peak loss /
  // adjacent-bin lift so narrow tonal noise is not visually smeared.
  return power.map((v,i)=>i===0||i===power.length-1?v:
   Math.max(v*0.7943282347,Math.min(v*1.995262315,
    (power[i-1]+14*v+power[i+1])/16)));
 };
 const path=power=>{
  let d='';
  for(let i=1;i<power.length;i++){
   const db=10*Math.log10(Math.max(1e-12,power[i]));
   const y=88*(1-Math.max(0,Math.min(120,db+120))/120);
   d+=(i===1?'M':'L')+x[i]+' '+y.toFixed(1);
  }
  return d;
 };
 function reset(){previous=null;stamp=0;gridKey='';x=[];noiseRaw=null;noisePath='';}
 function update(spec,now){
  if(!spec){reset();return {input:'',noise:'',fill:''};}
  const key=spec.bins+':'+spec.binHz;
  if(key!==gridKey){
   reset();gridKey=key;
   // First resolvable non-DC frequency through actual Nyquist.
   const span=Math.log(spec.bins-1);
   x=Array.from({length:spec.bins},(_,i)=>i===0?'0':(480*Math.log(i)/span).toFixed(1));
  }
  let input='';
  if(spec.input.length){
   const target=smooth(spec.input);
   if(previous&&now>=stamp&&now-stamp<=650){
    const dt=now-stamp,attack=1-Math.exp(-dt/40),release=1-Math.exp(-dt/200);
    for(let i=1;i<target.length;i++){
     const old=previous[i],v=target[i];
     target[i]=old+(v-old)*(v>=old?attack:release);
    }
   }
   previous=target;stamp=now;input=path(target);
  }else{previous=null;stamp=0;}
  // Trusted/learning noise is never animated; repaint only on changed
  // native values. A new Learn checkpoint may legitimately change it.
  if(!same(noiseRaw,spec.noise)){
   noiseRaw=spec.noise.slice();noisePath=path(smooth(spec.noise));
  }
  return {input,noise:noisePath,fill:noisePath?noisePath+'L480 88L0 88Z':''};
 }
 return {update,reset};
}
export function mount(root,transport,options={}){
 const assetBase=options.nativeAssets?'assets/':'../../../site/preview-v2/assets/';
 root.innerHTML=`<section class="an-panel" aria-label="Anti Noiz">
 <header class="an-head"><div class="an-identity"><h1>ANTI<span> NOIZ</span></h1><small>ARSONMIX <span class="an-brand-divider">/</span> ADAPTIVE DENOISER</small></div><div class="an-head-actions"><button class="armix-builtin-fx-power" data-power data-fx-state="unknown" type="button" aria-label="FX state unavailable" aria-pressed="false" disabled>⏻</button><button class="armix-builtin-fx-close" data-close type="button" aria-label="Close Anti Noiz" title="Close Anti Noiz"></button></div></header>
 <div class="an-presets" hidden><span class="an-engraved">PROGRAM</span><select data-preset aria-label="Anti Noiz preset"><option value="CUSTOM">Custom</option><option value="DEFAULT">Default</option></select></div>
 <div class="an-display" data-lcd-mode="gr"><div class="an-top"><div class="an-display-heading"><span class="an-display-title"><span data-display-title>GAIN REDUCTION</span><b data-display-unit>dB</b></span><span data-state>Waiting for DSP</span></div><div class="an-lcd-cue" aria-hidden="true"><span data-lcd-hint>Tap for spectrum</span><span class="an-lcd-pages"><i></i><i></i></span></div></div><div class="an-lcd-content"><div class="an-meter-view"><div class="an-meter"><label>Deepest spectral cut <output data-spectral>—</output></label><div class="an-meter-track"><i data-spectral-bar></i></div></div><div class="an-meter"><label>Residual expander <output data-residual>—</output></label><div class="an-meter-track"><i data-residual-bar></i></div></div><div class="an-meter-scale" aria-hidden="true"><span>0</span><span>24</span><span>48</span><span>72</span><span>96</span></div></div><div class="an-spectrum-view" hidden><div class="an-spectrum-key"><span class="an-input-key">Input</span><span class="an-noise-key" data-curve-label>Captured noise</span><span data-progress></span></div><div class="an-spectrum-plot"><svg class="an-spectrum" viewBox="0 0 480 88" preserveAspectRatio="none" role="img" aria-label="Input spectrum and learned noise power per FFT bin"><path class="an-spectrum-grid" data-grid/><path class="an-noise-fill" data-noise-fill/><path class="an-input-curve" data-input-curve/><path class="an-noise-curve" data-noise-curve/></svg><span class="an-spectrum-empty" data-spectrum-empty>No spectrum available</span></div><div class="an-freq-axis" aria-hidden="true"><span>200 Hz</span><span>1k</span><span>5k</span><span>20k</span></div><div class="an-db-axis" aria-hidden="true"><span>0</span><span>−60</span><span>−120</span></div></div></div><div class="an-legend" data-observation><span data-profile>No live telemetry</span><span data-learn-state>Learn —</span><span data-latency>Latency —</span></div><button class="an-lcd-hit" data-lcd-switch type="button" aria-label="Noise spectrum display" aria-pressed="false" title="Click or tap LCD for noise spectrum"></button></div>
 <div class="an-knobs"></div><div class="an-source"><span class="an-engraved">SOURCE</span><div class="an-modes" role="group" aria-label="Source mode"></div></div>
 <details class="an-pro"><summary>Advanced · manual noise control</summary><div class="an-pro-body"><div class="an-pro-row"><label><input type="checkbox" data-pro-flag="spectralManual"> Manual spectral</label><label><input type="checkbox" data-pro-flag="spectralEnabled"> Spectral on</label><span>Clean = reduction · profile required</span></div><div class="an-pro-grid" data-pro-spectral></div><div class="an-pro-row"><button type="button" data-clear-profile>Reset profile</button><button type="button" data-audition title="Replaces this channel output for 2 seconds. Use only during soundcheck.">Noise only · 2s</button></div><small data-audition-warning>Noise audition affects channel output, including audience sends. Soundcheck only. Requires manual spectral and manual expander or Silence 0.</small><div class="an-pro-row"><label><input type="checkbox" data-pro-flag="manualExpander"> Manual expander</label><label><input type="checkbox" data-pro-flag="gate"> Gate</label></div><svg class="an-pro-curve" viewBox="0 0 300 88" role="img" aria-label="Static expander transfer curve and measured detector"><path class="an-pro-gridlines" d="M20 8V74H292 M20 41H292 M111 8V74 M202 8V74"/><path class="an-pro-reference" d="M20 41L292 8"/><path data-pro-threshold class="an-pro-threshold"/><path data-pro-curve class="an-pro-transfer"/><circle data-pro-dot r="3" hidden/><text x="20" y="85">−90</text><text x="111" y="85">−60</text><text x="202" y="85">−30</text><text x="280" y="85">0 dBFS</text></svg><div class="an-pro-grid" data-pro-expander></div><div class="an-pro-row"><span>Detector <output data-detector>—</output></span><span>Range 96 dB + Gate = mute</span></div><small>Manual expander replaces adaptive Silence. Range 0 = bypass; Clean 0 = spectral bypass. Music mode protects the full signal.</small></div></details>
 <footer><button data-learn data-learning="false">Learn noise</button><span data-status role="status"></span><span class="an-stereo" title="Stereo processing is linked; not a toggle">STEREO<span>LINK</span></span></footer></section>`;
 const $=s=>root.querySelector(s), controls=[],proControls=[];
 for(const [key,name,min,max,step,unit] of proDefs){const el=document.createElement("label");el.className="an-pro-param";el.innerHTML=`<span>${name}<output>—</output></span><input type="range" min="${min}" max="${max}" step="${step}" aria-label="${name}">`;$(["noiseOffsetDb","artifactControl","vocalProtection"].includes(key)?"[data-pro-spectral]":"[data-pro-expander]").append(el);proControls.push({key,unit,input:el.querySelector("input"),out:el.querySelector("output")});}
 const proFlags=[...root.querySelectorAll("[data-pro-flag]")];
 const defs=[['cleanDb','Clean',40,.5,'dB'],['silenceDb','Silence',48,.5,'dB'],['musicProtect','Music Protect',1,.01,'%']];
 // Tangential dashed LED arc: every segment has the SAME radius.
 // Stationary metallic PNG, independent shadow and rotating pointer.
 const arcPoint=deg=>{const t=deg*Math.PI/180;return `${(50+45.5*Math.sin(t)).toFixed(2)} ${(50-45.5*Math.cos(t)).toFixed(2)}`;};
 const ledSegments=Array.from({length:27},(_,i)=>{const a=-135+3.2+i*(270-6.4)/26;return `M${arcPoint(a-3.2)}A45.5 45.5 0 0 1 ${arcPoint(a+3.2)}`;});
 const ledRing=ledSegments.join('');
 for(const [key,label,max,step,unit] of defs){const el=document.createElement('label');el.className='an-control';el.dataset.control=key;el.innerHTML=`<span class="an-dial"><svg viewBox="0 0 100 100" class="an-scale-ring" aria-hidden="true"><path class="an-led-off" d="${ledRing}"/><path class="an-led-on"/></svg><span class="an-knob-shadow" aria-hidden="true"></span><img class="an-knob-art" src="${assetBase}mixer-gain-knob-brushed-steel.png" alt="" draggable="false"><i aria-hidden="true"></i><input type="range" min="0" max="${max}" step="${step}" aria-label="${label}" title="Drag up/down · Shift for fine control · Double-click to reset"></span><span class="an-readout"><output>—</output></span><span class="an-control-name">${label}</span><span class="an-range-label" aria-hidden="true"><span>0</span><span>${unit==='%'?'100%':max+' dB'}</span></span>`;$('.an-knobs').append(el);controls.push({key,max,step,unit,el,input:el.querySelector('input'),out:el.querySelector('output'),led:el.querySelector('.an-led-on'),lit:-1,value:null,gesture:null,timer:null,draft:null});}
 for(const mode of ['Auto','Voice','Singing','Music']){const b=document.createElement('button');b.textContent=mode;b.dataset.mode=mode;b.setAttribute('aria-pressed','false');$('.an-modes').append(b);}
 let current=null;const buttons=[...root.querySelectorAll('button:not([data-close])')];
 const setText=(el,value)=>{if(el.textContent!==value)el.textContent=value;};
 const setPressed=(el,value)=>{const v=String(value);if(el.getAttribute('aria-pressed')!==v)el.setAttribute('aria-pressed',v);};
 const paintDial=(c,v)=>{if(c.value===v)return;c.value=v;setText(c.out,c.unit==='%'?Math.round(v*100)+'%':v.toFixed(1)+' dB');c.el.style.setProperty('--angle',(-135+270*v/c.max)+'deg');const lit=v>0?Math.min(27,1+Math.floor(26*v/c.max)):0;if(lit!==c.lit){c.lit=lit;c.led.setAttribute('d',ledSegments.slice(0,lit).join(''));}};
 const cancel=c=>{clearTimeout(c.timer);c.timer=null;c.draft=null;const g=c.gesture;c.gesture=null;if(g&&c.input.hasPointerCapture(g.id))c.input.releasePointerCapture(g.id);};
 const meterEls=new Map();
 function meter(name,gain,live){
  const db=live&&Number.isFinite(gain)?Math.max(0,Math.min(96,-20*Math.log10(Math.max(1e-8,gain)))):null;
  let els=meterEls.get(name);if(!els){els={out:$(name),bar:$(name.slice(0,-1)+'-bar]'),value:undefined};meterEls.set(name,els);}
  const v=db===null?null:Math.round(db*10)/10;if(els.value===v)return;els.value=v;
  setText(els.out,v===null?'—':v.toFixed(1)+' dB');
  els.bar.style.transform='scaleX('+(v===null?0:v/96)+')';
 }
 // Existing host polling cadence (~5.6 Hz). No extra IPC, FFT, timer,
 // animation loop or PCM processing; smooth only newly observed frames.
 const lcd=$('.an-display'),lcdSwitch=$('[data-lcd-switch]'),lcdHint=$('[data-lcd-hint]'),meterView=$('.an-meter-view'),spectrumView=$('.an-spectrum-view');
 const inputPath=$('[data-input-curve]'),noisePath=$('[data-noise-curve]'),noiseFill=$('[data-noise-fill]');
 let profileView=false,lastSpectrumKey='';
 const spectrumPresentation=antiNoizSpectrumPresentation();
 const freqAxis=$('.an-freq-axis');
 let chartKey='';
 function chartGrid(binHz,bins){
  const key=bins+':'+binHz;if(chartKey===key)return;chartKey=key;
  const nyquist=(bins-1)*binHz;
  const logX=f=>480*Math.log(f/binHz)/Math.log(nyquist/binHz);
  const labels=[200,1000,5000,20000],ticks=[200,500,1000,2000,5000,10000,20000];
  $('[data-grid]').setAttribute('d',ticks.filter(f=>f>=binHz&&f<=nyquist).map(f=>`M${logX(f).toFixed(1)} 0V88`).join('')+[0,22,44,66,88].map(y=>`M0 ${y}H480`).join(''));
  for(const [i,f] of labels.entries())freqAxis.children[i].style.left=(logX(f)/4.8)+'%';
 }
 chartGrid(187.5,129);
 const setPath=(el,d)=>{if(el.getAttribute('d')!==d)el.setAttribute('d',d);};
 function showProfile(show){if(profileView===show)return;profileView=show;meterView.hidden=show;spectrumView.hidden=!show;lcd.dataset.lcdMode=show?'spectrum':'gr';setPressed(lcdSwitch,show);setText($('[data-display-title]'),show?'NOISE SPECTRUM':'GAIN REDUCTION');setText($('[data-display-unit]'),show?'dBFS/bin':'dB');lastSpectrumKey='';spectrumPresentation.reset();}
 function paintLcdCue(learning){
  if(lcdSwitch.disabled!==learning)lcdSwitch.disabled=learning;
  const locked=String(learning);
  if(lcd.dataset.learning!==locked)lcd.dataset.learning=locked;
  setText(lcdHint,learning?'Capturing…':profileView?'Tap for GR':'Tap for spectrum');
  const title=learning?'Spectrum locked during noise capture':profileView?'Click or tap LCD for gain reduction':'Click or tap LCD for noise spectrum';
  if(lcdSwitch.title!==title)lcdSwitch.title=title;
 }
 function paintSpectrum(observation,live,hostOff){
  const phase=live?observation.learnStatus:'';
  // Default GR on each open. Only actual ongoing Learn forces Spectrum;
  // historical Accepted telemetry on reopen must not steal the selected view.
  const learning=phase==='Learning'&&observation.learning===true&&!hostOff;
  if(learning)showProfile(true);
  paintLcdCue(learning);
  if(!profileView)return;
  const spec=live&&!hostOff?observation.spectrum:null;
  const key=spec?observation.serial+':'+phase+':'+spec.bins+':'+spec.binHz:'none';
  if(key===lastSpectrumKey)return;lastSpectrumKey=key;
  if(spec)chartGrid(spec.binHz,spec.bins);
  const curves=spectrumPresentation.update(spec,performance.now());
  setPath(inputPath,curves.input);
  setPath(noisePath,curves.noise);
  setPath(noiseFill,curves.fill);
  setText($('[data-curve-label]'),spec?.candidate?'Captured noise':'Trusted noise');
  setText($('[data-progress]'),spec?.candidate?Math.round(spec.progress*100)+'%':phase==='Rejected'?'Rejected':phase==='Cancelled'?'Cancelled':'');
  $('[data-spectrum-empty]').hidden=!!(spec?.input.length||spec?.noise.length);
 }
 // A single native button covers the glass; mouse, touch, Enter and Space
 // share one click handler. View switching never submits an audio command.
 lcdSwitch.onclick=()=>{if(!lcdSwitch.disabled){showProfile(!profileView);if(lastView)paintHost(lastView);}};
 let lastView=null;
 function paintHost(view){
  const {accepted,pending,adjusting,observation}=view;
  const power=options.hostPower?.()||'ON';
  const live=observation?.live===true;
  const hostOff=power!=='ON'||observation?.rackBypassed===true;
  const unavailable=power==='UNAVAILABLE';
  $('[data-learn]').disabled=!accepted||pending||!accepted.enabled||accepted.mode==='Music'||!live||hostOff||observation?.learning===true||observation?.effectiveMode==='Music';
  const label={GLOBAL:'Global FX bypassed',RACK:'Rack bypassed',SLOT:'Slot bypassed',LOGICAL:'Offline settings',UNAVAILABLE:'FX unavailable'}[power];
  setText($('[data-state]'),label||(accepted?(observation?.rackBypassed?'Rack bypassed':accepted.enabled?(live?observation.effectiveMode+' · Live':accepted.mode+' · Offline'):'DSP bypassed'):'Offline'));
  meter('[data-spectral]',observation?.spectralGain,live&&!hostOff&&accepted?.enabled);
  meter('[data-residual]',observation?.residualGain,live&&!hostOff&&accepted?.enabled);
  const observed=live&&!unavailable;
  const level=observation?.detectorLevel;
  if($(".an-pro").open&&accepted){
   const p=view.desired?{...accepted,...view.desired}:accepted;
   const px=x=>20+272*(Math.max(-90,Math.min(0,x))+90)/90,py=y=>8-66*Math.max(-180,Math.min(0,y))/180;
   const cut=x=>{if(!p.manualExpander)return 0;if(p.gate)return x<p.thresholdDb?p.rangeDb:0;const d=p.thresholdDb-x,k=p.kneeDb;const below=k===0?Math.max(0,d):d<=-k/2?0:d>=k/2?d:(d+k/2)**2/(2*k);return Math.min(p.rangeDb,below*(p.ratio-1));};
   setPath($("[data-pro-curve]"),Array.from({length:181},(_,i)=>{const x=-90+i/2;return (i?"L":"M")+px(x).toFixed(1)+" "+py(x-cut(x)).toFixed(1);}).join(" "));
   setPath($("[data-pro-threshold]"),"M"+px(p.thresholdDb)+" 8V74");
   const dot=$("[data-pro-dot]"),show=observed&&!hostOff&&p.manualExpander&&Number.isFinite(level)&&Number.isFinite(observation?.residualGain);
   dot.toggleAttribute('hidden',!show);if(show){const x=20*Math.log10(Math.max(1e-6,level)),gr=20*Math.log10(Math.max(1e-8,observation.residualGain));dot.setAttribute("cx",px(x));dot.setAttribute("cy",py(x+gr));}
  }
  setText($("[data-detector]"),observed&&!hostOff&&accepted?.manualExpander&&Number.isFinite(level)?(20*Math.log10(Math.max(1e-6,level))).toFixed(1)+" dBFS":"—");
  for(const f of proFlags){f.checked=!!accepted?.[f.dataset.proFlag];f.disabled=!accepted||pending||unavailable;}
  for(const c of proControls){const value=view.desired?.[c.key]??accepted?.[c.key];if(Number.isFinite(value)){c.input.value=value;setText(c.out,c.unit==="%"?Math.round(value*100)+"%":String(value)+(c.unit===":1"?":1":" "+c.unit));}else setText(c.out,"—");c.input.disabled=!accepted||(pending&&!adjusting)||unavailable||(["noiseOffsetDb","artifactControl","vocalProtection"].includes(c.key)?!accepted?.spectralManual||!accepted?.spectralEnabled:!accepted?.manualExpander);}
  const learning=observed&&observation.learning===true&&!hostOff&&accepted?.enabled===true;
  const learn=$('[data-learn]'), learningValue=String(learning);
  if(learn.dataset.learning!==learningValue)learn.dataset.learning=learningValue;
  setText($('[data-profile]'),observed?(observation.profile?'Profile ready':'No profile'):'No live telemetry');
  setText($('[data-learn-state]'),observed?(observation.learning?'Learning':observation.learnStatus==='Idle'?'Learn idle':observation.learnStatus):'Learn —');
  setText($('[data-latency]'),observed?observation.latency+' samples':'Latency —');
  paintSpectrum(observation,observed,hostOff||!accepted?.enabled);
  const profile=$('[data-profile]'), ready=String(observed&&observation.profile===true);
  if(profile.dataset.ready!==ready)profile.dataset.ready=ready;
  // Same semantic FX chrome states as PEQ. Accepted DSP and host bypass
  // remain separate authorities; never repaint from a submitted draft.
  const powerButton=$('[data-power]');
  const fxState=unavailable||!accepted?'unknown':power==='GLOBAL'||power==='RACK'||(observation?.rackBypassed&&power!=='SLOT')?'rack-off':power==='SLOT'||!accepted.enabled?'off':'on';
  if(powerButton.dataset.fxState!==fxState)powerButton.dataset.fxState=fxState;
  setPressed(powerButton,fxState==='on');
  const powerTitle=fxState==='unknown'?'FX state unavailable':fxState==='rack-off'?'FX rack/global bypassed · enable upstream first':power==='SLOT'?'FX slot bypassed · click to enable slot':fxState==='on'?'DSP on · click to bypass':'DSP bypassed · click to enable';
  if(powerButton.title!==powerTitle){powerButton.title=powerTitle;powerButton.setAttribute('aria-label',powerTitle);}
  powerButton.disabled=pending||fxState==='unknown'||fxState==='rack-off'||(power==='SLOT'&&!options.enableSlot);
  for(const b of buttons)if(!b.hasAttribute('data-learn')&&!b.hasAttribute('data-lcd-switch')&&!b.hasAttribute('data-power'))b.disabled=!accepted||pending||unavailable;
  const canCommand=!!accepted&&!pending&&live&&!hostOff&&!unavailable;
  $("[data-clear-profile]").disabled=!canCommand||(!observation?.profile&&!observation?.learning);
  const audition=$("[data-audition]"),active=live&&observation?.audition===true&&!hostOff;
  audition.disabled=!canCommand||(!active&&(!accepted.enabled||!accepted.spectralEnabled||!accepted.spectralManual||accepted.cleanDb===0||!observation?.profile||observation?.learning||observation?.effectiveMode==="Music"||(!accepted.manualExpander&&accepted.silenceDb!==0)));
  setText(audition,active?"Stop noise audition":"Noise only · 2s");setPressed(audition,active);
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
   }else{c.value=null;c.lit=-1;c.led.setAttribute('d','');setText(c.out,'—');}
  }
   for(const b of root.querySelectorAll('[data-mode]'))setPressed(b,b.dataset.mode===accepted?.mode);
  paintHost(view);
  setText($('[data-status]'),pending?'Applying…':error);
  options.onState?.(view);
 });
 for(const c of controls){
  const quantize=v=>Math.max(0,Math.min(c.max,Number((Math.round(v/c.step)*c.step).toFixed(2))));
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
 $("[data-clear-profile]").onclick=()=>model.submit("clear");
 $("[data-audition]").onclick=()=>model.submit("audition");
 $(".an-pro").addEventListener("toggle",()=>{if(lastView)paintHost(lastView);});
 for(const c of proControls)c.input.addEventListener("input",()=>model.adjust(c.key,Number(c.input.value)));
 for(const f of proFlags)f.addEventListener("change",()=>model.submit("settings",{[f.dataset.proFlag]:f.checked}));
 for(const b of root.querySelectorAll('[data-mode]'))b.onclick=()=>model.submit('settings',{mode:b.dataset.mode});
 $('[data-power]').onclick=()=>{if(!current||$('[data-power]').disabled)return;if(options.hostPower?.()==='SLOT')options.enableSlot?.();else model.submit('settings',{enabled:!current.enabled});};$('[data-learn]').onclick=()=>{if((!options.hostPower||options.hostPower()==='ON')&&!$('[data-learn]').disabled)model.submit('learn');};
 $('[data-close]').onclick=()=>options.onClose?.();
 $('[data-close]').hidden=!options.onClose;
 return {model,refreshHostPower(){if(lastView)paintHost(lastView);},dispose(){for(const c of controls)cancel(c);model.dispose();root.replaceChildren();}};
}
