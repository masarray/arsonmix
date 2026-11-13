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
// Original deterministic material detail: inset fasteners and visible brushed
// metal reflections are geometry, not separate interactive controls.
const screw=(x,y,cx,cy,r=6)=>{
  const dx=x-cx,dy=y-cy,d=Math.hypot(dx,dy);
  if(d>r+1)return null;
  if(d>r-.7)return grey(24);
  if(d>r-2.3)return metal(119+26*(-dy+dx*.35)/r,noise(x,y,185,5));
  const v=80+27*(-dy+dx*.35)/r+noise(x,y,184,6);
  if(Math.abs(dy-dx*.3)<.85&&d<r*.64)return grey(20);
  return metal(v);
};
const weave=(x,y)=>Math.sin((x+y)*1.83)*Math.sin((x-y)*1.88)*4.5
                     +Math.sin(x*2.17)*Math.cos(y*2.11)*1.8;
const micro=(x,y,seed)=>noise(x,y,seed,14)+
                      .7*noise(Math.floor(x/3),Math.floor(y/3),seed+7,11);
const pixel=(spec,x,y)=>{
  const [,w,h,kind]=spec, X=x/w,Y=y/h;
  // Every ON/OFF pair shares the SAME material grain and immovable shell.
  const family=kind.replace(/(?:On|Off)$/,"");
  const n=noise(x,y,family.length*71,8);
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
  if(kind==="amp"){
    const e=sdBox(x,y,8,8,w-9,h-10,24);
    if(e>0)return [0,0,0,0]; // immutable asset silhouette / alpha bounds
    const fasteners=[[37,36],[683,36],[37,264],[683,264]];
    for(const [cx,cy] of fasteners){
      const bolt=screw(x,y,cx,cy,5);
      if(bolt)return bolt;
    }
    if(e>-5) {
      const band=40+19*(1-Y)+micro(x,y,102)*.4;
      return grey(band+Math.max(0,4+e)*4);
    }
    const seam=sdBox(x,y,17,17,w-18,h-18,17);
    if(seam>-3)return grey(19+13*(1-Y)+micro(x,y,103)*.23);
    const tolex=micro(x,y,107)*.55;
    // Grille fabric is tightly woven and subtly directional. The inset grille
    // is a real textile surface, not the full hardware skin framed by gold.
    if(y<177){
      if(y>165){
        const lip=90+12*(1-Y)+4*Math.sin(y*.8);
        return rgb(lip+5,lip+2,lip-5,micro(x,y,108)*.2);
      }
      if(y<34) return rgb(24+8*(1-Y),25+8*(1-Y),26+8*(1-Y),tolex);
      const highlight=9*Math.max(0,1-Math.abs(y-62)/80);
      const grille=26+weave(x,y)*.79+highlight+13*(1-Y)-
                   Math.min(12,Math.abs(x-w*.5)*.029);
      // A low-profile metallic identification badge, not a false control.
      if(x>303&&x<417&&y>77&&y<113){
        const badge=sdBox(x,y,304,78,416,112,5);
        if(badge>-2)return metal(114+micro(x,y,109)*.4);
        return grey(42+6*(1-Y)+micro(x,y,110)*.2);
      }
      return rgb(grille+1,grille+2,grille+3,tolex*.44);
    }
    if(y<186)return metal(28+4*Math.sin(x*.27)+tolex*.15);
    if(y>272)return grey(30+17*(1-Y)+micro(x,y,111)*.38);
    // Steel front fascia uses anisotropic brushing, clean corner highlights
    // and carefully dimensioned empty holes; standalone JUCE knobs overlay.
    for(const [cx,cy] of mounts.amp.knobCenters){
      const hole=socket(x,y,cx,cy,26);
      if(hole)return hole;
    }
    const brushed=2.5*Math.sin(y*2.6)+micro(x,y,112)*.42;
    const longSheen=22*Math.exp(-Math.pow((y-207)/25,2));
    const local=8*(1-X)+9*(1-Y)+4*Math.sin((x+48)*.011);
    const face=89+brushed+longSheen+local;
    return rgb(face+11,face+8,face+3);
  }
  if(kind==="cab"){
    const e=sdBox(x,y,7,9,w-8,h-11,19);
    if(e>0)return [0,0,0,0];
    if(e>-6)return grey(18+17*(1-Y)+micro(x,y,167)*.55);
    const seam=sdBox(x,y,25,27,w-26,h-28,9);
    if(seam>-4)return grey(49+10*(1-Y)+micro(x,y,168)*.35);
    // Four restrained fasteners and inset cloth: no broad decorative frame.
    for(const [cx,cy] of [[39,41],[681,41],[39,257],[681,257]]){
       const bolt=screw(x,y,cx,cy,4);
       if(bolt)return bolt;
    }
    // Twin 12-inch black loudspeaker cones, concentric surround and dust caps
    // viewed through woven speaker grille: several non-flat radial surfaces.
    let cone=0;
    for(const cx of [w*.30,w*.70]){
      const dx=x-cx,dy=y-h*.53;
      const d=Math.hypot(dx,dy)/110;
      const angle=Math.atan2(dy,dx);
      if(d<1.055){
        const ripple=Math.sin(d*56+angle*9)*1.8;
        if(d>.96) cone-=12+ripple;
        else if(d>.84) cone+=10-3*Math.cos(d*24);
        else if(d>.47) cone-=22+15*(.84-d)+ripple;
        else if(d>.20) cone-=29+4*Math.sin(d*17);
        else cone-=15+6*(1-d/.2);
      }
    }
    const cloth=weave(x,y)*1.17;
    const lighting=13*(1-Y)+19*shadow(x,y,w,h);
    const vignette=8*Math.pow(Math.abs(X-.48),1.4);
    const v=56+lighting+cone+cloth-vignette+micro(x,y,173)*.24;
    // Understated embossed make plate; cabinet speaker texture stays visible.
    if(x>594&&x<663&&y>242&&y<262) {
       const plate=sdBox(x,y,594,242,663,262,3);
       if(plate>-1.5)return metal(81+micro(x,y,174)*.2);
       return grey(34+8*(1-Y));
    }
    return rgb(v,v+.6,v+1.4);
  }
  if(kind==="gate"||kind==="drive"){
    const gate=kind==="gate";
    // Physically distinct powder-coated cast gate vs sloped anodised drive.
    const r=gate?24:39;
    const edge=gate?sdBox(x,y,19,9,w-20,h-10,r):
        sdBox(x,y,10,20,w-11,h-19,r);
    if(edge>0)return [0,0,0,0];
    // Drive enclosure has angled side walls above its lower casting, unlike
    // the narrow round-rectangle gate. The lower extremal alpha is unchanged.
    const taper=gate?0:Math.max(0,(300-y)/300*18);
    if(!gate&&(x<10+taper||x>289-taper))return [0,0,0,0];
    const grain=micro(x,y,gate?211:313);
    const glint=25*shadow(x,y,w,h);
    if(edge>-5){
       const light=29+18*(1-Y)+5*(-edge)/5;
       return gate?rgb(light,light+15,light+18,grain*.48):
                   rgb(light+38,light+8,light-4,grain*.50);
    }
    const inset=gate?sdBox(x,y,26,16,w-27,h-17,r-7):
                       sdBox(x,y,18,28,w-19,h-27,r-9);
    if(inset>-3)return gate?
        rgb(64+9*(1-Y),89+11*(1-Y),92+14*(1-Y),grain*.36):
        rgb(146+10*(1-Y),83+13*(1-Y),50+13*(1-Y),grain*.42);
    // High-contrast flush metal corner fasteners are noninteractive chassis.
    for(const [cx,cy] of gate?
       [[47,48],[253,48],[47,385],[253,385]]:
       [[43,62],[257,62],[43,384],[257,384]]) {
      const bolt=screw(x,y,cx,cy,7);
      if(bolt)return bolt;
    }
    const light=13*(1-Y)+glint;
    const subtleWave=2.2*Math.sin(y*.43)+2.5*Math.sin(x*.042);
    let base=gate?
       rgb(34+light,75+light,80+light,grain*.58+subtleWave):
       rgb(126+light,67+light*.8,40+light*.6,
           grain*.54+2*Math.sin(y*1.4));
    if(gate){
      // One engraved service stripe, not an enclosing frame.
      if(x>39&&x<261&&y>h*.595&&y<h*.606)
         base=rgb(32+light*.5,56+light*.5,60+light*.5,grain*.3);
      if(x>42&&x<258&&y>31&&y<37)
         base=rgb(68+light,109+light,113+light,grain*.27);
    }else{
      // Industrial side casting with recessed equator and dark satin lower
      // casting. No false "knobs" or footswitch baked into the body.
      if(y>h*.56&&y<h*.575)return rgb(78+light*.5,43+light*.4,29+light*.3,grain*.31);
      if(y>h*.60)base=rgb(113+light,61+light*.8,38+light*.7,grain*.57);
      if(x<31||x>w-31)base=rgb(101+light,51+light*.6,30+light*.4,grain*.38);
    }
    const mount=mounts[kind];
    // Leave physically measured *empty* mounting recesses; JUCE owns the
    // independent PNG knobs, footswitch and live LED signal color.
    for(const [cx,cy] of mount.knobCenters){
      const hole=socket(x,y,cx,cy,27);
      if(hole)return hole;
    }
    const foot=socket(x,y,mount.footswitch[0],mount.footswitch[1],30);
    if(foot)return foot;
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
let changed=0;
for(const spec of SKINS){
  const file=resolve(output,spec[0]),bytes=render(spec);
  let previous=null;
  try {previous=await readFile(file);}
  catch(error){if(error?.code!=="ENOENT")throw error;}
  if(previous?.equals(bytes))continue; // preserve mtime: native incremental rebuild
  await writeFile(file,bytes); changed++;
}
console.log("Layered Guitar FX PNGs:",SKINS.map(s=>s[0]).join(", "),
            "changed:",changed);
