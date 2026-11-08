// SPDX-License-Identifier: AGPL-3.0-only
// One modeless editor for the approved /preview/ renderer. Browser: DEMO ONLY.
// Native: controls are applied only after same-instance audio ACK + readback.
(() => {
"use strict";
const ID="armix.builtin.arson-harmony.v1";
const root=document.createElement("section");
root.className="mx-harm";root.id="mxNativeHarmony";root.hidden=true;
root.setAttribute("role","dialog");root.setAttribute("aria-label","ArSon Harmony");
root.setAttribute("aria-modal","false");
root.innerHTML=`
  <header class="mx-harm-head">
   <span class="mx-harm-logo">◈</span><span class="mx-harm-brand">ArSon <em>Harmony</em></span>
   <span class="mx-harm-channel" id="mxHChannel"></span>
   <select id="mxHPreset" aria-label="Harmony preset" title="Preset">
    <option value="custom">Custom</option>
    <option value="duo">Natural Duo</option><option value="trio">Soft Trio</option>
    <option value="quartet">Wide Quartet</option><option value="choir">Tight Choir</option>
   </select>
   <button class="mx-harm-power" id="mxHPower" aria-label="FX power" title="Toggle FX bypass">⏻</button>
   <button class="mx-harm-close" id="mxHClose" aria-label="Close Harmony" title="Close"></button>
  </header>
  <div class="mx-harm-source">
    <div class="mx-harm-mode" role="group" aria-label="Harmony key mode">
     <button type="button" data-guide-mode="0" aria-label="Manual Key" aria-pressed="true">Manual</button>
     <button type="button" data-guide-mode="1" aria-label="Music Key Detection" aria-pressed="false">Music</button>
    </div>
    <label class="mx-harm-source-picker" id="mxHSourcePicker" hidden>
     <span class="mx-harm-sr">Music source</span>
     <select id="mxHGuideSource" aria-label="Music source channel">
      <option value="0">Choose track</option>
     </select>
    </label>
    <span class="mx-harm-guide-status" id="mxHGuideStatus" role="status" aria-live="polite">MANUAL</span>
   </div>
  <main class="mx-harm-body">
   <aside class="mx-harm-left">
    <span class="mx-harm-label">VOICES</span>
    <nav class="mx-harm-voices" id="mxHVoices" aria-label="Harmony voice count">
     <button data-voices="2">2</button><button data-voices="3">3</button><button data-voices="4">4</button>
    </nav>
    <label class="mx-harm-correction"><input id="mxHCorrection" type="checkbox"> InTune <span>Pitch correction</span></label>
    <div class="mx-harm-sliders" id="mxHSliders"></div>
   </aside>
   <section class="mx-harm-center">
    <span class="mx-harm-label">KEY</span>
    <div class="mx-harm-wheel" id="mxHWheel">
      <div class="mx-harm-wheel-core"><strong id="mxHKeyReadout">C</strong><small id="mxHNote">KEY</small></div>
    </div>
    <div class="mx-harm-scale">
      <button data-scale="0">Major</button><button data-scale="1">Minor</button>
    </div>
   </section>
  </main>
  <footer class="mx-harm-foot"><span id="mxHStatus">PREVIEW · NO AUDIO</span><span id="mxHLatency"></span></footer>
 `;
// Cached editor nodes and change-only text: no DOM traversal/paint churn on
// native readback ticks while the user sings.
const nodes=new Map();
const el=id=>{
 if(!nodes.has(id))nodes.set(id,root.querySelector("#"+id));
 return nodes.get(id);
};
const showText=(node,value)=>{
 const text=String(value);
 if(node.textContent!==text)node.textContent=text;
};
el("mxHClose").innerHTML='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="18" height="18" fill="currentColor" aria-hidden="true"><path d="M6.4,19L5,17.6L10.6,12L5,6.4L6.4,5L12,10.6L17.6,5L19,6.4L13.4,12L19,17.6L17.6,19L12,13.4L6.4,19Z"/></svg>';
const keys=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
for(let i=0;i<12;++i){
 const b=document.createElement("button");
 b.className="mx-harm-note";b.textContent=keys[i];b.dataset.key=String(i);
 const a=(i/12*2*Math.PI-Math.PI/2),x=50+39*Math.cos(a),y=50+39*Math.sin(a);
 b.style.left=x+"%";b.style.top=y+"%";
 el("mxHWheel").append(b);
}
const sliderDefs=[["color","COLOR",0,100,1],["space","SPACE",0,100,1],
                  ["wet","WET",0,100,1],["gainDb","GAIN",-24,12,0.5]];
for(const [key,label,min,max,step] of sliderDefs){
 const box=document.createElement("label");box.className="mx-harm-slider";
 box.innerHTML=`<span>${label}</span><input data-field="${key}" type="range" min="${min}" max="${max}" step="${step}"><output data-out="${key}"></output>`;
 el("mxHSliders").append(box);
}
const fresh=()=>({key:0,scale:0,voices:3,correction:false,
                   guideMode:0,guideChannelId:"0",
                   color:0.5,space:0.6,wet:0.45,gainDb:0});
const presets={
 duo:{voices:2,color:.45,space:.35,wet:.31,correction:false,gainDb:0},
 trio:{voices:3,color:.50,space:.56,wet:.40,correction:false,gainDb:0},
 quartet:{voices:4,color:.66,space:.95,wet:.52,correction:true,gainDb:-2},
 choir:{voices:4,color:.24,space:.30,wet:.58,correction:true,gainDb:-3}
};
const native=()=>Boolean(window.__JUCE__?.backend?.emitEvent);
const backend=()=>window.__JUCE__?.backend;
const state=()=>window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const demos=new Map();
let current=null,settings=fresh(),pending=null,latest=null,seq=0,readbackRequest="";
let readbackSentAt=0;
let mode="DEMO",readbackLatency=0,observedNote=-1,preset="trio";
let detectedKey=-1,detectedScale=-1,detectedConfidence=0,lastGuideRead=0;
let effectiveKey=-1,effectiveScale=-1,sourceSignature="",availableSources=[];
const noteButtons=[...root.querySelectorAll("[data-key]")];
const voiceButtons=[...root.querySelectorAll("[data-voices]")];
const scaleButtons=[...root.querySelectorAll("[data-scale]")];
const guideButtons=[...root.querySelectorAll("[data-guide-mode]")];
const sliders=sliderDefs.map(([key])=>({
 key,input:root.querySelector('[data-field="'+key+'"]'),
 output:root.querySelector('[data-out="'+key+'"]')
}));
const context=()=>{
 const n=state();
 if(!current||!native()||!n?.admitted)return null;
 const channel=n.graph?.channels?.find(ch=>ch.id===current.channelId);
 const insert=channel?.inserts?.[current.slot];
 if(insert?.id!==current.instanceId||insert?.catalogId!==ID)return null;
 return {channelId:current.channelId,deviceEpoch:n.device.epoch,
         graphRevision:n.graph.revision,slot:current.slot,instanceId:current.instanceId};
};
const hint=t=>showText(el("mxHStatus"),t);
const valid=s=>s&&Number.isInteger(s.key)&&s.key>=0&&s.key<12&&
 Number.isInteger(s.scale)&&s.scale>=0&&s.scale<=1&&
 Number.isInteger(s.voices)&&s.voices>=2&&s.voices<=4&&
 typeof s.correction==="boolean"&&
 ["wet","space","color"].every(k=>Number.isFinite(s[k])&&s[k]>=0&&s[k]<=1)&&
 Number.isFinite(s.gainDb)&&s.gainDb>=-24&&s.gainDb<=12&&
 Number.isInteger(s.guideMode)&&[0,1].includes(s.guideMode)&&
 typeof s.guideChannelId==="string"&&/^(0|[1-9][0-9]{0,18})$/.test(s.guideChannelId)&&
 (s.guideMode===0?s.guideChannelId==="0":s.guideChannelId!=="0");
function refreshGuideSources(){
 const selector=el("mxHGuideSource");
 const accepted=state()?.graph?.channels||[];
 availableSources=accepted.filter(ch=>ch.id!==current?.channelId &&
   typeof ch.id==="string" && (ch.role==="INPUT"||ch.role==="GROUP"));
 const signature=availableSources.map(ch=>ch.id+":"+ch.name).join("|");
 if(signature!==sourceSignature){
   sourceSignature=signature;
   selector.replaceChildren(new Option("Choose track","0"));
   for(const ch of availableSources)
     selector.add(new Option((ch.name||ch.role)+" · "+ch.id,ch.id));
 }
 const wanted=(latest||pending?.settings||settings).guideChannelId;
 const selected=availableSources.some(ch=>ch.id===wanted)?wanted:"0";
 if(selector.value!==selected)selector.value=selected;
}
function paint(){
 const v=latest||pending?.settings||settings;
 const auto=v.guideMode===1;
 // Native LCD never guesses from music confidence or pending UI edits.
 // One atomic native readback is the actual effective DSP key+scale pair.
 const nativeView=native();
 const displayKey=nativeView?effectiveKey:v.key;
 const displayScale=nativeView?effectiveScale:v.scale;
 const reliable=displayKey>=0&&displayKey<12&&displayScale>=0&&displayScale<=1;
 const inGraph=availableSources.some(ch=>ch.id===v.guideChannelId);
 const detected=auto&&detectedKey>=0&&detectedScale>=0;
 for(const b of guideButtons){
   const active=Number(b.dataset.guideMode)===v.guideMode;
   b.classList.toggle("active",active);
   b.setAttribute("aria-pressed",String(active));
 }
 el("mxHSourcePicker").hidden=!auto;
 const source=el("mxHGuideSource");
 const chosen=inGraph?v.guideChannelId:"0";
 if(source.value!==chosen)source.value=chosen;
 for(const b of noteButtons)b.classList.toggle("active",
   reliable&&Number(b.dataset.key)===displayKey);
 for(const b of voiceButtons)b.classList.toggle("active",
   Number(b.dataset.voices)===v.voices);
 for(const b of scaleButtons)b.classList.toggle("active",
   reliable&&Number(b.dataset.scale)===displayScale);
 el("mxHCorrection").checked=v.correction;
 showText(el("mxHKeyReadout"),reliable?keys[displayKey]+(displayScale?"m":""):"—");
 showText(el("mxHNote"),reliable?(displayScale?"MINOR":"MAJOR"):"KEY");
 el("mxHKeyReadout").setAttribute("aria-label",
   reliable?"Active Harmony key "+keys[displayKey]+(displayScale?" minor":" major"):
   "Harmony key awaiting native readback");
 const status=!auto?"MANUAL":!inGraph?"SAFE":detected?"AUTO":"LISTEN";
 showText(el("mxHGuideStatus"),status);
 el("mxHGuideStatus").dataset.state=status.toLowerCase();
 if(el("mxHPreset").value!==preset)el("mxHPreset").value=preset;
 for(const {key,input,output} of sliders){
  const value=key==="gainDb"?v[key]:v[key]*100;
  const setting=String(value);
  if(input.value!==setting)input.value=setting;
  showText(output,key==="gainDb"?v[key].toFixed(1)+"dB":Math.round(value)+"%");
 }
 const n=state(),ch=n?.graph?.channels?.find(x=>x.id===current?.channelId);
 const bypassed=nativeView?ch?.inserts?.[current?.slot]?.bypassed:
       current?.track?.slotBypass?.[current?.slot];
 el("mxHPower").classList.toggle("on",!bypassed);
 el("mxHPower").setAttribute("aria-pressed",String(!bypassed));
 showText(el("mxHLatency"),mode==="NATIVE"?readbackLatency+" samples":"DEMO");
}
// One in-flight readback, a bounded retry on the EXISTING 12 Hz console
// event (no additional timer). A post-ACK verification may replace an older
// music-poll readback so it never gets trapped behind a slow response.
function snapshot(force=false){
 const c=context();if(!c)return;
 const now=Date.now();
 if(!force&&readbackRequest&&now-readbackSentAt<2500)return;
 const id="hsnap-"+now+"-"+(++seq);
 readbackRequest=id;readbackSentAt=now;
 backend().emitEvent("arMixAction",{action:"nativeHarmonySnapshot",requestId:id,...c});
}
function send(next){
 const c=context();
 if(!c){hint("NATIVE INSTANCE CHANGED · REOPEN FX");return;}
 if(pending){latest=next;hint("DSP APPLYING · LATEST EDIT HELD");paint();return;}
 const id="harm-"+String(Date.now()*1000+(seq++%1000));
 pending={id,settings:next,ack:false,context:c,startedAt:Date.now()};
 backend().emitEvent("arMixAction",{action:"nativeHarmonySetSettings",
    requestId:id,...c,settings:next});
 hint("DSP QUEUED · AWAITING AUDIO ACK");paint();
}
function edit(patch){
 const next={...(latest||pending?.settings||settings),...patch};
 if(!valid(next))return;
 preset="custom";
 if(!native()){
   settings=next;
   demos.set(current?.track?.id+":"+current?.slot,{...settings});
   hint("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO");
 }else if(mode==="NATIVE")send(next);
 else hint("NATIVE HARMONY NOT ACCEPTED");
 paint();
}
noteButtons.forEach(b=>b.addEventListener("click",()=>
 edit({key:Number(b.dataset.key),guideMode:0,guideChannelId:"0"})));
voiceButtons.forEach(b=>b.addEventListener("click",()=>edit({voices:Number(b.dataset.voices)})));
scaleButtons.forEach(b=>b.addEventListener("click",()=>
 edit({scale:Number(b.dataset.scale),guideMode:0,guideChannelId:"0"})));
guideButtons.forEach(b=>b.addEventListener("click",()=>{
 const auto=Number(b.dataset.guideMode)===1;
 if(!auto){edit({guideMode:0,guideChannelId:"0"});return;}
 refreshGuideSources();
 const preferred=(latest||pending?.settings||settings).guideChannelId;
 const channel=availableSources.some(ch=>ch.id===preferred)?
     preferred:availableSources[0]?.id;
 if(!channel){hint("ADD MUSIC INPUT TRACK");return;}
 edit({guideMode:1,guideChannelId:channel});
}));
el("mxHGuideSource").addEventListener("change",e=>{
 const source=e.target.value;
 edit({guideMode:source==="0"?0:1,guideChannelId:source});
});
el("mxHCorrection").addEventListener("change",e=>edit({correction:e.target.checked}));
sliders.forEach(({input})=>input.addEventListener("input",()=>{
 const k=input.dataset.field,x=Number(input.value);
 edit({[k]:k==="gainDb"?x:x/100});
}));
el("mxHPreset").addEventListener("change",e=>{
 preset=e.target.value;
 const change=presets[preset];if(change){
  const next={...(latest||pending?.settings||settings),...change};
  if(!native()){settings=next;demos.set(current?.track?.id+":"+current?.slot,{...next});
    hint("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO");
  }else if(mode==="NATIVE")send(next);
 }paint();
});
el("mxHPower").addEventListener("click",()=>{
 if(!current)return;
 if(native())window.__ARMIX_NATIVE_M2__?.consumeAction?.(
    current.track,"slotPower",{dataset:{slot:String(current.slot)}});
 else current.track.slotBypass[current.slot]=!current.track.slotBypass[current.slot];
 paint();
});
el("mxHClose").addEventListener("click",()=>{root.hidden=true;});
let drag=null;
const head=root.querySelector(".mx-harm-head");
head.addEventListener("pointerdown",e=>{
 if(e.button!==0||e.target.closest("button,select"))return;
 const r=root.getBoundingClientRect();
 drag={pointer:e.pointerId,x:e.clientX-r.left,y:e.clientY-r.top};
 head.setPointerCapture(e.pointerId);
});
head.addEventListener("pointermove",e=>{
 if(!drag||drag.pointer!==e.pointerId)return;
 const r=root.getBoundingClientRect();
 root.style.left=Math.max(0,Math.min(innerWidth-r.width,e.clientX-drag.x))+"px";
 root.style.top=Math.max(0,Math.min(innerHeight-r.height,e.clientY-drag.y))+"px";
 root.style.transform="none";
});
head.addEventListener("pointerup",e=>{drag=null;head.releasePointerCapture?.(e.pointerId)});
head.addEventListener("pointercancel",()=>{drag=null});
function mount(){if(!root.isConnected)document.body.append(root)}
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});
else mount();
window.ArSonMixNativeHarmony=Object.freeze({
 id:ID,
 insert(target){return Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target));},
 open(track,slot){
  pending=null;latest=null;seq++;observedNote=-1;readbackLatency=0;
  readbackRequest="";readbackSentAt=0;
  detectedKey=-1;detectedScale=-1;detectedConfidence=0;lastGuideRead=0;
  effectiveKey=-1;effectiveScale=-1;sourceSignature="";availableSources=[];
  const n=state(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const ch=n?.graph?.channels?.find(x=>x.id===id),insert=ch?.inserts?.[slot];
  current={track,slot,channelId:null,instanceId:null};
  if(native()&&n?.admitted&&insert?.catalogId===ID&&insert.id){
    current.channelId=id;current.instanceId=insert.id;mode="NATIVE";
    settings=fresh();hint("NATIVE · READING ACCEPTED DSP");snapshot();
  }else if(!native()){
    mode="DEMO";settings={...(demos.get(track.id+":"+slot)||fresh())};
    hint("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO");
  }else{mode="UNAVAILABLE";settings=fresh();hint("NATIVE HARMONY SLOT NOT ACCEPTED");}
  el("mxHChannel").textContent=track.name+" · FX "+(slot+1);
  root.hidden=false;refreshGuideSources();paint();
 },
 close(){root.hidden=true;}
});
backend()?.addEventListener?.("arMixBuiltinHarmonyQueue",ack=>{
 if(!pending||ack?.requestId!==pending.id)return;
 if(ack.status!=="QUEUED"){pending=null;latest=null;
  hint("NATIVE REJECTED · "+(ack.errorCode||"UNKNOWN"));snapshot();paint();}
});
backend()?.addEventListener?.("arMixBuiltinHarmonyAck",ack=>{
 if(!pending||ack?.requestId!==pending.id)return;
 if(ack.status!=="APPLIED"){pending=null;latest=null;
  hint("DSP NOT APPLIED · "+(ack.errorCode||"UNKNOWN"));snapshot(true);paint();return;}
 pending.ack=true;hint("DSP APPLIED · VERIFYING READBACK");snapshot(true);
});
backend()?.addEventListener?.("arMixBuiltinHarmonySnapshot",snap=>{
 if(!native()||!current||snap?.requestId!==readbackRequest)return;
 const c=context();
 if(!c||snap.status!=="ACCEPTED"||snap.instanceId!==c.instanceId||
    snap.channelId!==c.channelId||snap.deviceEpoch!==c.deviceEpoch||
    snap.graphRevision!==c.graphRevision||!valid(snap.settings)||
    !Number.isInteger(snap.effectiveKey)||snap.effectiveKey<0||snap.effectiveKey>11||
    !Number.isInteger(snap.effectiveScale)||snap.effectiveScale<0||snap.effectiveScale>1) {
   readbackRequest="";
   hint("HARMONY SNAPSHOT INVALID OR STALE");return;
 }
 // Only an audio-applied request ID can commit GUI settings. If the native
 // readback races one control tick behind ACK, retry without overwriting
 // accepted values or retaining the old request token indefinitely.
 readbackRequest="";
 if(pending&&(!pending.ack||snap.appliedRequestId!==pending.id)){
   if(pending.ack)hint("DSP APPLIED · WAITING READBACK");
   return;
 }
 settings={...snap.settings};
 effectiveKey=snap.effectiveKey;effectiveScale=snap.effectiveScale;
 detectedKey=Number.isInteger(snap.guideDetectedKey)?snap.guideDetectedKey:-1;
 detectedScale=Number.isInteger(snap.guideDetectedScale)?snap.guideDetectedScale:-1;
 detectedConfidence=Number.isFinite(snap.guideConfidence)?snap.guideConfidence:0;
 observedNote=Number.isInteger(snap.observedNote)?snap.observedNote:-1;
 readbackLatency=Number.isInteger(snap.latencySamples)?snap.latencySamples:0;
 pending=null;
 if(latest){const next=latest;latest=null;send(next);}
 else hint(settings.guideMode===1?"NATIVE · LIVE MUSIC GUIDE":"NATIVE DSP ACCEPTED · "+c.instanceId);
 refreshGuideSources();paint();
});
backend()?.addEventListener?.("arMixConsoleSnapshot",()=>{
 if(root.hidden||!native()||!current?.instanceId)return;
 queueMicrotask(()=>{
   if(!context()){
     pending=null;latest=null;effectiveKey=-1;effectiveScale=-1;
     readbackRequest="";
     hint("DEVICE / GRAPH CHANGED · REOPEN ACCEPTED FX");paint();return;
   }
   const now=Date.now();
   if(pending){
     // A missing ACK must never block the editor forever. Preserve the
     // last accepted DSP state and request fresh authoritative readback.
     if(!pending.ack&&now-pending.startedAt>5000){
       pending=null;latest=null;
       hint("DSP ACK TIMEOUT · READBACK REQUIRED");snapshot(true);paint();
     }else if(pending.ack&&now-lastGuideRead>750){
       lastGuideRead=now;snapshot();
     }
     return;
   }
   if(settings.guideMode===1&&now-lastGuideRead>750){
     lastGuideRead=now;snapshot();
   }
 });
});
})();