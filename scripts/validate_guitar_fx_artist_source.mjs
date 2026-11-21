// SPDX-License-Identifier: AGPL-3.0-only
// Validate the artist's independent PNG layers BEFORE embedding into JUCE.
// Never let mismatched ON/OFF frames or missing transparent channels ship.
import {readFileSync,existsSync} from "node:fs";
import {resolve} from "node:path";
import {inflateSync} from "node:zlib";
const folder=resolve(process.argv[2]||"design-references/guitar-fx/artist-source");
const sizes={
 "gfx-amp-head.png":[720,300],"gfx-cabinet.png":[720,300],
 "gfx-pedal-gate-body.png":[300,420],"gfx-pedal-drive-body.png":[300,420],
 "gfx-knob.png":[120,120],"gfx-switch-off.png":[200,74],
 "gfx-switch-on.png":[200,74],"gfx-button-off.png":[280,80],
 "gfx-button-on.png":[280,80],
 "gfx-stomp-off.png":[110,110],"gfx-stomp-on.png":[110,110]
};
function decode(name) {
 const p=resolve(folder,name);
 if(!existsSync(p))throw Error("Missing independent guitar skin: "+name);
 const b=readFileSync(p);
 if(!b.subarray(0,8).equals(Buffer.from([137,80,78,71,13,10,26,10])))
   throw Error(name+" is not a PNG");
 let x=8,dat=[],w=0,h=0;
 while(x+12<=b.length){
   const n=b.readUInt32BE(x),type=b.toString("ascii",x+4,x+8),v=b.subarray(x+8,x+8+n);
   if(type==="IHDR"){
     w=v.readUInt32BE(0);h=v.readUInt32BE(4);
     if(v[8]!==8||v[9]!==6||v[12]!==0)throw Error(name+" must use 8-bit noninterlaced RGBA");
   }
   if(type==="IDAT")dat.push(v);
   x+=n+12;if(type==="IEND")break;
 }
 const [ex,ey]=sizes[name];
 if(w!==ex||h!==ey)throw Error(name+" must be "+ex+"x"+ey+"; got "+w+"x"+h);
 const raw=inflateSync(Buffer.concat(dat)),stride=w*4;
 const rows=Buffer.alloc(stride*h),rowLen=stride+1;
 if(raw.length!==h*rowLen)throw Error(name+" malformed PNG scanlines");
 for(let y=0;y<h;y++){
   const filter=raw[y*rowLen],start=y*stride;
   for(let i=0;i<stride;i++){
     const a=i>=4?rows[start+i-4]:0;
     const up=y?rows[start-stride+i]:0;
     const ul=y&&i>=4?rows[start-stride+i-4]:0;
     const p=a+up-ul,pa=Math.abs(p-a),pb=Math.abs(p-up),pc=Math.abs(p-ul);
     const predict=pa<=pb&&pa<=pc?a:pb<=pc?up:ul;
     const add=[0,a,up,Math.floor((a+up)/2),predict][filter];
     if(add===undefined)throw Error(name+" invalid PNG row filter "+filter);
     rows[start+i]=(raw[y*rowLen+1+i]+add)&255;
   }
 }
 return {w,h,bytes:rows};
}
const images=new Map(Object.keys(sizes).map(n=>[n,decode(n)]));
for(const kind of ["switch","button","stomp"]){
 const a=images.get("gfx-"+kind+"-off.png"),b=images.get("gfx-"+kind+"-on.png");
 let visible=0,changed=0;
 const borderX=Math.max(3,Math.floor(a.w*.055));
 const borderY=Math.max(3,Math.floor(a.h*.09));
 for(let y=0;y<a.h;y++)for(let x=0;x<a.w;x++){
  const i=(y*a.w+x)*4;
  if(a.bytes[i+3]!==b.bytes[i+3])
    throw Error(kind+" ON/OFF transparency silhouette differs at "+x+","+y);
  if(a.bytes[i+3])visible++;
  const isOuter=x<borderX||x>=a.w-borderX||y<borderY||y>=a.h-borderY;
  for(let c=0;c<3;c++){
   if(isOuter&&a.bytes[i+c]!==b.bytes[i+c])
     throw Error(kind+" ON/OFF outer frame differs at "+x+","+y);
   if(a.bytes[i+c]!==b.bytes[i+c])changed++;
  }
 }
 if(visible===0||changed===0)throw Error(kind+" has blank/identical states");
}
console.log("Guitar FX artist source validated: 12 RGBA PNGs, stable frames and exact ON/OFF alpha");
