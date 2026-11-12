/* 8D-SonKu. Presentation only: native JUCE remains the ONLY PCM authority.
   No WebAudio, source simulation, optimistic DSP success or second /preview/. */
(() => {
"use strict";
const ID="armix.builtin.8d-sonku.v1";
const defaults=()=>({startAzimuthDegrees:0,revolutionSeconds:12,
 stereoSpreadDegrees:60,spatialMix:1,outputTrim:.75,moving:true,clockwise:true});
const valid=s=>s && Number.isFinite(s.startAzimuthDegrees) &&
 Math.abs(s.startAzimuthDegrees)<=360 &&
 Number.isFinite(s.revolutionSeconds)&&s.revolutionSeconds>=.5&&s.revolutionSeconds<=120 &&
 Number.isFinite(s.stereoSpreadDegrees)&&s.stereoSpreadDegrees>=0&&s.stereoSpreadDegrees<=180 &&
 Number.isFinite(s.spatialMix)&&s.spatialMix>=0&&s.spatialMix<=1 &&
 Number.isFinite(s.outputTrim)&&s.outputTrim>=0&&s.outputTrim<=1 &&
 typeof s.moving==="boolean"&&typeof s.clockwise==="boolean";
const backend=()=>window.__JUCE__?.backend;
const native=()=>typeof backend()?.emitEvent==="function";
const snapshot=()=>window.__ARMIX_NATIVE_M2__?.fxEditorState?.()||
 window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const angle=(v)=>((v%360)+360)%360;
const root=document.createElement("section");
root.id="mxNativeSonKu";root.className="mx-sonku";root.hidden=true;
root.setAttribute("role","dialog");root.setAttribute("aria-modal","false");
root.setAttribute("aria-label","8D-SonKu binaural spatial audio");
root.innerHTML=[
 '<header class="sk-head"><span class="sk-brand">8D<span>·</span>SonKu</span>',
 '<span class="sk-headnote">BINAURAL SPATIAL MOTION</span>',
 '<span id="skChannel" class="sk-channel"></span>',
 '<button type="button" id="skClose" class="sk-close armix-floating-close" aria-label="Close 8D-SonKu">×</button></header>',
 '<div class="sk-content"><div class="sk-stage">',
 '<svg id="skMap" viewBox="0 0 400 330" role="group" aria-label="Top-down head and two draggable binaural emitters">',
 '<defs><radialGradient id="skBg"><stop offset="0" stop-color="#1c3345"/><stop offset="1" stop-color="#101b28"/></radialGradient></defs>',
 '<rect width="400" height="330" rx="12" fill="url(#skBg)"/>',
 '<path d="M200 12V316M12 165H388" stroke="#3f5667" stroke-dasharray="3 7" opacity=".65"/>',
 '<circle cx="200" cy="165" r="120" stroke="#75c2ce" stroke-width="1.4" stroke-dasharray="5 5" fill="none"/>',
 '<circle cx="200" cy="165" r="66" stroke="#344f61" fill="none"/>',
 '<text x="200" y="22" text-anchor="middle">FRONT</text>',
 '<text x="200" y="323" text-anchor="middle">REAR</text>',
 '<text x="16" y="160">LEFT</text><text x="353" y="160">RIGHT</text>',
 '<path d="M180 112 Q200 95 220 112" fill="none" stroke="#b1cbd8" stroke-width="7" stroke-linecap="round"/>',
 '<ellipse cx="200" cy="165" rx="30" ry="39" fill="#435c70" stroke="#8eacb9" stroke-width="1.5"/>',
 '<rect x="167" y="150" width="11" height="30" rx="5" fill="#8da8b7"/>',
 '<rect x="222" y="150" width="11" height="30" rx="5" fill="#8da8b7"/>',
 '<path d="M200 141 L195 161 L205 161 Z" fill="#92adbd"/>',
 '<g class="sk-src" data-index="0" tabindex="0" role="slider" aria-label="Left virtual source position" aria-valuemin="-180" aria-valuemax="180">',
 '<circle class="sk-source-bg" cx="200" cy="45" r="16"/><circle class="sk-source-core" cx="200" cy="45" r="5"/>',
 '<text class="sk-source-label" x="200" y="20" text-anchor="middle">L</text></g>',
 '<g class="sk-src sk-right" data-index="1" tabindex="0" role="slider" aria-label="Right virtual source position" aria-valuemin="-180" aria-valuemax="180">',
 '<circle class="sk-source-bg" cx="200" cy="45" r="16"/><circle class="sk-source-core" cx="200" cy="45" r="5"/>',
 '<text class="sk-source-label" x="200" y="20" text-anchor="middle">R</text></g>',
 '</svg><div class="sk-stage-caption">HEADPHONES · HRIR 0° ELEVATION · 44.1 / 48 kHz</div>',
 '</div><aside class="sk-controls"><div class="sk-control-title">MOTION</div>',
 '<div class="sk-buttons"><button type="button" id="skMotion">ORBIT</button><button type="button" id="skDirection">CLOCKWISE</button></div>',
 '<label>REVOLUTION <output id="skSpeedValue">12.0 s</output><input id="skSpeed" type="range" min="0.5" max="30" step="0.1" value="12"></label>',
 '<label>STEREO SPREAD <output id="skSpreadValue">60°</output><input id="skSpread" type="range" min="0" max="180" step="1" value="60"></label>',
 '<label>SPATIAL MIX <output id="skMixValue">100%</output><input id="skMix" type="range" min="0" max="100" step="1" value="100"></label>',
 '<label>OUTPUT TRIM <output id="skTrimValue">75%</output><input id="skTrim" type="range" min="0" max="100" step="1" value="75"></label>',
 '<div class="sk-guide">Drag L or R around the head to set start angle. Static mode tests front and rear localization.</div>',
 '</aside></div>',
 '<footer class="sk-status"><span id="skMode">VISUAL PREVIEW</span><span id="skHint" role="status">No browser audio</span></footer>'
].join("");
const $=id=>root.querySelector("#"+id);
const mount=()=>{if(!root.isConnected)document.body.append(root)};
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
let current=null,accepted=defaults(),draft=defaults(),pending=null,latest=null,
 readId="",seq=1,clock=null,observed=0,observedAt=0,drag=null;
const context=()=>{
 if(!current||!native())return null;
 const s=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const ch=s?.graph?.channels?.find(x=>x.id===id);
 const fx=ch?.inserts?.[current.slot];
 if(!(s?.admitted||s?.logical)||!id||fx?.catalogId!==ID||!fx?.id ||
     (current.instanceId&&current.instanceId!==fx.id))return null;
 return {channelId:id,slot:current.slot,instanceId:fx.id,
  deviceEpoch:s.logical?"0":s.device.epoch,
  ...(s.logical?{workspaceToken:s.workspaceToken}:{}),graphRevision:s.graph.revision};
};
function render(){
 const s=draft;
 $("skMotion").textContent=s.moving?"ORBIT":"STATIC";
 $("skMotion").setAttribute("aria-pressed",String(s.moving));
 $("skDirection").textContent=s.clockwise?"CLOCKWISE":"COUNTER-CW";
 $("skDirection").setAttribute("aria-pressed",String(s.clockwise));
 const vals=[["skSpeed",s.revolutionSeconds,"skSpeedValue",s.revolutionSeconds.toFixed(1)+" s"],
  ["skSpread",s.stereoSpreadDegrees,"skSpreadValue",Math.round(s.stereoSpreadDegrees)+"°"],
  ["skMix",s.spatialMix*100,"skMixValue",Math.round(s.spatialMix*100)+"%"],
  ["skTrim",s.outputTrim*100,"skTrimValue",Math.round(s.outputTrim*100)+"%"]];
 for(const [input,value,out,label] of vals){$(input).value=String(value);$(out).textContent=label;}
 const liveMotion=s.moving&&current&&!pending?observed:
    s.startAzimuthDegrees;
 const phase=native()&&s.moving&&current&&!pending&&observedAt?
    liveMotion+(performance.now()-observedAt)*.001*
     (s.clockwise?1:-1)*360/s.revolutionSeconds:liveMotion;
 for(let source=0;source<2;++source){
  const deg=angle(phase+(source?1:-1)*s.stereoSpreadDegrees/2);
  const rad=deg*Math.PI/180;
  const x=200+120*Math.sin(rad),y=165-120*Math.cos(rad);
  const g=root.querySelector('.sk-src[data-index="'+source+'"]');
  const bg=g.querySelector(".sk-source-bg"),dot=g.querySelector(".sk-source-core"),label=g.querySelector(".sk-source-label");
  for(const c of [bg,dot]){c.setAttribute("cx",x.toFixed(2));c.setAttribute("cy",y.toFixed(2));}
  label.setAttribute("x",x.toFixed(2));label.setAttribute("y",(y-22).toFixed(2));
  g.setAttribute("aria-valuenow",String(Math.round(deg>180?deg-360:deg)));
 }
 $("skMode").textContent=native()?(context()?"NATIVE HRTF":"NATIVE UNAVAILABLE"):"VISUAL ONLY · NO AUDIO";
}
function hint(s){$("skHint").textContent=s;}
function requestSnapshot(){
 const c=context();if(!c)return;
 readId="sk-read-"+String(Date.now())+"-"+(++seq);
 backend().emitEvent("arMixAction",{action:"nativeSonKuSnapshot",requestId:readId,...c});
}
function send(next){
 if(!valid(next))return false;
 draft={...next};render();
 if(!native()){accepted={...next};hint("BROWSER VISUAL ONLY · NO DSP");return true;}
 if(pending){latest={...next};hint("DSP BUSY · FINAL CHANGE HELD");return true;}
 const c=context();if(!c){hint("NATIVE INSERT NOT ACCEPTED");return false;}
 const id="sonku-"+String(Date.now()*1000+(++seq%1000));
 pending={requestId:id,settings:{...next},context:c,ack:false};
 backend().emitEvent("arMixAction",{action:"nativeSonKuSet",requestId:id,...c,settings:next});
 hint("DSP QUEUED · AWAITING AUDIO ACK");return true;
}
function flushLatest(){
 if(latest){const next=latest;latest=null;send(next);}
}
function finishFailure(reason){
 pending=null;latest=null;draft={...accepted};hint(reason);render();requestSnapshot();
}
backend()?.addEventListener?.("arMixBuiltinSonKuQueue",r=>{
 if(!pending||r?.requestId!==pending.requestId)return;
 if(r.status==="REJECTED")finishFailure("DSP REJECTED · "+(r.errorCode||"STALE"));
});
backend()?.addEventListener?.("arMixBuiltinSonKuAck",r=>{
 if(!pending||r?.requestId!==pending.requestId)return;
 if(r.status!=="APPLIED")return finishFailure("DSP NOT APPLIED · "+(r.errorCode||"STALE"));
 pending.ack=true;hint("DSP APPLIED · VERIFYING READBACK");requestSnapshot();
});
backend()?.addEventListener?.("arMixBuiltinSonKuSnapshot",r=>{
 if(r?.requestId!==readId||!current)return;
 const c=context();if(!c||r.status!=="ACCEPTED"||
   r.channelId!==c.channelId||r.instanceId!==c.instanceId)return;
 if(!valid(r.settings))return;
 if(pending) {
  if(!pending.ack || r.appliedRequestId!==pending.requestId)return;
  accepted={...r.settings};pending=null;hint("DSP APPLIED · ACCEPTED");flushLatest();
 }else if(!drag){
  accepted={...r.settings};draft={...r.settings};
 }
 if(Number.isFinite(r.observedAzimuthDegrees)){
  observed=r.observedAzimuthDegrees;observedAt=performance.now();
 }
 render();
});
$("skClose").addEventListener("click",()=>{root.hidden=true;current=null;
 pending=null;latest=null;if(clock!==null){clearInterval(clock);clock=null;}});
$("skMotion").addEventListener("click",()=>send({...draft,moving:!draft.moving}));
$("skDirection").addEventListener("click",()=>send({...draft,clockwise:!draft.clockwise}));
for(const [id,key,convert] of [
 ["skSpeed","revolutionSeconds",v=>v],
 ["skSpread","stereoSpreadDegrees",v=>v],
 ["skMix","spatialMix",v=>v/100],
 ["skTrim","outputTrim",v=>v/100]]) {
 const input=$(id);
 input.addEventListener("input",()=>{const v=Number(input.value);
  if(!Number.isFinite(v))return;draft={...draft,[key]:convert(v)};render();});
 input.addEventListener("change",()=>send({...draft,[key]:convert(Number(input.value))}));
}
const map=$("skMap");
function toAzimuth(e) {
 const rect=map.getBoundingClientRect();
 const x=(e.clientX-rect.left)*400/rect.width-200;
 const y=165-(e.clientY-rect.top)*330/rect.height;
 return Math.atan2(x,y)*180/Math.PI;
}
map.addEventListener("pointerdown",e=>{
 const g=e.target.closest(".sk-src");if(!g)return;
 drag={source:Number(g.dataset.index)};
 map.setPointerCapture?.(e.pointerId);e.preventDefault();
});
map.addEventListener("pointermove",e=>{
 if(!drag)return;
 const value=toAzimuth(e)-(drag.source?1:-1)*draft.stereoSpreadDegrees/2;
 draft={...draft,startAzimuthDegrees:Math.max(-360,Math.min(360,value)),
  moving:false};render();
});
map.addEventListener("pointerup",e=>{
 if(!drag)return;drag=null;map.releasePointerCapture?.(e.pointerId);
 send({...draft});
});
map.addEventListener("pointercancel",()=>{drag=null;draft={...accepted};render();});
map.addEventListener("keydown",e=>{
 const g=e.target.closest(".sk-src");if(!g)return;
 if(!["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"].includes(e.key))return;
 e.preventDefault();
 const step=(e.key==="ArrowLeft"||e.key==="ArrowDown"?-5:5);
 send({...draft,startAzimuthDegrees:Math.max(-360,Math.min(360,draft.startAzimuthDegrees+step)),moving:false});
});
const api={
 id:ID,
 insert(target){return Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target));},
 open(track,slot){
  pending=null;latest=null;accepted=defaults();draft=defaults();
  current={track,slot,instanceId:null};
  const s=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const fx=s?.graph?.channels?.find(x=>x.id===id)?.inserts?.[slot];
  if(native()&&(s?.admitted||s?.logical)&&fx?.catalogId===ID)current.instanceId=fx.id;
  $("skChannel").textContent=String(track.name)+" · FX "+(slot+1);
  root.hidden=false;
  hint(native()?"READING ACCEPTED DSP":"BROWSER VISUAL ONLY · NO AUDIO");
  if(native())requestSnapshot();
  if(clock!==null)clearInterval(clock);
  clock=setInterval(()=>{if(root.hidden)return;
    if(native()&&!pending&&context())requestSnapshot();
    render();},250);
  render();
 },
 close(){$("skClose").click();}
};
window.ArSonMixNativeSonKu=Object.freeze(api);
})();
