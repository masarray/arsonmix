// SPDX-License-Identifier: AGPL-3.0-only
// Validates the ACTUAL pure GUI geometry/serialization portion of SonKu
// without new DSP, mock visual rendering or third-party test dependencies.
"use strict";
const fs=require("node:fs");
const vm=require("node:vm");
const assert=require("node:assert/strict");
const source=fs.readFileSync("site/preview-v2/assets/mixer-native-sonku.js","utf8");
const start=source.indexOf("const defaults=");
const end=source.indexOf("const backend=()=>");
assert(start>=0&&end>start,"Expected independent actual native GUI geometry");
const api=vm.runInNewContext(source.slice(start,end)+
  "; ({defaults,roomHeights,elevationLimits,fitElevation,valid})",{
  Math,Number,Object
});
const mk=(preset,radial,elevation)=>{
 const s={...api.defaults(),spacePreset:preset,sourceDistanceMeters:radial};
 if(elevation!==undefined)s.directElevationDegrees=elevation;
 return s;
};
for(const room of [0,1,2,3]){
 const old=mk(room,1.6);
 assert(api.valid(old),"v2 legacy untouched");
 for(const elevation of [-25,0,25,45]){
  const candidate=mk(room,1.6,elevation);
  assert(api.valid(candidate),"measured source within room at "+room+" "+elevation);
  const limits=api.elevationLimits(candidate);
  assert(limits.min<=elevation&&elevation<=limits.max);
  const fit=api.fitElevation(candidate);
  assert.equal(fit.directElevationDegrees,elevation);
 }
}
for(const bad of [NaN,Infinity,-50,65]){
 assert(!api.valid(mk(1,1.6,bad)));
}
for(const [r,scene] of [[3.1,1],[6.7,2],[10,3]]){
 for(const e of [-25,25,45]){
  const original=mk(scene,r,e);
  const fitted=api.fitElevation(original);
  assert(api.valid(fitted),"fitted to physical "+scene+" "+r+" "+e);
  assert(fitted.directElevationDegrees>=api.elevationLimits(fitted).min);
  assert(fitted.directElevationDegrees<=api.elevationLimits(fitted).max);
  assert(fitted.directElevationDegrees===e||!api.valid(original));
 }
}
assert(!api.valid(mk(1,5,0)),"native room radial policy");
assert(!api.valid(mk(0,10.01,0)),"maximum radial admission");
assert(source.includes('r.schemaVersion===3'),"native explicit capability");
assert(source.includes('xyzCapable=measured'),"capability controlled by native snapshot");
assert(source.includes('r.appliedRequestId!==pending.requestId'),"audio ACK readback");
assert(source.includes('String(r.graphRevision)!==String(c.graphRevision)'),"reject stale revision");
assert(source.includes('id="skElevationRow" for="skElevation" hidden'),"v2 UI hidden");
assert(source.includes('elevationRow.hidden=!xyzCapable'),"v2 never shows research control");
assert(!source.includes("new AudioContext"),"no browser DSP authority");
console.log("PASS P7.3 real SonKu GUI geometry v2/v3 capability, safe room fit and audio-ACK UI contract");
