/* ArSonMix native Parametric EQ — ONE approved /preview/ editor.
 * SVG graph represents the same 8 RBJ biquads as engine/builtin_fx/ChannelEq.h.
 * In native mode a graph gesture is never "ACTIVE" until audio ACK + readback.
 * In static Pages mode interaction is explicitly DEMO, with NO audio engine. */
(() => {
"use strict";
const ID="armix.builtin.channel-eq.v1",NS="http://www.w3.org/2000/svg";
const names=["HPF","LOW SHELF","BELL 1","BELL 2","BELL 3","BELL 4","HIGH SHELF","LPF"];
const kinds=["hp","ls","bell","bell","bell","bell","hs","lp"];
const defaults=[80,160,250,500,1500,4500,9500,18000];
const colors=["#9b91f8","#fb81be","#f8be5c","#9de59e","#5bd6db","#68a9ff","#bc8af2","#ef82ba"];
const ICONS=["M2 15 C7 15 7 4 14 4 H25","M2 14 H8 Q11 14 13 9 T25 5",
"M2 14 H7 Q10 14 13 6 Q16 0 19 6 T25 14","M2 14 H7 Q10 14 13 6 Q16 0 19 6 T25 14",
"M2 14 H7 Q10 14 13 6 Q16 0 19 6 T25 14","M2 14 H7 Q10 14 13 6 Q16 0 19 6 T25 14",
"M2 15 H9 Q12 15 14 10 T25 4","M2 4 H13 Q19 4 19 15 H25"];
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
const fresh=()=>defaults.map((f,i)=>({enabled:false,frequencyHz:f,gainDb:0,q:i>=2&&i<=5?1:0.70710678,slope:12}));
const dom=(t,c,txt)=>{const e=document.createElement(t);if(c)e.className=c;if(txt!==undefined)e.textContent=txt;return e};
const native=()=>Boolean(window.__JUCE__?.backend?.emitEvent);
const backend=()=>window.__JUCE__?.backend;
const state=()=>window.__ARMIX_NATIVE_M2__?.fxEditorState?.()||window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const cap=()=>Math.min(20000,Math.max(20,Math.floor(sampleRate*.45)));
const CUT=i=>i===0||i===7, SHELF=i=>i===1||i===6;
const LOG=Math.log(1000),XL=34,XR=642,YT=21,YB=293,ZO=157,G=18;
const toX=f=>XL+clamp(Math.log(clamp(f,20,20000)/20)/LOG,0,1)*(XR-XL);
const toF=x=>20*Math.exp(clamp((x-XL)/(XR-XL),0,1)*LOG);
const toY=g=>ZO-clamp(g,-G,G)*(ZO-YT)/G;
const toDb=y=>clamp((ZO-y)*G/(ZO-YT),-G,G);
const prettyF=n=>n>=1000?(n/1000).toFixed(n>=10000?1:2).replace(/0+$/,"").replace(/\.$/,"")+"k":String(Math.round(n));
const db=n=>(n>0?"+":"")+Number(n).toFixed(1);
const valid=(i,b)=>b&&typeof b.enabled==="boolean"&&Number.isFinite(b.frequencyHz)&&
 b.frequencyHz>=20&&b.frequencyHz<=cap()&&Number.isFinite(b.gainDb)&&Math.abs(b.gainDb)<=18&&
 Number.isFinite(b.q)&&b.q>=.2&&b.q<=12&&(b.slope===12||b.slope===24);
const escape=s=>String(s).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
// Exact coefficients from native ChannelEq::makeBiquad (same shelf convention).
function coefficient(kind,b,rate,q) {
 const w=2*Math.PI*b.frequencyHz/rate,c=Math.cos(w),sin=Math.sin(w),
 a=Math.pow(10,b.gainDb/40),al=sin/(2*q),edge=sin*Math.sqrt(a)*Math.SQRT2;
 let v;
 if(kind==="bell")v=[1+al*a,-2*c,1-al*a,1+al/a,-2*c,1-al/a];
 else if(kind==="hp")v=[(1+c)/2,-(1+c),(1+c)/2,1+al,-2*c,1-al];
 else if(kind==="lp")v=[(1-c)/2,1-c,(1-c)/2,1+al,-2*c,1-al];
 else if(kind==="ls")v=[a*((a+1)-(a-1)*c+edge),2*a*((a-1)-(a+1)*c),a*((a+1)-(a-1)*c-edge),(a+1)+(a-1)*c+edge,-2*((a-1)+(a+1)*c),(a+1)+(a-1)*c-edge];
 else v=[a*((a+1)+(a-1)*c+edge),-2*a*((a-1)+(a+1)*c),a*((a+1)+(a-1)*c-edge),(a+1)-(a-1)*c+edge,2*((a-1)-(a+1)*c),(a+1)-(a-1)*c-edge];
 return [v[0]/v[3],v[1]/v[3],v[2]/v[3],v[4]/v[3],v[5]/v[3]];
}
const sections=(b,i)=>{
 if(!b.enabled)return [];
 const kind=kinds[i];
 return (CUT(i)&&b.slope===24?[.541196100146197,1.306562964876377]:[CUT(i)?Math.SQRT1_2:b.q])
 .map(q=>coefficient(kind,b,sampleRate,q));
};
function response(freq,ss){
 const w=2*Math.PI*freq/sampleRate,cos=Math.cos(w),sin=Math.sin(w),c2=Math.cos(2*w),s2=Math.sin(2*w);
 let total=0;
 for(const a of ss) {
  const nr=a[0]+a[1]*cos+a[2]*c2,ni=-(a[1]*sin+a[2]*s2);
  const dr=1+a[3]*cos+a[4]*c2,di=-(a[3]*sin+a[4]*s2);
  total+=10*Math.log10(Math.max(1e-24,(nr*nr+ni*ni)/Math.max(1e-24,dr*dr+di*di)));
 }
 return clamp(total,-160,80);
}
let sampleRate=48000,bands=fresh(),selected=2,current=null,gesture=null,draft=null,pending=null,
 raf=0,lastSend=0,seq=1,readbackRequest="",mode="";
// Preserve the FINAL edit for EACH band while one audio ACK is in flight.
// A scalar 'latest' loses changes when the user switches bands rapidly.
const latest=new Map();
const demos=new Map();
const root=dom("section","mx-peq");root.id="mxNativePeq";root.hidden=true;root.setAttribute("role","dialog");
root.setAttribute("aria-label","ArSonMix Parametric EQ");root.setAttribute("aria-modal","false");
const head=dom("header","mx-peq-title"),brand=dom("span","mx-peq-brand","ArSonMix  /  PARAMETRIC EQ"),
 channel=dom("span","mx-peq-channel"),power=dom("button","mx-peq-global","⏻"),
 close=dom("button","mx-peq-close");power.type=close.type="button";
close.innerHTML='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="20" height="20" fill="currentColor" aria-hidden="true"><path d="M6.4,19L5,17.6L10.6,12L5,6.4L6.4,5L12,10.6L17.6,5L19,6.4L13.4,12L19,17.6L17.6,19L12,13.4L6.4,19Z"/></svg>';close.setAttribute("aria-label","Close Parametric EQ");
 power.title="FX STATE UNAVAILABLE";power.dataset.fxState="unknown";
 power.setAttribute("aria-pressed","false");power.disabled=true;
 power.setAttribute("aria-label","EQ state unavailable");
 close.title="Close EQ";close.setAttribute("aria-label","Close EQ");
 power.classList.add("armix-builtin-fx-power");close.classList.add("armix-builtin-fx-close");
head.append(brand,channel,power,close);
const body=dom("div","mx-peq-body"),graph=dom("div","mx-peq-graph");
const svg=document.createElementNS(NS,"svg");svg.setAttribute("viewBox","0 0 676 330");
svg.setAttribute("preserveAspectRatio","none");svg.setAttribute("role","group");
svg.setAttribute("aria-label","Logarithmic EQ response; drag colored nodes to adjust");
graph.append(svg);
const side=dom("aside","mx-peq-knobs"),bandName=dom("div","mx-peq-selection","BELL 1");
side.append(bandName);
const knobs={};
function makeKnob(key,title) {
 const box=dom("div","mx-peq-knob");box.dataset.kind=key;
 const lab=dom("span","mx-peq-knob-label",title);
 const circle=dom("div","mx-peq-dial");circle.tabIndex=0;circle.setAttribute("role","slider");
 circle.setAttribute("aria-label",title);
 circle.innerHTML='<svg viewBox="0 0 74 74" aria-hidden="true"><circle cx="37" cy="37" r="29" fill="#152b37" stroke="#426073" stroke-width="5"/><circle class="mx-peq-arc" cx="37" cy="37" r="29" fill="none" stroke-width="5" stroke-linecap="round" transform="rotate(-130 37 37)"/><line class="mx-peq-needle" x1="37" y1="37" x2="37" y2="15" stroke-width="2.4" stroke-linecap="round"/></svg>';
 const input=dom("input","mx-peq-value");input.type="text";input.inputMode="decimal";input.setAttribute("aria-label",title+" numeric value");
 box.append(lab,circle,input);side.append(box);knobs[key]={box,lab,circle,input};
}
makeKnob("frequencyHz","FREQ");makeKnob("gainDb","GAIN");makeKnob("q","Q");
const bar=dom("nav","mx-peq-bands");bar.setAttribute("aria-label","EQ filter bands");
const controls=[];
names.forEach((n,i)=>{
 const item=dom("div","mx-peq-band");item.style.setProperty("--band",colors[i]);
 const choose=dom("button","mx-peq-select");
 choose.type="button";choose.title=n+" — select filter";choose.setAttribute("aria-label","Select "+n);
 choose.innerHTML='<svg viewBox="0 0 28 20" aria-hidden="true"><path d="'+ICONS[i]+'" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg><span>'+(i>=2&&i<=5?"P"+(i-1):n==="LOW SHELF"?"LS":n==="HIGH SHELF"?"HS":n)+'</span>';
 const toggle=dom("button","mx-peq-band-power","⏻");toggle.type="button";
 toggle.title="Enable or bypass "+n;toggle.setAttribute("aria-label","Toggle "+n);
 item.append(choose,toggle);bar.append(item);controls.push({item,choose,toggle});
 choose.addEventListener("click",()=>{selected=i;draft=null;draw()});
 toggle.addEventListener("click",()=>{selected=i;change(i,{enabled:!effective()[i].enabled})});
});
const status=dom("footer","mx-peq-status","PREVIEW · NO AUDIO");
body.append(graph,side);root.append(head,body,bar,status);
function mount(){if(!root.isConnected)document.body.append(root)}
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
const context=()=>{
 if(!native()||!current)return null;
 const n=state(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track)||
   n?.graph?.channels?.find(ch=>ch.id===current.channelId)?.id;
 const channelId=current.channelId||id;
 const insert=n?.graph?.channels?.find(ch=>ch.id===channelId)?.inserts?.[current.slot];
 if(!(n?.admitted||n?.logical)||!insert||insert.catalogId!==ID||!insert.id||
    (current.instanceId&&insert.id!==current.instanceId))return null;
 return {channelId,deviceEpoch:n.logical?"0":n.device.epoch,
  ...(n.logical?{workspaceToken:n.workspaceToken}:{}),graphRevision:n.graph.revision,
  instanceId:insert.id,slot:current.slot};
};
const effective=()=>draft?.list||bands;
function updateKnobs(){
 const b=effective()[selected],c=context(),enabled=Boolean(c||!native());
 const ch=c?state()?.graph?.channels?.find(x=>x.id===c.channelId):null;
 const insert=ch?.inserts?.[c?.slot];
 const fxState=!native()?
  (!current?.track?"unknown":current.track.fxOn===false?"rack-off":
   current.track.slotBypass?.[current.slot]?"off":"on"):
  (!ch||typeof ch.fxRackBypassed!=="boolean"||!insert||
   typeof insert.bypassed!=="boolean"?"unknown":
   ch.fxRackBypassed?"rack-off":insert.bypassed?"off":"on");
 power.dataset.fxState=fxState;
 power.disabled=fxState==="unknown"||fxState==="rack-off";
 power.setAttribute("aria-pressed",String(fxState==="on"));
 power.title=fxState==="on"?"EQ ON · click to bypass":
  fxState==="off"?"EQ BYPASSED · click to enable":
  fxState==="rack-off"?"FX RACK BYPASSED · enable rack first":
  "FX STATE UNAVAILABLE";
 power.setAttribute("aria-label",power.title);
 bandName.textContent=names[selected];bandName.style.color=colors[selected];
 for(const [key,k] of Object.entries(knobs)){
  const cut=CUT(selected),disabled=(key==="gainDb"&&cut)||(key==="q"&&SHELF(selected))||(!enabled);
  const slope=key==="q"&&cut,val=key==="frequencyHz"?b.frequencyHz:key==="gainDb"?b.gainDb:slope?b.slope:b.q;
  k.lab.textContent=slope?"SLOPE":key==="frequencyHz"?"FREQ":key==="gainDb"?"GAIN":"Q";
  k.input.value=key==="frequencyHz"?prettyF(val)+" Hz":key==="gainDb"?db(val)+" dB":slope?val+" dB/oct":val.toFixed(2);
  k.circle.setAttribute("aria-disabled",String(disabled));k.box.classList.toggle("disabled",disabled);
  k.circle.tabIndex=disabled?-1:0;k.input.disabled=disabled;
  let fraction=key==="frequencyHz"?Math.log(val/20)/LOG:key==="gainDb"?(val+18)/36:slope?(val===24?1:0):Math.log(val/.2)/Math.log(60);
  fraction=clamp(fraction,0,1);
  k.circle.style.setProperty("--turn",(-130+260*fraction)+"deg");
  const arc=k.circle.querySelector(".mx-peq-arc");
  arc.style.stroke=colors[selected];arc.style.strokeDasharray=(182*fraction)+" 182";
 }
 controls.forEach(({item,toggle},i)=>{
  item.classList.toggle("active",selected===i);
  item.classList.toggle("enabled",effective()[i].enabled);
  toggle.setAttribute("aria-pressed",String(effective()[i].enabled));
 });
}
function paint(){
 if(root.hidden)return;
 updateKnobs();
 const values=effective(),compiled=values.map((b,i)=>sections(b,i)),all=compiled.flat();
 const grid=[];
 for(const f of [20,50,100,200,500,1000,2000,5000,10000,20000]){
  const x=toX(f);grid.push('<path d="M'+x+' '+YT+' V'+YB+'" class="mx-peq-grid"/><text x="'+x+'" y="317" text-anchor="middle" class="mx-peq-tick">'+(f<1000?f:(f/1000)+"k")+'</text>');
 }
 for(const dbv of [-18,-12,-6,0,6,12,18]){
  const y=toY(dbv);grid.push('<path d="M'+XL+' '+y+' H'+XR+'" class="'+(dbv===0?"mx-peq-zero":"mx-peq-grid")+'"/><text x="3" y="'+(y+4)+'" class="mx-peq-tick">'+(dbv>0?"+":"")+dbv+'</text>');
 }
 const paths=[],node=[];
 const sample=ss=>{
  let d="";
  for(let t=0;t<=210;t++){
   const x=XL+t*(XR-XL)/210,f=toF(x),dbv=response(Math.min(f,sampleRate*.49),ss);
   d+=(t?"L":"M")+x.toFixed(2)+" "+toY(dbv).toFixed(2)+" ";
  }return d;
 };
 compiled.forEach((ss,i)=>{
  if(!ss.length)return;
  const d=sample(ss);
  paths.push('<path d="'+d+' L'+XR+' '+ZO+' L'+XL+' '+ZO+' Z" fill="'+colors[i]+'" opacity=".065"/>');
  paths.push('<path d="'+d+'" fill="none" stroke="'+colors[i]+'" opacity="'+(i===selected?".66":".25")+'" stroke-width="'+(i===selected?"1.6":"1.1")+'"/>');
 });
 const combined=sample(all);
 paths.push('<path d="'+combined+'" fill="none" stroke="#b6f2f5" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>');
 values.forEach((b,i)=>{
  const x=toX(b.frequencyHz),y=toY(CUT(i)?0:b.gainDb);
  if(i===selected)node.push('<circle cx="'+x+'" cy="'+y+'" r="14" fill="'+colors[i]+'" opacity=".14"/>');
  node.push('<circle data-eq-node="'+i+'" cx="'+x+'" cy="'+y+'" r="'+(i===selected?6.2:5.2)+'" fill="'+(b.enabled?colors[i]:"#233b4a")+'" stroke="'+colors[i]+'" stroke-width="1.6"/>');
  node.push('<circle data-eq-node="'+i+'" cx="'+x+'" cy="'+y+'" r="15" fill="transparent" tabindex="0" aria-label="'+escape(names[i])+' frequency '+Math.round(b.frequencyHz)+' Hz" role="slider"/>');
 });
 svg.innerHTML='<defs><clipPath id="mxPeqClip"><rect x="'+XL+'" y="'+YT+'" width="'+(XR-XL)+'" height="'+(YB-YT)+'"/></clipPath></defs>'+
   '<rect width="676" height="330" fill="#0f202b"/>'+grid.join("")+
   '<g clip-path="url(#mxPeqClip)">'+paths.join("")+node.join("")+'</g>';
}
function draw(){if(raf)cancelAnimationFrame(raf);raf=requestAnimationFrame(()=>{raf=0;paint()})}
function hint(t){status.textContent=t}
function requestSnapshot(){
 const c=context();if(!c)return;
 const requestId="eqsnap-"+(Date.now())+"-"+(++seq);
 readbackRequest=requestId;
 backend().emitEvent("arMixAction",{action:"nativeEqSnapshot",requestId,...c});
}
function send(i,value){
 const c=context();if(!c)return;
 if(pending){latest.set(i,value);hint("DSP · APPLYING · EDITS HELD UNTIL ACK");return;}
 const id="eq-"+String(Date.now()*1000+(seq++%1000));
 pending={id,index:i,band:value,context:c,ack:false};
 backend().emitEvent("arMixAction",{action:"nativeEqSetBand",requestId:id, ...c,
  bandIndex:i,band:value});
 hint("DSP · QUEUED — AWAITING AUDIO ACK");
}
function change(i,patch,preview=false){
 const arr=effective().map(b=>({...b}));const old=arr[i];
 const next={...old,...patch};
 next.frequencyHz=clamp(next.frequencyHz,20,cap());
 next.gainDb=CUT(i)?0:clamp(next.gainDb,-18,18);
 next.q=clamp(next.q,.2,12);next.slope=next.slope===24?24:12;
 if(!valid(i,next))return;
 arr[i]=next;draft={list:arr};draw();
 if(!native()){bands=arr;demos.set(current?.track?.id+":"+current?.slot,arr.map(b=>({...b})));draft=null;
  hint("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO");draw();return;}
 const now=performance.now();
 if(!preview||now-lastSend>90){lastSend=now;send(i,next);}
 else latest.set(i,next);
}
function movePoint(e){
 const r=svg.getBoundingClientRect();
 return {x:(e.clientX-r.left)*676/r.width,y:(e.clientY-r.top)*330/r.height};
}
svg.addEventListener("pointerdown",e=>{
 const hit=e.target.closest?.("[data-eq-node]");if(!hit||e.button!==0)return;
 const i=Number(hit.dataset.eqNode);if(!Number.isInteger(i)||i<0||i>=8)return;
 selected=i;const p=movePoint(e),b=effective()[i];
 gesture={id:e.pointerId,kind:"node",index:i,from:p,band:{...b}};
 svg.setPointerCapture(e.pointerId);e.preventDefault();draw();
});
svg.addEventListener("pointermove",e=>{
 if(!gesture||gesture.kind!=="node"||gesture.id!==e.pointerId)return;
 const p=movePoint(e),g=gesture,scale=e.shiftKey?.18:1;
 const dx=(p.x-g.from.x)*scale,dy=(p.y-g.from.y)*scale;
 const frequencyHz=clamp(toF(toX(g.band.frequencyHz)+dx),20,cap());
 const gainDb=CUT(g.index)?0:clamp(g.band.gainDb-dy*G/(ZO-YT),-18,18);
 change(g.index,{frequencyHz,gainDb,enabled:true},true);
 hint(prettyF(frequencyHz)+" Hz · "+db(gainDb)+" dB · Q "+effective()[g.index].q.toFixed(2));
 e.preventDefault();
});
function finish(e){
 if(!gesture||gesture.id!==e.pointerId)return;
 const g=gesture;gesture=null;
 if(g.kind==="node"&&draft){const b=effective()[g.index];change(g.index,b,false)}
 else if(g.kind==="knob"&&draft){const b=effective()[selected];change(selected,b,false)}
 try{svg.releasePointerCapture?.(e.pointerId)}catch{}
}
svg.addEventListener("pointerup",finish);svg.addEventListener("pointercancel",e=>{gesture=null;draft=null;draw()});
svg.addEventListener("dblclick",e=>{
 const hit=e.target.closest?.("[data-eq-node]");if(!hit)return;
 const i=Number(hit.dataset.eqNode);selected=i;
 change(i,fresh()[i]);e.preventDefault();
});
svg.addEventListener("wheel",e=>{
 const hit=e.target.closest?.("[data-eq-node]");if(!hit)return;
 const i=Number(hit.dataset.eqNode);selected=i;e.preventDefault();
 const b=effective()[i];
 if(CUT(i))change(i,{slope:b.slope===12?24:12,enabled:true});
 else if(!SHELF(i))change(i,{q:clamp(b.q*Math.exp((e.deltaY<0?1:-1)*(e.shiftKey?.025:.09)),.2,12),enabled:true});
},{passive:false});
svg.addEventListener("keydown",e=>{
 const hit=e.target.closest?.("[data-eq-node]");if(!hit||!["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"].includes(e.key))return;
 const i=Number(hit.dataset.eqNode),b=effective()[i],fine=e.shiftKey?.2:1;
 e.preventDefault();selected=i;
 const horizontal=e.key==="ArrowLeft"||e.key==="ArrowRight";
 const sign=e.key==="ArrowLeft"||e.key==="ArrowDown"?-1:1;
 if(horizontal)change(i,{frequencyHz:b.frequencyHz*Math.exp(sign*.035*fine),enabled:true});
 else if(!CUT(i))change(i,{gainDb:b.gainDb+sign*.2*fine,enabled:true});
});
for(const [key,k] of Object.entries(knobs)){
 const adjust=(e,amount)=>{
  const b=effective()[selected],fine=e.shiftKey?.2:1;
  if(key==="frequencyHz")change(selected,{frequencyHz:b.frequencyHz*Math.exp(amount*.018*fine),enabled:true},true);
  else if(key==="gainDb"&&!CUT(selected))change(selected,{gainDb:b.gainDb+amount*.14*fine,enabled:true},true);
  else if(key==="q"&&!SHELF(selected))change(selected,CUT(selected)?{slope:amount<0?12:24,enabled:true}:
    {q:b.q*Math.exp(amount*.035*fine),enabled:true},true);
 };
 k.circle.addEventListener("pointerdown",e=>{
  if(e.button!==0||k.box.classList.contains("disabled"))return;
  gesture={id:e.pointerId,kind:"knob",key,start:e.clientY,last:e.clientY};
  k.circle.setPointerCapture(e.pointerId);e.preventDefault();
 });
 k.circle.addEventListener("pointermove",e=>{
  if(!gesture||gesture.kind!=="knob"||gesture.key!==key||gesture.id!==e.pointerId)return;
  const dy=gesture.last-e.clientY;gesture.last=e.clientY;adjust(e,dy);e.preventDefault();
 });
 k.circle.addEventListener("pointerup",e=>{
  if(!gesture||gesture.kind!=="knob"||gesture.key!==key)return;
  gesture=null;if(draft)change(selected,effective()[selected],false);
  k.circle.releasePointerCapture?.(e.pointerId);
 });
 k.circle.addEventListener("pointercancel",()=>{gesture=null;draft=null;draw()});
 k.circle.addEventListener("wheel",e=>{
  if(k.box.classList.contains("disabled"))return;e.preventDefault();
  adjust(e,e.deltaY<0?3:-3);
  change(selected,effective()[selected],false);
 },{passive:false});
 k.circle.addEventListener("keydown",e=>{
  if(!["ArrowUp","ArrowDown","ArrowLeft","ArrowRight"].includes(e.key))return;
  e.preventDefault();adjust(e,e.key==="ArrowUp"||e.key==="ArrowRight"?3:-3);
  change(selected,effective()[selected],false);
 });
 k.input.addEventListener("change",()=>{
  const value=k.input.value.trim().toLowerCase();
  let raw=Number.parseFloat(value);if(key==="frequencyHz"&&/k(?:hz)?/.test(value))raw*=1000;
  if(!Number.isFinite(raw)){draw();return;}
  if(key==="frequencyHz")change(selected,{frequencyHz:raw},false);
  if(key==="gainDb"&&!CUT(selected))change(selected,{gainDb:raw},false);
  if(key==="q"&&!SHELF(selected))change(selected,CUT(selected)?{slope:raw>=18?24:12}:{q:raw},false);
 });
}
let windowGesture=null;
head.addEventListener("pointerdown",e=>{
 if(e.button!==0||e.target.closest("button"))return;
 const r=root.getBoundingClientRect();
 windowGesture={id:e.pointerId,x:e.clientX-r.left,y:e.clientY-r.top};
 head.setPointerCapture(e.pointerId);e.preventDefault();
});
head.addEventListener("pointermove",e=>{
 if(!windowGesture||windowGesture.id!==e.pointerId)return;
 const r=root.getBoundingClientRect();
 root.style.left=clamp(e.clientX-windowGesture.x,0,Math.max(0,innerWidth-r.width))+"px";
 root.style.top=clamp(e.clientY-windowGesture.y,0,Math.max(0,innerHeight-r.height))+"px";
 root.style.transform="none";e.preventDefault();
});
head.addEventListener("pointerup",e=>{windowGesture=null;head.releasePointerCapture?.(e.pointerId)});
head.addEventListener("pointercancel",()=>{windowGesture=null});
close.addEventListener("click",()=>{root.hidden=true;gesture=null});
power.addEventListener("click",()=>{
 if(!current)return;
 if(native())window.__ARMIX_NATIVE_M2__?.consumeAction?.(current.track,"slotPower",{dataset:{slot:String(current.slot)}});
 else {current.track.slotBypass[current.slot]=!current.track.slotBypass[current.slot];
  window.__ARMIX_NATIVE_EQ_REFRESH__?.(current.track.id);}
});
window.addEventListener("resize",()=>{
 const r=root.getBoundingClientRect();
 if(!root.hidden&&r.width){root.style.left=clamp(r.left,0,Math.max(0,innerWidth-r.width))+"px";
 root.style.top=clamp(r.top,0,Math.max(0,innerHeight-r.height))+"px";root.style.transform="none";}
 draw();
});
const api={
 id:ID,
 // Public catalog delegates insertion to this one native-only bridge method.
 // The public Pages HTML contains no native authority or private bridge name.
 insert(target){
  return Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target));
 },
 open(track,slot){
  current={track,slot,channelId:null,instanceId:null};selected=2;pending=null;latest.clear();draft=null;
  const n=state();const nativeId=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const ch=n?.graph?.channels?.find(x=>x.id===nativeId);
  if(native()&&(n?.admitted||n?.logical)&&ch?.inserts?.[slot]?.catalogId===ID){
   current.channelId=nativeId;current.instanceId=ch.inserts[slot].id;
   mode="NATIVE";bands=fresh();sampleRate=48000;
   hint("NATIVE · READING ACCEPTED DSP STATE");requestSnapshot();
  }else if(!native()){
   mode="DEMO";bands=(demos.get(track.id+":"+slot)||fresh()).map(b=>({...b}));
   sampleRate=48000;hint("BROWSER PREVIEW · NO AUDIO ENGINE");
  }else{mode="UNAVAILABLE";bands=fresh();hint("NATIVE EQ NOT ACCEPTED · CHECK FX SLOT");}
  channel.textContent=track.name+" · FX "+(slot+1);
  root.hidden=false;draw();
 },
 close(){root.hidden=true;}
};
window.ArSonMixNativeEq=Object.freeze(api);
backend()?.addEventListener?.("arMixBuiltinEqQueue",ack=>{
 if(!pending||ack?.requestId!==pending.id)return;
 if(ack.status==="REJECTED"){pending=null;latest.clear();draft=null;
  hint("NATIVE REJECTED · "+(ack.errorCode||"UNKNOWN"));requestSnapshot();}
});
backend()?.addEventListener?.("arMixBuiltinEqAck",ack=>{
 if(!pending||ack?.requestId!==pending.id)return;
 if(ack.status!=="APPLIED"){pending=null;latest.clear();draft=null;
  hint("DSP DID NOT APPLY · "+(ack.errorCode||"UNKNOWN"));requestSnapshot();return;}
 pending.ack=true;hint("DSP APPLIED · VERIFYING READBACK");requestSnapshot();
});
backend()?.addEventListener?.("arMixBuiltinEqSnapshot",s=>{
 if(!current||!native()||s?.requestId!==readbackRequest)return;
 const c=context();
 if(!c||s.status!=="ACCEPTED"||s.channelId!==c.channelId||
    s.instanceId!==c.instanceId||s.deviceEpoch!==c.deviceEpoch||
    s.graphRevision!==c.graphRevision||!Array.isArray(s.bands)||
    s.bands.length!==8 || !Number.isFinite(s.sampleRate)) {
  hint("NATIVE EQ STALE OR INVALID SNAPSHOT");return;
 }
 const rate=sampleRate;sampleRate=s.sampleRate;
 if(!s.bands.every((b,i)=>valid(i,b))){sampleRate=rate;hint("NATIVE EQ PARAMETER READBACK INVALID");return;}
 if(pending&&(!pending.ack||s.appliedRequestId!==pending.id))return;
 bands=s.bands.map(b=>({...b}));pending=null;
 if(latest.size){
  const [index,value]=latest.entries().next().value;
  latest.delete(index);send(index,value);
 }else draft=null;
 hint("NATIVE DSP ACCEPTED · "+c.instanceId);
 draw();
});
backend()?.addEventListener?.("arMixConsoleSnapshot",()=>{
 if(root.hidden||!native()||!current?.instanceId)return;
 queueMicrotask(()=>{
  if(!context()){pending=null;latest.clear();draft=null;
   hint("DEVICE/GRAPH CHANGED · REOPEN ACCEPTED FX");draw();}
  else updateKnobs(); // authoritative scalar readback, no EQ curve rebuild
 });
});
})();