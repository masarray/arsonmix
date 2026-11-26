// SPDX-License-Identifier: AGPL-3.0-only
// Source-bound tests: exact browser geometry and native FX UI contract.
"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),vm=require("node:vm");
const source=fs.readFileSync("site/preview-v2/assets/mixer-native-sonku.js","utf8");
const css=fs.readFileSync("site/preview-v2/assets/mixer-fx-rack.css","utf8");
const between=(a,b)=>{
 const i=source.indexOf(a),j=source.indexOf(b,i+a.length);
 assert(i>=0&&j>i,"Production SonKu code boundary missing: "+a);
 return source.slice(i,j);
};
const geometry=between("const STAGE=Object.freeze","const root=document.createElement");
const g=vm.runInNewContext("(function(){const angle=(v)=>((v%360)+360)%360;"+
 geometry+"return {STAGE,stagePoint,stageAzimuth,displayPair};})()",{});
// The ellipse is the APPROVED oblique 3D visualization of a circular world
// orbit. A 360° spatial acoustic scene must not be confused with SVG shape.
assert.equal(g.STAGE.rx,148);
assert.equal(g.STAGE.ry,83);
for(const azimuth of [0,22.5,45,90,135,180,225,270,315,359.9,-90,-179.9]){
 const p=g.stagePoint(azimuth);
 assert(Math.abs(((p.x-200)/148)**2+((p.y-174)/83)**2-1)<1.e-8);
 const recovered=g.stageAzimuth(p.x,p.y);
 assert(Math.abs((((recovered-azimuth+540)%360)+360)%360-180)<1.e-7);
}
const same=g.displayPair([0,0]);
assert(Math.abs(same[0].anchor.x-same[1].anchor.x)<1.e-9);
assert(Math.hypot(same[0].visible.x-same[1].visible.x,
                  same[0].visible.y-same[1].visible.y)>=45);
assert.match(source,/<ellipse id="skOrbitRail"[^>]*cx="200" cy="174" rx="148" ry="83"/);
assert.equal((source.match(/id="skOrbitRail"/g)||[]).length,1);
assert.match(source,/<ellipse id="skOrbitRail"[^>]*stroke-width="1.15" mask="url\(#skRailOcclusion\)"/);
assert.doesNotMatch(source,/<use[^>]*href="#skOrbitRail"|skForegroundClip|skListenerCoverage/);
assert.match(source,/<g id="skFarSources"[^>]*mask="url\(#skFarSpeakerOcclusion\)"/);
assert(source.indexOf('<ellipse id="skOrbitRail"')<source.indexOf('<g id="skFarSources"'));
assert(source.indexOf('<g id="skFarSources"')<source.indexOf('<g id="skNearSources"'));
assert.doesNotMatch(source,/g\.style\.opacity=/);
assert.match(source,/speakerShades\[source\]\.setAttribute\("opacity",\(\.12\+\.10\*depth\)/);
assert.match(source,/<g clip-path="url\(#skFarOcclusion\)"><image[^>]*filter="url\(#skAlphaBlack\)"/);
assert.doesNotMatch(source,/id="skRenderMode"|class="sk-control-title"|M193 68 L200 60 L207 68/);
assert.match(source,/class="sk-glass"[^>]*pointer-events="none"/);
assert.match(css,/mixer-gain-knob-brushed-steel\.png/);
const material=css.slice(css.indexOf("/* 8D-SonKu"),css.indexOf("/* GEQ31 / ONE approved browser",css.indexOf("/* 8D-SonKu")));
assert.match(material,/background-image:linear-gradient\([^;]+url\("geq31-brushed-metal\.jpg"\)/);
assert.match(material,/\.sk-stage\{[^}]+background:transparent/);
assert.match(material,/\.sk-controls\{[^}]+background:transparent/);
assert.doesNotMatch(material,/\.sk-right[^}]*\{[^}]*color:/);
assert.equal((source.match(/class="sk-tether"[^>]*stroke="#91dcef"/g)||[]).length,2);
assert.match(material,/box-shadow:var\(--sk-button-raised\)/);
assert.match(material,/--sk-button-raised:[^;]*#141e2570/);
assert.doesNotMatch(material,/--sk-button-raised:[^;]*#fff/i);
assert.match(material,/position:relative;border:1px solid #202b32;border-bottom-color:#10191f/);
assert.match(material,/\[aria-pressed="true"\]\{[^}]*box-shadow:var\(--sk-button-latched\)/);
assert.match(material,/:active:not\(:disabled\)\{[^}]*transform:translateY\(1px\)[^}]*box-shadow:inset/);
assert.match(material,/linear-gradient\(to right,#528a98 0 var\(--sk-fill\),#05080b var\(--sk-fill\) 100%\)/);
assert.match(material,/::-webkit-slider-runnable-track\{[^}]*box-shadow:inset 0 1px 3px/);
assert.match(material,/::-moz-range-track\{[^}]*background:#05080b/);
assert.doesNotMatch(source,/skFarArc|skNearArc|sk-secondary-rail/);
assert.match(source,/map\.getScreenCTM\(\)/);
assert.match(source,/p\.matrixTransform\(matrix\.inverse\(\)\)/);
assert.doesNotMatch(css,/#sk(?:Far|Near)Sources\s+\.sk-src\s*\{[^}]*filter/);
// Exhaustive projected near-plane clearance from the existing cranial
// silhouette envelope (body/shoulder may naturally be behind foreground).
for(let phase=0;phase<360;++phase)for(let spread=0;spread<=180;spread+=5){
 const az=[phase-spread/2,phase+spread/2];
 const pair=g.displayPair(az);
 for(let i=0;i<2;++i){
  const cos=Math.cos(az[i]*Math.PI/180);
  if(cos>=0)continue; // far plane is correctly occluded by the PNG
  const scale=.96-.20*cos,p=pair[i].visible;
  const left=p.x-14*scale,right=p.x+14*scale;
  const top=p.y-18*scale,bottom=p.y+18*scale;
  // Conservative head + ears box in the unchanged SVG viewBox.
  assert(right<150||left>250||bottom<100||top>193,
    "Foreground speaker penetrates head envelope "+JSON.stringify({phase,spread,p}));
 }
}
const f=between("const factoryPresets=Object.freeze","const valid=s=>");
const p=vm.runInNewContext("(function(){"+f+
 "return {factoryPresets,matchFactoryPreset};})()",{});
// All five approved historic scenes remain present, and the sixth is
// genuinely gated to the P9.9 engine; P9.8 must never fake its existence.
assert.deepEqual(Object.keys(p.factoryPresets),
 ["orbitStage","music","studio","wide","deep","orbit"]);
for(const [id,scene] of Object.entries(p.factoryPresets)){
 assert.equal(scene.spatialMix,1);
 assert(scene.outputTrim>=1.5&&scene.outputTrim<=2.0); // User-selected powerful scene, bounded linked limiter.
 assert(scene.sourceDistanceMeters>=1.6);
 const max=[10,3.1,6.7,10][scene.spacePreset];
 assert(scene.sourceDistanceMeters<=max);
 assert(scene.stereoSpreadDegrees>=0&&scene.stereoSpreadDegrees<=180);
 assert(scene.roomAmount>=0&&scene.roomAmount<=1);
 assert(scene.ambience>=0&&scene.ambience<=1);
 if(id==="orbitStage"){
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9},true),id);
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9},false),"CUSTOM");
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9,sceneVoicing:0},true),"CUSTOM");
 }else{
  assert.equal(p.matchFactoryPreset({...scene,renderMode:9}),id);
 }
 assert.equal(p.matchFactoryPreset({...scene,renderMode:8},true),"CUSTOM");
}
const modified={...p.factoryPresets.music,renderMode:9,outputTrim:2.238721};
assert.equal(p.matchFactoryPreset(modified),"CUSTOM");
const sync=between("function syncPower(){","const context=()=>{");
const state={graph:{channels:[{id:"ch-1",fxRackBypassed:false,
 inserts:[{id:"insert",catalogId:"armix.builtin.8d-sonku.v1",bypassed:false}]}]}};
const element={dataset:{},disabled:true,
 setAttribute(k,v){this[k]=v}};
const root={dataset:{},querySelector(){return {textContent:""};}},current={track:{},slot:0};
const power=vm.runInNewContext("(function(){let current=__current;"+
 "let root=__root;const ID=\"armix.builtin.8d-sonku.v1\";let $=()=>__element;let snapshot=()=>__state;"+
 "let native=()=>true;"+sync+"return syncPower;})()",{
 __current:current,__root:root,__element:element,__state:state,
 window:{__ARMIX_NATIVE_M2__:{nativeIdFor(){return "ch-1";}}},
 document:{querySelector(){return {classList:{contains(){return false}}};}}
});
power();assert.equal(element.dataset.fxState,"on");assert.equal(element.disabled,false);
state.graph.channels[0].inserts[0].bypassed=true;power();
assert.equal(element.dataset.fxState,"off");assert.equal(element.disabled,false);
state.graph.channels[0].fxRackBypassed=true;power();
assert.equal(element.dataset.fxState,"rack-off");assert.equal(element.disabled,true);
state.graph.channels[0].fxRackBypassed=false;
state.graph.channels[0].inserts[0].catalogId="unrelated-fx";power();
assert.equal(element.dataset.fxState,"unknown");assert.equal(element.disabled,true);
assert.match(source,/consumeAction\?\.\(\s*current\.track,"slotPower"/);
assert.match(source,/root\.querySelector\("\.sk-stage"\)\.appendChild\(root\.querySelector\("\.sk-rotary-grid"\)\)/);
assert.match(source,/head\.setPointerCapture\(e\.pointerId\)/);
assert.match(source,/window\.addEventListener\("resize",constrainWindow\)/);
assert.match(source,/if\(!p91p92Capable\|\|draft\.renderMode!==9\|\|!preset\|\|pending\|\|/);
assert.match(source,/const ACK_DEADLINE_MS=3000/);
assert.match(source,/if\(!pending\|\|pending\.ack\|\|pending\.timedOut\)requestSnapshot\(\)/);
assert.match(source,/if\(!pending\.ack\|\|appliedRequestId!==pending\.requestId\|\|/);
assert.match(source,/if\(pending\?\.timedOut\)\{render\(\);return false;\}/);
assert.match(source,/if\(!recovered\)flushLatest\(\)/);
assert.match(source,/DSP TIMEOUT · WAITING FOR ACK/);
// Deterministic real publisher/readback test with controllable time. No
// parallel reimplementation, no browser sleep or scheduling lottery.
const publisher=between("const LIVE_INTERVAL_MS=40;","backend()?.addEventListener?.(\"arMixBuiltinSonKuQueue\"");
const readback=between("function acceptSettingsReadback(","backend()?.addEventListener?.(\"arMixBuiltinSonKuSnapshot\"");
const tx=vm.runInNewContext("(function(){"+
 "let now=100,seq=0,current={},root={hidden:false},accepted={gain:0},draft={gain:0},pending=null,latest=null,liveTimer=null,lastLiveAt=0,drag=null,dialDrag=null,rangeEdits=new Map(),gestureStart=new Map();"+
 "let xyzCapable=false,p88Capable=false,p98Capable=false,p83Capable=false,p84Capable=false;"+
 "const defaults=()=>({gain:0}),valid=()=>true,native=()=>true,render=()=>{},hint=()=>{},requestSnapshot=()=>{};"+
 "const ctx={channelId:'1',instanceId:'2',deviceEpoch:'3',graphRevision:'4'},context=()=>ctx;"+
 "let events=[],timer=null;const backend=()=>({emitEvent(_,v){events.push(v)}});"+
 "const performance={now:()=>now},setTimeout=f=>{timer=f;return 1},clearTimeout=()=>{timer=null};"+
 publisher+readback+
 "return {edit(v,t){now=t;liveInput({gain:v})},state(){return {accepted:accepted.gain,draft:draft.gain,latest:latest?.gain,events:events.length,pending:!!pending,timer:!!timer}},id(){return pending.requestId},ack(){pending.ack=true},timeout(){enterRecovery()},read(v,id,c=ctx){return acceptSettingsReadback({gain:v},id,c)},fire(t){now=t;const f=timer;timer=null;f()},events:()=>events,ctx};})()",{});
tx.edit(1,100);const first=tx.id();
tx.edit(2,110);tx.ack();assert(tx.read(1,first));
assert.equal(tx.state().draft,2);assert.equal(tx.state().events,1);
tx.fire(140);assert.equal(tx.events()[1].settings.gain,2);
const second=tx.id();
tx.edit(3,200); // coalesced pending
tx.edit(4,210); // newer throttled value supersedes coalesced value
tx.ack();assert(tx.read(2,second));assert.equal(tx.state().draft,4);
assert.equal(tx.state().events,2);tx.fire(240);
assert.equal(tx.events()[2].settings.gain,4);
const third=tx.id();tx.timeout();
assert.equal(tx.read(4,third),false);assert(tx.state().pending);
tx.ack();assert.equal(tx.read(4,third,{...tx.ctx,graphRevision:'5'}),false);
assert(tx.state().pending);assert(tx.read(4,third));assert.equal(tx.state().pending,false);
assert.equal(tx.state().events,3);

assert.match(source,/picker\.value==="orbitStage"&&!p99Capable/);
assert.match(source,/root\.dataset\.fxState==="on"/);
assert.match(source,/if\(previous!==state\)/);
assert.match(css,/\.mx-sonku \.sk-space-panel\{display:grid/);
assert.match(source,/current\.track\.slotBypass\[current\.slot\]/);
// Actual production draw function, not a duplicate geometry algorithm.
const draw=between("function drawSpeakers(){","// rAF is a VISUAL");
const paint=vm.runInNewContext("(function(){"+
 "let draft={moving:false,startAzimuthDegrees:0,stereoSpreadDegrees:127},visualPhase=0,lastSpeakerPhase=null,lastSpeakerSpread=null;"+
 "let writes=0,aria=0;const layer={appendChild(n){n.parentNode=this;writes++}};const depthLayers={near:layer,far:layer};"+
 "const document={activeElement:null};const angle=v=>((v%360)+360)%360;"+
 "function node(){return {parentNode:layer,dataset:new Proxy({},{set(t,k,v){writes++;t[k]=v;return true}}),style:{},attrs:{},setAttribute(k,v){writes++;if(k.startsWith('aria-'))aria++;this.attrs[k]=v},getAttribute(k){return this.attrs[k]}}};"+
 "const speakerNodes=[node(),node()],speakerShades=[node(),node()],speakerTethers=[node(),node()];"+
 "const displayPair=__pair;"+draw+
 "return {draw:drawSpeakers,counts:()=>[writes,aria],set(phase,spread){draft.startAzimuthDegrees=phase;draft.stereoSpreadDegrees=spread},reset(){lastSpeakerPhase=lastSpeakerSpread=null}}})()",{__pair:g.displayPair});
paint.draw();const firstPaint=paint.counts();
assert(firstPaint[0]>0);
for(let i=0;i<100;++i)paint.draw();
assert.deepEqual(paint.counts(),firstPaint,"Identical static snapshots must not mutate orbit");
paint.set(.01,127);paint.draw();const smallPaint=paint.counts();
assert(smallPaint[0]>firstPaint[0]);assert.equal(smallPaint[1],firstPaint[1]);
paint.set(90,127);paint.draw();assert(paint.counts()[1]>smallPaint[1]);
const beforeReset=paint.counts()[0];paint.reset();paint.draw();assert(paint.counts()[0]>beforeReset);
const scheduler=between("function canAnimate(){","function hint(s)");
const visual=vm.runInNewContext("(function(){"+
 "let root={hidden:false,dataset:{fxState:'on'}},current={},draft={moving:true,revolutionSeconds:24,clockwise:true},drag=null,frame=null,visualAt=0,visualPhase=0,observedAt=0,pending=null,observed=0;"+
 "let document={hidden:false,addEventListener(){}};let queue=new Map(),id=0,draws=0;"+
 "const angle=v=>((v%360)+360)%360;const drawSpeakers=()=>draws++;"+
 "const requestAnimationFrame=cb=>{queue.set(++id,cb);return id};const cancelAnimationFrame=n=>queue.delete(n);"+
 scheduler+
 "return {sync:syncVisualFrame,tick(t){const entry=queue.entries().next().value;if(entry){queue.delete(entry[0]);entry[1](t)}},state(){return [queue.size,draws,visualPhase,visualAt]},set(k,v){if(k==='power')root.dataset.fxState=v;else if(k==='moving')draft.moving=v;else if(k==='drag')drag=v;else if(k==='hidden')document.hidden=v;else if(k==='closed')root.hidden=v;syncVisualFrame()}}})()",{});
visual.sync();visual.sync();assert.equal(visual.state()[0],1);
visual.tick(100);visual.tick(116);assert.equal(visual.state()[1],1);
const phaseBefore=visual.state()[2];assert(phaseBefore>0);
for(const [k,stop,resume] of [["moving",false,true],["power","off","on"],["drag",{},null],["hidden",true,false],["closed",true,false]]){
 visual.set(k,stop);assert.equal(visual.state()[0],0);assert.equal(visual.state()[3],0);
 visual.set(k,resume);assert.equal(visual.state()[0],1);
 visual.tick(10000);assert.equal(visual.state()[2],phaseBefore);
}
assert.match(css,/:root:not\(\.armix-native-desktop\) \.mx-sonku \.sk-stage svg\{max-height:37vh\}/);
console.log("PASS SonKu P9.3: approved perspective orbit, inverse drag, native power readback, draggable window, five historic + gated new default Orbit presets, compact SPACE placement");
