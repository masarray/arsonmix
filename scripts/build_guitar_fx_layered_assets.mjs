// SPDX-License-Identifier: AGPL-3.0-only
// Production-only layered Guitar FX RGBA PNGs. Native JUCE owns all
// controls, hit targets, state and PCM ACK; this is decorative bitmap art.
// Deterministic, dependency-free, and small (zlib-compressed).
import { deflateSync } from "node:zlib";
import { mkdir, writeFile, readFile } from "node:fs/promises";
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
  ["gfx-panel.png",800,330,"panel"],
  ["gfx-stomp-off.png",110,110,"stompOff"],
  ["gfx-stomp-on.png",110,110,"stompOn"]
]);
// Geometry is a physical manufacturing contract, never painted-by-eye.
const manifest=JSON.parse(await readFile(resolve(output,"../hardware-layout.json"),"utf8"));
const mounts=Object.fromEntries(manifest.modules.map(m=>[m.key,m]));
const clamp=x=>Math.max(0,Math.min(255,Math.round(x)));
const sdBox=(x,y,l,t,r,b,rad)=>{
  const cx=Math.max(l+rad,Math.min(r-rad,x));
  const cy=Math.max(t+rad,Math.min(b-rad,y));
  return Math.hypot(x-cx,y-cy)-rad;
};
const hash=(x,y,s=7)=>{
  let n=Math.imul(x+27,374761393)^Math.imul(y+43,668265263)^Math.imul(s,2246822519);
  n=Math.imul(n^(n>>>13),1274126177);return ((n^(n>>>16))>>>0)/4294967295;
};
const noise=(x,y,seed,amplitude=8)=>(hash(x,y,seed)-.5)*amplitude;
const rgba=(r,g,b,a=255)=>[clamp(r),clamp(g),clamp(b),clamp(a)];
const grey=(v,g=0)=>rgba(v+g*.28,v+g*.55,v+g);
const rgb=(r,g,b,n=0)=>rgba(r+n,g+n,b+n);
const radial=(x,y,cx,cy)=>Math.hypot(x-cx,y-cy);
const sine=(t)=>Math.sin(t);
const metal=(light,n=0)=>rgb(light+8,light+11,light+12,n);
const aa=(d)=>clamp(.5-d)*255;
const shadow=(x,y,w,h)=>Math.max(0,1-Math.hypot(x/w-.32,y/h-.12)*1.12);
const bevel=(dist,base,shine=21)=>base+Math.max(0,Math.min(1,(-dist)/5))*shine;
const socket=(x,y,cx,cy,r=23)=>{
  const d=radial(x,y,cx,cy);
  if(d>r+4)return null;
  if(d>r)return metal(57+10*(cy-y)/r);
  if(d>r-4)return grey(47+8*sine(d*1.7));
  return grey(31+2*(cy-y)/r);
};
const pixel=(spec,x,y)=>{
  const [,w,h,kind]=spec, X=x/w,Y=y/h;
  const n=noise(x,y,kind.length*71,8);
  if(kind==="knob"){
    const d=radial(x,y,w/2,h/2),angle=Math.atan2(y-h/2,x-w/2),edge=w*.475;
    if(d>edge+1)return [0,0,0,0];
    if(d>edge-5) return rgba(21,23,26,aa(d-edge));
    if(d>w*.422)return metal(48+17*Math.abs(sine(angle*42)),n);
    if(d>w*.380)return metal(89+22*sine(angle+1.2),n);
    if(d>w*.340)return metal(39+5*sine(angle*48),n);
    // Satin dark graphite cap and a fixed independent 12-o'clock pointer.
    const hlight=shadow(x,y,w,h);
    if(Math.abs(x-w*.5)<w*.023 && y>h*.203 && y<h*.35)
       return rgb(226,231,225);
    return rgb(66+40*hlight+7*(1-Y),72+42*hlight,78+45*hlight,n*.5);
  }
  if(kind==="switchOn"||kind==="switchOff"){
    const e=sdBox(x,y,6,7,w-7,h-8,18);
    if(e>0)return [0,0,0,0];
    if(e>-2)return metal(53+n*.3);
    const inner=sdBox(x,y,10,11,w-11,h-12,15);
    if(inner>-3)return metal(85+12*(1-Y)+n*.6);
    const track=sdBox(x,y,17,18,w-18,h-19,10);
    if(track>-2)return grey(23+n*.4);
    const active=kind==="switchOn",thumbX=active?w-48:48;
    const thumb=sdBox(x,y,thumbX-27,20,thumbX+27,h-21,8);
    if(thumb<0){
      if(thumb>-3)return metal(108+n*.5);
      return metal(162+17*(.5-Y),n*.4);
    }
    return active?rgb(27,61,59,n*.5):grey(30+n*.5);
  }
  if(kind==="buttonOn"||kind==="buttonOff"){
    const e=sdBox(x,y,4,7,w-5,h-8,12);
    if(e>0)return [0,0,0,0];
    if(e>-2)return grey(59+n*.25);
    const inner=sdBox(x,y,8,11,w-9,h-12,9);
    if(inner>-2)return metal(64+n*.3);
    return kind==="buttonOn"?rgb(45,75,76,n*.4+3*(1-Y)):
           rgb(44,48,51,n*.4+4*(1-Y));
  }
  if(kind==="stompOn"||kind==="stompOff"){
    const d=radial(X,Y,.5,.5),a=Math.atan2(Y-.5,X-.5);
    if(d>.472)return [0,0,0,0];
    if(d>.424)return metal(27+Math.abs(sine(a*40))*13,n*.4);
    // Outer housing stays absolutely pixel-identical; state never moves.
    if(d>.32)return metal(71+19*(.5-Y)+Math.abs(sine(a*22))*5,n*.35);
    if(d>.277)return kind==="stompOn"?rgb(66,145,121,n*.3):
                                          grey(69+n*.3);
    if(d>.25)return metal(36,n*.4);
    return metal(133+24*shadow(x,y,w,h),n*.35);
  }
  if(kind==="panel"){
    // A deliberately subtle utility backdrop. New editor does NOT render it.
    const d=sdBox(x,y,3,3,w-4,h-4,9);
    return d>0?[0,0,0,0]:grey(26+9*(1-Y)+n*.4);
  }
  if(kind==="amp"){
    const e=sdBox(x,y,8,8,w-9,h-10,24);
    if(e>0)return [0,0,0,0];
    if(e>-5)return grey(42+17*(1-Y)+n*.45);
    const seam=sdBox(x,y,17,17,w-18,h-18,17);
    if(seam>-3)return grey(18+7*(1-Y)+n*.4);
    // Separate upper tolex grille and satin control fascia, not a generic panel.
    if(y<177){
      if(y>166)return rgb(93+6*(1-Y),86+7*(1-Y),75+7*(1-Y),n*.3);
      const mesh=3.5*sine((x+y)*.84)+3.5*sine((x-y)*.93);
      const rimshade=Math.min(16,Math.abs(x-w*.5)*.027);
      return rgb(22+mesh+18*(1-Y)-rimshade,23+mesh+18*(1-Y)-rimshade,
                 23+mesh+18*(1-Y)-rimshade,n*.5);
    }
    if(y<186)return metal(33+n*.3);
    if(y>272)return grey(28+14*(1-Y)+n*.4);
    const brushed=3*sine(y*2.4)+noise(x,y,49,4);
    const sheen=13*shadow(x,y,w,h);
    const face=94+sheen+brushed+10*(1-Y);
    for(const [cx,cy] of mounts.amp.knobCenters){
      const hole=socket(x,y,cx,cy,26);
      if(hole)return hole; // EMPTY mount: separate JUCE knob
    }
    return rgb(face+10,face+5,face-1,n*.25);
  }
  if(kind==="cab"){
    const e=sdBox(x,y,7,9,w-8,h-11,19);
    if(e>0)return [0,0,0,0];
    if(e>-6)return grey(18+13*(1-Y)+n*.55);
    const seam=sdBox(x,y,25,27,w-26,h-28,9);
    if(seam>-4)return grey(56+8*(1-Y)+n*.5);
    // Two plausible 12-inch speaker circles behind a real textile grille.
    const centres=[w*.30,w*.70],cy=h*.53;
    let cone=0;
    for(const cx of centres){
      const d=radial(x,y,cx,cy)/110;
      if(d<1.05){
        const rim=d>.93?-.05:0;
        const bowl=d<.45?-.26:(d<.79?-.48:.11);
        cone+=bowl+rim;
      }
    }
    const weave=3.2*sine((x+y)*2.3)+3.2*sine((x-y)*2.3);
    const lighting=8*(1-Y)+13*shadow(x,y,w,h);
    const v=52+lighting+cone*46+weave;
    return rgb(v,v+1,v+2,n*.55);
  }
  if(kind==="gate"||kind==="drive"){
    const gate=kind==="gate";
    // Different chassis tooling: slim powder-coated studio gate vs sloped,
    // warm oxidised-aluminium overdrive. Matching canvas != identical body.
    const r=gate?24:39;
    const edge=gate?sdBox(x,y,19,9,w-20,h-10,r):
        sdBox(x,y,10,20,w-11,h-19,r);
    if(edge>0)return [0,0,0,0];
    if(edge>-5)return gate?rgb(28,46,50,n*.8):rgb(73,40,29,n*.8);
    const inside=gate?sdBox(x,y,26,16,w-27,h-17,r-7):
                       sdBox(x,y,18,28,w-19,h-27,r-9);
    if(inside>-3)return gate?rgb(69,92,95,n*.8):rgb(141,78,48,n*.8);
    const glint=17*shadow(x,y,w,h);
    const powder=noise(x,y,gate?211:313,15);
    let base=gate?rgb(39+glint,76+glint,83+glint,powder*.55):
                   rgb(128+glint,69+glint*.65,37+glint*.4,powder*.62);
    // Distinct industrial shaping; no oversize holes or baked footswitch.
    if(!gate){
      if(y>h*.56 && y<h*.575)return rgb(88,47,31,n*.4);
      if(x<33||x>w-33)base=rgb(108+glint,57+glint,34,powder*.5);
    }else if(x>40&&x<w-40&&y>h*.595&&y<h*.608)
      base=rgb(32,56,61,n*.5);
    const mount=mounts[kind];
    for(const [cx,cy] of mount.knobCenters){
      const hole=socket(x,y,cx,cy,27);
      if(hole)return hole;
    }
    const foot=socket(x,y,mount.footswitch[0],mount.footswitch[1],30);
    if(foot)return foot;
    // Small LED recess, no state burned into chassis.
    const led=socket(x,y,mount.led[0],mount.led[1],10);
    if(led)return led;
    return base;
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
