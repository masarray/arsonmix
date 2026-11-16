/* 8D-SonKu. Presentation only: native JUCE remains the ONLY PCM authority.
   No WebAudio, source simulation, optimistic DSP success or second /preview/. */
(() => {
"use strict";
const ID="armix.builtin.8d-sonku.v1";
const defaults=()=>({startAzimuthDegrees:0,revolutionSeconds:12,
 stereoSpreadDegrees:60,spatialMix:1,outputTrim:.75,moving:true,clockwise:true,
 spacePreset:0,sourceDistanceMeters:1.6,roomAmount:.55,ambience:.30});
const normalizeSettings=s=>({...defaults(),...s});
// Native schema v3 is the ONLY authority that enables elevation controls.
// Public v2 never receives, edits or serializes a speculative XYZ field.
const roomHeights=[3.2,3.2,4.8,7.2];
const elevationLimits=s=>{
 const distance=s.sourceDistanceMeters;
 const height=roomHeights[s.spacePreset];
 if(!Number.isFinite(distance)||!height)return {min:0,max:0};
 const spectralOffset=(s.renderMode===7||s.renderMode===8||s.renderMode===9)?(s.hrtfElevationTrimDegrees??0):0;
  const bottom=Math.max(-40-spectralOffset,Math.asin(Math.max(-1,Math.min(1,(.5-1.45)/distance)))*180/Math.PI);
 const top=Math.min(60-spectralOffset,Math.asin(Math.max(-1,Math.min(1,(height-.5-1.45)/distance)))*180/Math.PI);
 // Keep a 0.1 degree safety margin for C++ float boundary rounding.
 return {min:Math.ceil((bottom+0.001)*10)/10,
         max:Math.floor((top-0.001)*10)/10};
};
const fitElevation=s=>{
 if(!Object.prototype.hasOwnProperty.call(s,"directElevationDegrees"))return s;
 const {min,max}=elevationLimits(s);
 return {...s,directElevationDegrees:Math.min(max,Math.max(min,s.directElevationDegrees))};
};
// Legacy v1/v2 states already persist a LINEAR outputTrim number (0..1).
// Keep the native JSON key and its meaning intact; the visible GAIN dial
// now expresses the SAME linear multiplier in dB and can exceed unity.
// No schema/serialized-bytes migration is necessary.
const toDb=g=>g>0?Math.max(-24,20*Math.log10(g)):-24;
const fromDb=db=>Number(Math.pow(10,Number(db)/20).toFixed(6));
const dbLabel=db=>(db>0?"+":"")+db.toFixed(1)+" dB";
const roomMaxDistance=[10,3.1,6.7,10];
// Factory presets are explicit native scene values, never browser audio DSP.
// A preset applies the COMPLETE documented scene in one ACK-verified command.
// Preserve custom .smix settings until the operator actually selects a preset.
const factoryPresets=Object.freeze({
 music:Object.freeze({moving:false,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:82,spatialMix:1,outputTrim:1,
  spacePreset:3,sourceDistanceMeters:1.6,roomAmount:.78,ambience:.33}),
 studio:Object.freeze({moving:false,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:68,spatialMix:1,outputTrim:1,
  spacePreset:1,sourceDistanceMeters:1.6,roomAmount:.42,ambience:.18}),
 wide:Object.freeze({moving:false,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:118,spatialMix:1,outputTrim:1,
  spacePreset:2,sourceDistanceMeters:3.2,roomAmount:.68,ambience:.32}),
 deep:Object.freeze({moving:false,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:104,spatialMix:1,outputTrim:1,
  spacePreset:3,sourceDistanceMeters:4.5,roomAmount:.82,ambience:.42}),
 orbit:Object.freeze({moving:true,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:8.5,stereoSpreadDegrees:100,spatialMix:1,outputTrim:1,
  spacePreset:3,sourceDistanceMeters:2.5,roomAmount:.72,ambience:.38})
});
const matchFactoryPreset=s=>Object.entries(factoryPresets).find(([id,p])=>
  s.renderMode===9 && Object.keys(p).every(k=>Math.abs(Number(s[k])-Number(p[k]))<.00002))?.[0]||"CUSTOM";
const valid=s=>s && Number.isFinite(s.startAzimuthDegrees) &&
 Math.abs(s.startAzimuthDegrees)<=360 &&
 Number.isFinite(s.revolutionSeconds)&&s.revolutionSeconds>=.5&&s.revolutionSeconds<=120 &&
 Number.isFinite(s.stereoSpreadDegrees)&&s.stereoSpreadDegrees>=0&&s.stereoSpreadDegrees<=180 &&
 Number.isFinite(s.spatialMix)&&s.spatialMix>=0&&s.spatialMix<=1 &&
 Number.isFinite(s.outputTrim)&&s.outputTrim>=0&&s.outputTrim<=4 &&
 typeof s.moving==="boolean"&&typeof s.clockwise==="boolean" &&
 Number.isInteger(s.spacePreset) && s.spacePreset>=0 && s.spacePreset<=3 &&
 Number.isFinite(s.sourceDistanceMeters) && s.sourceDistanceMeters>=1.6 &&
 s.sourceDistanceMeters<=roomMaxDistance[s.spacePreset]+.0001 &&
 Number.isFinite(s.roomAmount) && s.roomAmount>=0 && s.roomAmount<=1 &&
 Number.isFinite(s.ambience) && s.ambience>=0 && s.ambience<=1 &&
 (s.directElevationDegrees===undefined || (
   Number.isFinite(s.directElevationDegrees) &&
   s.directElevationDegrees>= -40 && s.directElevationDegrees<=60 &&
   s.sourceDistanceMeters*Math.cos(s.directElevationDegrees*Math.PI/180)>=.7 &&
   1.45+s.sourceDistanceMeters*Math.sin(s.directElevationDegrees*Math.PI/180)>=.5 &&
   1.45+s.sourceDistanceMeters*Math.sin(s.directElevationDegrees*Math.PI/180)<=
       roomHeights[s.spacePreset]-.5)) &&
 (s.renderMode===undefined || (
    Number.isInteger(s.renderMode)&&s.renderMode>=0&&s.renderMode<=9 &&
    typeof s.musicCompatibleLowFrequency==="boolean" &&
    (s.hrtfElevationTrimDegrees===undefined || (
      Number.isFinite(s.hrtfElevationTrimDegrees) &&
      s.hrtfElevationTrimDegrees>=-30 && s.hrtfElevationTrimDegrees<=20 &&
      ((s.renderMode!==7&&s.renderMode!==8&&s.renderMode!==9) || (s.directElevationDegrees+s.hrtfElevationTrimDegrees>=-40 &&
                            s.directElevationDegrees+s.hrtfElevationTrimDegrees<=60)))) &&
    Number.isFinite(s.headYawDegrees)&&Math.abs(s.headYawDegrees)<=360 &&
    (s.renderMode!==2 || (s.stereoSpreadDegrees>=20&&s.stereoSpreadDegrees<=120))));
const backend=()=>window.__JUCE__?.backend;
const native=()=>typeof backend()?.emitEvent==="function";
const snapshot=()=>window.__ARMIX_NATIVE_M2__?.fxEditorState?.()||
 window.__ARMIX_NATIVE_M2__?.state?.()?.current;
const angle=(v)=>((v%360)+360)%360;
// TOP-REAR camera looks toward FRONT (image top): FRONT=0°, RIGHT=+90°, REAR=180°,
// LEFT=-90°. Presentation-only SVG projection; native HRIR ITD/ILD
// and audio rotation coordinates NEVER change.
const STAGE=Object.freeze({x:200,y:174,rx:148,ry:83});
const stagePoint=(azimuthDegrees)=>{
 const rad=angle(azimuthDegrees)*Math.PI/180;
 return {x:STAGE.x+STAGE.rx*Math.sin(rad),
         y:STAGE.y-STAGE.ry*Math.cos(rad)};
};
const stageAzimuth=(screenX,screenY)=>
 Math.atan2((screenX-STAGE.x)/STAGE.rx,
            (STAGE.y-screenY)/STAGE.ry)*180/Math.PI;
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
  anchor,visible:{x:anchor.x+(i===0?-1:1)*offset*tangent.x/n,
                  y:anchor.y+(i===0?-1:1)*offset*tangent.y/n}
 }));
};
const root=document.createElement("section");
root.id="mxNativeSonKu";root.className="mx-sonku";root.hidden=true;
root.setAttribute("role","dialog");root.setAttribute("aria-modal","false");
root.setAttribute("aria-label","8D-SonKu binaural spatial audio");
root.innerHTML=[
 '<header class="sk-head"><span class="sk-brand">8D<span>·</span>SonKu</span>',
 '<span class="sk-headnote">SPATIAL AUDIO</span>',
 '<select id="skPreset" class="sk-preset" aria-label="SonKu ready-to-use sound presets" title="Ready-to-use spatial scenes; settings remain editable">',
 '<option value="CUSTOM">CUSTOM · MANUAL</option>',
 '<option value="music">MUSIC · FRONT STAGE</option>',
 '<option value="studio">STUDIO · CLOSE</option>',
 '<option value="wide">CINEMA · WIDE</option>',
 '<option value="deep">IMMERSIVE · DEEP</option>',
 '<option value="orbit">ORBIT · SHOWCASE</option></select>',
 '<span id="skChannel" class="sk-channel"></span>',
 '<button type="button" id="skPower" class="armix-builtin-fx-power" data-fx-state="unknown" aria-label="SonKu FX state unavailable" aria-pressed="false" disabled>⏻</button>',
 '<button type="button" id="skClose" class="sk-close armix-builtin-fx-close armix-floating-close" aria-label="Close 8D-SonKu">×</button></header>',
 '<div class="sk-content"><div class="sk-stage">',
 '<svg id="skMap" viewBox="0 0 400 330" role="group" aria-label="Top-rear listener with two depth-aware draggable binaural speakers">',
 '<defs><radialGradient id="skBg" cx="50%" cy="48%" r="73%"><stop offset="0" stop-color="#263b49"/><stop offset=".65" stop-color="#152533"/><stop offset="1" stop-color="#0a1622"/></radialGradient>',
 '<radialGradient id="skFloor" cx="50%" cy="55%" r="65%"><stop offset="0" stop-color="#7fdae7" stop-opacity=".14"/><stop offset="1" stop-color="#46758b" stop-opacity="0"/></radialGradient>',
 '<linearGradient id="skRail" x1="0" x2="0" y1="0" y2="1"><stop stop-color="#80dce8"/><stop offset="1" stop-color="#355969"/></linearGradient>',
 '</defs>',
 '<rect width="400" height="330" rx="10" fill="url(#skBg)"/>',
 '<ellipse cx="200" cy="174" rx="148" ry="83" fill="url(#skFloor)"/>',
 '<path class="sk-axis" d="M200 79V270 M45 174H355" fill="none" stroke="#597d89" stroke-opacity=".21" stroke-width=".8" stroke-dasharray="2 9"/>',
 // FRONT is the far upper orbit, behind head from this top-rear camera.
 '<path id="skFarArc" class="sk-orbit-far" d="M348 174 A148 83 0 0 0 52 174" fill="none" stroke="#679aaa" stroke-opacity=".47" stroke-width="1.4" stroke-dasharray="4 7"/>',
 '<path class="sk-secondary-rail" d="M336 174 A136 70 0 0 0 64 174" fill="none" stroke="#538096" stroke-opacity=".19" stroke-width="1"/>',
 '<text class="sk-direction sk-front" x="200" y="55" text-anchor="middle">FRONT</text>',
 '<text class="sk-direction" x="200" y="317" text-anchor="middle">REAR</text>',
 '<text class="sk-direction" x="11" y="149">LEFT</text><text class="sk-direction" x="352" y="149">RIGHT</text>',
 // Rear/top-view translucent PNG: back of skull, protruding ears and shoulders.
 // Listener looks toward FRONT at top; native source azimuth is unchanged.
 '<ellipse class="sk-head-shadow" cx="200" cy="246" rx="76" ry="22" fill="#031621" fill-opacity=".27"/>',
 '<g id="skFarSources" class="sk-far-sources"></g>',
 '<image id="skListenerArt" class="sk-listener-art" href="assets/sonku-listener-top.png" x="103" y="90" width="194" height="202" preserveAspectRatio="xMidYMid meet" data-orientation="front-up-rear" aria-label="Rear/top-view listener facing front at top" pointer-events="none"/>',
 // REAR is the near lower orbit, overlaying upper back from this camera.
 '<path id="skNearArc" class="sk-orbit-near-shadow" d="M52 174 A148 83 0 0 0 348 174" fill="none" stroke="#061723" stroke-opacity=".7" stroke-width="4.5"/>',
 '<path class="sk-orbit-near" d="M52 174 A148 83 0 0 0 348 174" fill="none" stroke="url(#skRail)" stroke-opacity=".9" stroke-width="1.8" stroke-dasharray="5 7"/>',
 // Paint independent rear/near speakers above the back; far speakers behind head.
 '<g id="skNearSources" class="sk-near-sources"></g>',
 '<path d="M193 68 L200 60 L207 68" fill="none" stroke="#77d9e6" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" opacity=".83"/>',
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
 '<div id="skRenderRow" class="sk-render-row" hidden>',
 '<span class="sk-render-label">RENDER MODE · P8.3 TEST</span>',
 '<select id="skRenderMode" aria-label="Native SonKu P8.3 renderer">',
 '<option value="music">MUSIC BED · P8.2</option>',
 '<option value="mono">MONO OBJECT · LEFT INPUT</option>',
 '<option value="physical">SPEAKER MONITOR · PHYSICAL</option>',
 '<option value="musiclf">SPEAKER MONITOR · MUSIC LF</option>',
 '</select><label class="sk-yaw" for="skHeadYaw">HEAD YAW <output id="skYawValue">0°</output>',
 '<input id="skHeadYaw" type="range" min="-180" max="180" step="5" value="0"/></label></div>',
 '<div class="sk-buttons"><button type="button" id="skMotion">ORBIT</button><button type="button" id="skDirection">CLOCKWISE</button></div>',
 '<div class="sk-rotary-grid">',
 '<label class="sk-rotary"><span class="sk-param-name">REVOLUTION</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skSpeed" type="range" min="0.5" max="30" step="0.1" value="12" title="Drag up/down, arrow keys or double-click to reset"/></span><output id="skSpeedValue">12.0 s</output></label>',
 '<label class="sk-rotary"><span class="sk-param-name">SPREAD</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skSpread" type="range" min="0" max="180" step="1" value="60" title="Drag up/down, arrow keys or double-click to reset"/></span><output id="skSpreadValue">60°</output></label>',
 '<label class="sk-rotary"><span class="sk-param-name">SPATIAL MIX</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skMix" type="range" min="0" max="100" step="1" value="100" title="Drag up/down, arrow keys or double-click to reset"/></span><output id="skMixValue">100%</output></label>',
 '<label class="sk-rotary"><span class="sk-param-name">OUTPUT GAIN</span><span class="sk-dial"><span class="sk-dial-track"></span><span class="sk-dial-cap"><span class="sk-dial-needle"></span></span><input id="skTrim" type="range" min="-24" max="12" step="0.5" value="-2.5" title="Gain in dB (-24 to +12). Double-click for unity 0 dB."/></span><output id="skTrimValue">-2.5 dB</output></label>',
 '</div><div class="sk-space-panel" aria-label="Room simulation controls">',
 '<div class="sk-space-head"><span>SPACE</span><output id="skSpaceText">DIRECT ONLY</output></div>',
 '<div class="sk-space-options" role="group" aria-label="Room size">',
 '<button type="button" data-sk-room="0">OFF</button>',
 '<button type="button" data-sk-room="1">SQUARE</button>',
 '<button type="button" data-sk-room="2">CHAMBER</button>',
 '<button type="button" data-sk-room="3">LARGE</button></div>',
 '<label class="sk-space-slider" for="skDistance">SOURCE DISTANCE <output id="skDistanceValue">1.6 m</output>',
 '<input id="skDistance" type="range" min="1.6" max="10" step="0.1" value="1.6"/></label>',
 '<label class="sk-space-slider" id="skElevationRow" for="skElevation" hidden>SOURCE ELEVATION <output id="skElevationValue">0.0°</output>',
 '<input id="skElevation" type="range" min="-30" max="45" step="0.1" value="0" aria-label="Direct source elevation in degrees" title="Measured binaural source height · native experimental schema v3 only"/></label>',
 '<label class="sk-space-slider" id="skHrtfAlignRow" for="skHrtfAlign" hidden>EAR HEIGHT ALIGN · HRTF <output id="skHrtfAlignValue">0.0°</output>',
 '<input id="skHrtfAlign" type="range" min="-30" max="20" step="0.1" value="0" aria-label="Calibrate headphone HRTF perceived elevation, not physical XYZ source height" title="Ear-level alignment: changes measured KEMAR spectral angle, not room speaker height"/></label>',
 '<label class="sk-space-slider" for="skReflections">REFLECTIONS <output id="skReflectionsValue">55%</output>',
 '<input id="skReflections" type="range" min="0" max="100" step="1" value="55" title="First-wall reflections level, 0 is dry"/></label>',
 '<label class="sk-space-slider" for="skAmbience">REVERB <output id="skAmbienceValue">30%</output>',
 '<input id="skAmbience" type="range" min="0" max="100" step="1" value="30"/></label>',
 '<div class="sk-space-hint">Independent first reflections + diffuse late reverb · OFF = original HRTF</div></div>',
 '<div class="sk-guide">Drag monitors to position · rotary knobs: vertical drag, double-click to reset</div>',
 '</aside></div>',
 '<footer class="sk-status"><span id="skMode">VISUAL PREVIEW</span><span id="skHint" role="status">No browser audio</span></footer>'
].join("");
// Move the complete existing SPACE controls below the orbit, retaining the
// SAME native slider IDs, ACK, gesture ownership, and room-validation logic.
// This is a DOM layout move, never a duplicated render/control authority.
root.querySelector(".sk-stage").appendChild(root.querySelector(".sk-space-panel"));
const $=id=>root.querySelector("#"+id);
// Cache fixed SVG node references; only their transforms change at 60fps.
const speakerNodes=[...root.querySelectorAll("#skMap .sk-src")];
const speakerTethers=speakerNodes.map(n=>n.querySelector(".sk-tether"));
const depthLayers={far:$("skFarSources"),near:$("skNearSources")};
const mount=()=>{if(!root.isConnected)document.body.append(root)};
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
let current=null,accepted=defaults(),draft=defaults(),pending=null,latest=null,
 xyzCapable=false,p83Capable=false,p84Capable=false,p85Capable=false,p86Capable=false,p87Capable=false,p88Capable=false,p89Capable=false,p90Capable=false,p91p92Capable=false,p94Capable=false,musicSafeStereo=false,p8TransparentBass=false,p82MultibandRoom=false,
 readId="",readSentAt=0,seq=1,clock=null,frame=null,observed=0,observedAt=0,
 drag=null,dialDrag=null,rangeEdits=new Set(),gestureStart=new Map(),
 visualPhase=0,visualAt=0,liveTimer=null,lastLiveAt=0;
function syncPower(){
 const p=$("skPower");
 if(!current){
  p.dataset.fxState="unknown";p.disabled=true;
  root.querySelector(".sk-engine-led").textContent="HRTF WAIT";
  return;
 }
 const n=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(current.track);
 const lane=n?.graph?.channels?.find(x=>x.id===id);
 const insert=lane?.inserts?.[current.slot];
 const globalOff=document.querySelector(".console-body")?.classList.contains("global-fx-bypassed")===true;
 const state=!native()?
  (current.track?.slotBypass?.[current.slot]?"off":"on"):
  (!lane||typeof lane.fxRackBypassed!=="boolean"||
    !insert||insert.catalogId!==ID||typeof insert.bypassed!=="boolean"?"unknown":
    (lane.fxRackBypassed||globalOff)?"rack-off":insert.bypassed?"off":"on");
 const beforeState=p.dataset.fxState;
 p.dataset.fxState=state;
 p.disabled=state==="unknown"||state==="rack-off";
 p.setAttribute("aria-pressed",String(state==="on"));
 p.title=state==="on"?"SONKU ON · click to bypass this insert":
    state==="off"?"SONKU BYPASSED · click to enable":
    state==="rack-off"?"FX RACK DISABLED · enable rack first":
    "SONKU STATE UNAVAILABLE · waiting for native readback";
 p.setAttribute("aria-label",p.title);
 root.dataset.fxState=state;
 // Audio stops when native FX is bypassed. Do not keep predicting visual
 // orbit against a frozen PCM phase (old behavior visibly oscillated).
 if(beforeState!==state){
  visualAt=0;
  if(state!=="on")observedAt=0;
 }
 root.querySelector(".sk-engine-led").textContent=
  state==="on"?"HRTF ACTIVE":
  state==="off"?"FX BYPASSED":
  state==="rack-off"?"RACK OFF":"HRTF WAIT";
}
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
 // Header identity is always authoritative, including v2 native, reconnect
 // and logical sessions. A missing capability must never unlock presets.
 syncPower();
 const presetPicker=$("skPreset");
 presetPicker.disabled=!p91p92Capable || s.renderMode!==9 || !current || Boolean(pending);
 const factoryName=p91p92Capable?matchFactoryPreset(s):"CUSTOM";
 if(presetPicker.value!==factoryName)presetPicker.value=factoryName;
 const row=$("skRenderRow");row.hidden=!(p83Capable||p84Capable);
 if(p83Capable||p84Capable){
 const picker=$("skRenderMode");
  if(p90Capable && picker.dataset.nativeModeSet!==(p94Capable?"p94":p91p92Capable?"p91p92":"p90")){
    picker.innerHTML='<option value="music">MUSIC BED · P8.2 GOLDEN</option>'+
     '<option value="clarity">SPATIAL · P8.4 REFERENCE</option>'+
     '<option value="coherence">SPATIAL · P8.5 REFERENCE</option>'+
     '<option value="focus">SPATIAL · P8.6 REFERENCE</option>'+
     '<option value="body">SPATIAL · P8.7 REFERENCE</option>'+
     '<option value="align">SPATIAL · P8.8 REFERENCE</option>'+
     '<option value="depth">SPATIAL · P8.9 REFERENCE</option>'+
     (p94Capable?
       '<option value="external">P9.4 · FRONT/REAR DEPTH (A/B)</option>':
       p91p92Capable?
       '<option value="external">P9.1 + P9.2 · MUSIC / BASS</option>':
       '<option value="external">P9.0 · TRUE EXTERNAL SPEAKER (EXPERIMENT)</option>');
    picker.dataset.nativeModeSet=p94Capable?"p94":p91p92Capable?"p91p92":"p90";
    root.querySelector(".sk-render-label").textContent=p94Capable?
      "RENDER MODE · P9.4 EXPERIMENT":p91p92Capable?
      "RENDER MODE · P9.1 + P9.2 FAT":"RENDER MODE · P9.0 HEADPHONE FAT";
   }else if(!p90Capable && p89Capable && picker.dataset.nativeModeSet!=="p89"){
    picker.innerHTML='<option value="music">MUSIC BED · P8.2 GOLDEN</option>'+
     '<option value="clarity">SPATIAL · P8.4 REFERENCE</option>'+
     '<option value="coherence">SPATIAL · P8.5 REFERENCE</option>'+
     '<option value="focus">SPATIAL · P8.6 REFERENCE</option>'+
     '<option value="body">SPATIAL · P8.7 REFERENCE</option>'+
     '<option value="align">SPATIAL · P8.8 EAR ALIGN + DISTANCE</option>'+
     '<option value="depth">SPATIAL · P8.9 ROOM-ANCHORED STEREO</option>';
    picker.dataset.nativeModeSet="p89";
    root.querySelector(".sk-render-label").textContent="RENDER MODE · P8.9 HEADPHONE FAT";
   }else if(!p89Capable && p88Capable && picker.dataset.nativeModeSet!=="p88"){
   picker.innerHTML='<option value="music">MUSIC BED · P8.2 GOLDEN</option>'+
    '<option value="clarity">SPATIAL · P8.4 REFERENCE</option>'+
    '<option value="coherence">SPATIAL · P8.5 REFERENCE</option>'+
    '<option value="focus">SPATIAL · P8.6 FOCUS</option>'+
    '<option value="body">SPATIAL · P8.7 AIR + BODY</option>'+
    '<option value="align">SPATIAL · P8.8 EAR ALIGN + DISTANCE</option>';
   picker.dataset.nativeModeSet="p88";
   root.querySelector(".sk-render-label").textContent="RENDER MODE · P8.8 HEADPHONE FAT";
  }else if(!p88Capable && p87Capable && picker.dataset.nativeModeSet!=="p87"){
   picker.innerHTML='<option value="music">MUSIC BED · P8.2 GOLDEN</option>'+
    '<option value="clarity">SPATIAL · P8.4 REFERENCE</option>'+
    '<option value="coherence">SPATIAL · P8.5 REFERENCE</option>'+
    '<option value="focus">SPATIAL · P8.6 FOCUS REFERENCE</option>'+
    '<option value="body">SPATIAL · P8.7 MUSICAL AIR + BODY</option>';
   picker.dataset.nativeModeSet="p87";
   root.querySelector(".sk-render-label").textContent="RENDER MODE · P8.7 HEADPHONE FAT";
  }else if(!p87Capable && p86Capable && picker.dataset.nativeModeSet!=="p86"){
   picker.innerHTML='<option value="music">MUSIC BED · P8.2 GOLDEN</option>'+
    '<option value="clarity">SPATIAL · P8.4 FAT REFERENCE</option>'+
    '<option value="coherence">SPATIAL · P8.5 FAT REFERENCE</option>'+
    '<option value="focus">SPATIAL · P8.6 HORIZONTAL FOCUS</option>';
   picker.dataset.nativeModeSet="p86";
   root.querySelector(".sk-render-label").textContent="RENDER MODE · P8.6 HEADPHONE FAT";
  }else if(!p86Capable && p85Capable && picker.dataset.nativeModeSet!=="p85"){
   picker.innerHTML='<option value="music">MUSIC BED · P8.2 GOLDEN</option>'+
    '<option value="clarity">SPATIAL · P8.4 FAT REFERENCE</option>'+
    '<option value="coherence">SPATIAL · P8.5 COHERENT WORLD</option>';
   picker.dataset.nativeModeSet="p85";
   root.querySelector(".sk-render-label").textContent="RENDER MODE · P8.5 HEADPHONE FAT";
  }else if(!p85Capable && p84Capable && picker.dataset.nativeModeSet!=="p84"){
   picker.innerHTML='<option value="music">MUSIC BED · P8.2 GOLDEN</option>'+
    '<option value="clarity">SPATIAL · P8.4 MUSIC FIRST</option>';
   picker.dataset.nativeModeSet="p84";
   root.querySelector(".sk-render-label").textContent="RENDER MODE · P8.4 HEADPHONE FAT";
  }else if(p83Capable && picker.dataset.nativeModeSet!=="p83"){
   picker.innerHTML='<option value="music">MUSIC BED · P8.2</option>'+
    '<option value="mono">MONO OBJECT · LEFT INPUT</option>'+
    '<option value="physical">SPEAKER MONITOR · PHYSICAL</option>'+
    '<option value="musiclf">SPEAKER MONITOR · MUSIC LF</option>';
   picker.dataset.nativeModeSet="p83";
   root.querySelector(".sk-render-label").textContent="RENDER MODE · P8.3 TEST";
  }
  const v=p90Capable?(s.renderMode===9?"external":s.renderMode===8?"depth":s.renderMode===7?"align":s.renderMode===6?"body":s.renderMode===5?"focus":s.renderMode===4?"coherence":s.renderMode===3?"clarity":"music"):
     p89Capable?(s.renderMode===8?"depth":s.renderMode===7?"align":s.renderMode===6?"body":s.renderMode===5?"focus":s.renderMode===4?"coherence":s.renderMode===3?"clarity":"music"):
     p88Capable?(s.renderMode===7?"align":s.renderMode===6?"body":s.renderMode===5?"focus":s.renderMode===4?"coherence":s.renderMode===3?"clarity":"music"):
    p87Capable?(s.renderMode===6?"body":s.renderMode===5?"focus":s.renderMode===4?"coherence":s.renderMode===3?"clarity":"music"):
    p86Capable?(s.renderMode===5?"focus":s.renderMode===4?"coherence":s.renderMode===3?"clarity":"music"):
    p85Capable?(s.renderMode===4?"coherence":s.renderMode===3?"clarity":"music"):
    p84Capable?(s.renderMode===3?"clarity":"music"):
    s.renderMode===1?"mono":s.renderMode===2?
    (s.musicCompatibleLowFrequency?"musiclf":"physical"):"music";
  // Native readback arrives every 250ms. A no-op assignment to an open native
  // select is unnecessary; preserve popup interaction while value is unchanged.
  if(picker.value!==v)picker.value=v;
  $("skHeadYaw").value=String(s.headYawDegrees);
  $("skYawValue").textContent=Math.round(s.headYawDegrees)+"°";
 }
 speakerNodes[1].style.display=p83Capable&&s.renderMode===1?"none":"";
 $("skMotion").textContent=s.moving?"ORBIT":"STATIC";
 $("skMotion").setAttribute("aria-pressed",String(s.moving));
 $("skDirection").textContent=s.clockwise?"CLOCKWISE":"COUNTER-CW";
 $("skDirection").setAttribute("aria-pressed",String(s.clockwise));
 const vals=[["skSpeed",s.revolutionSeconds,"skSpeedValue",s.revolutionSeconds.toFixed(1)+" s"],
  ["skSpread",s.stereoSpreadDegrees,"skSpreadValue",Math.round(s.stereoSpreadDegrees)+"°"],
  ["skMix",s.spatialMix*100,"skMixValue",Math.round(s.spatialMix*100)+"%"],
  ["skTrim",toDb(s.outputTrim),"skTrimValue",dbLabel(toDb(s.outputTrim))]];
 for(const [input,value,out,label] of vals){
  const el=$(input);el.value=String(value);$(out).textContent=label;
  const min=Number(el.min),max=Number(el.max);
  const p=Math.max(0,Math.min(1,(value-min)/(max-min)));
  const dial=el.closest(".sk-dial");
  dial.style.setProperty("--sk-sweep",(p*270).toFixed(2)+"deg");
  dial.style.setProperty("--sk-needle",(-135+p*270).toFixed(2)+"deg");
 }
 // ROOM is one typed native scene, never a browser audio simulator.
 root.querySelectorAll("[data-sk-room]").forEach(el=>{
  el.setAttribute("aria-pressed",
   String(Number(el.dataset.skRoom)===s.spacePreset));
 });
 const sizeLabels=["DIRECT ONLY","7.2 × 7.2 m","14.4 × 14.4 m","28.8 × 28.8 m"];
 $("skSpaceText").textContent=sizeLabels[s.spacePreset];
 const distance=$("skDistance"),max=roomMaxDistance[s.spacePreset];
 distance.max=String(max);
 distance.disabled=s.spacePreset===0;
 // Native readback may arrive while the pointer is held. Never send the
 // slider thumb back to the last ACK while the operator is editing.
 if(!rangeEdits.has("skDistance"))
  distance.value=String(Math.min(max,s.sourceDistanceMeters));
 $("skDistanceValue").textContent=s.sourceDistanceMeters.toFixed(1)+" m";
 // Never expose a nonfunctional XYZ control in the publicly compiled
 // v2-only application; the actual native snapshot controls capability.
 const elevationRow=$("skElevationRow"),elevation=$("skElevation");
 elevationRow.hidden=!xyzCapable;
 if(xyzCapable){
  const limits=elevationLimits(s);
  elevation.min=String(limits.min);elevation.max=String(limits.max);
  if(!rangeEdits.has("skElevation"))elevation.value=String(s.directElevationDegrees);
  $("skElevationValue").textContent=s.directElevationDegrees.toFixed(1)+"°";
 }
 const alignRow=$("skHrtfAlignRow"),align=$("skHrtfAlign");
 alignRow.hidden=!(p88Capable&&(s.renderMode===7||s.renderMode===8||s.renderMode===9));
 if(!alignRow.hidden){
  const min=Math.max(-30,-40-s.directElevationDegrees);
  const max=Math.min(20,60-s.directElevationDegrees);
  align.min=String(min);align.max=String(max);
  if(!rangeEdits.has("skHrtfAlign"))align.value=String(s.hrtfElevationTrimDegrees);
  $("skHrtfAlignValue").textContent=(s.hrtfElevationTrimDegrees>=0?"+":"")+s.hrtfElevationTrimDegrees.toFixed(1)+"°";
 }
 root.querySelector(".sk-stage-caption").textContent=p94Capable&&s.renderMode===9?
    "P9.4 · FRONT/REAR ROOM DEPTH · SAME 3D MOTION · HEADPHONE FAT REQUIRED":
    p91p92Capable&&s.renderMode===9?
    "P9.1 + P9.2 · MUSIC FIDELITY · WORLD BASS · ROOM MODES · FAT REQUIRED":
    p90Capable&&s.renderMode===9?
    "P9.0 · MEASURED TWIN-SPEAKER HRIR · HEADPHONE FAT REQUIRED":
    p89Capable&&s.renderMode===8?
    "P8.9 ROOM-ANCHORED STEREO · DIRECT MUSICAL BODY PRESERVED · FAT REQUIRED":
    p88Capable&&s.renderMode===7?
   "P8.8 EAR-LEVEL HRTF ALIGNMENT · PHYSICAL XYZ INDEPENDENT · FAT REQUIRED":
   p87Capable&&s.renderMode===6?
   "P8.7 FOCUSED MUSIC · DIRECT AIR + DIFFUSE LF BODY · FAT REQUIRED":
   p86Capable&&s.renderMode===5?
   "P8.6 FOCUSED HORIZONTAL SOURCES · CONTROLLED ROOM · HEADPHONE FAT REQUIRED":
   p85Capable&&s.renderMode===4?
   "P8.5 WORLD-LOCKED MUSIC MID · EXPERIMENT · HEADPHONE FAT REQUIRED":
   p84Capable&&s.renderMode===3?
   "P8.4 FUSED HRIR · MUSIC-FIRST RESEARCH · NOT DEARVR QUALIFIED":
   p83Capable&&s.renderMode===2?
  (s.musicCompatibleLowFrequency?"P8.3 MUSIC LF480 · SOURCE-POSITIONED COMPROMISE":
    "P8.3 PHYSICAL 2×2 · MEASURED HRIR"):
  p83Capable&&s.renderMode===1?"P8.3 MONO OBJECT · LEFT INPUT":
  p82MultibandRoom
  ?"HEADPHONES · P8.2 SIX-WALL 3-BAND ROOM · EXPERIMENT"
  :p8TransparentBass?"HEADPHONES · P8 STEREO BASS + MEASURED 3D HRTF · EXPERIMENT"
  :musicSafeStereo?"HEADPHONES · MEASURED CENTER 3D HRTF + MUSIC SIDE · EXPERIMENT"
  :xyzCapable?"HEADPHONES · MEASURED 3D HRIR · 44.1 / 48 kHz"
  :"HEADPHONES · HRIR 0° ELEVATION · 44.1 / 48 kHz";
 $("skSpread").closest("label").querySelector(".sk-param-name").textContent=
  (p83Capable||p84Capable)&&s.renderMode!==0?"SPEAKER ANGLE":
  musicSafeStereo?"SIDE WIDTH":"SPREAD";
 root.querySelector(".sk-guide").textContent=p90Capable&&s.renderMode===9
  ?"Two positioned measured HRIR speakers · 0° co-location cannot preserve anti-phase"
  :musicSafeStereo
  ?"Legacy music WIDTH adjusts headlocked SIDE; P9 spatializes it"
  :"Drag monitors to position · rotary knobs: vertical drag, double-click to reset";
 if(!rangeEdits.has("skAmbience"))
  $("skAmbience").value=String(Math.round(s.ambience*100));
 const reflections=$("skReflections");
 if(!rangeEdits.has("skReflections"))
  reflections.value=String(Math.round(s.roomAmount*100));
 reflections.disabled=s.spacePreset===0;
 $("skReflectionsValue").textContent=Math.round(s.roomAmount*100)+"%";
 $("skAmbience").disabled=s.spacePreset===0;
 $("skAmbienceValue").textContent=Math.round(s.ambience*100)+"%";
 // Only source placement is animated; knobs and input ranges are NOT
 // redrawn at 60 Hz. The native audio callback remains the time authority.
 drawSpeakers();
 $("skMode").textContent=native()?(context()?
  (p94Capable&&s.renderMode===9?"NATIVE P9.4 FRONT/REAR DEPTH · A/B FAT":
   p91p92Capable&&s.renderMode===9?"NATIVE P9.1+P9.2 WORLD BASS · FAT":
   p90Capable&&s.renderMode===9?"NATIVE P9.0 TRUE EXTERNAL · TEST":
   p89Capable&&s.renderMode===8?"NATIVE P8.9 STEREO + ROOM · TEST":
    p88Capable&&s.renderMode===7?"NATIVE P8.8 EAR ALIGN + DISTANCE · TEST":
    p87Capable&&s.renderMode===6?"NATIVE P8.7 MUSICAL AIR + BODY · TEST":
   p86Capable&&s.renderMode===5?"NATIVE P8.6 HORIZONTAL FOCUS · TEST":
   p85Capable&&s.renderMode===4?"NATIVE P8.5 WORLD COHERENCE · TEST":
   p84Capable&&s.renderMode===3?"NATIVE P8.4 MUSIC FUSION · TEST":
    p83Capable&&s.renderMode!==0?"NATIVE P8.3 SPEAKERS · RESEARCH":
   p82MultibandRoom?"NATIVE P8.2 ROOM · RESEARCH":
   p8TransparentBass?"NATIVE P8 ACOUSTICS · RESEARCH":
   musicSafeStereo?"NATIVE MUSIC SAFE · RESEARCH":"NATIVE HRTF"):
  "NATIVE UNAVAILABLE"):"VISUAL ONLY · NO AUDIO";
}
function drawSpeakers(){
 const s=draft;
 const phase=s.moving?visualPhase:s.startAzimuthDegrees;
 const angles=[phase-s.stereoSpreadDegrees/2,phase+s.stereoSpreadDegrees/2];
 const pair=displayPair(angles);
 for(let source=0;source<2;++source) {
  const deg=angle(angles[source]);
  const {anchor,visible}=pair[source];
  const g=speakerNodes[source];
  // View-only: positive cosine = far FRONT, negative = near REAR.
  // Continuous perspective at each side horizon; never change native HRIR angles.
  const depth=Math.cos(deg*Math.PI/180);
  const near=depth<0;
  const layer=near?depthLayers.near:depthLayers.far;
  if(g.parentNode!==layer)layer.appendChild(g);
  const scale=(.96-.20*depth).toFixed(3); // far .76, side .96, near 1.16
  g.setAttribute("transform","translate("+visible.x.toFixed(2)+","+visible.y.toFixed(2)+") scale("+scale+")");
  g.style.opacity=(.83-.14*depth).toFixed(3);
  g.dataset.depth=near?"near":"far";
  g.dataset.depthFactor=depth.toFixed(4);
  g.dataset.acousticX=anchor.x.toFixed(2);
  g.dataset.acousticY=anchor.y.toFixed(2);
  g.dataset.visualX=visible.x.toFixed(2);
  g.dataset.visualY=visible.y.toFixed(2);
  const deltaX=anchor.x-visible.x,deltaY=anchor.y-visible.y;
  const leader=speakerTethers[source];
  leader.setAttribute("d","M0 0 L"+deltaX.toFixed(2)+" "+deltaY.toFixed(2));
  leader.style.opacity=Math.hypot(deltaX,deltaY)>4?".65":"0";
  g.setAttribute("aria-valuenow",String(Math.round(deg>180?deg-360:deg)));
  const direction=deg<45||deg>=315?"FRONT":deg<135?"RIGHT":deg<225?"REAR":"LEFT";
  g.setAttribute("aria-valuetext",direction+" "+Math.round(deg)+"° ("+(near?"near":"far")+" view)");
 }
}
// rAF is a VISUAL interpolator, not an audio clock or optimistic DSP ACK.
// A bounded phase-locked correction follows the real native callback without
// teleporting at each ~250ms readback. Only SVG geometry is painted per frame.
function animate(time){
 if(root.hidden||!current){frame=null;return;}
 if(root.dataset.fxState!=="on"){
  visualAt=0;
  frame=requestAnimationFrame(animate);
  return;
 }
 if(visualAt && draft.moving && !drag){
  const dt=Math.max(0,Math.min(.08,(time-visualAt)*.001));
  const speed=(draft.clockwise?1:-1)*360/draft.revolutionSeconds;
  visualPhase=angle(visualPhase+speed*dt);
  if(observedAt&&!pending){
   const expected=angle(observed+(time-observedAt)*.001*speed);
   const error=((expected-visualPhase+540)%360)-180;
   // No hard reset to a delayed readback; max 1.5deg/frame correction.
   const correction=error*(1-Math.exp(-dt/.32));
   visualPhase=angle(visualPhase+Math.max(-1.5,Math.min(1.5,correction)));
  }
  drawSpeakers();
 }
 visualAt=time;
 frame=requestAnimationFrame(animate);
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
// One reusable CONTROL gesture publisher for every SonKu slider, knob,
// keyboard nudge and SVG speaker. The JUCE PCM callback still owns audio.
// Limit UI→native emissions to <= 25Hz. At most ONE change in flight, with
// latest-wins coalescing; ACK + accepted snapshot remain mandatory before
// issuing the next. Never fill the DSP SPSC queue with raw pointer events.
const LIVE_INTERVAL_MS=40;
const sameSettings=(a,b)=>a&&b&&Object.keys(defaults()).every(k=>a[k]===b[k]) &&
 (!xyzCapable || a.directElevationDegrees===b.directElevationDegrees) &&
 (!(p83Capable||p84Capable) || (a.renderMode===b.renderMode &&
   a.musicCompatibleLowFrequency===b.musicCompatibleLowFrequency &&
   a.headYawDegrees===b.headYawDegrees));
function send(next,continuous=false){
 if(!valid(next))return false;
 draft={...next};render();
 if(!native()){accepted={...next};hint("BROWSER VISUAL ONLY · NO DSP");return true;}
 const c=context();if(!c){hint("NATIVE INSERT NOT ACCEPTED");return false;}
 if(pending){
  latest=sameSettings(next,pending.settings)?null:{...next};
  if(!continuous)hint("DSP FOLLOWING · FINAL VALUE PENDING");
  return true;
 }
 if(sameSettings(next,accepted))return true;
 const id="sonku-"+String(Date.now()*1000+(++seq%1000));
 pending={requestId:id,settings:{...next},context:c,ack:false};
 backend().emitEvent("arMixAction",{action:"nativeSonKuSet",requestId:id,...c,settings:next});
 hint(continuous?"DSP LIVE · AWAITING AUDIO ACK":"DSP QUEUED · AWAITING AUDIO ACK");
 return true;
}
function cancelLiveTimer(){
 if(liveTimer!==null){clearTimeout(liveTimer);liveTimer=null;}
}
function liveInput(next){
 if(!valid(next))return false;
 draft={...next};render();
 const now=performance.now(),wait=LIVE_INTERVAL_MS-(now-lastLiveAt);
 if(wait<=0){
  cancelLiveTimer();lastLiveAt=now;return send(next,true);
 }
 if(liveTimer===null)liveTimer=setTimeout(()=>{
  liveTimer=null;lastLiveAt=performance.now();
  if(current&&!root.hidden)send({...draft},true);
 },Math.max(0,wait));
 return true;
}
function commitInput(next){
 cancelLiveTimer();lastLiveAt=performance.now();
 return send(next,false);
}
function flushLatest(){
 if(latest){const next=latest;latest=null;
  if(!sameSettings(next,accepted))send(next,true);
 }
}
function finishFailure(reason){
 cancelLiveTimer();pending=null;latest=null;
 // Native failure is real. Never display false "applied" audio; read back
 // authoritative DSP state, do not retry a stale device/graph on a timer.
 draft={...accepted};hint(reason);render();requestSnapshot(true);
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
   r.channelId!==c.channelId||r.instanceId!==c.instanceId||
   String(r.deviceEpoch)!==String(c.deviceEpoch)||
   String(r.graphRevision)!==String(c.graphRevision))return;
 const measured=(r.schemaVersion===3 || r.schemaVersion===4 ||
   r.schemaVersion===5 || r.schemaVersion===6 || r.schemaVersion===7 || r.schemaVersion===8 || r.schemaVersion===9 || r.schemaVersion===10 || r.schemaVersion===11) &&
   Number.isFinite(r.settings?.directElevationDegrees);
 if(r.schemaVersion!==2&&!measured)return;
 const incoming=normalizeSettings(r.settings);
 if(!measured)delete incoming.directElevationDegrees;
 if(!valid(incoming))return;
 // Activation happens only after a fully identity-bound native readback.
 xyzCapable=measured;
 p83Capable=r.schemaVersion===4 && r.p83SpeakerModes===true &&
   Number.isInteger(incoming.renderMode) &&
   typeof incoming.musicCompatibleLowFrequency==="boolean" &&
   Number.isFinite(incoming.headYawDegrees);
 p90Capable=r.schemaVersion===11 && r.p90TrueExternalSpeaker===true &&
    Number.isFinite(incoming.hrtfElevationTrimDegrees);
 // Separate from schema v11: old P9.0 DSP must not receive the P9.1/9.2 label.
 p91p92Capable=p90Capable && r.p91p92MusicBassFoundation===true;
 // Independent of schema version: only this exact measured native DSP
 // can claim the *experimental* depth calibration, not P9.1 baseline.
 p94Capable=p91p92Capable && r.p94FrontBackDepthCue===true;
 p89Capable=(r.schemaVersion===10||p90Capable) && r.p89StereoRoomDepth===true &&
    Number.isFinite(incoming.hrtfElevationTrimDegrees);
  p88Capable=(r.schemaVersion===9||p89Capable) && r.p88PerceptualAlignment===true &&
   Number.isFinite(incoming.hrtfElevationTrimDegrees);
 p87Capable=(r.schemaVersion===8||p88Capable) && r.p87MusicalAirBody===true;
 p86Capable=(r.schemaVersion===7 || p87Capable) && r.p86HorizontalFocus===true;
 p85Capable=(r.schemaVersion===6 || p86Capable) && r.p85AcousticCoherence===true;
 p84Capable=(r.schemaVersion===5 || p85Capable) && r.p84MusicFusion===true &&
   Number.isInteger(incoming.renderMode) &&
   (incoming.renderMode===0||incoming.renderMode===3||
    (p85Capable&&incoming.renderMode===4)||
    (p86Capable&&incoming.renderMode===5)||
    (p87Capable&&incoming.renderMode===6)||
    (p88Capable&&incoming.renderMode===7)||
     (p89Capable&&incoming.renderMode===8)||
     (p90Capable&&incoming.renderMode===9)) &&
   typeof incoming.musicCompatibleLowFrequency==="boolean" &&
   incoming.musicCompatibleLowFrequency===false &&
   Number.isFinite(incoming.headYawDegrees);
 if(!p88Capable)delete incoming.hrtfElevationTrimDegrees;
 if(!(p83Capable||p84Capable)){
  delete incoming.renderMode;
  delete incoming.musicCompatibleLowFrequency;
  delete incoming.headYawDegrees;
 }
 musicSafeStereo=measured && r.musicSafeStereo===true;
 p8TransparentBass=musicSafeStereo && r.p8TransparentBass===true;
 p82MultibandRoom=p8TransparentBass && r.p82MultibandRoom===true;
 readId="";readSentAt=0;
 if(pending) {
  if(!pending.ack || r.appliedRequestId!==pending.requestId){
   if(pending.ack)requestSnapshot();
   return;
  }
  accepted={...incoming};pending=null;hint("DSP APPLIED · ACCEPTED");flushLatest();
 }else if(!drag&&!dialDrag&&rangeEdits.size===0){
  accepted={...incoming};draft={...incoming};
 }else if(!pending){
  // Preserve live thumb ownership, yet advance the accepted mirror so a
  // later failure/reopen never restores a stale value.
  accepted={...incoming};
 }
 if(Number.isFinite(r.observedAzimuthDegrees) && root.dataset.fxState==="on"){
  // First authoritative phase anchors the loop once. Subsequent readbacks
  // only adjust the PLL; never set the visible phase discontinuously.
  if(!observedAt)visualPhase=angle(r.observedAzimuthDegrees);
  observed=r.observedAzimuthDegrees;observedAt=performance.now();
 }
 render();
});
let windowGesture=null;
const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
function constrainWindow(){
 if(root.hidden)return;
 const r=root.getBoundingClientRect();
 if(!r.width||!r.height)return;
 const x=clamp(r.left,0,Math.max(0,innerWidth-r.width));
 const y=clamp(r.top,0,Math.max(0,innerHeight-r.height));
 if(x===r.left && y===r.top)return;
 root.style.left=x+"px";root.style.top=y+"px";root.style.transform="none";
}
const head=root.querySelector(".sk-head");
head.addEventListener("pointerdown",e=>{
 if(e.button!==0||e.target.closest("button,select,input,a"))return;
 const r=root.getBoundingClientRect();
 windowGesture={id:e.pointerId,dx:e.clientX-r.left,dy:e.clientY-r.top};
 head.setPointerCapture(e.pointerId);e.preventDefault();
});
head.addEventListener("pointermove",e=>{
 if(!windowGesture||windowGesture.id!==e.pointerId)return;
 const r=root.getBoundingClientRect();
 root.style.left=clamp(e.clientX-windowGesture.dx,0,Math.max(0,innerWidth-r.width))+"px";
 root.style.top=clamp(e.clientY-windowGesture.dy,0,Math.max(0,innerHeight-r.height))+"px";
 root.style.transform="none";
 e.preventDefault();
});
const stopWindowDrag=e=>{
 if(!windowGesture||e.pointerId!==windowGesture.id)return;
 windowGesture=null;
 if(head.hasPointerCapture?.(e.pointerId))head.releasePointerCapture(e.pointerId);
};
head.addEventListener("pointerup",stopWindowDrag);
head.addEventListener("pointercancel",stopWindowDrag);
window.addEventListener("resize",constrainWindow);
$("skPower").addEventListener("click",()=>{
 if(!current || $("skPower").disabled)return;
 if(native()){
  window.__ARMIX_NATIVE_M2__?.consumeAction?.(
   current.track,"slotPower",{dataset:{slot:String(current.slot)}});
 }else{
  current.track.slotBypass??=[];
  current.track.slotBypass[current.slot]=!current.track.slotBypass[current.slot];
  syncPower();
 }
});
$("skPreset").addEventListener("change",()=>{
 const picker=$("skPreset");
 const preset=factoryPresets[picker.value];
 if(!p91p92Capable||draft.renderMode!==9||!preset||pending){
  render();return;
 }
 const scene=fitElevation({...draft,...preset,renderMode:9,
  musicCompatibleLowFrequency:false});
 if(!send(scene))render();
});
$("skClose").addEventListener("click",()=>{root.hidden=true;current=null;xyzCapable=false;p85Capable=false;p86Capable=false;p87Capable=false;p88Capable=false;p89Capable=false;p90Capable=false;p91p92Capable=false;p94Capable=false;musicSafeStereo=false;p8TransparentBass=false;p82MultibandRoom=false;
 cancelLiveTimer();pending=null;latest=null;readId="";readSentAt=0;dialDrag=null;
 drag=null;windowGesture=null;rangeEdits.clear();gestureStart.clear();lastLiveAt=0;
 if(clock!==null){clearInterval(clock);clock=null;}
 if(frame!==null){cancelAnimationFrame(frame);frame=null;}});
$("skRenderMode").addEventListener("change",()=>{
 if(!(p83Capable||p84Capable))return;
 const v=$("skRenderMode").value;
 const mode=p90Capable?(v==="external"?9:v==="depth"?8:v==="align"?7:v==="body"?6:v==="focus"?5:v==="coherence"?4:v==="clarity"?3:0):
     p89Capable?(v==="depth"?8:v==="align"?7:v==="body"?6:v==="focus"?5:v==="coherence"?4:v==="clarity"?3:0):
     p88Capable?(v==="align"?7:v==="body"?6:v==="focus"?5:v==="coherence"?4:v==="clarity"?3:0):
    p87Capable?(v==="body"?6:v==="focus"?5:v==="coherence"?4:v==="clarity"?3:0):
    p86Capable?(v==="focus"?5:v==="coherence"?4:v==="clarity"?3:0):
    p85Capable?(v==="coherence"?4:v==="clarity"?3:0):
    p84Capable?(v==="clarity"?3:0):
    v==="mono"?1:v==="physical"||v==="musiclf"?2:0;
 const scene={...draft,renderMode:mode,musicCompatibleLowFrequency:v==="musiclf"};
 // P9.1/P9.2: a factory-new P8.2 state is NOT a musical P9 default.
 // Only on first user selection of P9, and only when every relevant
 // parameter still matches the legacy factory values, initialize a
 // real close-room music stage. Saved .smix/manual settings are untouched.
 // The entire scene goes through the existing native ACK, never optimistic DSP.
 const legacy=draft.renderMode===0 && draft.spacePreset===0 &&
   draft.startAzimuthDegrees===0 && draft.moving===true &&
   Math.abs(draft.stereoSpreadDegrees-60)<.001 &&
   Math.abs(draft.outputTrim-.75)<.001 &&
   Math.abs(draft.roomAmount-.55)<.001 &&
   Math.abs(draft.ambience-.30)<.001 &&
   Math.abs(draft.sourceDistanceMeters-1.6)<.001;
 if(mode===9 && p90Capable && legacy){
  Object.assign(scene,{moving:false,stereoSpreadDegrees:82,
    outputTrim:1,spacePreset:3,sourceDistanceMeters:1.6,
    roomAmount:.78,ambience:.33});
 }
 // Mode changes in v6 must not reset the owner scene, yaw or motion.
 if(mode!==0 && !p85Capable){
  scene.moving=false;
  scene.startAzimuthDegrees=0;
  if(mode===2 && (scene.stereoSpreadDegrees<20 ||
                  scene.stereoSpreadDegrees>120))
   scene.stereoSpreadDegrees=60;
 }
 if(!send(scene))render();
});
$("skHeadYaw").addEventListener("change",()=>{
 if(!(p83Capable||p84Capable))return;
 if(!send({...draft,headYawDegrees:Number($("skHeadYaw").value)}))render();
});
$("skMotion").addEventListener("click",()=>send({...draft,moving:!draft.moving}));
$("skDirection").addEventListener("click",()=>send({...draft,clockwise:!draft.clockwise}));
root.querySelectorAll("[data-sk-room]").forEach(btn=>btn.addEventListener("click",()=>{
 const preset=Number(btn.dataset.skRoom),max=roomMaxDistance[preset];
 const proposed={...draft,spacePreset:preset,
  sourceDistanceMeters:preset===0?draft.sourceDistanceMeters:
   Math.min(draft.sourceDistanceMeters,max)};
 const fitted=xyzCapable?fitElevation(proposed):proposed;
 if(send(fitted)&&xyzCapable &&
    fitted.directElevationDegrees!==proposed.directElevationDegrees)
  hint("ELEVATION FITTED TO ROOM · AWAITING DSP ACK");
}));
for(const [id,key,convert] of [
 ["skDistance","sourceDistanceMeters",v=>v],
 ["skElevation","directElevationDegrees",v=>v],
 ["skHrtfAlign","hrtfElevationTrimDegrees",v=>v],
 ["skReflections","roomAmount",v=>v/100],
 ["skAmbience","ambience",v=>v/100]]) {
 const slider=$(id);
 // ALL acoustic sliders stream PCM changes on input, not only on release.
 // Thumb ownership survives stale ACK snapshots; no speculative "APPLIED".
 slider.addEventListener("pointerdown",()=>{
  rangeEdits.add(id);gestureStart.set(id,draft[key]);
 });
 slider.addEventListener("input",()=>{
  if(slider.disabled)return;
  const value=Number(slider.value);
  if(!Number.isFinite(value))return;
  rangeEdits.add(id);
  const proposed={...draft,[key]:convert(value)};
  const fitted=key==="sourceDistanceMeters"&&xyzCapable
   ?fitElevation(proposed):proposed;
  liveInput(fitted);
 });
 slider.addEventListener("change",()=>{
  gestureStart.delete(id);rangeEdits.delete(id);
  if(slider.disabled)return;
  const value=Number(slider.value);
  if(Number.isFinite(value))commitInput({...draft,[key]:convert(value)});
 });
 slider.addEventListener("pointercancel",()=>{
  const original=gestureStart.has(id)?gestureStart.get(id):accepted[key];
  gestureStart.delete(id);rangeEdits.delete(id);
  commitInput({...draft,[key]:original});
 });
}
for(const [id,key,convert] of [
 ["skSpeed","revolutionSeconds",v=>v],
 ["skSpread","stereoSpreadDegrees",v=>v],
 ["skMix","spatialMix",v=>v/100],
 ["skTrim","outputTrim",v=>fromDb(v)]]) {
 const input=$(id);
 input.addEventListener("input",()=>{const v=Number(input.value);
  if(Number.isFinite(v))liveInput({...draft,[key]:convert(v)});
 });
 input.addEventListener("change",()=>{
  const value=Number(input.value);
  if(Number.isFinite(value))commitInput({...draft,[key]:convert(value)});
 });
}
// Tactile dials use the SAME bounded live gesture publisher as acoustic
// sliders. Keyboard, mouse and touch all have one native PCM authority.
for(const [id,initial] of [["skSpeed",12],["skSpread",60],["skMix",100],["skTrim",0]]){
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
 dial.addEventListener("pointercancel",e=>{
  if(dialDrag?.id!==id || dialDrag.pointer!==e.pointerId)return;
  input.value=String(dialDrag.origin);dialDrag=null;
  input.dispatchEvent(new Event("input",{bubbles:true}));
  input.dispatchEvent(new Event("change",{bubbles:true}));
 });
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
 drag={source:Number(g.dataset.index),pointer:e.pointerId,
       starting:{...draft}};
 map.setPointerCapture?.(e.pointerId);e.preventDefault();
});
map.addEventListener("pointermove",e=>{
 if(!drag||drag.pointer!==e.pointerId)return;
 const value=toAzimuth(e)-(drag.source?1:-1)*draft.stereoSpreadDegrees/2;
 liveInput({...draft,startAzimuthDegrees:Math.max(-360,Math.min(360,value)),
  moving:false});
});
map.addEventListener("pointerup",e=>{
 if(!drag||drag.pointer!==e.pointerId)return;
 drag=null;map.releasePointerCapture?.(e.pointerId);
 commitInput({...draft});
});
map.addEventListener("pointercancel",e=>{
 if(!drag||drag.pointer!==e.pointerId)return;
 const prior=drag.starting;drag=null;
 commitInput(prior);
});
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
  cancelLiveTimer();pending=null;latest=null;readId="";readSentAt=0;dialDrag=null;
  observedAt=0;visualAt=0;visualPhase=0;lastLiveAt=0;
  drag=null;rangeEdits.clear();gestureStart.clear();
  accepted=defaults();draft=defaults();xyzCapable=false;
  p83Capable=p84Capable=p85Capable=p86Capable=p87Capable=p88Capable=
   p89Capable=p90Capable=p91p92Capable=p94Capable=false;
  musicSafeStereo=p8TransparentBass=p82MultibandRoom=false;
  current={track,slot,instanceId:null};
  const s=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const fx=s?.graph?.channels?.find(x=>x.id===id)?.inserts?.[slot];
  if(native()&&(s?.admitted||s?.logical)&&fx?.catalogId===ID)current.instanceId=fx.id;
  $("skChannel").textContent=String(track.name)+" · FX "+(slot+1);
  root.hidden=false;
  constrainWindow();syncPower();
  hint(native()?"READING ACCEPTED DSP":"BROWSER VISUAL ONLY · NO AUDIO");
  if(native())requestSnapshot();
  if(clock!==null)clearInterval(clock);
  clock=setInterval(()=>{if(root.hidden)return;
    syncPower();if(native()&&!pending&&context())requestSnapshot();},250);
  render();
  if(frame!==null)cancelAnimationFrame(frame);
  frame=requestAnimationFrame(animate);
 },
 close(){$("skClose").click();}
};
window.ArSonMixNativeSonKu=Object.freeze(api);
})();
