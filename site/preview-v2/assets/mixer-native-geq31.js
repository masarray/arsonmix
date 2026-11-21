/* ArSonMix Graphic EQ 31 — ONE /preview/ browser/WebView2 editor.
 * Audio is ALWAYS native GraphicEq31Processor. Browser preview is DEMO only.
 * No separate JUCE SVG renderer, popup HWND, WebAudio or second PCM graph. */
(() => {
"use strict";
const ID="armix.builtin.graphic-eq31.v1";
const hz=[20,25,31.5,40,50,63,80,100,125,160,200,250,315,400,500,630,800,1000,1250,1600,2000,2500,3150,4000,5000,6300,8000,10000,12500,16000,20000];
const labels=["20","25","31.5","40","50","63","80","100","125","160","200","250","315","400","500","630","800","1k","1.25k","1.6k","2k","2.5k","3.15k","4k","5k","6.3k","8k","10k","12.5k","16k","20k"];
const native=()=>typeof window.__JUCE__?.backend?.emitEvent==="function";
const backend=()=>window.__JUCE__?.backend;
const state=()=>window.__ARMIX_NATIVE_M2__?.fxEditorState?.()||window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const el=(tag,klass,text)=>{const x=document.createElement(tag);if(klass)x.className=klass;if(text!==undefined)x.textContent=text;return x;};
const clamp=(x,min,max)=>Math.max(min,Math.min(max,x));
const fresh=()=>({bands:Array(31).fill(0),levelDb:0});
const valid=s=>Boolean(s&&Array.isArray(s.bands)&&s.bands.length===31&&
 s.bands.every(v=>typeof v==="number"&&Number.isFinite(v)&&v>=-12&&v<=12)&&
 typeof s.levelDb==="number"&&Number.isFinite(s.levelDb)&&s.levelDb>=-24&&s.levelDb<=6);
const same=(a,b)=>valid(a)&&valid(b)&&a.bands.every((v,i)=>Math.abs(v-b.bands[i])<.051)&&Math.abs(a.levelDb-b.levelDb)<.051;
// v13: 0 dB captures once, but a wheel edit can escape on the next tick.
const wheelEscape=new WeakMap();
function wheelZeroSnap(current,target,capture,owner){
 const state=wheelEscape.get(owner)||{escaping:false,dir:0};
 if(Math.abs(current)<=.001){
  state.escaping=true;state.dir=Math.sign(target);wheelEscape.set(owner,state);
  return target;
 }
 if(state.escaping&&current*state.dir>0&&target*state.dir>0&&
    Math.abs(target)>Math.abs(current)){
  if(Math.abs(target)>capture)state.escaping=false;
  wheelEscape.set(owner,state);return target;
 }
 state.escaping=false;wheelEscape.set(owner,state);
 return Math.abs(target)<=capture||(current<0&&target>0)||
        (current>0&&target<0)?0:target;
}
const format=v=>(v>0?"+":"")+v.toFixed(1)+" dB";
const factory=(which)=>{
 const bands=hz.map(f=>{
  if(which==="VOICE")return f<100?-2.5:f<270?-1.5:f>=1800&&f<5600?2:0;
  if(which==="WARM")return f<=160?2:f>=5000?-1:0;
  if(which==="SPEECH")return f<=125?-3:f>=200&&f<=500?-1:f>=1600&&f<=4000?2.5:0;
  return 0;
 });
 return {bands,levelDb:0};
};
const matchFactory=s=>{
 if(!valid(s))return "CUSTOM";
 for(const value of ["FLAT","VOICE","WARM","SPEECH"])if(same(s,factory(value)))return value;
 return "CUSTOM";
};
let active=null,accepted=fresh(),draft=fresh(),pending=null,queued=new Map();
let readId="",readAt=0,seq=1,dragHeader=null,dragOutput=null;
let activeSlider=-1,lastSendAt=0,failed="",loaded=false,requestToken=0;
const demos=new Map();
const root=el("section","mx-geq31");
root.id="mxNativeGeq31";root.hidden=true;root.setAttribute("role","dialog");
root.setAttribute("aria-label","Graphic EQ 31");root.setAttribute("aria-modal","false");
const header=el("header","mx-geq31-head");
const title=el("div","mx-geq31-brand","Graphic EQ 31");
const meta=el("span","mx-geq31-meta","ARSONMIX  /  1/3 OCTAVE  /  ±12 dB");
const channel=el("span","mx-geq31-channel");
const preset=el("select","mx-geq31-preset");
preset.setAttribute("aria-label","Graphic EQ 31 preset");
for(const [value,name] of [["FLAT","Flat"],["VOICE","Vocal Presence"],
 ["WARM","Warm"],["SPEECH","Speech Clarity"],["CUSTOM","Custom"]]){
 const opt=el("option","",name);opt.value=value;
 if(value==="CUSTOM")opt.disabled=true;preset.append(opt);
}
const save=el("button","mx-geq31-save","Save");
save.type="button";save.title="Save the accepted native FX settings as a user preset";
const flat=el("button","mx-geq31-flat","Flat");flat.type="button";
const power=el("button","mx-geq31-power armix-builtin-fx-power","⏻");
power.type="button";power.disabled=true;power.setAttribute("aria-label","Graphic EQ power");
const close=el("button","mx-geq31-close armix-builtin-fx-close","×");
close.type="button";close.setAttribute("aria-label","Close Graphic EQ 31");
header.append(title,meta,channel,preset,save,flat,power,close);
// Exact v13 author geometry: each fader is a cropped 40 x 229 original SVG,
 // not a CSS-imitation rail/THUMB. DSP ownership remains in native C++.
const body=el("div","mx-geq31-body");
const left=el("div","mx-geq31-scale mx-geq31-left");
const right=el("div","mx-geq31-scale mx-geq31-right");
for(const [label,y] of [["+12",23.4],["0",103],["−12",182.5]]){
 for(const edge of [left,right]){
  const text=el("span","mx-geq31-scale-text",label);
  text.style.top=y+"px";edge.append(text);
 }
}
const scroll=el("div","mx-geq31-scroll");
const bank=el("div","mx-geq31-bank");
const controls=[];
hz.forEach((f,i)=>{
 const cell=el("div","mx-geq31-cell");
 const track=el("div","mx-geq31-track");
 const index=el("span","mx-geq31-index");
 const range=el("input","mx-geq31-range");
 range.type="range";range.min="-12";range.max="12";range.step=".1";
 range.value="0";range.setAttribute("aria-label",labels[i]+" Hz band");
 range.setAttribute("aria-valuetext","0.0 dB");
 range.title=labels[i]+" Hz · double-click Flat · wheel · Shift fine";
 const label=el("div","mx-geq31-freq");
 label.setAttribute("aria-hidden","true");
 const name=el("span","mx-geq31-frequency",f>=1000?String(f/1000):String(f));
 const unit=el("span","mx-geq31-unit",f>=1000?"kHz":"Hz");
 label.append(name,unit);
 track.append(index,range);cell.append(track,label);bank.append(cell);
 controls.push({cell,range,thumb:null,index,frequency:name,unit,detent:false,escape:{escaping:false,dir:0}});
});
scroll.append(bank);
const output=el("div","mx-geq31-output");
const outputLabel=el("span","mx-geq31-output-label","OUTPUT");
const dial=el("div","mx-geq31-dial");
dial.setAttribute("role","slider");dial.tabIndex=0;
dial.setAttribute("aria-label","Output level");dial.setAttribute("aria-valuemin","-24");
dial.setAttribute("aria-valuemax","6");
dial.title="Drag vertically; wheel; double-click 0 dB";
const knobImage=el("img","mx-geq31-knob-image");
knobImage.src="assets/geq31-knob-black.png";
knobImage.alt="";knobImage.draggable=false;
const dialPointer=el("canvas","mx-geq31-dial-pointer");
dialPointer.setAttribute("aria-hidden","true");
dialPointer.width=336;dialPointer.height=336;
dial.append(knobImage,dialPointer);
const outValue=el("output","mx-geq31-output-value","0.0 dB");
// Three hardware LED segments: only confirmed native channel PCM peaks light up.
// Never derive fake audio from the EQ band positions or Output knob.
const levelMeter=el("div","mx-geq31-level-meter");
levelMeter.setAttribute("role","meter");
levelMeter.setAttribute("aria-label","Native channel level");
levelMeter.setAttribute("aria-valuemin","-90");
levelMeter.setAttribute("aria-valuemax","6");
levelMeter.title="Native channel PCM peak · no signal";
const levelLeds=[0,1,2].map(i=>{
 const led=el("span","mx-geq31-level-led mx-geq31-level-led-"+i);
 led.setAttribute("aria-hidden","true");
 levelMeter.append(led);return led;
});
output.append(outputLabel,levelMeter,dial,outValue);
body.append(left,scroll,right,output);
const bypassShade=el("div","mx-geq31-bypass-overlay","BYPASSED");
bypassShade.setAttribute("aria-hidden","true");
const knobCtx=dialPointer.getContext("2d");
function paintKnob(db){
 if(!knobCtx)return;
 // Exact v13 static PNG + high-DPI blue 270-degree pointer/arc geometry.
 const t=clamp(db<=0?(db+24)/48:.5+db/12,0,1);
 const start=-.75*Math.PI,end=start+1.5*Math.PI*t;
 const side=84,cx=side*96.7/192,cy=side*97.97/192;
 const polar=(radius,a)=>[cx+radius*Math.sin(a),cy-radius*Math.cos(a)];
 knobCtx.setTransform(4,0,0,4,0,0);
 knobCtx.clearRect(0,0,side,side);
 knobCtx.save();knobCtx.lineWidth=2;
 knobCtx.strokeStyle="#26b8f6";knobCtx.lineCap="round";knobCtx.lineJoin="round";
 if(t>0){knobCtx.beginPath();knobCtx.arc(cx,cy,side*.365,start-Math.PI/2,end-Math.PI/2);knobCtx.stroke();}
 const p=polar(side*.292,end);
 knobCtx.beginPath();knobCtx.arc(p[0],p[1],2.65,0,2*Math.PI);
 knobCtx.fillStyle="rgba(4,33,50,.82)";knobCtx.fill();
 knobCtx.beginPath();knobCtx.arc(p[0],p[1],1.9,0,2*Math.PI);
 knobCtx.fillStyle="#27c8ff";knobCtx.fill();knobCtx.restore();
}
function gainToShift(v){return v>=0?-(916.5-208.75)*v/12:-(1627.92-916.5)*v/12;}
function makeOwnerSvg(base,i){
 const svg=base.cloneNode(true);
 svg.setAttribute("viewBox","60 0 352 2042");
 svg.setAttribute("preserveAspectRatio","none");
 svg.removeAttribute("width");svg.removeAttribute("height");
 const style=svg.querySelector("style");
 if(!style)throw Error("Owner SVG missing authored style");
 style.textContent=style.textContent.replace(
  /[.]s0\s*[{][^}]*[}]/,
  ".s0 { fill:none;stroke:#131a1d;stroke-miterlimit:10;stroke-width:7.9 }");
 for(const [id,stops] of [
  ["g1",["#929b9f","#a6afb3","#969ea3","#858d92"]],
  ["g2",["#080b0d","#13181c","#1b2125","#20272b","#242a2d"]],
  ["g3",["#090b0e","#13181c","#262d32","#323a40","#262d32","#13181c","#090b0e"]],
  ["g4",["#30383e","#252d32","#141a1e","#252d32","#30383e"]],
  ["g5",["#252e33","#171e22","#0d1216","#171e22","#252e33"]],
  ["g6",["#646e74","#88969e","#b4bdc1","#88969e","#5b656b"]]]){
  const gradient=svg.querySelector('[id="'+id+'"]');
  if(!gradient)throw Error("Owner SVG missing "+id);
  gradient.querySelectorAll("stop").forEach((stop,n)=>{
   if(stops[n])stop.setAttribute("stop-color",stops[n]);
  });
 }
 // Inline 31 copies must not share paint-server IDs.
 for(const id of ["g1","g2","g3","g4","g5","g6","img1"]){
  const element=svg.querySelector('[id="'+id+'"]');
  if(!element)throw Error("Owner SVG missing "+id);
  const unique="geq31-"+i+"-"+id;
  element.setAttribute("id",unique);
  style.textContent=style.textContent.replaceAll("url(#"+id+")","url(#"+unique+")");
  if(id==="img1"){
   const use=svg.querySelector('use[id="_Image3"]');
   if(!use)throw Error("Owner raster shadow missing");
   use.setAttribute("href","#"+unique);
  }
 }
 const freq=svg.querySelector('g[id="Text"] text[id="32"]');
 const unit=svg.querySelector('g[id="Hz"] text');
 const shadow=svg.querySelector('g[id="THUMB"] > g');
 const thumb=svg.querySelector('g[id="THUMB"]');
 // Exact approved v13 SVG composition omits original green/red end-stop
 // marker groups entirely, including their dark shadow paths. The source
 // asset stays byte-for-byte original; only this WebView clone is cropped.
 for(const markerClass of ["s5","s6"]){
  const marker=svg.querySelector('g[id="Track"] path.'+markerClass);
  const holder=marker?.parentElement;
  if(!marker||!holder||holder.children.length!==2||
     holder.parentElement?.getAttribute("id")!=="Track")
    throw Error("Owner end-stop marker SVG structure changed");
  holder.remove();
 }
 if(!freq||!unit||!shadow||!thumb)throw Error("Owner SVG Text/THUMB missing");
 freq.setAttribute("text-anchor","middle");
 freq.setAttribute("style","transform:matrix(4.167,0,0,4.167,237.76,1850.466);font-family:Inter,Arial,sans-serif");
 const number=freq.querySelector("tspan");
 number.textContent=hz[i]>=1000?String(hz[i]/1000):String(hz[i]);
 number.setAttribute("style","font-family:Inter,Arial,sans-serif;font-size:27px;font-weight:700;fill:#344047");
 unit.setAttribute("text-anchor","middle");
 unit.setAttribute("style","transform:matrix(4.167,0,0,4.167,237.76,1933.799);font-family:Inter,Arial,sans-serif");
 const letter=unit.querySelector("tspan");
 letter.textContent=hz[i]>=1000?"kHz":"Hz";
 letter.setAttribute("style","font-family:Inter,Arial,sans-serif;font-size:18px;font-weight:600;fill:#5b6a72");
 shadow.setAttribute("opacity","0.87");
 return {svg,thumb};
}
let artworkLoaded=false;
async function loadArtwork(){
 try{
  const response=await fetch("assets/geq31-owner-slider.svg");
  if(!response.ok)throw Error("GEQ31_OWNER_SVG_HTTP_"+response.status);
  const doc=new DOMParser().parseFromString(await response.text(),"image/svg+xml");
  const svg=doc.documentElement;
  if(svg.localName!=="svg"||!svg.querySelector('g[id="THUMB"]'))throw Error("GEQ31_OWNER_SVG_INVALID");
  const rendered=controls.map((c,i)=>makeOwnerSvg(svg,i));
  rendered.forEach(({svg,thumb},i)=>{
   controls[i].range.before(svg);
   controls[i].thumb=thumb;
  });
  artworkLoaded=true;root.dataset.artwork="v13";paint();
 }catch(error){
  root.dataset.artwork="unavailable";
  hint("GEQ31 V13 ARTWORK UNAVAILABLE · CHECK GUI BUNDLE");
  console.error("GEQ31 visual resource",error);
 }
}
// Keep operator feedback accessible, but remove the redundant visible status bar.
const message=el("span","mx-geq31-message","BROWSER PREVIEW · NO AUDIO");
message.setAttribute("role","status");
message.setAttribute("aria-live","polite");
root.append(header,body,bypassShade,message);
function mount(){if(!root.isConnected)document.body.append(root);loadArtwork();}
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
function hint(value){message.textContent=value;root.dataset.state=/STALE|REJECT|FAIL|UNAVAILABLE|TIMEOUT/i.test(value)?"error":/WAIT|APPLY|READING/i.test(value)?"pending":"ready";}
function context(){
 if(!native()||!active)return null;
 const s=state(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(active.track);
 const ins=s?.graph?.channels?.find(ch=>ch.id===id)?.inserts?.[active.slot];
 if(!(s?.admitted||s?.logical)||!ins||ins.catalogId!==ID||!ins.id||
    (active.instanceId&&active.instanceId!==ins.id))return null;
 return {channelId:id,deviceEpoch:s.logical?"0":s.device.epoch,
  ...(s.logical?{workspaceToken:s.workspaceToken}:{}),
  graphRevision:s.graph.revision,instanceId:ins.id,slot:active.slot};
}
function updatePower(){
 const s=state(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(active?.track);
 const ch=s?.graph?.channels?.find(c=>c.id===id),ins=ch?.inserts?.[active?.slot];
 const usable=Boolean(context());
 power.disabled=!usable;
 power.dataset.fxState=!usable?"unknown":ch.fxRackBypassed?"rack-off":ins?.bypassed?"off":"on";
 power.setAttribute("aria-pressed",String(usable&&!ins?.bypassed&&!ch?.fxRackBypassed));
 power.title=!usable?"DSP instance not accepted":ins?.bypassed?"Enable Graphic EQ":"Bypass Graphic EQ";
 const bypassed=usable&&Boolean(ins?.bypassed||ch?.fxRackBypassed);
 bypassShade.classList.toggle("is-on",bypassed);
}
function paintChannelMeter(view){
 const lease=context();
 const entry=native()&&lease&&view?.admitted&&!view?.retained&&!view?.logical
   ?view?.telemetry?.channelMeterRmsPeak?.[lease.channelId]:null;
 const validPeak=n=>typeof n==="number"&&Number.isFinite(n)&&n>=0;
 const peaks=typeof entry==="number"?(validPeak(entry)?[entry]:[]):
   [entry?.peakL,entry?.peakR].filter(validPeak);
 const db=peaks.length?(Math.max(...peaks)>0
   ?20*Math.log10(Math.max(...peaks)):-90):null;
 // Thresholds are intentionally sparse and quiet: signal, nominal, hot.
 const thresholds=[-30,-14,-3];
 levelLeds.forEach((led,i)=>led.classList.toggle("is-lit",db!==null&&db>=thresholds[i]));
 if(db===null){
  levelMeter.removeAttribute("aria-valuenow");
  levelMeter.title="Native channel PCM peak · unavailable";
 }else{
  levelMeter.setAttribute("aria-valuenow",String(Math.max(-90,Math.min(6,db)).toFixed(1)));
  levelMeter.title="Native channel PCM peak · "+db.toFixed(1)+" dBFS";
 }
}
function paint(){
 controls.forEach((c,i)=>{
  const v=draft.bands[i];
  c.range.value=String(v);
  c.range.setAttribute("aria-valuenow",String(v));
  c.range.setAttribute("aria-valuetext",format(v));
  const shift=gainToShift(v);
  if(c.thumb)c.thumb.setAttribute("transform","translate(0 "+shift.toFixed(3)+")");
  c.index.style.top=Math.round((916.5+shift)*229/2042)+"px";
 });
 const t=clamp((draft.levelDb+24)/30,0,1);
 dial.style.setProperty("--geq-dial-angle",(-132+264*t).toFixed(2)+"deg");
 dial.setAttribute("aria-valuenow",String(draft.levelDb));
 dial.setAttribute("aria-valuetext",format(draft.levelDb));
 outValue.textContent=format(draft.levelDb);paintKnob(draft.levelDb);
 const value=matchFactory(draft);
 // A selected user preset is owned by the shared .armixfx bar. Do not
 // reset that selected option on every graphic-slider paint.
 if(!preset.value.startsWith("user:")||!same(accepted,draft))
   preset.value=value;
 presetBar?.paint?.();
 updatePower();paintChannelMeter(state());
}
function demoSave(){if(active)demos.set(active.track.id+":"+active.slot,{bands:[...draft.bands],levelDb:draft.levelDb});}
function requestSnapshot(force=false){
 const c=context();if(!c)return;
 const now=Date.now();
 if(!force&&readId&&now-readAt<1200)return;
 readId="geqsnap-"+now+"-"+(++seq);readAt=now;
 backend().emitEvent("arMixAction",{action:"nativeGeqSnapshot",requestId:readId,...c});
}
function emit(edit){
 const c=context();
 if(!c){hint("FX LEASE CHANGED · REOPEN GRAPHIC EQ");return false;}
 if(pending)return false;
 if(edit.kind==="curve"&&!valid(edit.settings))return false;
 if((edit.kind==="band"&&(!Number.isInteger(edit.index)||edit.index<0||edit.index>=31||
     !Number.isFinite(edit.value)||edit.value< -12||edit.value>12))||
    (edit.kind==="output"&&(!Number.isFinite(edit.value)||edit.value< -24||edit.value>6)))return false;
 const id="geq-"+String(Date.now()*1000+(seq++%1000));
 pending={id,edit,ack:false,context:c,sentAt:Date.now()};
 backend().emitEvent("arMixAction",{action:"nativeGeqSet",requestId:id,edit,...c});
 lastSendAt=Date.now();hint("DSP · APPLYING · AWAITING AUDIO ACK");
 return true;
}
function flush(){
 if(pending||!native()||!queued.size)return;
 const [key,edit]=queued.entries().next().value;queued.delete(key);
 emit(edit);
}
function queue(edit){
 if(!native()){demoSave();hint("BROWSER PREVIEW · NO AUDIO ENGINE");return;}
 const key=edit.kind==="band"?"b"+edit.index:edit.kind==="output"?"output":"curve";
 // Latest user intent per band/output is never discarded across an ACK.
 if(edit.kind==="curve")queued.clear();
 queued.set(key,edit);
 if(!pending)flush();
}
function updateBand(i,v){
 v=Math.round(clamp(v,-12,12)*10)/10;
 if(draft.bands[i]===v)return;
 draft.bands[i]=v;paint();
 queue({kind:"band",index:i,value:v});
}
function updateOutput(v){
 v=Math.round(clamp(v,-24,6)*10)/10;
 if(draft.levelDb===v)return;
 draft.levelDb=v;paint();
 queue({kind:"output",value:v});
}
controls.forEach((c,i)=>{
 c.range.addEventListener("input",()=>updateBand(i,Number(c.range.value)));
 c.range.addEventListener("change",()=>{if(native())flush();});
 function fromPointer(event){
  const rect=c.cell.getBoundingClientRect();
  const y=clamp((event.clientY-rect.top)/rect.height*2042,208.75,1627.92);
  const proposed=y<916.5?(916.5-y)/(916.5-208.75)*12:
    (916.5-y)/(1627.92-916.5)*12;
  c.detent=Math.abs(proposed)<=(c.detent?1.8:1.1);
  updateBand(i,c.detent?0:proposed);
 }
 c.cell.addEventListener("pointerdown",event=>{
  if(event.button!==0||!artworkLoaded)return;
  activeSlider=i;c.detent=Math.abs(draft.bands[i])<.001;
  c.cell.setPointerCapture(event.pointerId);fromPointer(event);
  event.preventDefault();
 });
 c.cell.addEventListener("pointermove",event=>{
  if(c.cell.hasPointerCapture(event.pointerId))fromPointer(event);
 });
 c.cell.addEventListener("pointerup",event=>{
  if(c.cell.hasPointerCapture(event.pointerId))c.cell.releasePointerCapture(event.pointerId);
  activeSlider=-1;flush();
 });
 c.cell.addEventListener("pointercancel",event=>{
  if(c.cell.hasPointerCapture(event.pointerId))c.cell.releasePointerCapture(event.pointerId);
  activeSlider=-1;requestSnapshot(true);
 });
 c.cell.addEventListener("wheel",event=>{
  if(!artworkLoaded)return;
  event.preventDefault();
  const cur=draft.bands[i],target=clamp(cur-Math.sign(event.deltaY)*.5,-12,12);
  updateBand(i,wheelZeroSnap(cur,target,1.1,c.cell));
 },{passive:false});
 c.cell.addEventListener("dblclick",event=>{event.preventDefault();updateBand(i,0);});
});
dial.addEventListener("pointerdown",e=>{
 if(e.button!==0)return;
 dragOutput={id:e.pointerId,startY:e.clientY,value:draft.levelDb,held:Math.abs(draft.levelDb)<.001};
 dial.setPointerCapture(e.pointerId);e.preventDefault();
});
dial.addEventListener("pointermove",e=>{
 if(!dragOutput||dragOutput.id!==e.pointerId)return;
 const proposed=dragOutput.value+(dragOutput.startY-e.clientY)*(e.shiftKey?.025:.2);
 dragOutput.held=Math.abs(proposed)<=(dragOutput.held?1.2:.65);
 updateOutput(dragOutput.held?0:proposed);
 e.preventDefault();
});
const stopDial=e=>{if(dragOutput?.id!==e.pointerId)return;dragOutput=null;try{dial.releasePointerCapture(e.pointerId);}catch{}flush();};
dial.addEventListener("pointerup",stopDial);dial.addEventListener("pointercancel",stopDial);
dial.addEventListener("wheel",e=>{e.preventDefault();updateOutput(wheelZeroSnap(draft.levelDb,clamp(draft.levelDb+(e.deltaY<0?.5:-.5),-24,6),.65,dial));},{passive:false});
dial.addEventListener("dblclick",e=>{e.preventDefault();updateOutput(0);});
dial.addEventListener("keydown",e=>{
 const step=e.shiftKey?.1:.5;
 if(e.key==="ArrowUp"||e.key==="ArrowRight")updateOutput(draft.levelDb+step);
 else if(e.key==="ArrowDown"||e.key==="ArrowLeft")updateOutput(draft.levelDb-step);
 else if(e.key==="Home")updateOutput(-24);
 else if(e.key==="End")updateOutput(6);
 else return;e.preventDefault();
});
function applyCurve(s){
 if(!valid(s))return null;
 draft={bands:[...s.bands],levelDb:s.levelDb};
 paint();queue({kind:"curve",settings:{bands:[...s.bands],levelDb:s.levelDb}});
 return pending?.id||null;
}
const presetBar=window.ArSonMixBuiltinFxPresetBar?.create({
 host:root,select:preset,label:"Graphic EQ 31",catalogId:ID,context,
 stable:()=>!pending&&!queued.size,readAccepted:()=>({bands:[...accepted.bands],levelDb:accepted.levelDb}),
 validate:valid,factoryValue:matchFactory,notice:hint,apply:applyCurve
});
if(!presetBar)throw Error("Shared built-in FX preset bar unavailable");
preset.addEventListener("change",()=>{
 if(presetBar.selectUser(preset.value))return;
 if(preset.value==="CUSTOM")return;
 applyCurve(factory(preset.value));
 presetBar.factoryIntent(pending?.id||null);
});
flat.addEventListener("click",()=>{
 const s={bands:Array(31).fill(0),levelDb:draft.levelDb};
 applyCurve(s);
});
save.addEventListener("click",()=>{
 if(!native()){hint("BROWSER PREVIEW · NO AUDIO ENGINE");return;}
 root.querySelector(".mx-fxp-save-as")?.click();
});
power.addEventListener("click",()=>{
 if(native()&&context())window.__ARMIX_NATIVE_M2__?.consumeAction?.(active.track,"slotPower",{dataset:{slot:String(active.slot)}});
 else if(!native()&&active){active.track.slotBypass[active.slot]=!active.track.slotBypass[active.slot];paint();}
});
function dismiss(){
 root.hidden=true;active=null;pending=null;queued.clear();readId="";
 paintChannelMeter(null);presetBar.close();
}
close.addEventListener("click",dismiss);
function centerEditor(){
 if(root.hidden)return false;
 const r=root.getBoundingClientRect();
 root.style.transform="none";
 root.style.left=Math.max(0,(innerWidth-r.width)/2)+"px";
 root.style.top=Math.max(0,(innerHeight-r.height)/2)+"px";
 return true;
}
header.addEventListener("dblclick",e=>{
 if(!e.target.closest("button,select")){e.preventDefault();centerEditor();}
});
header.addEventListener("pointerdown",e=>{
 if(e.button!==0||e.target.closest("button,select"))return;
 const r=root.getBoundingClientRect();
 dragHeader={id:e.pointerId,dx:e.clientX-r.left,dy:e.clientY-r.top};
 header.setPointerCapture(e.pointerId);e.preventDefault();
});
header.addEventListener("pointermove",e=>{
 if(!dragHeader||dragHeader.id!==e.pointerId)return;
 const r=root.getBoundingClientRect();
 // WebView2 cannot paint outside its native HWND; permit edge parking with
 // a 48px visible grip, recover with FX-rack double-click (common handler).
 root.style.left=clamp(e.clientX-dragHeader.dx,Math.min(0,48-r.width),Math.max(0,innerWidth-48))+"px";
 root.style.top=clamp(e.clientY-dragHeader.dy,0,Math.max(0,innerHeight-38))+"px";
 e.preventDefault();
});
header.addEventListener("pointerup",e=>{dragHeader=null;try{header.releasePointerCapture(e.pointerId);}catch{}});
header.addEventListener("pointercancel",()=>{dragHeader=null;});
window.addEventListener("keydown",e=>{if(e.key==="Escape"&&!root.hidden&&!e.target.closest?.(".mx-fxp-form"))dismiss();});
window.addEventListener("resize",()=>{
 if(root.hidden)return;const r=root.getBoundingClientRect();
 root.style.left=clamp(r.left,0,Math.max(0,innerWidth-r.width))+"px";
 root.style.top=clamp(r.top,0,Math.max(0,innerHeight-r.height))+"px";
});
function open(track,slot){
 pending=null;queued.clear();readId="";readAt=0;activeSlider=-1;failed="";
 presetBar.reset();active={track,slot,instanceId:null};
 const s=state(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
 const ins=s?.graph?.channels?.find(c=>c.id===id)?.inserts?.[slot];
 if(native()&&(s?.admitted||s?.logical)&&ins?.catalogId===ID&&ins.id){
  active.instanceId=ins.id;accepted=fresh();draft=fresh();
  hint("NATIVE · READING ACCEPTED DSP");requestSnapshot(true);
 }else if(!native()){
  accepted=demos.get(track.id+":"+slot)||fresh();
  draft={bands:[...accepted.bands],levelDb:accepted.levelDb};
  hint("BROWSER PREVIEW · NO AUDIO ENGINE");
 }else{
  accepted=fresh();draft=fresh();hint("FX INSTANCE NOT ACCEPTED · NO AUDIO EDIT");
 }
 channel.textContent=(track.name||"Channel")+" · FX "+(slot+1);
 root.dataset.fxTrackId=String(track.id);root.dataset.fxSlot=String(slot);
 root.hidden=false;paint();
}
window.ArSonMixGraphicEq31=Object.freeze({
 id:ID,open,close:dismiss,recenter:centerEditor,
 insert:target=>Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target))
});
backend()?.addEventListener?.("arMixBuiltinGeqQueue",ack=>{
 if(!pending||ack?.requestId!==pending.id)return;
 if(ack.status==="REJECTED"){
  pending=null;queued.clear();hint("DSP REJECTED · "+(ack.errorCode||"INVALID"));
  requestSnapshot(true);
 }
});
backend()?.addEventListener?.("arMixBuiltinGeqAck",reply=>{
 if(!pending||reply?.requestId!==pending.id)return;
 if(reply.status!=="APPLIED"){
  pending=null;queued.clear();hint("DSP NOT APPLIED · "+(reply.errorCode||"STALE"));
  requestSnapshot(true);return;
 }
 pending.ack=true;requestSnapshot(true);
});
backend()?.addEventListener?.("arMixBuiltinGeqSnapshot",reply=>{
 if(root.hidden||!native()||reply?.requestId!==readId)return;
 readId="";readAt=0;
 const c=context();
 if(!c||reply.status!=="ACCEPTED"||reply.channelId!==c.channelId||
    reply.instanceId!==c.instanceId||reply.deviceEpoch!==c.deviceEpoch||
    reply.graphRevision!==c.graphRevision||
    !valid({bands:reply.bands,levelDb:reply.levelDb})||
    reply.addedLatencySamples!==0){
  hint("GEQ DSP READBACK STALE · REOPEN FX");return;
 }
 const settings={bands:[...reply.bands],levelDb:reply.levelDb};
 let appliedId="";
 if(pending){
  if(!pending.ack||reply.appliedRequestId!==pending.id)return;
  appliedId=pending.id;pending=null;accepted=settings;
  if(queued.size){flush();}
  else if(activeSlider<0&&!dragOutput)draft={bands:[...settings.bands],levelDb:settings.levelDb};
  hint("NATIVE DSP ACCEPTED · 0 ADDED PCM FRAMES");
 }else{
  accepted=settings;
  if(!queued.size&&activeSlider<0&&!dragOutput)draft={bands:[...settings.bands],levelDb:settings.levelDb};
  hint("NATIVE DSP ACCEPTED · 0 ADDED PCM FRAMES");
 }
 presetBar.onAccepted(accepted,appliedId);paint();
});
backend()?.addEventListener?.("arMixConsoleSnapshot",()=>{
 if(root.hidden||!native())return;
 if(!context()){
  pending=null;queued.clear();hint("DEVICE OR GRAPH CHANGED · REOPEN FX");
 }else {updatePower();paintChannelMeter(state());}
});
window.setInterval(()=>{
 if(root.hidden||!active)return;
 presetBar.tick();
 if(native()&&context()){
  if(pending&&Date.now()-pending.sentAt>5000){
   pending=null;queued.clear();hint("DSP ACK TIMEOUT · CHECK DEVICE");
   requestSnapshot(true);
  }else if(!pending)requestSnapshot();
 }
},220);
})();