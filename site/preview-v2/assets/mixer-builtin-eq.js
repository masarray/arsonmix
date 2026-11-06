/* ArSonMix Built-in Parametric EQ: small operator overlay in the ONLY /preview/.
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
 const maxHz=()=>Math.min(20000,Math.floor(sampleRate*.45));
 const hzToX=(hz,w)=>28+Math.log(clamp(hz,20,20000)/20)/Math.log(1000)*(w-58);
 const xToHz=(x,w)=>20*Math.pow(1000,clamp((x-28)/(w-58),0,1));
 const dbToY=(db,h)=>h/2-clamp(db,-18,18)*(h-39)/36;
 const yToDb=(y,h)=>clamp((h/2-y)*36/(h-39),-18,18);
 const isCut=i=>i===0||i===7;
 const gainEnabled=i=>i!==0&&i!==7;
 const mk=(tag,cl,text)=>{const el=document.createElement(tag);if(cl)el.className=cl;if(text!==undefined)el.textContent=text;return el;};
 const root=mk('section');root.id='mxBuiltinEq';root.hidden=true;root.setAttribute('role','dialog');root.setAttribute('aria-modal','false');root.setAttribute('aria-label','Built-in parametric equalizer');
 const head=mk('header','mx-eq-top'),mark=mk('i','mx-eq-mark'),brand=mk('strong','', 'ArSonMix · Parametric EQ');
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
 // Real modeless title-bar dragging, isolated from frequency-node gestures.
 let windowGesture=null;
 const stopDrag=e=>{
   if(!windowGesture||windowGesture.id!==e.pointerId)return;
   windowGesture=null;head.releasePointerCapture?.(e.pointerId);
 };
 head.addEventListener('pointerdown',e=>{
   if(e.button!==0||e.target.closest?.('button'))return;
   const r=root.getBoundingClientRect();
   windowGesture={id:e.pointerId,offsetX:e.clientX-r.left,offsetY:e.clientY-r.top};
   head.setPointerCapture?.(e.pointerId);e.preventDefault();
 });
 head.addEventListener('pointermove',e=>{
   if(!windowGesture||e.pointerId!==windowGesture.id)return;
   const r=root.getBoundingClientRect();
   root.style.left=clamp(e.clientX-windowGesture.offsetX,0,Math.max(0,(window.innerWidth||1024)-r.width))+'px';
   root.style.top=clamp(e.clientY-windowGesture.offsetY,0,Math.max(0,(window.innerHeight||768)-r.height))+'px';
   root.style.transform='none';e.preventDefault();
 });
 head.addEventListener('pointerup',stopDrag);
 head.addEventListener('pointercancel',stopDrag);
 const ctx=canvas.getContext('2d',{alpha:false});
 let current=null,channelId=null,epoch=null,revision=null,bands=empty(),slot=2,sampleRate=48000;
 let pending=null,waitingAck=false,snapshotReady=false,reqSeq=1,activeGesture=null,
     demonstration=!Boolean(window.__JUCE__?.backend); // native readback works even before editor opened
 const nativeBackend=()=>window.__JUCE__?.backend;
 const nativeRoute=()=>window.__ARMIX_BUILTIN_EQ_CHANNEL__;
 const demoStates=new Map(),nativeStates=new Map();
 let lastGraphEqSweep='';
 const sameGraph=(v,n)=>v&&n&&v.epoch===n.device?.epoch&&v.revision===n.graph?.revision;
 const stateFor=t=>{
  if(!t||t.kind==='master')return null;
  if(!nativeBackend())return demoStates.get(t.id)||null;
  const id=nativeRoute()?.resolve?.(t.id),n=nativeRoute()?.state?.();
  const accepted=nativeStates.get(id);return sameGraph(accepted,n)?accepted:null;
 };
 const refresh=t=>window.__ARMIX_BUILTIN_EQ_REFRESH__?.(t?.id);
 const notify=message=>window.__ARMIX_BUILTIN_EQ_NOTIFY__?.(message);
 const safeName=t=>String(t?.name||'Channel').replace(/[&<>"']/g,c=>({
   '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
 })[c]);
 const nextRequestId=()=> (BigInt(Date.now())*10000n+BigInt(reqSeq++%10000)).toString();
 const setStatus=(message,kind='')=>{status.textContent=message;status.className='mx-eq-status '+kind;};
 const isOpen=()=>!root.hidden;
 const isEditable=()=>demonstration||Boolean(channelId&&snapshotReady&&!pending&&
                     current&&stateFor(current)?.installed);
 const resetNative=()=>{channelId=null;epoch=null;revision=null;pending=null;waitingAck=false;snapshotReady=false;bands=empty();setStatus('NATIVE: WAITING FOR ACCEPTED CHANNEL', 'warn');render();};
 const updateInputs=()=>{
   const b=bands[slot];
   buttons.forEach((node,i)=>{node.classList.toggle('selected',slot===i);node.classList.toggle('on',bands[i].enabled);node.setAttribute('aria-pressed',String(bands[i].enabled));});
   power.setAttribute('aria-pressed',String(b.enabled));power.textContent=b.enabled?'BAND ON':'BAND OFF';
   freq.v.value=String(Math.round(b.frequencyHz));gain.v.value=b.gainDb.toFixed(1);q.v.value=b.q.toFixed(2);slope.v.value=String(b.slope);
   typeName.textContent=LABELS[slot];gain.v.disabled=isCut(slot)||!isEditable();q.v.disabled=isCut(slot)||slot===1||slot===6||!isEditable();
   slope.v.disabled=!isCut(slot)||!isEditable();freq.v.disabled=!isEditable();power.disabled=!isEditable();flat.disabled=!isEditable();
   buttons.forEach(x=>{x.disabled=false;});
 };
 const num=(x,d)=>Number.isFinite(Number(x))?Number(x):d;
 const validBand=(i,b)=>Boolean(b)&&typeof b.enabled==='boolean'&&Number.isFinite(b.frequencyHz)&&b.frequencyHz>=20&&b.frequencyHz<=(b.enabled?maxHz():20000)&&Number.isFinite(b.gainDb)&&Math.abs(b.gainDb)<=18&&Number.isFinite(b.q)&&b.q>=.2&&b.q<=12&&(b.slope===12||b.slope===24);
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
 // Coefficients depend on settings and sample rate, NOT on graph X pixels.
 // Build once per animation frame; no 8x redundant trig/pow coefficient work.
 const compiledSections=src=>{
   const sections=[];
   for(let i=0;i<src.length;i++){
     const b=src[i];if(!b.enabled)continue;
     const cut=isCut(i),qs=cut&&b.slope===24?[.541196100146197,1.306562964876377]:[cut?Math.SQRT1_2:b.q];
     for(const q of qs)sections.push(coeff(TYPES[i],b,sampleRate,q));
   }return sections;
 };
 const responseDb=(hz,sections)=>{
   if(!sections.length)return 0;
   const w=2*Math.PI*hz/sampleRate,cos=Math.cos(w),sin=Math.sin(w);
   const cos2=Math.cos(2*w),sin2=Math.sin(2*w);
   let result=0;
   for(const c of sections){
     const nr=c[0]+c[1]*cos+c[2]*cos2,ni=-(c[1]*sin+c[2]*sin2);
     const dr=1+c[4]*cos+c[5]*cos2,di=-(c[4]*sin+c[5]*sin2);
     result+=10*Math.log10(Math.max(1e-24,(nr*nr+ni*ni)/Math.max(1e-24,dr*dr+di*di)));
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
   const displayBands=activeGesture?.draft ?
     bands.map((band,i)=>i===activeGesture.slot?activeGesture.draft:band):bands;
   const sections=compiledSections(displayBands);
   const steps=Math.max(80,Math.floor(w-58)),points=[];
   for(let i=0;i<=steps;i++){const f=xToHz(28+(w-58)*i/steps,w);points.push([hzToX(f,w),dbToY(responseDb(f,sections),h)]);}
   ctx.beginPath();points.forEach(([x,y],i)=>i?ctx.lineTo(x,y):ctx.moveTo(x,y));ctx.lineTo(w-28,y0);ctx.lineTo(28,y0);ctx.closePath();ctx.fillStyle='rgba(79,174,207,.09)';ctx.fill();
   ctx.beginPath();points.forEach(([x,y],i)=>i?ctx.lineTo(x,y):ctx.moveTo(x,y));ctx.strokeStyle=activeGesture?.draft?'#9ae9f3':'#67d3e5';
   ctx.lineWidth=2;ctx.setLineDash(activeGesture?.draft?[5,3]:[]);ctx.stroke();ctx.setLineDash([]);
   for(let i=0;i<displayBands.length;i++){
      const b=displayBands[i],x=hzToX(b.frequencyHz,w),y=dbToY(isCut(i)?0:b.gainDb,h),r=i===slot?7:5;
      ctx.beginPath();ctx.arc(x,y,r,0,Math.PI*2);ctx.fillStyle=b.enabled?COLORS[i]:'#293844';ctx.fill();ctx.lineWidth=i===slot?2:1;ctx.strokeStyle=i===slot?'#e7f9fb':COLORS[i];ctx.stroke();
   }
 };
 let frame=0;const render=()=>{updateInputs();if(frame)cancelAnimationFrame(frame);frame=requestAnimationFrame(()=>{frame=0;draw();});};
 const apply=(candidate)=>{
   if(!isEditable()||!validBand(slot,candidate))return;
   if(demonstration){
   bands[slot]={...candidate};
   if(current)demoStates.set(current.id,{
      ...(demoStates.get(current.id)||{installed:false,bypassed:false}),
      bands:bands.map(b=>({...b}))});
   setStatus('DESIGN DEMO · VISUAL ONLY · NO AUDIO','warn');render();return;
 }
   if(!nativeBackend()?.emitEvent||!channelId||!epoch||!revision)return;
   const id=nextRequestId(),command={action:'builtinEqSetBand',channelId,deviceEpoch:epoch,graphRevision:revision,requestId:id,slot,band:candidate};
   pending={id,channelId,epoch,revision,slot};waitingAck=false;
   setStatus('NATIVE: REQUEST SENT · WAITING FOR AUDIO CALLBACK','warn');render();nativeBackend().emitEvent('arMixAction',command);
 };
 const selected=()=>bands[slot];
 const update=(patch)=>apply({...selected(),...patch});
 power.addEventListener('click',()=>update({enabled:!selected().enabled,frequencyHz:Math.min(selected().frequencyHz,maxHz())}));
 flat.addEventListener('click',()=>update({...empty()[slot]}));
 freq.v.addEventListener('change',()=>update({frequencyHz:clamp(num(freq.v.value,selected().frequencyHz),20,maxHz())}));
 gain.v.addEventListener('change',()=>update({gainDb:clamp(num(gain.v.value,selected().gainDb),-18,18)}));
 q.v.addEventListener('change',()=>update({q:clamp(num(q.v.value,selected().q),.2,12)}));
 slope.v.addEventListener('change',()=>update({slope:num(slope.v.value,12)===24?24:12}));
 rail.addEventListener('click',e=>{const btn=e.target.closest('[data-slot]');if(!btn)return;slot=Number(btn.dataset.slot);render();});
 const locate=e=>{const r=canvas.getBoundingClientRect();return {x:e.clientX-r.left,y:e.clientY-r.top,w:r.width,h:r.height};};
 const pick=(p)=>{let index=-1,d=17*17;bands.forEach((b,i)=>{const x=hzToX(b.frequencyHz,p.w),y=dbToY(isCut(i)?0:b.gainDb,p.h),dist=(x-p.x)**2+(y-p.y)**2;if(dist<d){d=dist;index=i;}});return index;};
 canvas.addEventListener('pointerdown',e=>{
   if(e.button!==0||!isEditable())return;
   const p=locate(e),i=pick(p);if(i<0)return;
   slot=i;activeGesture={id:e.pointerId,slot:i,draft:null};
   canvas.setPointerCapture?.(e.pointerId);render();
 });
 canvas.addEventListener('pointermove',e=>{
   const g=activeGesture;if(!g||g.id!==e.pointerId||!isEditable())return;
   const p=locate(e),selectedBand=bands[g.slot];
   g.draft={...selectedBand,enabled:true,
      frequencyHz:clamp(Math.round(xToHz(p.x,p.w)),20,maxHz()),
      gainDb:isCut(g.slot)?0:Math.round(yToDb(p.y,p.h)*10)/10};
   setStatus('DRAG PREVIEW · RELEASE TO REQUEST AUDIO CHANGE','warn');
   render(); // dashed preview only; the accepted band stays unchanged
 });
 canvas.addEventListener('pointerup',e=>{
   const g=activeGesture;if(!g||g.id!==e.pointerId)return;
   activeGesture=null;slot=g.slot;
   const p=locate(e),old=bands[g.slot];
   const candidate=g.draft||{...old,enabled:true,
     frequencyHz:clamp(Math.round(xToHz(p.x,p.w)),20,maxHz()),
     gainDb:isCut(g.slot)?0:Math.round(yToDb(p.y,p.h)*10)/10};
   if(isEditable())apply(candidate);
   else render();
 });
 canvas.addEventListener('pointercancel',()=>{activeGesture=null;render();});
 canvas.addEventListener('wheel',e=>{const p=locate(e),i=pick(p);if(i<0||!isEditable())return;e.preventDefault();slot=i;const b=bands[i];if(isCut(i))update({slope:b.slope===12?24:12,enabled:true});else update({q:clamp(Math.round((b.q+(e.deltaY<0?.08:-.08))*100)/100,.2,12),enabled:true});},{passive:false});
 close.addEventListener('click',()=>{root.hidden=true;activeGesture=null;});
 root.addEventListener('keydown',e=>{if(e.key==='Escape'){e.stopPropagation();root.hidden=true;}});
 if(typeof ResizeObserver!=='undefined'){new ResizeObserver(()=>{if(isOpen())render();}).observe(graph);}else window.addEventListener('resize',()=>{if(isOpen())render();},{passive:true});
 const validateSnap=s=>s?.schemaVersion===1&&s.channelId===channelId&&
 s.deviceEpoch===epoch&&s.graphRevision===revision&&
 typeof s.installed==='boolean'&&typeof s.bypassed==='boolean'&&
 /^\d+$/.test(String(s.deviceEpoch||''))&&/^\d+$/.test(String(s.graphRevision||''))&&
 Array.isArray(s.bands)&&s.bands.length===8&&s.bands.every((b,i)=>validBand(i,b));
 const backend=nativeBackend();
 if(backend?.addEventListener){
   // A historical EQ snapshot must never stay editable after hotplug,
   // device restart, graph replacement or channel removal.
   backend.addEventListener('arMixConsoleSnapshot',s=>{
     // The approved native graph projection may install strips after this
     // listener in the same browser turn. Request each actual ChannelId once
     // per admitted epoch/revision, after its projection has settled.
     const requery=()=>{
       const n=nativeRoute()?.state?.();
       if(!n?.admitted||!s?.admitted||
          s.device?.epoch!==n.device?.epoch||
          s.graph?.revision!==n.graph?.revision||
          !Array.isArray(n.graph?.channels))return;
       const key=String(n.device.epoch)+'/'+String(n.graph.revision);
       if(key===lastGraphEqSweep)return;
       lastGraphEqSweep=key;
       for(const ch of n.graph.channels) {
         const t=window.__ARMIX_BUILTIN_EQ_FIND_CHANNEL__?.(ch.id);
         if(t&&t.kind!=='master')
           backend.emitEvent('arMixAction',{action:'builtinEqSnapshot',
                                           channelId:ch.id});
       }
     };
     if(typeof window.setTimeout==='function')window.setTimeout(requery,0);
     else requery();
     if(!isOpen()||demonstration||!channelId)return;
     const accepted=s?.admitted===true&&s.device?.epoch===epoch&&
       s.graph?.revision===revision&&Array.isArray(s.graph.channels)&&
       s.graph.channels.some(ch=>ch.id===channelId);
     if(!accepted){mode.textContent='NATIVE · ROUTE CHANGED';resetNative();}
   });
   backend.addEventListener('arMixBuiltinEqQueue',r=>{if(!pending||r?.requestId!==pending.id||r.channelId!==channelId)return;if(r.status==='REJECTED'){const t=current;pending=null;setStatus('NATIVE: EQ REJECTED; STATE UNCHANGED','warn');notify('Parametric EQ insert/edit rejected by native engine');refresh(t);render();}else setStatus('QUEUED · WAITING FOR DSP APPLY ACK','warn');});
   backend.addEventListener('arMixBuiltinEqAck',r=>{if(!pending||r?.requestId!==pending.id||r.channelId!==channelId)return;if(r.status==='APPLIED'&&r.deviceEpoch===pending.epoch&&r.graphRevision===pending.revision){waitingAck=true;setStatus('DSP APPLIED · AWAITING ACCEPTED READBACK','warn');}else {const t=current;pending=null;waitingAck=false;setStatus('NATIVE: NOT APPLIED; STATE UNCHANGED','warn');notify('Parametric EQ change not applied');refresh(t);render();}});
   backend.addEventListener('arMixBuiltinEqSnapshot',s=>{
     const n=nativeRoute()?.state?.();
     const validIdentity=s?.schemaVersion===1&&
       typeof s.channelId==='string'&&typeof s.installed==='boolean'&&
       typeof s.bypassed==='boolean'&&s.deviceEpoch===n?.device?.epoch&&
       s.graphRevision===n?.graph?.revision;
     const otherTrack=validIdentity?
       window.__ARMIX_BUILTIN_EQ_FIND_CHANNEL__?.(s.channelId):null;
     const hasUnconfirmedCurrent=pending&&pending.channelId===s?.channelId&&
       (!waitingAck||s.appliedRequestId!==pending.id);
     if(otherTrack&&!hasUnconfirmedCurrent) {
       const prior=nativeStates.get(s.channelId);
       nativeStates.set(s.channelId,{installed:s.installed,bypassed:s.bypassed,
                                    epoch:s.deviceEpoch,revision:s.graphRevision});
       if(!prior||prior.installed!==s.installed||prior.bypassed!==s.bypassed||
          prior.revision!==s.graphRevision||prior.epoch!==s.deviceEpoch)
          refresh(otherTrack);
     }
     if(demonstration||!validateSnap(s))return;
     if(pending&&(!waitingAck||s.appliedRequestId!==pending.id))return;
     bands=s.bands.map(b=>({...b}));epoch=s.deviceEpoch;revision=s.graphRevision;
      sampleRate=Number(s.sampleRate)||48000;pending=null;waitingAck=false;snapshotReady=true;
      nativeStates.set(channelId,{installed:s.installed,bypassed:s.bypassed,
        epoch:s.deviceEpoch,revision:s.graphRevision});
      setStatus(s.installed?'NATIVE · DSP ACCEPTED · LIVE PCM':
        'NATIVE · NOT INSERTED · CHOOSE FROM FX LIST',s.installed?'good':'warn');
      refresh(current);render();
   });
 }
 const setGate=(track,installed,bypassed)=>{
  if(!track||track.kind==='master'||pending)return false;
  if(!nativeBackend()){
   const previous=demoStates.get(track.id)||{bands:empty()};
   demoStates.set(track.id,{...previous,installed,bypassed:installed&&bypassed,
     bands:installed?previous.bands:empty()});
   if(current?.id===track.id)bands=demoStates.get(track.id).bands.map(b=>({...b}));
   setStatus('DESIGN DEMO · NO AUDIO SIGNAL','warn');refresh(track);render();return true;
  }
  demonstration=false;
  const id=nativeRoute()?.resolve?.(track.id),n=nativeRoute()?.state?.();
  if(!id||!n?.admitted||!n?.device?.epoch||!n?.graph?.revision)return false;
  if(current?.id!==track.id&&isOpen())root.hidden=true;
  current=track;channelId=id;epoch=n.device.epoch;revision=n.graph.revision;
  snapshotReady=false;
  const requestId=nextRequestId();
  pending={id:requestId,channelId:id,epoch,revision,slot:-1,kind:'gate',trackId:track.id,installRequested:installed};
  waitingAck=false;
  setStatus('NATIVE · GATE QUEUED · WAITING FOR AUDIO ACK','warn');
  nativeBackend().emitEvent('arMixAction',{action:'builtinEqSetGate',channelId:id,
    deviceEpoch:epoch,graphRevision:revision,requestId,installed,bypassed});
  refresh(track);render();return true;
 };
 window.ArSonMixBuiltinEq=Object.freeze({
  installed(track){return Boolean(stateFor(track)?.installed||(pending?.kind==='gate'&&pending.installRequested&&pending.trackId===track?.id));},
  slotMarkup(track,materialIcon){
   const st=stateFor(track);
   const installing=!st?.installed&&pending?.kind==='gate'&&pending.installRequested&&pending.trackId===track?.id;
   if(!st?.installed&&!installing)return '';
   if(installing)return '<div class="fxslot-row is-builtin is-pending" data-builtin-eq="pending" data-fx-effective="false" aria-label="Parametric EQ insertion pending native audio readback"><span class="fx-slot-placeholder" aria-hidden="true"></span><span class="fx-pick">Parametric EQ · APPLYING…</span></div>';
   const enabled=track.fxOn!==false&&!st.bypassed;
   const power=materialIcon?.('power_settings_new')||'⏻';
   const remove=materialIcon?.('delete_forever')||'×';
   const label=safeName(track);
   return '<div class="fxslot-row is-builtin '+(enabled?'is-active':'is-bypassed')+
     '" data-builtin-eq="true" data-fx-effective="'+enabled+
     '" title="Native Parametric EQ · PRE VST3">'+
     '<button class="mx-util-key" data-util-kind="fx-slot" data-action="builtinEqPower" title="'+
     (st.bypassed?'Enable':'Bypass')+' Parametric EQ" aria-label="Toggle Parametric EQ on '+
     label+'" aria-pressed="'+enabled+'">'+power+'</button>'+
     '<button class="fx-pick" data-action="openBuiltinEq" aria-label="Open Parametric EQ for '+
     label+'">Parametric EQ <small>PRE</small></button>'+
     '<button class="mx-util-key" data-util-kind="fx-remove" data-action="builtinEqRemove" '+
     'title="Remove built-in EQ" aria-label="Remove Parametric EQ">'+remove+'</button></div>';
  },
  insert(track){
   if(!track||track.kind==='master'||this.installed(track))return false;
   // Selecting in the picker ONLY inserts. Clicking the accepted rack row
   // is the sole editor-opening action (DAW-style modeless FX workflow).
   const accepted=setGate(track,true,false);
   if(!accepted)notify('Parametric EQ insert rejected · channel unchanged');
   return accepted;
  },
  power(track){const st=stateFor(track);if(st?.installed)setGate(track,true,!st.bypassed);},
  remove(track){const st=stateFor(track);if(st?.installed)setGate(track,false,false);},
  open(track){
   if(!track||track.kind==='master')return;
   current=track;channelLabel.textContent=String(track.name||'Channel');root.hidden=false;slot=2;pending=null;waitingAck=false;
   const host=nativeRoute(),resolved=host?.resolve?.(track.id),s=host?.state?.();
   demonstration=!nativeBackend();
   if(demonstration){channelId=null;snapshotReady=false;
     bands=demoStates.get(track.id)?.bands?.map(b=>({...b}))||empty();
     sampleRate=48000;mode.textContent='UI DEMO · NO AUDIO';
     setStatus('DESIGN DEMO · NO AUDIO SIGNAL','warn');}
   else if(resolved&&s?.admitted){channelId=resolved;epoch=s.device?.epoch;revision=s.graph?.revision;bands=empty();pending=null;snapshotReady=false;
     // Control disabled until real native snapshot arrives; never pretend
     // that a screenshot/browser fixture is already loaded as channel state.
     mode.textContent='NATIVE · CHANNEL ID '+channelId;setStatus('NATIVE · LOADING ACCEPTED PARAMETERS','warn');nativeBackend()?.emitEvent('arMixAction',{action:'builtinEqSnapshot',channelId});
   }else{mode.textContent='NATIVE · UNAVAILABLE';resetNative();}
   render();root.querySelector('.mx-eq-close')?.focus();
 },close(){root.hidden=true;}});
})();
