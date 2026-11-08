/* ArSonMix Compressor: zero-lookahead ChannelId native insert.
 * One modeless /preview/ editor, no simulated native audio. Settings are
 * accepted ONLY after callback ACK plus current graph-revision readback. */
(() => {
"use strict";
const ID="armix.builtin.channel-compressor.v1";
const NS="http://www.w3.org/2000/svg";
const defs=Object.freeze({
 thresholdDb:{label:"THRESHOLD",min:-60,max:0,step:.5,unit:"dB"},
 ratio:{label:"RATIO",min:1,max:20,step:.1,unit:":1"},
 attackMs:{label:"ATTACK",min:.1,max:100,step:.1,unit:"ms"},
 releaseMs:{label:"RELEASE",min:10,max:2000,step:5,unit:"ms"},
 kneeDb:{label:"KNEE",min:0,max:24,step:.5,unit:"dB"},
 makeupDb:{label:"MAKEUP",min:-12,max:24,step:.5,unit:"dB"},
 mix:{label:"MIX",min:0,max:1,step:.01,unit:"%"}
});
const defaults=()=>({thresholdDb:-18,ratio:3,attackMs:10,releaseMs:120,
 kneeDb:6,makeupDb:0,mix:1});
const presets=Object.freeze({
 "VOCAL":{thresholdDb:-18,ratio:3,attackMs:10,releaseMs:120,kneeDb:6,makeupDb:2,mix:1},
 "SPEECH":{thresholdDb:-24,ratio:3,attackMs:5,releaseMs:110,kneeDb:8,makeupDb:3,mix:1},
 "GENTLE":{thresholdDb:-14,ratio:2,attackMs:25,releaseMs:240,kneeDb:12,makeupDb:1,mix:1},
 "DRUM":{thresholdDb:-16,ratio:4,attackMs:25,releaseMs:90,kneeDb:4,makeupDb:0,mix:1}
});
const clamp=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
const native=()=>Boolean(window.__JUCE__?.backend?.emitEvent);
const backend=()=>window.__JUCE__?.backend;
const snapshot=()=>window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const dom=(tag,css,value)=>{const e=document.createElement(tag);if(css)e.className=css;
 if(value!==undefined)e.textContent=value;return e;};
const valid=v=>v&&Object.entries(defs).every(([key,r])=>
 Number.isFinite(v[key])&&v[key]>=r.min&&v[key]<=r.max);
const demos=new Map();
let current=null,accepted=defaults(),draft=defaults(),pending=null,delayed=null,
 readId="",seq=1,lastSend=0,windowDrag=null,graphDrag=null,grDb=0;
const panel=dom("section","mx-comp");panel.hidden=true;panel.id="mxNativeComp";
panel.setAttribute("role","dialog");
panel.setAttribute("aria-label","ArSonMix Compressor");
panel.setAttribute("aria-modal","false");
const header=dom("header","mx-comp-head"),brand=dom("span","mx-comp-brand","ArSonMix  /  COMPRESSOR"),
 channel=dom("span","mx-comp-channel"),power=dom("button","mx-comp-power","⏻"),
 close=dom("button","mx-comp-close","×");
power.type=close.type="button";power.title="Toggle accepted FX insert bypass";
close.setAttribute("aria-label","Close Compressor");
header.append(brand,channel,power,close);
const visual=dom("div","mx-comp-visual");
const plot=dom("div","mx-comp-plot"),svg=document.createElementNS(NS,"svg");
svg.setAttribute("viewBox","0 0 480 225");svg.setAttribute("preserveAspectRatio","none");
svg.setAttribute("aria-label","Compressor input-output transfer curve; drag threshold handle");
svg.setAttribute("role","img");
plot.append(svg);
const meter=dom("div","mx-comp-meter");
const meterName=dom("span","","GR");
const meterRail=dom("div","mx-comp-grrail");
const meterFill=dom("div","mx-comp-grfill");meterRail.append(meterFill);
const meterValue=dom("span","mx-comp-grvalue","0.0 dB");
meter.append(meterName,meterRail,meterValue);
visual.append(plot,meter);
const controls=dom("div","mx-comp-controls"),inputs={};
Object.entries(defs).forEach(([key,r])=>{
 const item=dom("label","mx-comp-control");
 const name=dom("span","mx-comp-label",r.label);
 const output=dom("output","mx-comp-value");
 const input=dom("input","mx-comp-slider");
 input.type="range";input.min=String(r.min);input.max=String(r.max);
 input.step=String(r.step);input.setAttribute("aria-label",r.label);
 input.setAttribute("aria-valuetext",r.label);
 item.append(name,output,input);controls.append(item);inputs[key]={input,output};
 input.addEventListener("input",()=>{
  const value=Number(input.value);if(!Number.isFinite(value))return;
  draft={...draft,[key]:value};paint();
  if(!native()){saveDemo();hint("BROWSER PREVIEW · NO AUDIO ENGINE");return;}
  if(Date.now()-lastSend>=130)send();
  else delayed={...draft};
 });
 input.addEventListener("change",()=>{
  if(native())send();else saveDemo();
 });
});
const foot=dom("div","mx-comp-foot");
const presetButtons=dom("div","mx-comp-presets");
Object.keys(presets).forEach(name=>{
 const b=dom("button","mx-comp-preset",name);b.type="button";
 b.addEventListener("click",()=>{
  draft={...presets[name]};paint();
  if(native())send();else{saveDemo();hint("BROWSER PREVIEW · NO AUDIO ENGINE");}
 });
 presetButtons.append(b);
});
const reset=dom("button","mx-comp-preset","RESET");reset.type="button";
reset.addEventListener("click",()=>{draft=defaults();paint();
 if(native())send();else{saveDemo();hint("BROWSER PREVIEW · NO AUDIO ENGINE");}
});
presetButtons.append(reset);
const status=dom("span","mx-comp-status","BROWSER PREVIEW · NO AUDIO");
foot.append(presetButtons,status);
panel.append(header,visual,controls,foot);
function mount(){if(!panel.isConnected)document.body.append(panel);}
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
function hint(value){status.textContent=value;}
function context(){
 if(!native()||!current)return null;
 const n=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const lane=n?.graph?.channels?.find(c=>c.id===id);
 const insert=lane?.inserts?.[current.slot];
 if(!n?.admitted||!insert||insert.catalogId!==ID||!insert.id||
    (current.instanceId&&current.instanceId!==insert.id))return null;
 return {channelId:id,deviceEpoch:n.device.epoch,
  graphRevision:n.graph.revision,instanceId:insert.id,slot:current.slot};
}
function format(key,value){
 if(key==="ratio")return value.toFixed(1)+":1";
 if(key==="mix")return Math.round(value*100)+"%";
 return (key==="attackMs"||key==="releaseMs"?value.toFixed(value<10?1:0):value.toFixed(1))+
   (key.endsWith("Ms")?" ms":" dB");
}
function compressedOutput(inputDb,s){
 const distance=inputDb-s.thresholdDb,strength=1-1/s.ratio;
 let reduction=0;
 if(s.kneeDb<=0)reduction=Math.max(0,distance)*strength;
 else if(distance>=s.kneeDb/2)reduction=distance*strength;
 else if(distance>-s.kneeDb/2)
  reduction=(distance+s.kneeDb/2)**2/(2*s.kneeDb)*strength;
 // The displayed transfer curve must match the *actual* parallel wet/dry
 // output. At MIX=0 this becomes exact unity, even with makeup enabled.
 const wetGain=Math.pow(10,(-reduction+s.makeupDb)/20);
 const outputGain=(1-s.mix)+s.mix*wetGain;
 return inputDb+20*Math.log10(Math.max(1e-12,outputGain));
}
function paint(){
 for(const [key,c] of Object.entries(inputs)){
  const value=draft[key],r=defs[key];
  c.input.value=String(value);
  c.output.textContent=format(key,value);
  c.input.style.setProperty("--progress",(100*(value-r.min)/(r.max-r.min))+"%");
 }
 // Exact static transfer geometry from the native compressor DSP.
 const x=db=>32+(clamp(db,-60,0)+60)/60*425;
 const y=db=>198-(clamp(db,-60,0)+60)/60*176;
 const path=Array.from({length:121},(_,i)=>{
  const v=-60+i*.5;
  return (i?"L":"M")+x(v).toFixed(1)+","+y(compressedOutput(v,draft)).toFixed(1);
 }).join(" ");
 const grid=[];
 for(const d of [-60,-48,-36,-24,-12,0]){
  grid.push('<line x1="'+x(d)+'" y1="22" x2="'+x(d)+'" y2="198" class="mx-comp-grid"/>');
  grid.push('<line x1="32" y1="'+y(d)+'" x2="457" y2="'+y(d)+'" class="mx-comp-grid"/>');
  grid.push('<text x="'+x(d)+'" y="214" class="mx-comp-tick" text-anchor="middle">'+d+'</text>');
  grid.push('<text x="24" y="'+(y(d)+3)+'" class="mx-comp-tick" text-anchor="end">'+d+'</text>');
 }
 svg.innerHTML='<rect width="480" height="225" fill="#0e1e29"/>'+
  grid.join("")+'<path d="M32 198 L457 22" class="mx-comp-unity"/>'+
  '<path d="'+path+'" class="mx-comp-curve"/>'+
  '<circle cx="'+x(draft.thresholdDb)+'" cy="'+y(compressedOutput(draft.thresholdDb,draft))+
  '" r="6" class="mx-comp-handle"/>'+
  '<text x="40" y="14" class="mx-comp-axis">OUTPUT dBFS</text>'+
  '<text x="451" y="14" class="mx-comp-axis" text-anchor="end">INPUT →</text>';
 const c=context(),insert=c?snapshot()?.graph?.channels?.find(x=>x.id===c.channelId)?.inserts?.[current.slot]:null;
 // A bypassed graph node is not rendering PCM; its atomic block meter may
 // still contain its last active value, so never display stale GR as live.
 const shownReduction=insert?.bypassed===true?0:grDb;
 meterFill.style.height=(100*clamp(shownReduction,0,30)/30)+"%";
 meterValue.textContent=shownReduction.toFixed(1)+" dB";
 power.classList.toggle("bypassed",insert?.bypassed===true);
 power.title=insert?.bypassed?"Enable compressor":"Bypass compressor";
}
function saveDemo(){if(current)demos.set(current.track.id+":"+current.slot,{...draft});}
function requestSnapshot(){
 const c=context();if(!c)return;
 readId="compsnap-"+Date.now()+"-"+(++seq);
 backend().emitEvent("arMixAction",{action:"nativeCompSnapshot",requestId:readId,...c});
}
function send(){
 if(!native()||!current)return;
 const c=context();if(!c){hint("NATIVE FX INSTANCE UNAVAILABLE");return;}
 if(!valid(draft)){hint("INVALID COMPRESSOR SETTINGS");return;}
 if(pending){
  // A slider input + change commonly fires twice at the same value. Keep
  // only a genuinely different final setting; never replay an identical edit.
  delayed=Object.keys(defs).every(k=>draft[k]===pending.settings[k])?
   null:{...draft};
  return;
 }
 delayed=null;
 const id="comp-"+String(Date.now()*1000+(seq++%1000));
 pending={id,ack:false,context:c,settings:{...draft}};
 lastSend=Date.now();
 backend().emitEvent("arMixAction",{action:"nativeCompSet",requestId:id,
  settings:{...draft},...c});
 hint("DSP · APPLYING");
}
svg.addEventListener("pointerdown",e=>{
 if(e.button!==0)return;
 graphDrag=e.pointerId;svg.setPointerCapture(e.pointerId);
 e.preventDefault();
});
svg.addEventListener("pointermove",e=>{
 if(graphDrag!==e.pointerId)return;
 const r=svg.getBoundingClientRect();
 const x=(e.clientX-r.left)*480/r.width;
 draft={...draft,thresholdDb:Math.round(clamp(-60+(x-32)*60/425,-60,0)*2)/2};
 paint();if(native()&&Date.now()-lastSend>=130)send();
 e.preventDefault();
});
const finishGraph=e=>{
 if(graphDrag!==e.pointerId)return;
 graphDrag=null;try{svg.releasePointerCapture(e.pointerId);}catch{}
 if(native())send();else{saveDemo();hint("BROWSER PREVIEW · NO AUDIO ENGINE");}
};
svg.addEventListener("pointerup",finishGraph);
svg.addEventListener("pointercancel",()=>{graphDrag=null;});
header.addEventListener("pointerdown",e=>{
 if(e.button!==0||e.target.closest("button"))return;
 const r=panel.getBoundingClientRect();
 windowDrag={id:e.pointerId,dx:e.clientX-r.left,dy:e.clientY-r.top};
 header.setPointerCapture(e.pointerId);e.preventDefault();
});
header.addEventListener("pointermove",e=>{
 if(!windowDrag||windowDrag.id!==e.pointerId)return;
 const r=panel.getBoundingClientRect();
 panel.style.left=clamp(e.clientX-windowDrag.dx,0,Math.max(0,innerWidth-r.width))+"px";
 panel.style.top=clamp(e.clientY-windowDrag.dy,0,Math.max(0,innerHeight-r.height))+"px";
 e.preventDefault();
});
header.addEventListener("pointerup",e=>{windowDrag=null;header.releasePointerCapture?.(e.pointerId)});
header.addEventListener("pointercancel",()=>{windowDrag=null});
close.addEventListener("click",()=>{panel.hidden=true;pending=null;delayed=null;});
power.addEventListener("click",()=>{
 if(!current)return;
 if(native())window.__ARMIX_NATIVE_M2__?.consumeAction?.(
  current.track,"slotPower",{dataset:{slot:String(current.slot)}});
 else{current.track.slotBypass[current.slot]=!current.track.slotBypass[current.slot];paint();}
});
window.addEventListener("resize",()=>{
 if(panel.hidden)return;const r=panel.getBoundingClientRect();
 panel.style.left=clamp(r.left,0,Math.max(0,innerWidth-r.width))+"px";
 panel.style.top=clamp(r.top,0,Math.max(0,innerHeight-r.height))+"px";
});
window.addEventListener("keydown",e=>{
 if(e.key==="Escape"&&!panel.hidden){panel.hidden=true;pending=null;delayed=null;}
});
window.ArSonMixNativeCompressor=Object.freeze({
 id:ID,
 insert(target){return Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target));},
 open(track,slot){
  current={track,slot,instanceId:null};
  pending=null;delayed=null;grDb=0;
  const n=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const insert=n?.graph?.channels?.find(c=>c.id===id)?.inserts?.[slot];
  if(native()&&n?.admitted&&insert?.catalogId===ID&&insert.id){
   current.instanceId=insert.id;
   accepted=defaults();draft={...accepted};
   hint("NATIVE · READING DSP STATE");requestSnapshot();
  }else if(!native()){
   accepted=demos.get(track.id+":"+slot)||defaults();draft={...accepted};
   hint("BROWSER PREVIEW · NO AUDIO ENGINE");
  }else{accepted=defaults();draft={...accepted};hint("NATIVE FX NOT ACCEPTED");}
  channel.textContent=track.name+" · FX "+(slot+1);
  panel.hidden=false;paint();
 },
 close(){panel.hidden=true;}
});
backend()?.addEventListener?.("arMixBuiltinCompQueue",reply=>{
 if(!pending||reply?.requestId!==pending.id)return;
 if(reply.status==="REJECTED"){
  pending=null;delayed=null;hint("NATIVE REJECTED · "+(reply.errorCode||"UNKNOWN"));
  requestSnapshot();
 }
});
backend()?.addEventListener?.("arMixBuiltinCompAck",reply=>{
 if(!pending||reply?.requestId!==pending.id)return;
 if(reply.status!=="APPLIED"){
  pending=null;delayed=null;hint("DSP EDIT REJECTED · "+(reply.errorCode||"UNKNOWN"));
  requestSnapshot();return;
 }
 pending.ack=true;requestSnapshot();
});
backend()?.addEventListener?.("arMixBuiltinCompSnapshot",reply=>{
 if(panel.hidden||!native()||reply?.requestId!==readId)return;
 const c=context();
 if(!c||reply.status!=="ACCEPTED"||reply.channelId!==c.channelId||
    reply.instanceId!==c.instanceId||reply.deviceEpoch!==c.deviceEpoch||
    reply.graphRevision!==c.graphRevision||!valid(reply.settings)||
    reply.addedLatencySamples!==0){
  hint("NATIVE COMPRESSOR READBACK STALE");return;
 }
 if(Number.isFinite(reply.gainReductionDb))
  grDb=clamp(reply.gainReductionDb,0,30);
 if(pending){
  if(!pending.ack||reply.appliedRequestId!==pending.id){paint();return;}
  pending=null;
  accepted={...reply.settings};
  if(delayed){draft={...delayed};delayed=null;send();}
  else{draft={...accepted};hint("NATIVE DSP ACCEPTED · 0 SAMPLE ADDED");}
 }else{
  accepted={...reply.settings};
  if(!delayed)draft={...accepted};
  hint("NATIVE DSP ACCEPTED · 0 SAMPLE ADDED");
 }
 paint();
});
backend()?.addEventListener?.("arMixConsoleSnapshot",()=>{
 if(panel.hidden||!native()||!current?.instanceId)return;
 queueMicrotask(()=>{
  if(!context()){pending=null;delayed=null;
   hint("DEVICE OR GRAPH CHANGED · REOPEN FX");}
 });
});
// 6 Hz metering only while visible. Never poll while closed, never touch PCM.
window.setInterval(()=>{
 if(!panel.hidden&&native()&&context())requestSnapshot();
},160);
})();
