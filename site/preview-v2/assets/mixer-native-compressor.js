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
 readId="",readSentAt=0,seq=1,lastSend=0,windowDrag=null,graphDrag=null,grDb=0;
const panel=dom("section","mx-comp");panel.hidden=true;panel.id="mxNativeComp";
panel.setAttribute("role","dialog");
panel.setAttribute("aria-label","ArSonMix Compressor");
panel.setAttribute("aria-modal","false");
const header=dom("header","mx-comp-head");
const brand=dom("span","mx-comp-brand","Compressor");
const channel=dom("span","mx-comp-channel");
const presetSelect=dom("select","mx-comp-preset-select");
presetSelect.setAttribute("aria-label","Compressor preset");
presetSelect.title="Compressor preset";
for(const [value,label] of [
 ["DEFAULT","Default"],["VOCAL","Vocal"],["SPEECH","Speech"],
 ["GENTLE","Gentle"],["DRUM","Drum"],["CUSTOM","Custom"]]){
 const option=dom("option","",label);
 option.value=value;
 if(value==="CUSTOM")option.disabled=true;
 presetSelect.append(option);
}
const stateDot=dom("span","mx-comp-state-dot");
stateDot.setAttribute("aria-label","DSP state");
const power=dom("button","mx-comp-power","⏻");
const close=dom("button","mx-comp-close","×");
power.type=close.type="button";
power.title="FX STATE UNAVAILABLE";power.dataset.fxState="unknown";
power.disabled=true;power.setAttribute("aria-pressed","false");
power.setAttribute("aria-label","Compressor state unavailable");
close.setAttribute("aria-label","Close Compressor");
power.classList.add("armix-builtin-fx-power");close.classList.add("armix-builtin-fx-close");
header.append(brand,channel,presetSelect,stateDot,power,close);
const controls=dom("div","mx-comp-controls");
const left=dom("div","mx-comp-left");
const visual=dom("div","mx-comp-visual");
const right=dom("div","mx-comp-right");
const plot=dom("div","mx-comp-plot");
const svg=document.createElementNS(NS,"svg");
svg.setAttribute("viewBox","0 0 252 202");
svg.setAttribute("preserveAspectRatio","none");
svg.setAttribute("aria-label","Input/output compression curve, drag to edit threshold");
svg.setAttribute("role","img");
const meter=dom("div","mx-comp-meter");
const meterName=dom("span","mx-comp-meter-name","GR");
const meterRail=dom("div","mx-comp-grrail");
const meterFill=dom("div","mx-comp-grfill");
meterRail.append(meterFill);
const meterValue=dom("span","mx-comp-grvalue","0.0 dB");
meter.append(meterName,meterValue,meterRail);
plot.append(svg,meter);
visual.append(plot);
controls.append(left,visual,right);
const inputs={};
Object.entries(defs).forEach(([key,r])=>{
 const item=dom("label","mx-comp-control");
 item.dataset.key=key;
 const name=dom("span","mx-comp-label",r.label==="MAKEUP"?"GAIN":r.label);
 const output=dom("output","mx-comp-value");
 const dial=dom("span","mx-comp-dial");
 dial.setAttribute("aria-hidden","true");
 const input=dom("input","mx-comp-slider");
 input.type="range";input.min=String(r.min);input.max=String(r.max);
 input.step=String(r.step);input.setAttribute("aria-label",r.label);
 input.title="Drag vertically; Shift for fine; arrows for steps; double-click to reset";
 item.append(name,dial,output,input);
 inputs[key]={item,input,output,dial};
 const changeValue=value=>{
  if(!Number.isFinite(value)||draft[key]===value)return;
  draft={...draft,[key]:value};paint();
  if(!native()){saveDemo();hint("BROWSER PREVIEW · NO AUDIO ENGINE");return;}
  if(Date.now()-lastSend>=130)send();
  else delayed={...draft};
 };
 input.addEventListener("input",()=>changeValue(Number(input.value)));
 input.addEventListener("change",()=>{if(native())send();else saveDemo();});
 let drag=null;
 input.addEventListener("pointerdown",e=>{
  if(e.button!==0)return;
  drag={id:e.pointerId,y:e.clientY,start:draft[key]};
  input.setPointerCapture(e.pointerId);e.preventDefault();
 });
 input.addEventListener("pointermove",e=>{
  if(!drag||drag.id!==e.pointerId)return;
  const delta=(drag.y-e.clientY)*(r.max-r.min)/140*(e.shiftKey ? 0.1 : 1);
  const raw=clamp(drag.start+delta,r.min,r.max);
  const adjusted=Number((r.min+Math.round((raw-r.min)/r.step)*r.step).toFixed(5));
  changeValue(clamp(adjusted,r.min,r.max));e.preventDefault();
 });
 const release=e=>{
  if(!drag||e.pointerId!==drag.id)return;
  drag=null;try{input.releasePointerCapture(e.pointerId);}catch{}
  if(native())send();else saveDemo();
 };
 input.addEventListener("pointerup",release);
 input.addEventListener("pointercancel",release);
 input.addEventListener("dblclick",e=>{
  e.preventDefault();changeValue(defaults()[key]);if(native())send();
 });
});
left.append(inputs.ratio.item,inputs.thresholdDb.item,inputs.kneeDb.item);
right.append(inputs.makeupDb.item,inputs.mix.item,inputs.attackMs.item,inputs.releaseMs.item);
const presetFor=settings=>{
 const equal=target=>Object.keys(defs).every(k=>Math.abs(settings[k]-target[k])<1e-5);
 if(equal(defaults()))return "DEFAULT";
 for(const [name,values] of Object.entries(presets))if(equal(values))return name;
 return "CUSTOM";
};
presetSelect.addEventListener("change",()=>{
 const value=presetSelect.value;
 if(value==="CUSTOM")return;
 draft=value==="DEFAULT"?defaults():{...presets[value]};
 paint();
 if(native())send();else{saveDemo();hint("BROWSER PREVIEW · NO AUDIO ENGINE");}
});
const status=dom("span","mx-comp-status","BROWSER PREVIEW · NO AUDIO");
status.setAttribute("role","status");
status.setAttribute("aria-live","polite");
header.append(status);
panel.append(header,controls);
function mount(){if(!panel.isConnected)document.body.append(panel);}
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
function hint(value){
 status.textContent=value;
 stateDot.title=value;
 stateDot.dataset.state=/REJECTED|STALE|UNAVAILABLE|CHANGED|INVALID|NOT ACCEPTED/.test(value)?
  "error":/APPLYING|READING/.test(value)?"busy":/ACCEPTED/.test(value)?"ok":"preview";
}
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
 presetSelect.value=presetFor(draft);
 for(const [key,c] of Object.entries(inputs)){
  const value=draft[key],r=defs[key];
  c.input.value=String(value);
  c.output.textContent=format(key,value);
  c.input.setAttribute("aria-valuetext",format(key,value));
  const fraction=(value-r.min)/(r.max-r.min);
  c.dial.style.setProperty("--dial-sweep",(fraction*270).toFixed(2)+"deg");
  c.dial.style.setProperty("--dial-angle",(-135+fraction*270).toFixed(2)+"deg");
 }
 // A restrained four-line -60...0 dBFS grid keeps the graph legible at
 // true plug-in size without duplicating large axis titles or labels.
 const x=db=>28+(clamp(db,-60,0)+60)/60*206;
 const y=db=>184-(clamp(db,-60,0)+60)/60*150;
 const path=Array.from({length:121},(_,i)=>{
  const v=-60+i*.5;
  return (i?"L":"M")+x(v).toFixed(1)+","+y(compressedOutput(v,draft)).toFixed(1);
 }).join(" ");
 const grid=[];
 for(const d of [-60,-40,-20,0]){
  grid.push('<line x1="'+x(d)+'" y1="34" x2="'+x(d)+'" y2="184" class="mx-comp-grid"/>');
  grid.push('<line x1="28" y1="'+y(d)+'" x2="234" y2="'+y(d)+'" class="mx-comp-grid"/>');
  grid.push('<text x="'+x(d)+'" y="195" class="mx-comp-tick" text-anchor="middle">'+d+'</text>');
  grid.push('<text x="23" y="'+(y(d)+3)+'" class="mx-comp-tick" text-anchor="end">'+d+'</text>');
 }
 svg.innerHTML='<rect width="252" height="202" fill="#0a1822"/>'+
  grid.join("")+'<path d="M28 184 L234 34" class="mx-comp-unity"/>'+
  '<path d="'+path+'" class="mx-comp-curve"/>'+
  '<circle cx="'+x(draft.thresholdDb)+'" cy="'+y(compressedOutput(draft.thresholdDb,draft))+
  '" r="5" class="mx-comp-handle"/>';
 syncPower();
}
function syncPower(){
 const c=context(),lane=c?snapshot()?.graph?.channels?.find(x=>x.id===c.channelId):null;
 const insert=lane?.inserts?.[current?.slot];
 const fxState=!native()?
  (!current?.track?"unknown":current.track.fxOn===false?"rack-off":
   current.track.slotBypass?.[current.slot]?"off":"on"):
  (!lane||typeof lane.fxRackBypassed!=="boolean"||!insert||
   typeof insert.bypassed!=="boolean"?"unknown":
   lane.fxRackBypassed?"rack-off":insert.bypassed?"off":"on");
 power.dataset.fxState=fxState;
 power.disabled=fxState==="unknown"||fxState==="rack-off";
 power.setAttribute("aria-pressed",String(fxState==="on"));
 power.title=fxState==="on"?"COMPRESSOR ON · click to bypass":
  fxState==="off"?"COMPRESSOR BYPASSED · click to enable":
  fxState==="rack-off"?"FX RACK BYPASSED · enable rack first":
  "FX STATE UNAVAILABLE";
 power.setAttribute("aria-label",power.title);
 const shownReduction=fxState==="on"?grDb:0;
 meterFill.style.height=(100*clamp(shownReduction,0,30)/30)+"%";
 meterValue.textContent=shownReduction.toFixed(1)+" dB";
}
function saveDemo(){if(current)demos.set(current.track.id+":"+current.slot,{...draft});}
function requestSnapshot(force=false){
 const c=context();if(!c)return;
 // Never overwrite an in-flight read ID on every 160ms meter poll.
 // A slow native UI response would otherwise be discarded forever.
 const now=Date.now();
 if(!force&&readId&&now-readSentAt<800)return;
 readId="compsnap-"+now+"-"+(++seq);
 readSentAt=now;
 backend().emitEvent("arMixAction",{action:"nativeCompSnapshot",requestId:readId,...c});
}
function send(){
 if(!native()||!current)return;
 const c=context();if(!c){hint("NATIVE FX INSTANCE UNAVAILABLE");return;}
 if(!valid(draft)){hint("INVALID COMPRESSOR SETTINGS");return;}
 // Click/no-motion, reselecting the same preset or a duplicate change event
 // must not enqueue a no-op DSP transaction on this realtime graph.
 if(!pending&&Object.keys(defs).every(k=>draft[k]===accepted[k]))return;
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
 const x=(e.clientX-r.left)*252/r.width;
 draft={...draft,thresholdDb:Math.round(clamp(-60+(x-28)*60/206,-60,0)*2)/2};
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
 if(e.button!==0||e.target.closest("button,select"))return;
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
  pending=null;delayed=null;grDb=0;readId="";readSentAt=0;
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
  channel.textContent=track.name;
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
 pending.ack=true;requestSnapshot(true);
});
backend()?.addEventListener?.("arMixBuiltinCompSnapshot",reply=>{
 if(panel.hidden||!native()||reply?.requestId!==readId)return;
 // Release the flight slot even when graph identity validation fails.
 readId="";readSentAt=0;
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
  if(!context()){pending=null;delayed=null;readId="";readSentAt=0;
   hint("DEVICE OR GRAPH CHANGED · REOPEN FX");}
  syncPower(); // accepted graph readback, no curve SVG rebuild
 });
});
// 6 Hz metering only while visible. Never poll while closed, never touch PCM.
window.setInterval(()=>{
 if(!panel.hidden&&native()&&context())requestSnapshot();
},160);
})();
