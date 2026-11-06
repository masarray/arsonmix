/* ArSonMix Built-in Channel EQ: small operator overlay in the ONLY /preview/.
 * Browser renders RESPONSE FROM ACCEPTED PARAMETERS, never audio/FFT/WebAudio.
 * Static Pages is explicitly a visual DESIGN DEMO. Native uses ACK + LATER
 * authoritative snapshot; browser drafts never enter the mixer graph. */
(()=>{
 'use strict';
 const LABELS=['HPF','LOW','P1','P2','P3','P4','HIGH','LPF'];
 const TYPES=['hp','ls','bell','bell','bell','bell','hs','lp'];
 const DEFAULT=[80,160,250,500,1500,4500,9500,18000];
 const COLORS=['#9dc7ff','#eabb80','#e0a5db','#8eace8','#86d4ac','#e0b67c','#d99da8','#86cfe4'];
 const empty=()=>DEFAULT.map((f,i)=>({enabled:false,frequencyHz:f,gainDb:0,q:i>=2&&i<=5?1:.70710678,slope:12}));
 const clamp=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
 const hzToX=(hz,w)=>28+Math.log(clamp(hz,20,20000)/20)/Math.log(1000)*(w-58);
 const xToHz=(x,w)=>20*Math.pow(1000,clamp((x-28)/(w-58),0,1));
 const dbToY=(db,h)=>h/2-clamp(db,-18,18)*(h-39)/36;
 const yToDb=(y,h)=>clamp((h/2-y)*36/(h-39),-18,18);
 const isCut=i=>i===0||i===7;
 const gainEnabled=i=>i!==0&&i!==7;
 const mk=(tag,cl,text)=>{const el=document.createElement(tag);if(cl)el.className=cl;if(text!==undefined)el.textContent=text;return el;};
 const root=mk('section');root.id='mxBuiltinEq';root.hidden=true;root.setAttribute('role','dialog');root.setAttribute('aria-modal','false');root.setAttribute('aria-label','Built-in channel equalizer');
 const head=mk('header','mx-eq-top'),mark=mk('i','mx-eq-mark'),brand=mk('strong','', 'ArSonMix · Channel EQ');
 const channelLabel=mk('span','mx-eq-channel'),spacer=mk('span','mx-eq-spacer'),mode=mk('span','mx-eq-mode');
 const close=mk('button','mx-eq-close','×');close.type='button';close.title='Close channel EQ';head.append(mark,brand,channelLabel,spacer,mode,close);
 const workspace=mk('div','mx-eq-workspace');
 const graph=mk('div','mx-eq-chart'),canvas=mk('canvas'),hint=mk('div','mx-eq-hint','LOG FREQUENCY · RESPONSE (NOT SPECTRUM)'),legend=mk('div','mx-eq-legend','±18 dB');canvas.setAttribute('aria-label','Parametric EQ frequency response graph');graph.append(canvas,hint,legend);
 const rail=mk('div','mx-eq-rail'),buttons=[];LABELS.forEach((title,i)=>{const b=mk('button','mx-eq-band',title);b.type='button';b.dataset.slot=String(i);b.style.setProperty('--eq-band',COLORS[i]);const led=mk('i');b.prepend(led);rail.append(b);buttons.push(b);});
 const controls=mk('div','mx-eq-controls');
 const powerCol=mk('div','mx-eq-vertical');const power=mk('button','mx-eq-switch','ENABLE');power.type='button';power.setAttribute('aria-pressed','false');const flat=mk('button','mx-eq-reset','FLAT BAND');flat.type='button';powerCol.append(power,flat);
 const field=(label,type,min,max,step)=>{const wrap=mk('label','',label),v=mk(type==='slope'?'select':'input');v.setAttribute('aria-label',label);if(type==='slope'){[12,24].forEach(n=>{const o=mk('option','',`${n} dB/oct`);o.value=String(n);v.append(o);});}else{v.type='number';v.min=String(min);v.max=String(max);v.step=String(step);}wrap.append(v);return {wrap,v};};
 const freq=field('FREQUENCY · Hz','frequency',20,20000,1),gain=field('GAIN · dB','gain',-18,18,.1),q=field('Q · WIDTH','q',.2,12,.01),slope=field('CUT SLOPE','slope');
 const resetCol=mk('div','mx-eq-vertical');const typeName=mk('div','mx-eq-channel','BAND');resetCol.append(typeName);
 controls.append(powerCol,freq.wrap,gain.wrap,q.wrap,slope.wrap,resetCol);
 const footer=mk('footer','mx-eq-footer'),status=mk('span','mx-eq-status'),footerNote=mk('span','mx-eq-footnote','IIR · NO LOOKAHEAD · NO FFT');footer.append(status,footerNote);
 workspace.append(graph,rail,controls,footer);root.append(head,workspace);
 const attach=()=>{if(!root.isConnected)document.body.append(root);};
 if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',attach,{once:true});else attach();
 const ctx=canvas.getContext('2d',{alpha:false});
 let current=null,channelId=null,epoch=null,revision=null,bands=empty(),slot=2,sampleRate=48000;
 let pending=null,waitingAck=false,snapshotReady=false,reqSeq=1,activeGesture=null,demonstration=true;
 const nativeBackend=()=>window.__JUCE__?.backend;
 const nativeRoute=()=>window.__ARMIX_BUILTIN_EQ_CHANNEL__;
 const setStatus=(message,kind='')=>{status.textContent=message;status.className='mx-eq-status '+kind;};
 const isOpen=()=>!root.hidden;
 const isEditable=()=>demonstration||Boolean(channelId&&snapshotReady&&!pending);
 const resetNative=()=>{channelId=null;epoch=null;revision=null;pending=null;waitingAck=false;snapshotReady=false;bands=empty();setStatus('NATIVE: WAITING FOR ACCEPTED CHANNEL', 'warn');render();};
 const updateInputs=()=>{
   const b=bands[slot];
   buttons.forEach((node,i)=>{node.classList.toggle('selected',slot===i);node.classList.toggle('on',bands[i].enabled);node.setAttribute('aria-pressed',String(bands[i].enabled));});
   power.setAttribute('aria-pressed',String(b.enabled));power.textContent=b.enabled?'ENABLED':'ENABLE';
   freq.v.value=String(Math.round(b.frequencyHz));gain.v.value=b.gainDb.toFixed(1);q.v.value=b.q.toFixed(2);slope.v.value=String(b.slope);
   typeName.textContent=LABELS[slot];gain.v.disabled=isCut(slot)||!isEditable();q.v.disabled=isCut(slot)||!isEditable();
   slope.v.disabled=!isCut(slot)||!isEditable();freq.v.disabled=!isEditable();power.disabled=!isEditable();flat.disabled=!isEditable();
   buttons.forEach(x=>{x.disabled=false;});
 };
 const num=(x,d)=>Number.isFinite(Number(x))?Number(x):d;
 const validBand=(i,b)=>Boolean(b)&&typeof b.enabled==='boolean'&&Number.isFinite(b.frequencyHz)&&b.frequencyHz>=20&&b.frequencyHz<=20000&&Number.isFinite(b.gainDb)&&Math.abs(b.gainDb)<=18&&Number.isFinite(b.q)&&b.q>=.2&&b.q<=12&&(b.slope===12||b.slope===24);
 const coeff=(type,b,sr,q)=>{
   const w=2*Math.PI*b.frequencyHz/sr,c=Math.cos(w),sin=Math.sin(w),a=Math.pow(10,b.gainDb/40),al=sin/(2*q),edge=sin*Math.sqrt(a)*Math.SQRT2;
   let v;
   if(type==='bell')v=[1+al*a,-2*c,1-al*a,1+al/a,-2*c,1-al/a];
   else if(type==='hp')v=[(1+c)/2,-(1+c),(1+c)/2,1+al,-2*c,1-al];
   else if(type==='lp')v=[(1-c)/2,1-c,(1-c)/2,1+al,-2*c,1-al];
   else if(type==='ls')v=[a*((a+1)-(a-1)*c+edge),2*a*((a-1)-(a+1)*c),a*((a+1)-(a-1)*c-edge),(a+1)+(a-1)*c+edge,-2*((a-1)+(a+1)*c),(a+1)+(a-1)*c-edge];
   else v=[a*((a+1)+(a-1)*c+edge),-2*a*((a-1)+(a+1)*c),a*((a+1)+(a-1)*c-edge),(a+1)-(a-1)*c+edge,2*((a-1)-(a+1)*c),(a+1)-(a-1)*c-edge];
   const a0=v[3];return [v[0]/a0,v[1]/a0,v[2]/a0,1,v[4]/a0,v[5]/a0];
 };
 const responseDb=(hz)=>{
   let result=0;
   for(let i=0;i<8;++i){const b=bands[i];if(!b.enabled)continue;
     const cut=isCut(i),qs=cut&&b.slope===24?[.541196100146197,1.306562964876377]:[cut?Math.SQRT1_2:b.q];
     for(const q of qs){const c=coeff(TYPES[i],b,sampleRate,q),w=2*Math.PI*hz/48000,cos=Math.cos(w),sin=Math.sin(w),cos2=Math.cos(2*w),sin2=Math.sin(2*w);
       const nr=c[0]+c[1]*cos+c[2]*cos2,ni=-(c[1]*sin+c[2]*sin2),dr=1+c[4]*cos+c[5]*cos2,di=-(c[4]*sin+c[5]*sin2);
       result+=10*Math.log10(Math.max(1e-24,(nr*nr+ni*ni)/Math.max(1e-24,dr*dr+di*di)));
     }
   }return result;
 };
 const draw=()=>{
   if(root.hidden||!ctx)return;
   const r=canvas.getBoundingClientRect();if(r.width<100||r.height<80)return;
   const dpr=Math.min(window.devicePixelRatio||1,2),w=r.width,h=r.height;
   canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);
   ctx.fillStyle='#15212a';ctx.fillRect(0,0,w,h);const y0=dbToY(0,h);
   ctx.font='9px Consolas,monospace';ctx.textBaseline='top';
   for(const f of [20,50,100,200,500,1000,2000,5000,10000,20000]){
     const x=hzToX(f,w);ctx.strokeStyle=f===1000?'#334c57':'#273a45';ctx.lineWidth=.75;ctx.beginPath();ctx.moveTo(x,17);ctx.lineTo(x,h-20);ctx.stroke();ctx.fillStyle='#78919f';ctx.fillText(f>=1000?`${f/1000}k`:String(f),x-8,h-15);
   }
   for(const db of [-18,-12,-6,0,6,12,18]){
     const y=dbToY(db,h);ctx.strokeStyle=db===0?'#587581':'#2b3b45';ctx.lineWidth=db===0?1.2:.7;ctx.beginPath();ctx.moveTo(23,y);ctx.lineTo(w-28,y);ctx.stroke();ctx.fillStyle='#8ca5b3';ctx.fillText(db>0?'+'+db:String(db),3,y-5);
   }
   const steps=Math.max(80,Math.floor(w-58)),points=[];
   for(let i=0;i<=steps;i++){const f=xToHz(28+(w-58)*i/steps,w);points.push([hzToX(f,w),dbToY(responseDb(f),h)]);}
   ctx.beginPath();points.forEach(([x,y],i)=>i?ctx.lineTo(x,y):ctx.moveTo(x,y));ctx.lineTo(w-28,y0);ctx.lineTo(28,y0);ctx.closePath();ctx.fillStyle='rgba(79,174,207,.09)';ctx.fill();
   ctx.beginPath();points.forEach(([x,y],i)=>i?ctx.lineTo(x,y):ctx.moveTo(x,y));ctx.strokeStyle='#67d3e5';ctx.lineWidth=2;ctx.stroke();
   for(let i=0;i<bands.length;i++){
      const b=bands[i],x=hzToX(b.frequencyHz,w),y=dbToY(isCut(i)?0:b.gainDb,h),r=i===slot?7:5;
      ctx.beginPath();ctx.arc(x,y,r,0,Math.PI*2);ctx.fillStyle=b.enabled?COLORS[i]:'#293844';ctx.fill();ctx.lineWidth=i===slot?2:1;ctx.strokeStyle=i===slot?'#e7f9fb':COLORS[i];ctx.stroke();
   }
 };
 let frame=0;const render=()=>{updateInputs();if(frame)cancelAnimationFrame(frame);frame=requestAnimationFrame(()=>{frame=0;draw();});};
 const apply=(candidate)=>{
   if(!isEditable()||!validBand(slot,candidate))return;
   if(demonstration){bands[slot]={...candidate};setStatus('DESIGN DEMO · VISUAL ONLY · NO AUDIO','warn');render();return;}
   if(!nativeBackend()?.emitEvent||!channelId||!epoch||!revision)return;
   const id=String(reqSeq++),command={action:'builtinEqSetBand',channelId,deviceEpoch:epoch,graphRevision:revision,requestId:id,slot,band:candidate};
   pending={id,channelId,epoch,revision,slot};waitingAck=false;
   setStatus('NATIVE: REQUEST SENT · WAITING FOR AUDIO CALLBACK','warn');render();nativeBackend().emitEvent('arMixAction',command);
 };
 const selected=()=>bands[slot];
 const update=(patch)=>apply({...selected(),...patch});
 power.addEventListener('click',()=>update({enabled:!selected().enabled}));
 flat.addEventListener('click',()=>update({...empty()[slot]}));
 freq.v.addEventListener('change',()=>update({frequencyHz:clamp(num(freq.v.value,selected().frequencyHz),20,Math.min(20000,Math.floor(sampleRate*.45)))}));
 gain.v.addEventListener('change',()=>update({gainDb:clamp(num(gain.v.value,selected().gainDb),-18,18)}));
 q.v.addEventListener('change',()=>update({q:clamp(num(q.v.value,selected().q),.2,12)}));
 slope.v.addEventListener('change',()=>update({slope:num(slope.v.value,12)===24?24:12}));
 rail.addEventListener('click',e=>{const btn=e.target.closest('[data-slot]');if(!btn)return;slot=Number(btn.dataset.slot);render();});
 const locate=e=>{const r=canvas.getBoundingClientRect();return {x:e.clientX-r.left,y:e.clientY-r.top,w:r.width,h:r.height};};
 const pick=(p)=>{let index=-1,d=17*17;bands.forEach((b,i)=>{const x=hzToX(b.frequencyHz,p.w),y=dbToY(isCut(i)?0:b.gainDb,p.h),dist=(x-p.x)**2+(y-p.y)**2;if(dist<d){d=dist;index=i;}});return index;};
 canvas.addEventListener('pointerdown',e=>{if(e.button!==0)return;const p=locate(e),i=pick(p);if(i<0)return;slot=i;activeGesture={id:e.pointerId,slot:i,original:bands[i],p};canvas.setPointerCapture?.(e.pointerId);render();});
 canvas.addEventListener('pointerup',e=>{if(!activeGesture||activeGesture.id!==e.pointerId)return;const gesture=activeGesture;activeGesture=null;const p=locate(e);if(!isEditable())return;const changedHz=Math.round(xToHz(p.x,p.w)),changedDb=isCut(slot)?0:Math.round(yToDb(p.y,p.h)*10)/10;
   update({enabled:true,frequencyHz:clamp(changedHz,20,20000),gainDb:changedDb});});
 canvas.addEventListener('pointercancel',()=>{activeGesture=null;});
 canvas.addEventListener('wheel',e=>{const p=locate(e),i=pick(p);if(i<0||!isEditable())return;e.preventDefault();slot=i;const b=bands[i];if(isCut(i))update({slope:b.slope===12?24:12,enabled:true});else update({q:clamp(Math.round((b.q+(e.deltaY<0?.08:-.08))*100)/100,.2,12),enabled:true});},{passive:false});
 close.addEventListener('click',()=>{root.hidden=true;activeGesture=null;});
 root.addEventListener('keydown',e=>{if(e.key==='Escape'){e.stopPropagation();root.hidden=true;}});
 if(typeof ResizeObserver!=='undefined'){new ResizeObserver(()=>{if(isOpen())render();}).observe(graph);}else window.addEventListener('resize',()=>{if(isOpen())render();},{passive:true});
 const validateSnap=s=>s?.schemaVersion===1&&s.channelId===channelId&&/^\d+$/.test(String(s.deviceEpoch||''))&&/^\d+$/.test(String(s.graphRevision||''))&&Array.isArray(s.bands)&&s.bands.length===8&&s.bands.every((b,i)=>validBand(i,b));
 const backend=nativeBackend();
 if(backend?.addEventListener){
   backend.addEventListener('arMixBuiltinEqQueue',r=>{if(!pending||r?.requestId!==pending.id||r.channelId!==channelId)return;if(r.status==='REJECTED'){pending=null;setStatus('NATIVE: EDIT REJECTED; STATE UNCHANGED','warn');render();}else setStatus('QUEUED · WAITING FOR DSP APPLY ACK','warn');});
   backend.addEventListener('arMixBuiltinEqAck',r=>{if(!pending||r?.requestId!==pending.id||r.channelId!==channelId)return;if(r.status==='APPLIED'&&r.deviceEpoch===pending.epoch&&r.graphRevision===pending.revision){waitingAck=true;setStatus('DSP APPLIED · AWAITING ACCEPTED READBACK','warn');}else {pending=null;waitingAck=false;setStatus('NATIVE: NOT APPLIED; STATE UNCHANGED','warn');render();}});
   backend.addEventListener('arMixBuiltinEqSnapshot',s=>{if(!isOpen()||demonstration||!validateSnap(s))return;
     if(pending&&(!waitingAck||s.appliedRequestId!==pending.id))return;
     bands=s.bands.map(b=>({...b}));epoch=s.deviceEpoch;revision=s.graphRevision;sampleRate=Number(s.sampleRate)||48000;pending=null;waitingAck=false;snapshotReady=true;
     setStatus('NATIVE · ACK + ACCEPTED PARAMETER READBACK','good');render();
   });
 }
 window.ArSonMixBuiltinEq=Object.freeze({open(track){
   if(!track||track.kind==='master')return;
   current=track;channelLabel.textContent=String(track.name||'Channel');root.hidden=false;slot=2;pending=null;waitingAck=false;
   const host=nativeRoute(),resolved=host?.resolve?.(track.id),s=host?.state?.();
   demonstration=!nativeBackend();
   if(demonstration){channelId=null;snapshotReady=false;bands=empty();mode.textContent='UI DEMO · NO AUDIO';setStatus('DESIGN DEMO · NO AUDIO SIGNAL','warn');}
   else if(resolved&&s?.admitted){channelId=resolved;epoch=s.device?.epoch;revision=s.graph?.revision;bands=empty();pending=null;snapshotReady=false;
     // Control disabled until real native snapshot arrives; never pretend
     // that a screenshot/browser fixture is already loaded as channel state.
     mode.textContent='NATIVE · CHANNEL ID '+channelId;setStatus('NATIVE · LOADING ACCEPTED PARAMETERS','warn');nativeBackend()?.emitEvent('arMixAction',{action:'builtinEqSnapshot',channelId});
   }else{mode.textContent='NATIVE · UNAVAILABLE';resetNative();}
   render();root.querySelector('.mx-eq-close')?.focus();
 },close(){root.hidden=true;}});
})();
