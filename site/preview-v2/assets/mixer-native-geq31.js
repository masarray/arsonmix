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
const snap0=v=>Math.abs(v)<.75?0:v;
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
const body=el("div","mx-geq31-body");
const left=el("div","mx-geq31-scale mx-geq31-left");
const right=el("div","mx-geq31-scale mx-geq31-right");
for(const [text,y] of [["+12",0],["0",50],["−12",100]]){
 for(const target of [left,right]){
  const t=el("span","mx-geq31-scale-text",text);
  t.style.top=y+"%";target.append(t);
 }
}
const scroll=el("div","mx-geq31-scroll");
const bank=el("div","mx-geq31-bank");
const controls=[];
hz.forEach((f,i)=>{
 const cell=el("div","mx-geq31-cell");
 const track=el("div","mx-geq31-track");
 const ticks=el("div","mx-geq31-ticks");
 const zero=el("span","mx-geq31-zero");
 const thumb=el("span","mx-geq31-thumb");
 const ridge=el("span","mx-geq31-ridge");thumb.append(ridge);
 const range=el("input","mx-geq31-range");
 range.type="range";range.min="-12";range.max="12";range.step=".1";
 range.value="0";range.setAttribute("aria-label",labels[i]+" Hz band");
 range.setAttribute("aria-valuetext","0.0 dB");
 range.title=labels[i]+" Hz · double-click Flat · wheel adjusts · Shift for fine";
 track.append(ticks,zero,thumb,range);
 const label=el("div","mx-geq31-freq");
 const name=el("span","mx-geq31-frequency",labels[i]);
 const unit=el("span","mx-geq31-unit",f>=1000?"kHz":"Hz");
 label.append(name,unit);cell.append(track,label);bank.append(cell);
 controls.push({cell,range,thumb,frequency:name,unit});
});
scroll.append(bank);
const output=el("div","mx-geq31-output");
const outputLabel=el("span","mx-geq31-output-label","OUTPUT");
const dial=el("div","mx-geq31-dial");
dial.setAttribute("role","slider");dial.tabIndex=0;
dial.setAttribute("aria-label","Output level");dial.setAttribute("aria-valuemin","-24");
dial.setAttribute("aria-valuemax","6");dial.title="Drag vertically; wheel; double-click to 0 dB";
const dialPointer=el("span","mx-geq31-dial-pointer");dial.append(dialPointer);
const outValue=el("output","mx-geq31-output-value","0.0 dB");
output.append(outputLabel,dial,outValue);
body.append(left,scroll,right,output);
const footer=el("footer","mx-geq31-footer");
const message=el("span","mx-geq31-message","BROWSER PREVIEW · NO AUDIO");
footer.append(message);
root.append(header,body,footer);
function mount(){if(!root.isConnected)document.body.append(root);}
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
}
function paint(){
 controls.forEach((c,i)=>{
  const v=draft.bands[i];
  c.range.value=String(v);
  c.range.setAttribute("aria-valuenow",String(v));
  c.range.setAttribute("aria-valuetext",format(v));
  c.thumb.style.top=((12-v)/24*100).toFixed(3)+"%";
 });
 const t=clamp((draft.levelDb+24)/30,0,1);
 dial.style.setProperty("--geq-dial-angle",(-132+264*t).toFixed(2)+"deg");
 dial.setAttribute("aria-valuenow",String(draft.levelDb));
 dial.setAttribute("aria-valuetext",format(draft.levelDb));
 outValue.textContent=format(draft.levelDb);
 const value=matchFactory(draft);
 // A selected user preset is owned by the shared .armixfx bar. Do not
 // reset that selected option on every graphic-slider paint.
 if(!preset.value.startsWith("user:")||!same(accepted,draft))
   preset.value=value;
 presetBar?.paint?.();
 updatePower();
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
 v=Math.round(clamp(snap0(v),-12,12)*10)/10;
 if(draft.bands[i]===v)return;
 draft.bands[i]=v;paint();
 queue({kind:"band",index:i,value:v});
}
function updateOutput(v){
 v=Math.round(clamp(snap0(v),-24,6)*10)/10;
 if(draft.levelDb===v)return;
 draft.levelDb=v;paint();
 queue({kind:"output",value:v});
}
controls.forEach((c,i)=>{
 c.range.addEventListener("input",()=>updateBand(i,Number(c.range.value)));
 c.range.addEventListener("change",()=>{if(native())flush();});
 c.range.addEventListener("pointerdown",()=>{activeSlider=i;});
 c.range.addEventListener("pointerup",()=>{activeSlider=-1;flush();});
 c.range.addEventListener("pointercancel",()=>{activeSlider=-1;requestSnapshot(true);});
 c.range.addEventListener("wheel",e=>{
  e.preventDefault();const delta=(e.deltaY<0?1:-1)*(e.shiftKey?.2:1);
  updateBand(i,draft.bands[i]+delta);
 },{passive:false});
 c.range.addEventListener("dblclick",e=>{e.preventDefault();updateBand(i,0);});
});
dial.addEventListener("pointerdown",e=>{
 if(e.button!==0)return;
 dragOutput={id:e.pointerId,startY:e.clientY,value:draft.levelDb};
 dial.setPointerCapture(e.pointerId);e.preventDefault();
});
dial.addEventListener("pointermove",e=>{
 if(!dragOutput||dragOutput.id!==e.pointerId)return;
 updateOutput(dragOutput.value+(dragOutput.startY-e.clientY)*(e.shiftKey?.025:.12));
 e.preventDefault();
});
const stopDial=e=>{if(dragOutput?.id!==e.pointerId)return;dragOutput=null;try{dial.releasePointerCapture(e.pointerId);}catch{}flush();};
dial.addEventListener("pointerup",stopDial);dial.addEventListener("pointercancel",stopDial);
dial.addEventListener("wheel",e=>{e.preventDefault();updateOutput(draft.levelDb+(e.deltaY<0?.5:-.5));},{passive:false});
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
function dismiss(){root.hidden=true;active=null;pending=null;queued.clear();readId="";presetBar.close();}
close.addEventListener("click",dismiss);
header.addEventListener("pointerdown",e=>{
 if(e.button!==0||e.target.closest("button,select"))return;
 const r=root.getBoundingClientRect();
 dragHeader={id:e.pointerId,dx:e.clientX-r.left,dy:e.clientY-r.top};
 header.setPointerCapture(e.pointerId);e.preventDefault();
});
header.addEventListener("pointermove",e=>{
 if(!dragHeader||dragHeader.id!==e.pointerId)return;
 const r=root.getBoundingClientRect();
 root.style.left=clamp(e.clientX-dragHeader.dx,0,Math.max(0,innerWidth-r.width))+"px";
 root.style.top=clamp(e.clientY-dragHeader.dy,0,Math.max(0,innerHeight-r.height))+"px";
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
 root.hidden=false;paint();
}
window.ArSonMixGraphicEq31=Object.freeze({
 id:ID,open,close:dismiss,
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
 }else updatePower();
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