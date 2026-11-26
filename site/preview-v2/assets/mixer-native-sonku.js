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
// Curated against operator's *own* immersive reference (3.1 m, MAX early,
// 62% late, 82° spread, +7dB). Scene values are deliberate starting points,
// not acoustic measurements. No background mutation of existing .smix.
const factoryPresets=Object.freeze({
 // 2026-11-16 operator-accepted full scene (source of truth in P9.9
 // native DefaultOrbitStage). Only opt-in P9.9 exposes this preset;
 // existing sessions remain manual until their operator selects it.
 orbitStage:Object.freeze({moving:true,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:127,spatialMix:1,outputTrim:1.8,
  spacePreset:3,sourceDistanceMeters:2.7,roomAmount:1,ambience:.77,sceneVoicing:1}),
 music:Object.freeze({moving:true,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:6.5,stereoSpreadDegrees:82,spatialMix:1,outputTrim:1.9,
  spacePreset:3,sourceDistanceMeters:3.1,roomAmount:1,ambience:.62,sceneVoicing:1}),
 studio:Object.freeze({moving:false,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:76,spatialMix:1,outputTrim:1.65,
  spacePreset:3,sourceDistanceMeters:4.8,roomAmount:.94,ambience:.72,sceneVoicing:2}),
 wide:Object.freeze({moving:false,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:98,spatialMix:1,outputTrim:1.7,
  spacePreset:3,sourceDistanceMeters:7.5,roomAmount:1,ambience:.84,sceneVoicing:3}),
 deep:Object.freeze({moving:false,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:12,stereoSpreadDegrees:106,spatialMix:1,outputTrim:1.8,
  spacePreset:3,sourceDistanceMeters:10,roomAmount:1,ambience:1,sceneVoicing:0}),
 orbit:Object.freeze({moving:true,clockwise:true,startAzimuthDegrees:0,
  revolutionSeconds:8.5,stereoSpreadDegrees:82,spatialMix:1,outputTrim:1.75,
  spacePreset:3,sourceDistanceMeters:6.5,roomAmount:1,ambience:.83,sceneVoicing:0})
});
const matchFactoryPreset=(s,p99Enabled=false)=>Object.entries(factoryPresets).find(([id,p])=>
  // Older v11/P9.8 binaries cannot misrepresent the P9.9-only preset.
  (id!=="orbitStage" || (p99Enabled && s.sceneVoicing===1)) &&
  s.renderMode===9 && Object.keys(p).every(k=>
   (k==="sceneVoicing" && s[k]===undefined) ||
   Math.abs(Number(s[k])-Number(p[k]))<.00002))?.[0]||"CUSTOM";
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
 (s.sceneVoicing===undefined ||
  (Number.isInteger(s.sceneVoicing) && s.sceneVoicing>=0 && s.sceneVoicing<=3)) &&
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
 '<select id="skPreset" class="sk-preset" aria-label="SonKu ready-to-use sound presets" title="Ready-to-use spatial scenes; settings remain editable">',
 '<option value="CUSTOM">CUSTOM · MANUAL</option>',
 '<option value="orbitStage">ORBIT STAGE · DEFAULT</option>',
 '<option value="music">SIGNATURE · IMMERSIVE</option>',
 '<option value="studio">LARGE STUDIO · LIVE</option>',
 '<option value="wide">CINEMA · GRAND STAGE</option>',
 '<option value="deep">VENUE · MEGA SPACE</option>',
 '<option value="orbit">LIVE ORBIT · SOUND SYSTEM</option></select>',
 '<span id="skChannel" class="sk-channel"></span>',
 '<button type="button" id="skPower" class="armix-builtin-fx-power" data-fx-state="unknown" aria-label="SonKu FX state unavailable" aria-pressed="false" disabled>⏻</button>',
 '<button type="button" id="skClose" class="sk-close armix-builtin-fx-close armix-floating-close" aria-label="Close 8D-SonKu">×</button></header>',
 '<div class="sk-content"><div class="sk-stage">',
 '<div class="sk-lcd"><svg id="skMap" viewBox="0 0 400 330" role="group" aria-label="Top-rear listener with two depth-aware draggable binaural speakers">',

 '<defs><radialGradient id="skBg" cx="50%" cy="42%" r="72%"><stop offset="0" stop-color="#203a49"/><stop offset=".52" stop-color="#112633"/><stop offset="1" stop-color="#07121b"/></radialGradient>',
 '<radialGradient id="skFloor" cx="50%" cy="55%" r="65%"><stop offset="0" stop-color="#64bac9" stop-opacity=".15"/><stop offset="1" stop-color="#46758b" stop-opacity="0"/></radialGradient>',
 '<radialGradient id="skShadow"><stop stop-color="#02070c" stop-opacity=".66"/><stop offset="1" stop-color="#02070c" stop-opacity="0"/></radialGradient>',
 '<linearGradient id="skRail" gradientUnits="userSpaceOnUse" x1="200" x2="200" y1="91" y2="257"><stop stop-color="#304852"/><stop offset=".50" stop-color="#435e69"/><stop offset="1" stop-color="#597881"/></linearGradient>',
 // A SINGLE painted ellipse. Only the far half of the alpha silhouette
 // subtracts from its coverage; the rear stroke is never drawn twice.
 '<clipPath id="skFarOcclusion" clipPathUnits="userSpaceOnUse"><rect width="400" height="174"/></clipPath>',
 '<filter id="skAlphaBlack" color-interpolation-filters="sRGB"><feColorMatrix type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 1 0"/></filter>',
 '<mask id="skRailOcclusion" maskUnits="userSpaceOnUse" maskContentUnits="userSpaceOnUse" x="0" y="0" width="400" height="330" style="mask-type:luminance"><rect width="400" height="330" fill="white"/><g clip-path="url(#skFarOcclusion)"><image href="assets/sonku-listener-top.png" x="103" y="90" width="194" height="202" preserveAspectRatio="xMidYMid meet" filter="url(#skAlphaBlack)"/></g></mask>',
 '<mask id="skFarSpeakerOcclusion" maskUnits="userSpaceOnUse" maskContentUnits="userSpaceOnUse" x="0" y="0" width="400" height="330" style="mask-type:luminance"><rect width="400" height="330" fill="white"/><image href="assets/sonku-listener-top.png" x="103" y="90" width="194" height="202" preserveAspectRatio="xMidYMid meet" filter="url(#skAlphaBlack)"/></mask>',
 '<linearGradient id="skCabinet" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#798791"/><stop offset=".23" stop-color="#37444d"/><stop offset="1" stop-color="#111a20"/></linearGradient>',
 '<linearGradient id="skBaffle" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#28323a"/><stop offset=".55" stop-color="#141e25"/><stop offset="1" stop-color="#0a1218"/></linearGradient>',
 '<radialGradient id="skCone" cx=".43" cy=".34" r=".69"><stop stop-color="#778791"/><stop offset=".25" stop-color="#4a5963"/><stop offset=".61" stop-color="#202b32"/><stop offset=".84" stop-color="#10191e"/><stop offset="1" stop-color="#4d606c"/></radialGradient>',
 '<radialGradient id="skDome" cx=".35" cy=".25" r=".75"><stop stop-color="#b0bec5"/><stop offset=".45" stop-color="#4c5c67"/><stop offset="1" stop-color="#172129"/></radialGradient>',
 '<linearGradient id="skGlass" x1="0" y1="0" x2=".72" y2="1"><stop stop-color="#e0f5ff" stop-opacity=".085"/><stop offset=".34" stop-color="#e0f5ff" stop-opacity=".018"/><stop offset=".35" stop-color="#e0f5ff" stop-opacity="0"/><stop offset="1" stop-color="#d8efff" stop-opacity=".015"/></linearGradient>',
 '</defs>',
 '<rect width="400" height="330" fill="url(#skBg)"/>',
 '<ellipse cx="200" cy="174" rx="148" ry="83" fill="url(#skFloor)"/>',
 '<path class="sk-axis" d="M200 79V270 M45 174H355" fill="none" stroke="#597d89" stroke-opacity=".21" stroke-width=".8" stroke-dasharray="2 9"/>',
 // FRONT is the far upper orbit, behind head from this top-rear camera.
 '<text class="sk-direction sk-front" x="200" y="55" text-anchor="middle">FRONT</text>',
 '<text class="sk-direction" x="200" y="317" text-anchor="middle">REAR</text>',
 '<text class="sk-direction" x="11" y="149">LEFT</text><text class="sk-direction" x="352" y="149">RIGHT</text>',
 // Rear/top-view translucent PNG: back of skull, protruding ears and shoulders.
 // Listener looks toward FRONT at top; native source azimuth is unchanged.
 '<ellipse class="sk-head-shadow" cx="200" cy="252" rx="101" ry="28" fill="url(#skShadow)"/>',
 '<image id="skListenerArt" class="sk-listener-art" href="assets/sonku-listener-top.png" x="103" y="90" width="194" height="202" preserveAspectRatio="xMidYMid meet" data-orientation="front-up-rear" aria-label="Rear/top-view listener facing front at top" pointer-events="none"/>',
 // One continuous stroke above the PNG, with only far silhouette occlusion.
 '<ellipse id="skOrbitRail" class="sk-orbit-rail" cx="200" cy="174" rx="148" ry="83" fill="none" stroke="url(#skRail)" stroke-width="1.15" mask="url(#skRailOcclusion)" pointer-events="none"/>',
 // Both cabinet layers paint ABOVE the rail. The far layer subtracts the
 // listener's full alpha silhouette, retaining actual head occlusion.
 '<g id="skFarSources" class="sk-far-sources" mask="url(#skFarSpeakerOcclusion)"></g>',
 // Near cabinets remain unmasked and above the listener's back.
 '<g id="skNearSources" class="sk-near-sources"></g>',
 '<rect class="sk-glass" x="0" y="0" width="400" height="330" fill="url(#skGlass)" pointer-events="none" aria-hidden="true"/>',
 // No circle markers: each emitter is a small raised studio monitor.
 // Offsets at coincident directions are display-only; native azimuth stays equal.
 '<g class="sk-src" data-index="0" tabindex="0" role="slider" aria-label="Left virtual source position" aria-valuemin="-180" aria-valuemax="180">',
 '<path class="sk-tether" d="" fill="none" stroke="#91dcef" stroke-width=".9" stroke-dasharray="2 2"/>',
 '<rect class="sk-speaker-shell" x="-13" y="-17" width="26" height="34" rx="3.3" fill="url(#skCabinet)"/>',
 '<path d="M9 -14L12 -16V14L9 16Z" fill="#0b1118" opacity=".85"/>',
 '<rect class="sk-speaker-inner" x="-10" y="-14" width="19" height="28" rx="2" fill="url(#skBaffle)"/>',
 '<circle cx="-.5" cy="-7.4" r="4.3" fill="#080e14" stroke="#4f626d" stroke-width=".65"/>',
 '<circle cx="-.5" cy="-7.4" r="2.65" fill="url(#skDome)"/>',
 '<circle cx="-.5" cy="5" r="8" fill="#050b10" stroke="#5b6b75" stroke-width=".65"/>',
 '<circle cx="-.5" cy="5" r="6.65" fill="url(#skCone)"/>',
 '<circle cx="-.5" cy="5" r="2.55" fill="url(#skDome)" stroke="#151f26" stroke-width=".5"/>',
 '<path d="M-8 12H-4" stroke="currentColor" stroke-width="1" stroke-linecap="round"/>',
 '<rect class="sk-speaker-shade" x="-13" y="-17" width="26" height="34" rx="3.3" fill="#03090e" opacity=".12" pointer-events="none"/>',
 '<text class="sk-speaker-label" x="0" y="-22" text-anchor="middle">L</text></g>',
 '<g class="sk-src sk-right" data-index="1" tabindex="0" role="slider" aria-label="Right virtual source position" aria-valuemin="-180" aria-valuemax="180">',
 '<path class="sk-tether" d="" fill="none" stroke="#91dcef" stroke-width=".9" stroke-dasharray="2 2"/>',
 '<rect class="sk-speaker-shell" x="-13" y="-17" width="26" height="34" rx="3.3" fill="url(#skCabinet)"/>',
 '<path d="M9 -14L12 -16V14L9 16Z" fill="#0b1118" opacity=".85"/>',
 '<rect class="sk-speaker-inner" x="-10" y="-14" width="19" height="28" rx="2" fill="url(#skBaffle)"/>',
 '<circle cx="-.5" cy="-7.4" r="4.3" fill="#080e14" stroke="#4f626d" stroke-width=".65"/>',
 '<circle cx="-.5" cy="-7.4" r="2.65" fill="url(#skDome)"/>',
 '<circle cx="-.5" cy="5" r="8" fill="#050b10" stroke="#5b6b75" stroke-width=".65"/>',
 '<circle cx="-.5" cy="5" r="6.65" fill="url(#skCone)"/>',
 '<circle cx="-.5" cy="5" r="2.55" fill="url(#skDome)" stroke="#151f26" stroke-width=".5"/>',
 '<path d="M-8 12H-4" stroke="currentColor" stroke-width="1" stroke-linecap="round"/>',
 '<rect class="sk-speaker-shade" x="-13" y="-17" width="26" height="34" rx="3.3" fill="#03090e" opacity=".12" pointer-events="none"/>',
 '<text class="sk-speaker-label" x="0" y="-22" text-anchor="middle">R</text></g>',
 '</svg></div><span class="sk-stage-caption sk-sr">HEADPHONES · HRIR 0° ELEVATION · 44.1 / 48 kHz</span>',

 '</div><aside class="sk-controls">',
 '<button type="button" id="skUpgrade" class="sk-upgrade" hidden title="Switch the saved legacy sound to the current engine. Audio will change only after native acceptance.">Use current spatial sound</button>',
 '<label class="sk-space-slider sk-yaw" id="skYawRow" for="skHeadYaw" hidden>HEAD YAW <output id="skYawValue">0°</output>',
 '<input id="skHeadYaw" type="range" min="-180" max="180" step="5" value="0"/></label>',
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
 '<label class="sk-space-slider" for="skDistance">DISTANCE <output id="skDistanceValue">1.6 m</output>',
 '<input id="skDistance" type="range" min="1.6" max="10" step="0.1" value="1.6"/></label>',
 '<label class="sk-space-slider" id="skElevationRow" for="skElevation" hidden>ELEVATION <output id="skElevationValue">0.0°</output>',
 '<input id="skElevation" type="range" min="-30" max="45" step="0.1" value="0" aria-label="Direct source elevation in degrees" title="Measured binaural source height · native experimental schema v3 only"/></label>',
 '<label class="sk-space-slider" id="skHrtfAlignRow" for="skHrtfAlign" hidden>EAR ALIGN <output id="skHrtfAlignValue">0.0°</output>',
 '<input id="skHrtfAlign" type="range" min="-30" max="20" step="0.1" value="0" aria-label="Calibrate headphone HRTF perceived elevation, not physical XYZ source height" title="Ear-level alignment: changes measured KEMAR spectral angle, not room speaker height"/></label>',
 '<label class="sk-space-slider" for="skReflections">REFLECTIONS <output id="skReflectionsValue">55%</output>',
 '<input id="skReflections" type="range" min="0" max="100" step="1" value="55" title="First-wall reflections level, 0 is dry"/></label>',
 '<label class="sk-space-slider" for="skAmbience">REVERB <output id="skAmbienceValue">30%</output>',
 '<input id="skAmbience" type="range" min="0" max="100" step="1" value="30"/></label>',
 '</div>',
 '</aside></div>',
 '<footer class="sk-status"><span id="skMode" class="sk-sr">VISUAL PREVIEW</span><span id="skHint" role="status">No browser audio</span></footer>'
].join("");
// One panel, one set of native controls. Four primary knobs form the LCD deck;
 // room and movement parameters occupy the right-hand column.
root.querySelector(".sk-stage").appendChild(root.querySelector(".sk-rotary-grid"));
root.querySelector(".sk-controls").prepend(root.querySelector(".sk-buttons"));
const $=id=>root.querySelector("#"+id);
// Cache fixed SVG node references; only their transforms change at 60fps.
const speakerNodes=[...root.querySelectorAll("#skMap .sk-src")];
const speakerTethers=speakerNodes.map(n=>n.querySelector(".sk-tether"));
const speakerShades=speakerNodes.map(n=>n.querySelector(".sk-speaker-shade"));
const depthLayers={far:$("skFarSources"),near:$("skNearSources")};
function paintSlider(el){
 const min=Number(el.min),max=Number(el.max),value=Number(el.value);
 const p=max>min?Math.max(0,Math.min(1,(value-min)/(max-min))):0;
 el.style.setProperty("--sk-fill",(p*100).toFixed(2)+"%");
}
root.addEventListener("input",e=>{
 if(e.target.matches(".sk-space-slider input[type=range]"))paintSlider(e.target);
});
const mount=()=>{if(!root.isConnected)document.body.append(root)};
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",mount,{once:true});else mount();
let current=null,accepted=defaults(),draft=defaults(),pending=null,latest=null,
 xyzCapable=false,p83Capable=false,p84Capable=false,p85Capable=false,p86Capable=false,p87Capable=false,p88Capable=false,p89Capable=false,p90Capable=false,p91p92Capable=false,p94Capable=false,p95Capable=false,p96Capable=false,p97Capable=false,p98Capable=false,p99Capable=false,musicSafeStereo=false,p8TransparentBass=false,p82MultibandRoom=false,
 readId="",readSentAt=0,seq=1,clock=null,frame=null,observed=0,observedAt=0,
 drag=null,dialDrag=null,rangeEdits=new Set(),gestureStart=new Map(),
 visualPhase=0,visualAt=0,liveTimer=null,lastLiveAt=0;
function syncPower(){
 const p=$("skPower");
 if(!current){
  p.dataset.fxState="unknown";p.disabled=true;
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
 const previous=root.dataset.fxState;
 p.dataset.fxState=state;
 p.disabled=state==="unknown"||state==="rack-off";
 // The native insert does not render PCM/advance azimuth while bypassed.
 // Freeze the LAST rendered pose; do not keep extrapolating an absent PCM
 // clock, and do not PLL-correct stale observed azimuth upon re-enable.
 // Avoid resetting visual phase on every 250ms status poll.
 if(previous!==state){
  visualAt=0;
  if(state!=="on")observedAt=0;
 }
 p.setAttribute("aria-pressed",String(state==="on"));
 p.title=state==="on"?"SONKU ON · click to bypass this insert":
    state==="off"?"SONKU BYPASSED · click to enable":
    state==="rack-off"?"FX RACK DISABLED · enable rack first":
    "SONKU STATE UNAVAILABLE · waiting for native readback";
 p.setAttribute("aria-label",p.title);
 root.dataset.fxState=state;

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
 // A saved P9.8 session may match the visible numbers but lacks P9.9
 // room coupling. Don't claim this P9.9 preset exists on that engine.
 const defaultOrbitOption=presetPicker.querySelector('option[value="orbitStage"]');
 if(defaultOrbitOption)defaultOrbitOption.disabled=!p99Capable;
 const factoryName=p91p92Capable?matchFactoryPreset(s,p99Capable):"CUSTOM";
 if(presetPicker.value!==factoryName)presetPicker.value=factoryName;

 // Latest native renderer is the normal product experience. Preserve any
 // restored mode exactly; offer an explicit upgrade only for legacy sessions
 // on a capable host. Never silently change saved audio or expose test menus.
 $("skUpgrade").hidden=!(p90Capable&&s.renderMode!==9);
 $("skUpgrade").disabled=Boolean(pending)||!context();
 $("skYawRow").hidden=!(p83Capable||p84Capable||p90Capable);
 if(!$("skYawRow").hidden){
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
 root.querySelector(".sk-stage-caption").textContent=
   p99Capable&&s.renderMode===9&&s.sceneVoicing>0?
    "VOCAL BODY + CENTER CLARITY · ROOM-COUPLED REFLECTIONS · VENUE LOCKED":
   p98Capable&&s.renderMode===9?
    "SIGNATURE / CINEMA / STUDIO VOICING · VENUE P9.7 PRESERVED":
    p97Capable&&s.renderMode===9?
    "IMMERSIVE STAGE · BASS IMPACT · CENTER PRESENCE · EARLY AIR":
    p96Capable&&s.renderMode===9?
    "IMMERSIVE STAGE · REINFORCED BASS + AIR · CINEMA / VENUE":
    p95Capable&&s.renderMode===9?
    "IMMERSIVE SPATIAL · MUSIC PRESENCE · WORLD BASS · CINEMA / VENUE":
    p94Capable&&s.renderMode===9?
    "LATEST SPATIAL · FRONT/REAR DEPTH · MUSIC FIDELITY · WORLD BASS":
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
 if(!rangeEdits.has("skAmbience"))
  $("skAmbience").value=String(Math.round(s.ambience*100));
 const reflections=$("skReflections");
 if(!rangeEdits.has("skReflections"))
  reflections.value=String(Math.round(s.roomAmount*100));
 reflections.disabled=s.spacePreset===0;
 $("skReflectionsValue").textContent=Math.round(s.roomAmount*100)+"%";
 $("skAmbience").disabled=s.spacePreset===0;
 $("skAmbienceValue").textContent=Math.round(s.ambience*100)+"%";
 for(const slider of root.querySelectorAll(".sk-space-slider input[type=range]"))paintSlider(slider);
 // Only source placement is animated; knobs and input ranges are NOT
 // redrawn at 60 Hz. The native audio callback remains the time authority.
 drawSpeakers();
 $("skMode").textContent=native()?(context()?
  (p99Capable&&s.renderMode===9&&s.sceneVoicing>0?
     "NATIVE IMMERSIVE · MUSICAL VOCAL DEPTH · REAL STAGE":
   p98Capable&&s.renderMode===9?"NATIVE IMMERSIVE · SCENE VOICING · VENUE LOCKED":
   p97Capable&&s.renderMode===9?"NATIVE IMMERSIVE · STAGE IMPACT + EXTERNAL AIR":
   p96Capable&&s.renderMode===9?"NATIVE IMMERSIVE · STAGE BASS + AIR":
   p95Capable&&s.renderMode===9?"NATIVE IMMERSIVE · MUSIC PRESENCE · WORLD BASS":
   p94Capable&&s.renderMode===9?"NATIVE IMMERSIVE SPATIAL · WORLD BASS":
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
 syncVisualFrame();
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
  if(g.parentNode!==layer){
   // Preserve the proven front/back painter ordering and map-owned pointer
   // capture. A focused monitor must remain keyboard-accessible at crossover.
   const focused=document.activeElement===g;
   layer.appendChild(g);
   if(focused)g.focus({preventScroll:true});
  }
  const scale=(.96-.20*depth).toFixed(3); // far .76, side .96, near 1.16
  g.setAttribute("transform","translate("+visible.x.toFixed(2)+","+visible.y.toFixed(2)+") scale("+scale+")");
  // Real speaker cabinets are opaque. Continuous shading gives depth
  // without leaking the orbit rail through the cabinet.
  speakerShades[source].setAttribute("opacity",(.12+.10*depth).toFixed(3));
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
function canAnimate(){
 return !root.hidden && !document.hidden && !!current &&
  draft.moving && !drag && root.dataset.fxState==="on";
}
function syncVisualFrame(){
 if(!canAnimate()){
  if(frame!==null)cancelAnimationFrame(frame);
  frame=null;visualAt=0;
 }else if(frame===null){
  visualAt=0;frame=requestAnimationFrame(animate);
 }
}
function animate(time){
 frame=null;
 if(!canAnimate()){visualAt=0;return;}
 if(visualAt){
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
document.addEventListener("visibilitychange",syncVisualFrame);
function hint(s){
 $("skHint").textContent=s;
 // Keep accepted/pending diagnostics accessible without a noisy footer.
 // Real failures and the browser no-audio warning remain visibly distinct.
 root.dataset.attention=String(/REJECTED|NOT APPLIED|NOT ACCEPTED|TIMEOUT|UNAVAILABLE|NO AUDIO|NO DSP/i.test(s));
}
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
 (!p88Capable || a.hrtfElevationTrimDegrees===b.hrtfElevationTrimDegrees) &&
 (!p98Capable || a.sceneVoicing===b.sceneVoicing) &&
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
   r.schemaVersion===5 || r.schemaVersion===6 || r.schemaVersion===7 || r.schemaVersion===8 || r.schemaVersion===9 || r.schemaVersion===10 || r.schemaVersion===11 || r.schemaVersion===12) &&
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
 p90Capable=(r.schemaVersion===11||r.schemaVersion===12) && r.p90TrueExternalSpeaker===true &&
    Number.isFinite(incoming.hrtfElevationTrimDegrees);
 // Separate from schema v11: old P9.0 DSP must not receive the P9.1/9.2 label.
 p91p92Capable=p90Capable && r.p91p92MusicBassFoundation===true;
 // Independent of schema version: only this exact measured native DSP
 // can claim the *experimental* depth calibration, not P9.1 baseline.
 p94Capable=p91p92Capable && r.p94FrontBackDepthCue===true;
 p95Capable=p91p92Capable && r.p95MusicalPresence===true;
 p96Capable=p95Capable && r.p96DistanceReinforcement===true;
 p97Capable=p96Capable && r.p97StageImpact===true;
 p98Capable=p97Capable && r.schemaVersion===12 &&
   r.p98SceneVoicing===true && Number.isInteger(incoming.sceneVoicing) &&
   incoming.sceneVoicing>=0 && incoming.sceneVoicing<=3;
 // Schema v12 remains unchanged. Truthfully require the native DSP flag.
 p99Capable=p98Capable && r.p99MusicalVocalDepth===true;
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
  // Disabled PCM has no running phase: never oscillate around stale native
  // readbacks or repeatedly restart a non-existent bypassed audio orbit.
  if(!observedAt)visualPhase=angle(r.observedAzimuthDegrees);
  observed=r.observedAzimuthDegrees;observedAt=performance.now();
 }
 render();
});
let windowGesture=null;
const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
function constrainWindow(){
 if(root.hidden||window.__ARMIX_NATIVE_INLINE_FX_CLIP__)return;
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
  syncPower();syncVisualFrame();
 }
});
$("skPreset").addEventListener("change",()=>{
 const picker=$("skPreset");
 const preset=factoryPresets[picker.value];
 if(!p91p92Capable||draft.renderMode!==9||!preset||pending||
    (picker.value==="orbitStage"&&!p99Capable)){
  render();return;
 }
 const scene=fitElevation({...draft,...preset,renderMode:9,
  musicCompatibleLowFrequency:false});
 // P9.7/v11 native MUST NOT accept speculative v12 voicing fields.
 // Venue's acoustic numbers and stage ID=0 are identical to the checkpoint.
 if(!p98Capable)delete scene.sceneVoicing;
 if(!send(scene))render();
});
$("skClose").addEventListener("click",()=>{root.hidden=true;current=null;xyzCapable=false;p85Capable=false;p86Capable=false;p87Capable=false;p88Capable=false;p89Capable=false;p90Capable=false;p91p92Capable=false;p94Capable=false;p95Capable=false;p96Capable=false;p97Capable=false;p98Capable=false;p99Capable=false;musicSafeStereo=false;p8TransparentBass=false;p82MultibandRoom=false;
 cancelLiveTimer();pending=null;latest=null;readId="";readSentAt=0;dialDrag=null;
 drag=null;windowGesture=null;rangeEdits.clear();gestureStart.clear();lastLiveAt=0;
 if(clock!==null){clearInterval(clock);clock=null;}
 if(frame!==null){cancelAnimationFrame(frame);frame=null;}});

$("skUpgrade").addEventListener("click",()=>{
 if(!p90Capable||draft.renderMode===9||pending||!context())return;
 const scene={...draft,renderMode:9,musicCompatibleLowFrequency:false};
 const legacy=draft.renderMode===0 && draft.spacePreset===0 &&
   draft.startAzimuthDegrees===0 && draft.moving===true &&
   Math.abs(draft.stereoSpreadDegrees-60)<.001 &&
   Math.abs(draft.outputTrim-.75)<.001 &&
   Math.abs(draft.roomAmount-.55)<.001 && Math.abs(draft.ambience-.30)<.001 &&
   Math.abs(draft.sourceDistanceMeters-1.6)<.001;
 if(legacy)Object.assign(scene,{moving:false,stereoSpreadDegrees:82,
   outputTrim:1,spacePreset:3,sourceDistanceMeters:1.6,roomAmount:.78,ambience:.33});
 if(!send(scene))render();
});
$("skHeadYaw").addEventListener("change",()=>{
 if(!(p83Capable||p84Capable||p90Capable))return;
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
 // SVG user-space is authoritative despite CSS borders/DPI/letterboxing.
 const matrix=map.getScreenCTM();
 if(!matrix)return null;
 const p=map.createSVGPoint();
 p.x=e.clientX;p.y=e.clientY;
 const local=p.matrixTransform(matrix.inverse());
 return Number.isFinite(local.x)&&Number.isFinite(local.y)?
  stageAzimuth(local.x,local.y):null;
}
map.addEventListener("pointerdown",e=>{
 const g=e.target.closest(".sk-src");if(!g)return;
 drag={source:Number(g.dataset.index),pointer:e.pointerId,
       starting:{...draft}};
 syncVisualFrame();
 map.setPointerCapture?.(e.pointerId);e.preventDefault();
});
map.addEventListener("pointermove",e=>{
 if(!drag||drag.pointer!==e.pointerId)return;
 const azimuth=toAzimuth(e);
 if(azimuth===null)return;
 const value=azimuth-(drag.source?1:-1)*draft.stereoSpreadDegrees/2;
 liveInput({...draft,startAzimuthDegrees:Math.max(-360,Math.min(360,value)),
  moving:false});
});
map.addEventListener("pointerup",e=>{
 if(!drag||drag.pointer!==e.pointerId)return;
 drag=null;map.releasePointerCapture?.(e.pointerId);
 commitInput({...draft});syncVisualFrame();
});
map.addEventListener("pointercancel",e=>{
 if(!drag||drag.pointer!==e.pointerId)return;
 const prior=drag.starting;drag=null;
 commitInput(prior);syncVisualFrame();
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
   p89Capable=p90Capable=p91p92Capable=p94Capable=p95Capable=p96Capable=p97Capable=p98Capable=p99Capable=false;
  musicSafeStereo=p8TransparentBass=p82MultibandRoom=false;
  current={track,slot,instanceId:null};
  const s=snapshot(),id=window.__ARMIX_NATIVE_M2__?.nativeIdFor?.(track);
  const fx=s?.graph?.channels?.find(x=>x.id===id)?.inserts?.[slot];
  if(native()&&(s?.admitted||s?.logical)&&fx?.catalogId===ID)current.instanceId=fx.id;
  $("skChannel").textContent=String(track.name)+" · FX "+(slot+1);
  root.dataset.fxTrackId=String(track.id);root.dataset.fxSlot=String(slot);
  root.hidden=false;
  constrainWindow();syncPower();
  hint(native()?"READING ACCEPTED DSP":"BROWSER VISUAL ONLY · NO AUDIO");
  if(native())requestSnapshot();
  if(clock!==null)clearInterval(clock);
  clock=setInterval(()=>{if(root.hidden)return;
    syncPower();syncVisualFrame();if(native()&&!pending&&context())requestSnapshot();},250);
  if(frame!==null){cancelAnimationFrame(frame);frame=null;}
  render();
 },
 close(){$("skClose").click();}
};
window.ArSonMixNativeSonKu=Object.freeze(api);
})();
