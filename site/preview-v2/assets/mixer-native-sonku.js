/* 8D-SonKu. Presentation only: native JUCE remains the ONLY PCM authority.
   No WebAudio, source simulation, optimistic DSP success or second /preview/. */
(() => {
"use strict";
const ID="armix.builtin.8d-sonku.v1";
const defaults=()=>({startAzimuthDegrees:0,revolutionSeconds:12,
 stereoSpreadDegrees:60,spatialMix:1,outputTrim:.75,moving:true,clockwise:true,
 roomAmount:0,ambienceAmount:0});
const valid=s=>s && Number.isFinite(s.startAzimuthDegrees) &&
 Math.abs(s.startAzimuthDegrees)<=360 &&
 Number.isFinite(s.revolutionSeconds)&&s.revolutionSeconds>=.5&&s.revolutionSeconds<=120 &&
 Number.isFinite(s.stereoSpreadDegrees)&&s.stereoSpreadDegrees>=0&&s.stereoSpreadDegrees<=180 &&
 Number.isFinite(s.spatialMix)&&s.spatialMix>=0&&s.spatialMix<=1 &&
 Number.isFinite(s.outputTrim)&&s.outputTrim>=0&&s.outputTrim<=1 &&
 Number.isFinite(s.roomAmount)&&s.roomAmount>=0&&s.roomAmount<=1 &&
 Number.isFinite(s.ambienceAmount)&&s.ambienceAmount>=0&&s.ambienceAmount<=1 &&
 typeof s.moving==="boolean"&&typeof s.clockwise==="boolean";
const backend=()=>window.__JUCE__?.backend;
const native=()=>typeof backend()?.emitEvent==="function";
const snapshot=()=>window.__ARMIX_NATIVE_M2__?.fxEditorState?.()||
 window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const angle=(v)=>((v%360)+360)%360;
// Listener faces the BOTTOM of this top-down stage.
// Acoustic coordinates stay fixed: FRONT=0°, RIGHT=+90°, REAR=180°,
// LEFT=-90°. When facing down, anatomical RIGHT appears screen-left.
// Perspective projection is presentation-only. Never remap the HRTF azimuth:
// 0° FRONT (screen bottom), +90° listener RIGHT (screen left).
// All emitters lie on one physically plausible horizontal ellipse;
 // its rear half is occluded by the head and near half overlays the bust.
const STAGE=Object.freeze({x:200,y:174,rx:148,ry:83});
const stagePoint=(azimuthDegrees)=>{
 const rad=angle(azimuthDegrees)*Math.PI/180;
 return {x:STAGE.x-STAGE.rx*Math.sin(rad),
         y:STAGE.y+STAGE.ry*Math.cos(rad)};
};
const stageAzimuth=(screenX,screenY)=>
 Math.atan2((STAGE.x-screenX)/STAGE.rx,
            (screenY-STAGE.y)/STAGE.ry)*180/Math.PI;
// At SPREAD=0 the two actual HRIR directions coincide. Separate the
// drawings (not their acoustic coordinates) tangentially, with tiny tethers
// identifying the shared acoustic point. L and R remain independently draggable.
const displayPair=(azimuths)=>{
 const anchors=azimuths.map(stagePoint);
 const distance=Math.hypot(anchors[0].x-anchors[1].x,anchors[0].y-anchors[1].y);
 const mid=(azimuths[0]+azimuths[1])*0.5*Math.PI/180;
 const tangent={x:STAGE.rx*Math.cos(mid),y:STAGE.ry*Math.sin(mid)};
 const n=Math.hypot(tangent.x,tangent.y)||1;
 const offset=Math.max(0,(46-distance)*0.5);
 return anchors.map((anchor,i)=>({
  anchor,visible:{x:anchor.x+(i===0?1:-1)*offset*tangent.x/n,
                  y:anchor.y+(i===0?1:-1)*offset*tangent.y/n}
 }));
};
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
 '<defs><radialGradient id="skBg" cx="50%" cy="48%" r="73%"><stop offset="0" stop-color="#263b49"/><stop offset=".65" stop-color="#152533"/><stop offset="1" stop-color="#0a1622"/></radialGradient>',
 '<radialGradient id="skFloor" cx="50%" cy="55%" r="65%"><stop offset="0" stop-color="#7fdae7" stop-opacity=".14"/><stop offset="1" stop-color="#46758b" stop-opacity="0"/></radialGradient>',
 '<linearGradient id="skRail" x1="0" x2="0" y1="0" y2="1"><stop stop-color="#355969"/><stop offset="1" stop-color="#8bdbe6"/></linearGradient></defs>',
 '<rect width="400" height="330" rx="10" fill="url(#skBg)"/>',
 '<ellipse cx="200" cy="174" rx="148" ry="83" fill="url(#skFloor)"/>',
 '<path class="sk-axis" d="M200 79V270 M45 174H355" fill="none" stroke="#597d89" stroke-opacity=".21" stroke-width=".8" stroke-dasharray="2 9"/>',
 // Far orbit belongs BEHIND the listener; true perspective elliptical rail.
 '<path id="skRearArc" class="sk-orbit-rear" d="M52 174 A148 83 0 0 1 348 174" fill="none" stroke="#679aaa" stroke-opacity=".47" stroke-width="1.4" stroke-dasharray="4 7"/>',
 '<path class="sk-secondary-rail" d="M64 174 A136 70 0 0 1 336 174" fill="none" stroke="#538096" stroke-opacity=".19" stroke-width="1"/>',
 '<text class="sk-direction" x="200" y="55" text-anchor="middle">REAR</text>',
 '<text class="sk-direction sk-front" x="200" y="317" text-anchor="middle">FRONT</text>',
 '<text class="sk-direction" x="11" y="149">RIGHT</text><text class="sk-direction" x="352" y="149">LEFT</text>',
 // Image stays a single 128px transparent original: never canvas-generated DSP.
 '<ellipse class="sk-head-shadow" cx="200" cy="206" rx="72" ry="32" fill="#031621" fill-opacity=".3"/>',
 '<image id="skListenerArt" class="sk-listener-art" href="assets/sonku-listener-top.png" x="107" y="73" width="186" height="190" preserveAspectRatio="xMidYMid meet" aria-label="Listener facing bottom" pointer-events="none"/>',
 // Near orbit overlays lower bust, forming ONE plausible 3D spatial ring.
 '<path id="skFrontArc" class="sk-orbit-front-shadow" d="M348 174 A148 83 0 0 1 52 174" fill="none" stroke="#061723" stroke-opacity=".7" stroke-width="4.5"/>',
 '<path class="sk-orbit-front" d="M348 174 A148 83 0 0 1 52 174" fill="none" stroke="url(#skRail)" stroke-opacity=".9" stroke-width="1.8" stroke-dasharray="5 7"/>',
 '<path d="M193 283 L200 291 L207 283" fill="none" stroke="#77d9e6" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" opacity=".83"/>',
 // No circle markers: each emitter is a small raised studio monitor.
 // Offsets at coincident directions are display-only; native azimuth stays equal.
 '<g class="sk-src" data-index="0" tabindex="0" role="slider" aria-label="Left virtual source position" aria-valuemin="-180" aria-valuemax="180">',
 '<path class="sk-tether" d="" fill="none" stroke="#91dcef" stroke-width=".9" stroke-dasharray="2 2"/>',
 '<rect class="sk-speaker-shell" x="-13" y="-17" width="26" height="34" rx="5"/>',
 '<rect class="sk-speaker-inner" x="-10" y="-14" width="20" height="28" rx="3"/>',
 '<circle class="sk-speaker-tweeter" cx="0" cy="-7" r="3.2"/>',
 '<circle class="sk-speaker-woofer" cx="0" cy="5" r="6.9"/>',
 '<circle class="sk-speaker-cone" cx="0" cy="5" r="3.1"/>',
 '<text class="sk-speaker-label" x="0" y="-22" text-anchor="middle">L</text></g>',
 '<g class="sk-src sk-right" data-index="1" tabindex="0" role="slider" aria-label="Right virtual source position" aria-valuemin="-180" aria-valuemax="180">',
 '<path class="sk-tether" d="" fill="none" stroke="#eac2e3" stroke-width=".9" stroke-dasharray="2 2"/>',
 '<rect class="sk-speaker-shell" x="-13" y="-17" width="26" height="34" rx="5"/>',
 '<rect class="sk-speaker-inner" x="-10" y="-14" width="20" height="28" rx="3"/>',
 '<circle class="sk-speaker-tweeter" cx="0" cy="-7" r="3.2"/>',
 '<circle class="sk-speaker-woofer" cx="0" cy="5" r="6.9"/>',
 '<circle class="sk-speaker-cone" cx="0" cy="5" r="3.1"/>',
 '<text class="sk-speaker-label" x="0" y="-22" text-anchor="middle">R</text></g>',
 '</svg><div class="sk-stage-caption">HEADPHONES · HRIR 0° ELEVATION · 44.1 / 48 kHz</div>',
 '</div><aside class="sk-controls"><div class="sk-control-title">SPATIAL ENGINE <span class="sk-engine-led">HRTF ACTIVE</span></div>',
 '<div class="sk-buttons"><button type="button" id="skMotion">ORBIT</button><button type="button" id="skDirection">CLOCKWISE</button></div>',
 '<div class="sk-rotary-grid">',
 '<label class="sk-rotary"><span class="sk-param-name">REVOLUTION</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skSpeed" type="range" min="0.5" max="30" step="0.1" value="12" title="Drag up/down, arrow keys or double-click to reset"/></span><output id="skSpeedValue">12.0 s</output></label>',
 '<label class="sk-rotary"><span class="sk-param-name">SPREAD</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skSpread" type="range" min="0" max="180" step="1" value="60" title="Drag up/down, arrow keys or double-click to reset"/></span><output id="skSpreadValue">60°</output></label>',
 '<label class="sk-rotary"><span class="sk-param-name">SPATIAL MIX</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skMix" type="range" min="0" max="100" step="1" value="100" title="Drag up/down, arrow keys or double-click to reset"/></span><output id="skMixValue">100%</output></label>',
 '<label class="sk-rotary"><span class="sk-param-name">OUTPUT TRIM</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skTrim" type="range" min="0" max="100" step="1" value="75" title="Drag up/down, arrow keys or double-click to reset"/></span><output id="skTrimValue">75%</output></label>',
 '</div><div class="sk-space" role="group" aria-label="Room ambience">',
 '<div class="sk-space-head"><span>SPACE</span><span>SQUARE ROOM · NATIVE PCM</span></div>',
 '<label class="sk-space-row" for="skRoom"><span>Reflections</span><input id="skRoom" type="range" min="0" max="100" value="0" step="1"/><output id="skRoomValue">OFF</output></label>',
 '<label class="sk-space-row" for="skAmbience"><span>Ambience</span><input id="skAmbience" type="range" min="0" max="100" value="0" step="1"/><output id="skAmbienceValue">OFF</output></label>',
 '</div><div class="sk-guide">Drag the two monitors on the orbit · knobs: vertical drag, double-click to reset</div>',
 '</aside></div>',
 '<footer class="sk-status"><span id="skMode">VISUAL PREVIEW</span><span id="skHint" role="status">No browser audio</span></footer>'
].join("");
const $=id=>root.querySelector("#"+id);
const mount=()=>{if(!root.isConnected)document.body.append(root)};
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
let current=null,accepted=defaults(),draft=defaults(),pending=null,latest=null,
 readId="",readSentAt=0,seq=1,clock=null,observed=0,observedAt=0,drag=null,dialDrag=null;
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
 for(const [input,value,out,label] of vals){
  const el=$(input);el.value=String(value);$(out).textContent=label;
  const min=Number(el.min),max=Number(el.max);
  const p=Math.max(0,Math.min(1,(value-min)/(max-min)));
  const dial=el.closest(".sk-dial");
  dial.style.setProperty("--sk-sweep",(p*270).toFixed(2)+"deg");
  dial.style.setProperty("--sk-needle",(-135+p*270).toFixed(2)+"deg");
 }
 for(const [id,value,out] of [["skRoom",s.roomAmount,"skRoomValue"],
      ["skAmbience",s.ambienceAmount,"skAmbienceValue"]]){
   $(id).value=String(Math.round(value*100));
   $(out).textContent=value===0?"OFF":Math.round(value*100)+"%";
 }
 const liveMotion=s.moving&&current&&!pending?observed:
    s.startAzimuthDegrees;
 const phase=native()&&s.moving&&current&&!pending&&observedAt?
    liveMotion+(performance.now()-observedAt)*.001*
     (s.clockwise?1:-1)*360/s.revolutionSeconds:liveMotion;
 const angles=[phase-s.stereoSpreadDegrees/2,phase+s.stereoSpreadDegrees/2];
 const pair=displayPair(angles);
 for(let source=0;source<2;++source) {
  const deg=angle(angles[source]);
  const {anchor,visible}=pair[source];
  const g=root.querySelector('.sk-src[data-index="'+source+'"]');
  g.setAttribute("transform","translate("+visible.x.toFixed(2)+","+visible.y.toFixed(2)+")");
  g.dataset.acousticX=anchor.x.toFixed(2);
  g.dataset.acousticY=anchor.y.toFixed(2);
  g.dataset.visualX=visible.x.toFixed(2);
  g.dataset.visualY=visible.y.toFixed(2);
  const deltaX=anchor.x-visible.x,deltaY=anchor.y-visible.y;
  const leader=g.querySelector(".sk-tether");
  leader.setAttribute("d","M0 0 L"+deltaX.toFixed(2)+" "+deltaY.toFixed(2));
  leader.style.opacity=Math.hypot(deltaX,deltaY)>4?".65":"0";
  g.setAttribute("aria-valuenow",String(Math.round(deg>180?deg-360:deg)));
  const direction=deg<45||deg>=315?"FRONT":deg<135?"RIGHT":deg<225?"REAR":"LEFT";
  g.setAttribute("aria-valuetext",direction+" "+Math.round(deg)+"°");
 }
 $("skMode").textContent=native()?(context()?"NATIVE HRTF":"NATIVE UNAVAILABLE"):"VISUAL ONLY · NO AUDIO";
}
function hint(s){$("skHint").textContent=s;}
function requestSnapshot(force=false){
 const c=context();if(!c)return;
 const now=performance.now();
 // Avoid replacing an unanswered request at each 250ms render tick.
 if(!force && readId && now-readSentAt<1500)return;
 readId="sk-read-"+String(Date.now())+"-"+(++seq);
 readSentAt=now;
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
 pending=null;latest=null;draft={...accepted};hint(reason);render();requestSnapshot(true);
}
backend()?.addEventListener?.("arMixBuiltinSonKuQueue",r=>{
 if(!pending||r?.requestId!==pending.requestId)return;
 if(r.status==="REJECTED")finishFailure("DSP REJECTED · "+(r.errorCode||"STALE"));
});
backend()?.addEventListener?.("arMixBuiltinSonKuAck",r=>{
 if(!pending||r?.requestId!==pending.requestId)return;
 if(r.status!=="APPLIED")return finishFailure("DSP NOT APPLIED · "+(r.errorCode||"STALE"));
 pending.ack=true;hint("DSP APPLIED · VERIFYING READBACK");requestSnapshot(true);
});
backend()?.addEventListener?.("arMixBuiltinSonKuSnapshot",r=>{
 if(r?.requestId!==readId||!current)return;
 const c=context();if(!c||r.status!=="ACCEPTED"||
   r.channelId!==c.channelId||r.instanceId!==c.instanceId)return;
 if(!valid(r.settings))return;
 readId="";readSentAt=0;
 if(pending) {
  if(!pending.ack || r.appliedRequestId!==pending.requestId){
   if(pending.ack)requestSnapshot();
   return;
  }
  accepted={...r.settings};pending=null;hint("DSP APPLIED · ACCEPTED");flushLatest();
 }else if(!drag&&!dialDrag){
  accepted={...r.settings};draft={...r.settings};
 }
 if(Number.isFinite(r.observedAzimuthDegrees)){
  observed=r.observedAzimuthDegrees;observedAt=performance.now();
 }
 render();
});
$("skClose").addEventListener("click",()=>{root.hidden=true;current=null;
 pending=null;latest=null;readId="";readSentAt=0;dialDrag=null;
 if(clock!==null){clearInterval(clock);clock=null;}});
$("skMotion").addEventListener("click",()=>send({...draft,moving:!draft.moving}));
$("skDirection").addEventListener("click",()=>send({...draft,clockwise:!draft.clockwise}));
for(const [id,key] of [["skRoom","roomAmount"],["skAmbience","ambienceAmount"]]) {
 const el=$(id);
 el.addEventListener("input",()=>{
   const value=Number(el.value)/100;
   if(Number.isFinite(value)){draft={...draft,[key]:value};render();}
 });
 el.addEventListener("change",()=>send({...draft,[key]:Number(el.value)/100}));
}
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
// Control-surface tactile dials. Retain native <input type=range> as the
// accessible focus/keyboard source; direct pointer motion is vertical and
// commits exactly once on release (never streams speculative host DSP values).
for(const [id,initial] of [["skSpeed",12],["skSpread",60],["skMix",100],["skTrim",75]]){
 const input=$(id),dial=input.closest(".sk-dial");
 const min=Number(input.min),max=Number(input.max),step=Number(input.step)||1;
 dial.addEventListener("pointerdown",e=>{
  if(e.button!==0)return;
  e.preventDefault();input.focus({preventScroll:true});
  dialDrag={id,pointer:e.pointerId,startY:e.clientY,origin:Number(input.value),moved:false};
  dial.setPointerCapture(e.pointerId);
 });
 dial.addEventListener("pointermove",e=>{
  if(!dialDrag||dialDrag.id!==id||dialDrag.pointer!==e.pointerId)return;
  const delta=dialDrag.startY-e.clientY;
  if(Math.abs(delta)>2)dialDrag.moved=true;
  if(!dialDrag.moved)return;
  const raw=dialDrag.origin+delta*(max-min)/135;
  const bounded=Math.min(max,Math.max(min,raw));
  const next=Math.min(max,Math.max(min,min+Math.round((bounded-min)/step)*step));
  if(Number(input.value)!==next){input.value=String(next);input.dispatchEvent(new Event("input",{bubbles:true}));}
 });
 const finish=e=>{
  if(!dialDrag||dialDrag.id!==id||dialDrag.pointer!==e.pointerId)return;
  const moved=dialDrag.moved;
  dialDrag=null;
  if(dial.hasPointerCapture(e.pointerId))dial.releasePointerCapture(e.pointerId);
  if(moved)input.dispatchEvent(new Event("change",{bubbles:true}));
 };
 dial.addEventListener("pointerup",finish);
 dial.addEventListener("pointercancel",e=>{if(dialDrag?.id===id)dialDrag=null;});
 dial.addEventListener("dblclick",e=>{
  e.preventDefault();input.value=String(initial);
  input.dispatchEvent(new Event("input",{bubbles:true}));
  input.dispatchEvent(new Event("change",{bubbles:true}));
 });
}
const map=$("skMap");
function toAzimuth(e) {
 const rect=map.getBoundingClientRect();
 const x=(e.clientX-rect.left)*400/rect.width;
 const y=(e.clientY-rect.top)*330/rect.height;
 return stageAzimuth(x,y);
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
 // Screen-space arrow nudges preserve anatomical orientation on every quadrant.
 const source=Number(g.dataset.index),offset=(source?1:-1)*draft.stereoSpreadDegrees/2;
 const point=stagePoint(draft.startAzimuthDegrees+offset);
 const deltaX=e.key==="ArrowLeft"?-7:e.key==="ArrowRight"?7:0;
 const deltaY=e.key==="ArrowUp"?-7:e.key==="ArrowDown"?7:0;
 const next=stageAzimuth(point.x+deltaX,point.y+deltaY)-offset;
 send({...draft,startAzimuthDegrees:Math.max(-360,Math.min(360,next)),moving:false});
});
const api={
 id:ID,
 insert(target){return Boolean(window.__ARMIX_NATIVE_M2__?.insertPlugin?.(ID,target));},
 open(track,slot){
  pending=null;latest=null;readId="";readSentAt=0;dialDrag=null;
  observedAt=0;accepted=defaults();draft=defaults();
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
