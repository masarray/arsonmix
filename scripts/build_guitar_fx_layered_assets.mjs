// SPDX-License-Identifier: AGPL-3.0-only
// Production-only layered Guitar FX RGBA PNGs. Native JUCE owns all
// controls, hit targets, state and PCM ACK; this is decorative bitmap art.
// Deterministic, dependency-free, and small (zlib-compressed).
import { deflateSync } from "node:zlib";
import { mkdir, writeFile } from "node:fs/promises";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const output = resolve(dirname(fileURLToPath(import.meta.url)),
  "../design-references/guitar-fx/assets");
export const SKINS = Object.freeze([
  ["gfx-amp-head.png",720,300,"amp"],["gfx-cabinet.png",720,300,"cab"],
  ["gfx-pedal-gate-body.png",300,420,"gate"],
  ["gfx-pedal-drive-body.png",300,420,"drive"],
  ["gfx-knob.png",120,120,"knob"],
  ["gfx-switch-off.png",200,74,"switchOff"],
  ["gfx-switch-on.png",200,74,"switchOn"],
  ["gfx-button-off.png",280,80,"buttonOff"],
  ["gfx-button-on.png",280,80,"buttonOn"],
  ["gfx-panel.png",800,330,"panel"]
]);
const clamp = x => Math.max(0,Math.min(255,Math.round(x)));
const sdBox=(x,y,l,t,r,b,rad)=>{
  const cx=Math.max(l+rad,Math.min(r-rad,x));
  const cy=Math.max(t+rad,Math.min(b-rad,y));
  return Math.hypot(x-cx,y-cy)-rad;
};
const hash=(x,y,s=7)=>{
  let n=Math.imul(x+27,374761393)^Math.imul(y+43,668265263)^Math.imul(s,2246822519);
  n=Math.imul(n^(n>>>13),1274126177);return ((n^(n>>>16))>>>0)/4294967295;
};
const mix=(a,b,t)=>a.map((v,i)=>clamp(v*(1-t)+b[i]*t));
const gold=[190,128,58],silver=[166,158,149],dark=[28,30,34];
const pixel=(spec,x,y)=>{
  const [name,w,h,kind]=spec, X=x/w,Y=y/h,n=hash(x,y,kind.length),grain=(n-.5)*11;
  const gray=v=>[clamp(v+grain),clamp(v+grain*0.9),clamp(v+grain*0.8),255];
  const metal=(v,g=0)=>[clamp(v+g+grain),clamp(v+g*.67+grain),clamp(v+g*.24+grain),255];
  const alpha=(v,a)=>[v[0],v[1],v[2],clamp(a)];
  const dist=(px,py)=>Math.hypot(x-px,y-py);
  if(kind==="knob"){
    const d=Math.hypot(X-.5,Y-.5),a=Math.atan2(Y-.5,X-.5);
    if(d>.478)return [0,0,0,0];
    if(d>.421)return metal(29+Math.abs(Math.sin(a*44))*19,12);
    if(d>.379)return metal(78+Math.cos(a*5)*10,73);
    if(d>.344)return metal(33+Math.sin(a*31)*10,14);
    const shine=Math.max(0,1-Math.hypot(X-.30,Y-.27)*1.4);
    const v=110+66*shine+Math.sin(a*32)*3+Math.cos(d*350)*3;
    if(Y<.39&&Y>.205&&Math.abs(X-.5)<.024)return [247,214,149,255];
    return metal(v,6);
  }
  if(kind==="switchOn"||kind==="switchOff"){
    // Same exact body geometry/alpha, independent of state. No flicker.
    const edge=sdBox(x,y,6,7,w-7,h-8,28);
    if(edge>0)return [0,0,0,0];
    if(edge> -3)return metal(31,20);
    const rim=sdBox(x,y,10,11,w-11,h-12,24);
    if(rim>-4)return metal(62,95);
    const well=sdBox(x,y,16,17,w-17,h-18,19);
    if(well>-2)return metal(27,20);
    const on=kind==="switchOn", thumbX=on?w-44:44;
    const d=dist(thumbX,h*.5);
    if(d<23){
      if(d>20)return metal(67,95);
      return metal(133+35*Math.max(0,1-d/23)+24*(.5-Y),20);
    }
    if(on)return [clamp(52+grain),clamp(37+grain),clamp(23+grain),255];
    return gray(24+3*Y);
  }
  if(kind==="buttonOn"||kind==="buttonOff"){
    // Both states share the same opaque silhouette and fixed canvas.
    const e=sdBox(x,y,4,7,w-5,h-8,21);
    if(e>0)return [0,0,0,0];
    if(e>-3)return metal(46,18);
    const rim=sdBox(x,y,9,12,w-10,h-13,17);
    if(rim>-3)return metal(63,94);
    if(kind==="buttonOn")return [clamp(66+grain+22*(1-Y)),clamp(45+grain+11*(1-Y)),clamp(28+grain),255];
    return gray(31+11*(1-Y));
  }
  if(kind==="panel"){
    const e=sdBox(x,y,3,4,w-4,h-5,23);
    if(e>0)return [0,0,0,0];
    if(e>-4)return metal(30,20);
    const rim=sdBox(x,y,11,12,w-12,h-13,18);
    if(rim>-3)return metal(53,86);
    const inner=sdBox(x,y,21,23,w-22,h-24,12);
    if(inner>-2)return gray(15+12*(1-Y));
    return gray(25+9*(1-Y)+7*(.5-Math.abs(X-.5)));
  }
  if(kind==="amp"||kind==="cab"){
    const edge=sdBox(x,y,5,9,w-6,h-9,33);
    if(edge>0)return [0,0,0,0];
    if(edge>-5)return gray(25+21*(1-Y));
    const rim=sdBox(x,y,15,18,w-16,h-19,26);
    if(rim>-4)return metal(54+18*(1-Y),91);
    const inn=sdBox(x,y,24,27,w-25,h-28,18);
    if(inn>-3)return gray(13);
    if(kind==="cab"){
      const weave=((x+y*3)%7<2||(x-y*3+10000)%7<2)?7:0;
      const speakers=[w*.28,w*.72];
      let shadow=0;
      for(const px of speakers)shadow=Math.max(shadow,16*Math.max(0,1-dist(px,h*.54)/(h*.40)));
      if(Y>.79)return metal(64+grain*2,60);
      return gray(22+weave+8*(1-Y)-shadow);
    }
    // Physical head has NO baked controls: overlay independent PNG knobs.
    if(Y<.53){
      const weave=(x+y*2)%8<2?4:0;
      return gray(23+weave+8*(1-Y));
    }
    if(Y<.55)return metal(52,110);
    if(Y>.9)return metal(48,55);
    return metal(98+18*Math.sin(y*.08)+20*(1-Y),24);
  }
  if(kind==="gate"||kind==="drive"){
    const e=sdBox(x,y,10,7,w-11,h-7,29);
    if(e>0)return [0,0,0,0];
    if(e>-5)return gray(19+16*(1-Y));
    const rim=sdBox(x,y,19,16,w-20,h-16,20);
    if(rim>-4)return metal(64,79);
    const inner=sdBox(x,y,27,25,w-28,h-25,14);
    if(inner>-2)return gray(17);
    const wells=kind==="gate"?[[.31,.28],[.69,.28],[.50,.46]]:[[.50,.32]];
    for(const [sx,sy] of wells){
      const d=Math.hypot((X-sx)*w,(Y-sy)*h);
      if(d<38){
        if(d>34)return metal(56,78);
        if(d>29)return metal(21,30);
        return gray(12);
      }
    }
    // Empty footswitch mounting well, no baked stomp switch or LED.
    const foot=Math.hypot((X-.5)*w,(Y-.82)*h);
    if(foot<45){
      if(foot>39)return metal(82,70);
      if(foot>33)return metal(33,12);
      return gray(15);
    }
    if(Y>.65){
      const strip=sdBox(x,y,27,h*.63,w-28,h*.93,12);
      if(strip>-5&&strip<0)return metal(57,70);
      return metal(96+15*Math.sin(y*.1),22);
    }
    // Painted pedal chassis and empty sockets only.
    return gray(34+10*(1-Y));
  }
  return [0,0,0,0];
};
const crcTable=Array.from({length:256},(_,i)=>{
 let c=i;for(let k=0;k<8;k++)c=(c&1)?0xedb88320^(c>>>1):c>>>1;return c>>>0;
});
const crc=b=>{let c=0xffffffff;for(const k of b)c=crcTable[(c^k)&255]^(c>>>8);return (c^0xffffffff)>>>0;};
const u32=n=>Buffer.from([(n>>>24)&255,(n>>>16)&255,(n>>>8)&255,n&255]);
const chunk=(name,data)=>{const n=Buffer.from(name,"ascii"),b=Buffer.concat([n,data]);return Buffer.concat([u32(data.length),b,u32(crc(b))]);};
export function render(spec){
 const [,w,h]=spec,row=w*4,scan=Buffer.alloc((row+1)*h);
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){
  const [r,g,b,a]=pixel(spec,x,y),k=y*(row+1)+1+x*4;
  scan[k]=r;scan[k+1]=g;scan[k+2]=b;scan[k+3]=a;
 }
 return Buffer.concat([
  Buffer.from([137,80,78,71,13,10,26,10]),
  chunk("IHDR",Buffer.concat([u32(w),u32(h),Buffer.from([8,6,0,0,0])])),
  chunk("IDAT",deflateSync(scan,{level:9})),
  chunk("IEND",Buffer.alloc(0))
 ]);
}
await mkdir(output,{recursive:true});
for(const spec of SKINS)await writeFile(resolve(output,spec[0]),render(spec));
console.log("Layered Guitar FX PNGs:",SKINS.map(s=>s[0]).join(", "));
