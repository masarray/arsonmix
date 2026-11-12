// SPDX-License-Identifier: AGPL-3.0-only
// Original Guitar FX decorative PNGs: deterministic, dependency-free asset builder.
// Node: node scripts/build_guitar_fx_assets.mjs
// Uses indexed PNG to keep hardware textures small; labels, control values,
// hit targets and focus rings are always rendered as accessible HTML/CSS.
const GUITAR_ASSETS = [
  {name:"amp-head-class-a.png", w:920,h:340, kind:"amp",theme:"copper"},
  {name:"cabinet-dual-12.png", w:720,h:360,kind:"cab",theme:"copper"},
  {name:"pedal-gate.png",w:260,h:360,kind:"pedal",theme:"silver"},
  {name:"pedal-compressor.png",w:260,h:360,kind:"pedal",theme:"green"},
  {name:"pedal-overdrive.png",w:260,h:360,kind:"pedal",theme:"copper"},
  {name:"pedal-delay.png",w:260,h:360,kind:"pedal",theme:"blue"},
  {name:"pedal-modulation.png",w:260,h:360,kind:"pedal",theme:"green"},
  {name:"pedal-chorus.png",w:260,h:360,kind:"pedal",theme:"blue"},
  {name:"pedal-reverb.png",w:260,h:360,kind:"pedal",theme:"violet"},
  {name:"knob-brushed-metal.png",w:148,h:148,kind:"knob",theme:"copper"},
  {name:"led-active.png",w:48,h:48,kind:"ledOn",theme:"green"},
  {name:"led-bypassed.png",w:48,h:48,kind:"ledOff",theme:"copper"}
];
function fxClamp(v,a=0,b=63){ return Math.max(a,Math.min(b,Math.round(v))); }
function fxNoise(x,y,seed=0){
  let n=(Math.imul(x,374761393)^Math.imul(y,668265263)^Math.imul(seed+71,2246822519))|0;
  n=Math.imul(n^(n>>>13),1274126177); return ((n^(n>>>16))>>>0)/4294967295;
}
function fxInside(x,y,a,b,c,d,r){
  if(x<a||x>c||y<b||y>d)return false;
  const cx=Math.max(a+r,Math.min(c-r,x)),cy=Math.max(b+r,Math.min(d-r,y));
  return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=r*r;
}
function fxPalette(theme){
  const bytes=new Uint8Array(768);
  const themeRGB=theme==="blue"?[47,99,137]:theme==="green"?[56,103,77]:theme==="violet"?[104,76,124]:theme==="silver"?[102,110,119]:[143,85,46];
  const put=(i,r,g,b)=>{bytes[i*3]=fxClamp(r,0,255);bytes[i*3+1]=fxClamp(g,0,255);bytes[i*3+2]=fxClamp(b,0,255);};
  for(let j=0;j<64;j++){
    const v=j/63;
    put(j,j?11+57*v:0,j?12+54*v:0,j?15+53*v:0);
    put(64+j,54+156*v,54+146*v,55+130*v);
    put(128+j,64+177*v,42+135*v,19+76*v);
    put(192+j,themeRGB[0]*.38+themeRGB[0]*1.12*v,
      themeRGB[1]*.35+themeRGB[1]*1.23*v,themeRGB[2]*.4+themeRGB[2]*1.14*v);
  }
  return bytes;
}
function fxPixel(o,x,y){
  const {w,h,kind}=o; const n=fxNoise(x,y,kind.length);
  if(kind==="amp"){
    if(!fxInside(x,y,5,15,w-6,h-9,29))return 0;
    if(x>310&&x<610&&y<37){
      const hd=Math.min(Math.abs(y-18),9);
      return 64+fxClamp(18+(9-hd)*2+n*9);
    }
    const inner=fxInside(x,y,14,25,w-15,h-17,24);
    if(!inner)return 1+fxClamp(5+20*n);
    if(!fxInside(x,y,24,39,w-25,h-29,17))return 128+fxClamp(15+16*n);
    if(y<157){
      const stripe=(x+y)%7===0?4:0;
      return 1+fxClamp(19+n*16+stripe+5*Math.sin(y/21));
    }
    if(y<163)return 128+fxClamp(37+7*n);
    const face=fxInside(x,y,31,168,w-32,h-35,7);
    if(!face)return 128+fxClamp(18+16*n);
    const brushed=fxNoise(0,y,4),line=Math.sin(y*1.7)*3;
    const v=14+35*(1-(y-168)/(h-203))+brushed*6+n*4+line;
    if(y>h-50)return 128+fxClamp(12+n*8);
    const sx=(x<65||x>w-65)&&(y>180&&y<h-55);
    if(sx && ((x-49)**2+(y-187)**2<55 || (x-(w-49))**2+(y-187)**2<55))
      return 64+fxClamp(28+n*13);
    return 64+fxClamp(v);
  }
  if(kind==="cab"){
    if(!fxInside(x,y,5,8,w-6,h-9,28))return 0;
    if(!fxInside(x,y,15,18,w-16,h-19,20))return 128+fxClamp(12+n*17);
    if(!fxInside(x,y,31,34,w-32,h-35,8))return 1+fxClamp(24+n*13);
    const centers=[w*.29,w*.71], cy=h*.54;
    let speaker=0;
    for(const cx of centers){
      const d=Math.hypot(x-cx,y-cy);
      if(d<h*.34){
        speaker=Math.max(speaker,12*(1-d/(h*.34)));
        if(d>h*.285&&d<h*.32)speaker+=5;
      }
    }
    const cross=((x+y*3)%9<2 || (x-y*3+9999)%9<2)?5:0;
    return 1+fxClamp(19+n*14+cross-speaker);
  }
  if(kind==="pedal"){
    if(!fxInside(x,y,13,7,w-14,h-8,23))return 0;
    if(!fxInside(x,y,19,13,w-20,h-15,19))return 64+fxClamp(26+n*23);
    if(y<24||y>h-28)return 64+fxClamp(15+n*14);
    if(x<26||x>w-27)return 192+fxClamp(29+n*14);
    const shade=7+(x/w)*7+(1-y/h)*22+n*10;
    let body=192+fxClamp(shade);
    const fs=Math.hypot(x-w/2,y-h*.76);
    if(fs<31)body=64+fxClamp(44-fs*.4+n*8);
    if(fs<19)body=64+fxClamp(16+fs*.3+n*6);
    if(fs<9)body=64+fxClamp(41+n*9);
    const screw=((x-38)**2+(y-39)**2<32)||((x-(w-38))**2+(y-39)**2<32);
    if(screw)body=128+fxClamp(22+n*18);
    return body;
  }
  if(kind==="knob"){
    const cx=w/2,cy=h/2,d=Math.hypot(x-cx,y-cy);
    if(d>w*.47)return 0;
    const a=Math.atan2(y-cy,x-cx);
    if(d>w*.40)return 1+fxClamp(15+n*12);
    if(d>w*.34)return 128+fxClamp(19+21*Math.abs(Math.cos(a*19))+n*10);
    if(d>w*.29)return 64+fxClamp(27+n*19);
    const radial=Math.sin(a*37)*2,lighting=(cx-x)/w*25+(cy-y)/h*14;
    let v=30+lighting+radial+8*n;
    if(x>cx-3&&x<cx+3&&y<cy-10&&y>cy-w*.25)v=62;
    if(d<w*.05)v=22;
    return 64+fxClamp(v);
  }
  if(kind==="ledOn"||kind==="ledOff"){
    const d=Math.hypot(x-w/2,y-h/2);
    if(d>23)return 0;
    if(kind==="ledOff")return 1+fxClamp(11+8*n+(23-d)*.2);
    if(d>17)return 1+fxClamp(6+(23-d)*3+n*4);
    return 192+fxClamp(19+(17-d)*2+n*7);
  }
  return 0;
}
function fxCRC32(bytes){
  let c=0xffffffff;
  for(const b of bytes){c^=b;for(let k=0;k<8;k++)c=(c>>>1)^((c&1)?0xedb88320:0);}
  return (c^0xffffffff)>>>0;
}
function fxU32(v){return [(v>>>24)&255,(v>>>16)&255,(v>>>8)&255,v&255];}
function fxChunk(type,data){
  const t=Array.from(type).map(c=>c.charCodeAt(0)),part=Uint8Array.from([...t,...data]);
  return Uint8Array.from([...fxU32(data.length),...part,...fxU32(fxCRC32(part))]);
}
function fxPng(o){
  const {w,h}=o,raw=new Uint8Array((w+1)*h);
  for(let y=0;y<h;y++)for(let x=0;x<w;x++)raw[y*(w+1)+1+x]=fxPixel(o,x,y);
  const z=[0x78,0x01]; let a=1,b=0;
  for(let i=0;i<raw.length;i++){a=(a+raw[i])%65521;b=(b+a)%65521;}
  for(let start=0;start<raw.length;){
    const length=Math.min(65535,raw.length-start),end=start+length;
    z.push(end===raw.length?1:0,length&255,length>>>8,(~length)&255,((~length)>>>8)&255);
    for(let i=start;i<end;i++)z.push(raw[i]);start=end;
  }
  z.push(...fxU32(((b<<16)|a)>>>0));
  const ihdr=Uint8Array.from([...fxU32(w),...fxU32(h),8,3,0,0,0]);
  const pal=fxPalette(o.theme);
  const chunks=[fxChunk("IHDR",ihdr),fxChunk("PLTE",pal),fxChunk("tRNS",Uint8Array.from([0])),fxChunk("IDAT",Uint8Array.from(z)),fxChunk("IEND",new Uint8Array())];
  const size=8+chunks.reduce((v,x)=>v+x.length,0),png=new Uint8Array(size);
  png.set([137,80,78,71,13,10,26,10],0);let pos=8;
  for(const c of chunks){png.set(c,pos);pos+=c.length;}
  return png;
}
function fxBase64(bytes){
  const chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  let out="";
  for(let i=0;i<bytes.length;i+=3){
    const a=bytes[i],b=i+1<bytes.length?bytes[i+1]:0,c=i+2<bytes.length?bytes[i+2]:0;
    out+=chars[a>>>2]+chars[((a&3)<<4)|(b>>>4)]+(i+1<bytes.length?chars[((b&15)<<2)|(c>>>6)]:"=")+(i+2<bytes.length?chars[c&63]:"=");
  }
  return out;
}
function buildGuitarAssets(){return GUITAR_ASSETS.map(o=>({...o,bytes:fxPng(o)}));}

import {mkdir,writeFile} from "node:fs/promises";
import {fileURLToPath} from "node:url";
import {resolve,dirname} from "node:path";
const repoRoot=resolve(dirname(fileURLToPath(import.meta.url)),"..");
const output=resolve(repoRoot,"design-references/guitar-fx/assets");
await mkdir(output,{recursive:true});
for(const a of buildGuitarAssets())await writeFile(resolve(output,a.name),a.bytes);
console.log("Generated "+GUITAR_ASSETS.length+" original Guitar FX PNG assets in "+output);
