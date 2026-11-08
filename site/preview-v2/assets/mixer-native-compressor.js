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
const header=dom("header","mx-comp-head"),
 brand=dom("span","mx-comp-brand","ARMIX  /  COMPRESSOR"),
 channel=dom("span","mx-comp-channel"),
 power=dom("button","mx-comp-power","⏻"),
 close=dom("button","mx-comp-close","×");
power.type=close.type="button";
power.title="Toggle accepted FX insert bypass";
close.setAttribute("aria-label","Close Compressor");
header.append(brand,channel,power,close);

// The reference layout is a single compact mixing surface, not seven
// unrelated horizontal sliders: source controls | display | output controls.
const controls=dom("div","mx-comp-controls");
const left=dom("div","mx-comp-left");
const visual=dom("div","mx-comp-visual");
const right=dom("div","mx-comp-right");
const plot=dom("div","mx-comp-plot");
const plotBrand=dom("div","mx-comp-plot-brand","ARMIX  Compressor");
const svg=document.createElementNS(NS,"svg");
svg.setAttribute("viewBox","0 0 302 218");
svg.setAttribute("preserveAspectRatio","none");
svg.setAttribute("aria-label","Input output compression curve; drag to edit threshold");
svg.setAttribute("role","img");
const meter=dom("div","mx-comp-meter");
const meterName=dom("span","mx-comp-meter-name","GAIN REDUCTION");
const meterRail=dom("div","mx-comp-grrail");
const meterFill=dom("div","mx-comp-grfill");
meterRail.append(meterFill);
const meterValue=dom("span","mx-comp-grvalue","0.0 dB");
meter.append(meterName,meterValue,meterRail);
plot.append(plotBrand,svg,meter);
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
 // Preserve range keyboard/assistive technology controls.
 input.addEventListener("input",()=>changeValue(Number(input.value)));
 input.addEventListener("change",()=>{if(native())send();else saveDemo();});
 // In a compact 32px dial, horizontal drag is too coarse. Vertical
 // scrubbing gives 140px travel, with Shift for 10x finer adjustment.
 let drag=null;
 input.addEventListener("pointerdown",e=>{
  if(e.button!==0)return;
  drag={id:e.pointerId,y:e.clientY,start:draft[key]};
  input.setPointerCapture(e.pointerId);
  e.preventDefault();
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
const safety=dom("div","mx-comp-safety");
const staticFlag=(label,isOn,why)=>{
 const row=dom("span","mx-comp-static-flag");
 row.title=why;row.setAttribute("aria-label",label+": "+(isOn?"On":"Off")+". "+why);
 const led=dom("span","mx-comp-flag-led"+(isOn?" is-on":""));
 row.append(led,dom("span","",label));return row;
};
safety.append(
 staticFlag("Look Ahead",false,"Unavailable: this engine uses zero sample lookahead."),
 staticFlag("Stereo Link",true,"Always on: stereo-linked native detector.")
);
left.append(safety);

const mainRight=dom("div","mx-comp-right-main");
const topRight=dom("div","mx-comp-right-top");
const trim=dom("div","mx-comp-control mx-comp-readonly");
trim.title="Input gain is fixed at 0 dB; this parameter is not supported by the native DSP yet.";
trim.setAttribute("aria-label","Input Gain fixed at 0 dB. Not editable.");
trim.append(dom("span","mx-comp-label","INPUT GAIN"),
 dom("span","mx-comp-dial mx-comp-dial-disabled"),
 dom("output","mx-comp-value","0.0 dB"));
topRight.append(trim,inputs.makeupDb.item,inputs.mix.item);
const dynamics=dom("div","mx-comp-right-dynamics");
const auto=dom("div","mx-comp-auto");
auto.append(
 staticFlag("Auto Gain",false,"Automatic makeup gain is not supported; use Gain."),
 staticFlag("Auto Release",false,"Automatic release is not supported; use Release."),
 staticFlag("Adaptive",false,"Adaptive release is not supported by the native DSP.")
);
dynamics.append(inputs.attackMs.item,inputs.releaseMs.item,auto);
mainRight.append(topRight,dynamics);

const sidechain=dom("section","mx-comp-sidechain");
sidechain.setAttribute("aria-label","Sidechain controls");
const scHead=dom("div","mx-comp-sidechain-head","SIDECHAIN");
const scBody=dom("div","mx-comp-sidechain-body");
const scSource=dom("div","mx-comp-sc-source");
scSource.append(dom("span","mx-comp-sc-small","DETECTOR"),
 dom("span","mx-comp-sc-tag","INTERNAL"));
scSource.title="Internal signal detector is active. External sidechain routing is not implemented.";
const scInactive=(title)=>{
 const item=dom("div","mx-comp-sc-inactive");
 item.title="Unavailable until detector filter support is added to native DSP.";
 item.setAttribute("aria-label",title+" disabled");
 item.append(dom("span","mx-comp-sc-small",title),
  dom("span","mx-comp-sc-ring"),
  dom("span","mx-comp-sc-off","OFF"));
 return item;
};
scBody.append(scSource,scInactive("LOWCUT"),scInactive("HIGHCUT"));
sidechain.append(scHead,scBody);
right.append(mainRight,sidechain);

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
panel.append(header,controls,foot);
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
  c.input.setAttribute("aria-valuetext",format(key,value));
  const fraction=(value-r.min)/(r.max-r.min);
  c.dial.style.setProperty("--dial-sweep",(fraction*270).toFixed(2)+"deg");
  c.dial.style.setProperty("--dial-angle",(-135+fraction*270).toFixed(2)+"deg");
 }
 // Match the dense -60...0 dBFS dual-axis display in the reference.
 // Transfer shape reflects native threshold / ratio / knee / makeup / mix.
 const x=db=>31+(clamp(db,-60,0)+60)/60*248;
 const y=db=>195-(clamp(db,-60,0)+60)/60*154;
 const path=Array.from({length:121},(_,i)=>{
  const v=-60+i*.5;
  return (i?"L":"M")+x(v).toFixed(1)+","+y(compressedOutput(v,draft)).toFixed(1);
 }).join(" ");
 const grid=[];
 for(const d of [-60,-48,-36,-24,-12,0]){
  grid.push('<line x1="'+x(d)+'" y1="41" x2="'+x(d)+'" y2="195" class="mx-comp-grid"/>');
  grid.push('<line x1="31" y1="'+y(d)+'" x2="279" y2="'+y(d)+'" class="mx-comp-grid"/>');
  grid.push('<text x="'+x(d)+'" y="207" class="mx-comp-tick" text-anchor="middle">'+d+'</text>');
  grid.push('<text x="25" y="'+(y(d)+3)+'" class="mx-comp-tick" text-anchor="end">'+d+'</text>');
 }
 svg.innerHTML='<rect width="302" height="218" fill="#08141c"/>'+
  grid.join("")+'<path d="M31 195 L279 41" class="mx-comp-unity"/>'+
  '<path d="'+path+'" class="mx-comp-curve"/>'+
  '<circle cx="'+x(draft.thresholdDb)+'" cy="'+y(compressedOutput(draft.thresholdDb,draft))+
  '" r="5" class="mx-comp-handle"/>'+
  '<text x="35" y="31" class="mx-comp-axis">OUTPUT dBFS</text>'+
  '<text x="278" y="31" class="mx-comp-axis" text-anchor="end">INPUT →</text>';
 const c=context(),lane=c?snapshot()?.graph?.channels?.find(x=>x.id===c.channelId):null;
 const insert=lane?.inserts?.[current.slot];
 // A bypassed graph node (or entire rack) is not rendering PCM; its atomic
 // block meter may retain its last value. Never display stale GR as live.
 const shownReduction=(insert?.bypassed===true||lane?.fxRackBypassed===true)?0:grDb;
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
 const x=(e.clientX-r.left)*302/r.width;
 draft={...draft,thresholdDb:Math.round(clamp(-60+(x-31)*60/248,-60,0)*2)/2};
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
