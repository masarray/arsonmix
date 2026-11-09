// SPDX-License-Identifier: AGPL-3.0-only
// One modeless editor for the approved /preview/ renderer. Browser: DEMO ONLY.
// Native: controls are applied only after same-instance audio ACK + readback.
(() => {
"use strict";
const ID="armix.builtin.arson-harmony.v1";
const root=document.createElement("section");
root.className="mx-harm";root.id="mxNativeHarmony";root.hidden=true;
root.setAttribute("role","dialog");root.setAttribute("aria-label","Harmony Effect");
root.setAttribute("aria-modal","false");
// Simplified singer/microphone silhouette derived from the supplied SVG concept.
// Reused as one tiny original inline vector: no image request or heavyweight
// 800px illustration per button. The numeral is the real audible voice count.
const voiceGlyph='<svg class="mx-harm-voice-glyph" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M10.4 3.1a3.2 3.2 0 1 1 0 6.4 3.2 3.2 0 0 1 0-6.4ZM6.7 11.2c2.2-1.3 5.7-1.3 7.8 0 1.9 1.3 3 4.2 3 7.7H3.8c0-3.5 1.1-6.4 2.9-7.7Z" fill="currentColor"/><path d="M16.7 6.7c.5-.8 1.7-.9 2.3-.2l.8.9a1.7 1.7 0 0 1-.3 2.4l-1 .8-1.9-2.2.1-1.7ZM16.5 10.7l2-1.6 1 1.1-1.9 1.6-1.1-1.1Z" fill="currentColor" opacity=".72"/></svg>';
root.innerHTML=`
  <header class="mx-harm-head">
   <span class="mx-harm-logo" aria-hidden="true">◈</span>
   <span class="mx-harm-brand">Harmony <em>Effect</em></span>
   <span class="mx-harm-channel" id="mxHChannel"></span>
   <select id="mxHPreset" aria-label="Harmony preset" title="Preset">
    <option value="custom">Custom</option>
    <option value="duo">Natural Duo</option><option value="trio">Soft Trio</option>
    <option value="quartet">Wide Quartet</option><option value="quintet">Lush Five</option><option value="choir">Tight Choir</option>
   </select>
   <button type="button" class="mx-harm-power" id="mxHPower" aria-label="Harmony state unavailable" aria-pressed="false" data-fx-state="unknown" title="FX STATE UNAVAILABLE" disabled>⏻</button>
   <button type="button" class="mx-harm-close" id="mxHClose" aria-label="Close Harmony" title="Close Harmony"></button>
  </header>
  <main class="mx-harm-body">
   <aside class="mx-harm-left" aria-label="Harmony voice settings">
    <div class="mx-harm-signature"><span class="mx-harm-mark">ARSON</span>
     <strong>HARMONY</strong><small>VOCAL ENGINE</small></div>
    <span class="mx-harm-label">VOICES</span>
    <nav class="mx-harm-voices" id="mxHVoices" aria-label="Harmony voice count">
     <button type="button" data-voices="3" aria-label="3 harmony voices" aria-pressed="true">${voiceGlyph}<span>3</span></button><button type="button" data-voices="4" aria-label="4 harmony voices" aria-pressed="false">${voiceGlyph}<span>4</span></button><button type="button" data-voices="5" aria-label="5 harmony voices" aria-pressed="false">${voiceGlyph}<span>5</span></button>
    </nav>
    <label class="mx-harm-correction"><input id="mxHCorrection" type="checkbox"> PITCH TUNE</label>
   </aside>
   <section class="mx-harm-control-deck" aria-label="Harmony sound controls">
    <div class="mx-harm-sliders" id="mxHSliders"></div>
   </section>
   <section class="mx-harm-center" aria-label="Musical key">
    <div class="mx-harm-wheel" id="mxHWheel">
     <div class="mx-harm-wheel-core"><strong id="mxHKeyReadout">—</strong><small id="mxHNote">KEY</small></div>
    </div>
    <div class="mx-harm-scale" role="group" aria-label="Scale">
      <button type="button" data-scale="0">Major</button><button type="button" data-scale="1">Minor</button>
    </div>
   </section>
  </main>
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
// Close icon is owned by mixer-contextual-buttons.css (shared EQ/Comp).
const keys=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
for(let i=0;i<12;++i){
 const b=document.createElement("button");
 b.className="mx-harm-note";b.textContent=keys[i];b.dataset.key=String(i);
 // Center key labels in the BLUE annulus: 36.5% balances the LCD rim
// and outside border, while retaining >= 10px at the compact viewport.
 const a=(i/12*2*Math.PI-Math.PI/2),x=50+36.5*Math.cos(a),y=50+36.5*Math.sin(a);
 b.style.left=x+"%";b.style.top=y+"%";
 el("mxHWheel").append(b);
}
const sliderDefs=[["color","COLOR",0,100,1],["space","SPACE",0,100,1],
                  ["wet","WET",0,100,1],["gainDb","GAIN",-24,12,0.5]];
for(const [key,label,min,max,step] of sliderDefs){
 const box=document.createElement("label");box.className="mx-harm-slider";
 box.innerHTML=`<span class="mx-harm-dial-face" aria-hidden="true"><i class="mx-harm-dial-pointer"></i></span><span class="mx-harm-dial-label">${label}</span><output data-out="${key}"></output><input data-field="${key}" type="range" min="${min}" max="${max}" step="${step}" aria-label="${label}" title="Drag horizontally or use arrow keys">`;
 el("mxHSliders").append(box);
}
const fresh=()=>({key:0,scale:0,voices:3,correction:false,
                   guideMode:0,guideChannelId:"0",
                   color:0.5,space:0.6,wet:0.45,gainDb:0});
const presets={
 duo:{voices:2,color:.45,space:.35,wet:.31,correction:false,gainDb:0},
 trio:{voices:3,color:.50,space:.56,wet:.40,correction:false,gainDb:0},
 quartet:{voices:4,color:.66,space:.95,wet:.52,correction:true,gainDb:-2},
 quintet:{voices:5,color:.61,space:.88,wet:.45,correction:true,gainDb:-3},
 choir:{voices:4,color:.24,space:.30,wet:.58,correction:true,gainDb:-3}
};
const native=()=>Boolean(window.__JUCE__?.backend?.emitEvent);
const backend=()=>window.__JUCE__?.backend;
const state=()=>window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const demos=new Map();
let current=null,settings=fresh(),pending=null,latest=null,seq=0,readbackRequest="";
let readbackSentAt=0,verified=false,heldPatch=null;
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
// The native accepted graph is the only authority, including after reopen.
// Track exact insert identity through a slot move; allow a successor at the
// former slot only on the SAME stable ChannelId. Never retarget another strip.
const acceptedLocation=()=>{
 const n=state();
 if(!current||!native()||!n?.admitted)return null;
 const id=current.channelId||window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const channel=n.graph?.channels?.find(ch=>ch.id===id),inserts=channel?.inserts;
 if(!Array.isArray(inserts))return null;
 let slot=current.instanceId?
   inserts.findIndex(x=>x?.id===current.instanceId&&x.catalogId===ID):-1;
 if(slot<0&&inserts[current.slot]?.catalogId===ID&&inserts[current.slot]?.id)
   slot=current.slot;
 if(slot<0)return null;
 const insert=inserts[slot];
 if(!insert?.id||insert.catalogId!==ID)return null;
 return {channelId:id,deviceEpoch:n.device.epoch,
         graphRevision:n.graph.revision,slot,instanceId:insert.id};
};
const context=()=>{
 const c=acceptedLocation();
 if(!c||!current||c.channelId!==current.channelId||
    c.instanceId!==current.instanceId||c.slot!==current.slot||
    c.deviceEpoch!==current.boundEpoch||
    c.graphRevision!==current.boundRevision)return null;
 return c;
};
// Carry only actually edited fields; never push stale full plugin presets
// into a freshly created instance after a device/graph transition.
function holdUnfinishedEdit(){
 const desired=latest||pending?.settings;
 if(!desired)return;
 const diff={};
 for(const key of Object.keys(settings))
   if(desired[key]!==settings[key])diff[key]=desired[key];
 heldPatch={...diff,...(heldPatch||{})};
}
function reconcileBinding(){
 if(!native()||!current)return false;
 const c=acceptedLocation();
 if(!c){
   if(verified||pending||readbackRequest){
     holdUnfinishedEdit();verified=false;pending=null;latest=null;
     readbackRequest="";effectiveKey=-1;effectiveScale=-1;
     hint("HARMONY RECONNECTING · WAITING FOR ACCEPTED FX");paint();
   }
   return false;
 }
 const changed=current.channelId!==c.channelId||
   current.instanceId!==c.instanceId||current.slot!==c.slot||
   current.boundEpoch!==c.deviceEpoch||
   current.boundRevision!==c.graphRevision;
 if(changed){
   holdUnfinishedEdit();
   current.channelId=c.channelId;current.instanceId=c.instanceId;
   current.slot=c.slot;current.boundEpoch=c.deviceEpoch;
   current.boundRevision=c.graphRevision;
   pending=null;latest=null;verified=false;
   readbackRequest="";readbackSentAt=0;
   effectiveKey=-1;effectiveScale=-1;
   detectedKey=-1;detectedScale=-1;detectedConfidence=0;
   showText(el("mxHChannel"),current.track.name+" · FX "+(c.slot+1));
   hint("HARMONY CONNECTED · READING DSP");
   refreshGuideSources();paint();snapshot(true);
 }else if(!verified&&(!readbackRequest||Date.now()-readbackSentAt>2500)){
   snapshot(true);
 }
 return true;
}
const hint=t=>showText(el("mxHStatus"),t);
const valid=s=>s&&Number.isInteger(s.key)&&s.key>=0&&s.key<12&&
 Number.isInteger(s.scale)&&s.scale>=0&&s.scale<=1&&
 Number.isInteger(s.voices)&&s.voices>=2&&s.voices<=5&&
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
// Scalar FX bypass changes arrive with the native console snapshot, even
// in Manual Key mode. Only update the header button if its accepted state
// changes; never synthesize optimistic ON/OFF while an ACK is pending.
function syncPower(){
 const n=state(),ch=n?.graph?.channels?.find(x=>x.id===current?.channelId);
 const insert=ch?.inserts?.[current?.slot];
 const fxState=!native()?
   (current?.track?.slotBypass?.[current?.slot]?"off":"on"):
   (!ch||typeof ch.fxRackBypassed!=="boolean"||!insert||
    typeof insert.bypassed!=="boolean"?"unknown":
    ch.fxRackBypassed?"rack-off":insert.bypassed?"off":"on");
 const power=el("mxHPower");
 if(power.dataset.fxState===fxState)return;
 power.dataset.fxState=fxState;
 power.disabled=fxState==="unknown"||fxState==="rack-off";
 power.setAttribute("aria-pressed",String(fxState==="on"));
 power.title=fxState==="on"?"HARMONY ON · click to bypass":
  fxState==="off"?"HARMONY BYPASSED · click to enable":
  fxState==="rack-off"?"FX RACK BYPASSED · enable rack first":
  "HARMONY STATE UNAVAILABLE";
 power.setAttribute("aria-label",power.title);
}
function paint(){
 const v=latest||pending?.settings||
   (heldPatch?{...settings,...heldPatch}:settings);
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
 for(const b of voiceButtons){
   const active=Number(b.dataset.voices)===v.voices;
   b.classList.toggle("active",active);
   b.setAttribute("aria-pressed",String(active));
 }
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
  const ratio=Math.max(0,Math.min(1,(value-Number(input.min))/(Number(input.max)-Number(input.min))));
  // One numeric source drives the pointer AND the 270-degree illuminated
  // groove. Only touch CSS variables when the value changes: no extra
  // animation frame, renderer loop or audio work during native snapshots.
  const surface=input.parentElement;
  const turn=(-135+ratio*270)+"deg",sweep=(ratio*270)+"deg";
  if(surface.style.getPropertyValue("--dial-turn")!==turn)
    surface.style.setProperty("--dial-turn",turn);
  if(surface.style.getPropertyValue("--dial-sweep")!==sweep)
    surface.style.setProperty("--dial-sweep",sweep);
  showText(output,key==="gainDb"?v[key].toFixed(1)+" dB":Math.round(value)+"%");
 }
 syncPower();
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
 if(!c||!verified){hint("HARMONY RECONNECTING · EDIT HELD");return;}
 if(pending){latest=next;hint("DSP APPLYING · LATEST EDIT HELD");paint();return;}
 const id="harm-"+String(Date.now()*1000+(seq++%1000));
 pending={id,settings:next,ack:false,context:c,startedAt:Date.now()};
 backend().emitEvent("arMixAction",{action:"nativeHarmonySetSettings",
    requestId:id,...c,settings:next});
 hint("DSP QUEUED · AWAITING AUDIO ACK");paint();
}
function edit(patch){
 const next={...(latest||pending?.settings||
   (heldPatch?{...settings,...heldPatch}:settings)),...patch};
 if(!valid(next))return;
 preset="custom";
 if(!native()){
   settings=next;
   demos.set(current?.track?.id+":"+current?.slot,{...settings});
   hint("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO");
 }else if(mode==="NATIVE"){
   if(!reconcileBinding()||!verified){
     heldPatch={...(heldPatch||{}),...patch};
     hint("HARMONY RECONNECTING · EDIT SAVED");
   }else send(next);
 }else hint("NATIVE HARMONY NOT ACCEPTED");
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
  }else if(mode==="NATIVE"){
    if(!reconcileBinding()||!verified){
      heldPatch={...(heldPatch||{}),...change};
      hint("HARMONY RECONNECTING · PRESET SAVED");
    }else send(next);
  }
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
  pending=null;latest=null;heldPatch=null;verified=false;
  seq++;observedNote=-1;readbackLatency=0;
  readbackRequest="";readbackSentAt=0;
  detectedKey=-1;detectedScale=-1;detectedConfidence=0;lastGuideRead=0;
  effectiveKey=-1;effectiveScale=-1;sourceSignature="";availableSources=[];
  const n=state(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const ch=n?.graph?.channels?.find(x=>x.id===id),insert=ch?.inserts?.[slot];
  current={track,slot,channelId:id,instanceId:null,
    boundEpoch:null,boundRevision:null};
  showText(el("mxHChannel"),track.name+" · FX "+(slot+1));
  if(native()){
    mode="NATIVE";settings=fresh();
    hint("HARMONY · LOCATING ACCEPTED DSP");
    if(n?.admitted&&insert?.catalogId===ID&&insert.id)
      current.instanceId=insert.id;
    reconcileBinding();
  }else{
    mode="DEMO";settings={...(demos.get(track.id+":"+slot)||fresh())};
    hint("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO");
  }
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
 settings={...snap.settings};verified=true;
 effectiveKey=snap.effectiveKey;effectiveScale=snap.effectiveScale;
 detectedKey=Number.isInteger(snap.guideDetectedKey)?snap.guideDetectedKey:-1;
 detectedScale=Number.isInteger(snap.guideDetectedScale)?snap.guideDetectedScale:-1;
 detectedConfidence=Number.isFinite(snap.guideConfidence)?snap.guideConfidence:0;
 observedNote=Number.isInteger(snap.observedNote)?snap.observedNote:-1;
 readbackLatency=Number.isInteger(snap.latencySamples)?snap.latencySamples:0;
 pending=null;
 refreshGuideSources();
 if(latest){const next=latest;latest=null;send(next);}
 else if(heldPatch){
   const patch=heldPatch;heldPatch=null;
   const next={...settings,...patch};
   if(next.guideMode===1&&
      !availableSources.some(ch=>ch.id===next.guideChannelId)){
     next.guideMode=0;next.guideChannelId="0";
   }
   if(valid(next))send(next);
   else hint("HARMONY RESTORED · INVALID EDIT DISCARDED");
 }else hint(settings.guideMode===1?"NATIVE · LIVE MUSIC GUIDE":"NATIVE DSP ACCEPTED · "+c.instanceId);
 paint();
});
backend()?.addEventListener?.("arMixConsoleSnapshot",()=>{
 if(root.hidden||!native()||!current)return;
 queueMicrotask(()=>{
   if(!reconcileBinding()){
     hint("HARMONY RECONNECTING · WAITING FOR ACCEPTED FX");
     return;
   }
   if(!verified)return;
   syncPower();
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