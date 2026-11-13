// SPDX-License-Identifier: AGPL-3.0-only
// Fast test: node --test tests/guitar_fx_assets_contract.test.mjs
import test from "node:test";
import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import {resolve} from "node:path";
import {fileURLToPath} from "node:url";
import {createHash} from "node:crypto";
import {execFileSync} from "node:child_process";
import {inflateSync} from "node:zlib";
const root=resolve(fileURLToPath(new URL("..",import.meta.url)));
const assetDir=resolve(root,"design-references/guitar-fx/assets");
const expected=[
 ["amp-head-class-a.png",920,340,400000],["cabinet-dual-12.png",720,360,340000],
 ["pedal-gate.png",260,360,130000],["pedal-compressor.png",260,360,130000],["pedal-overdrive.png",260,360,130000],["pedal-delay.png",260,360,130000],
 ["pedal-modulation.png",260,360,130000],["pedal-chorus.png",260,360,130000],["pedal-reverb.png",260,360,130000],["knob-brushed-metal.png",148,148,45000],
 ["led-active.png",48,48,10000],["led-bypassed.png",48,48,10000]
];
const sha256=b=>createHash("sha256").update(b).digest("hex");
function readPNG(path){
 const b=readFileSync(path),sig=Buffer.from([137,80,78,71,13,10,26,10]);
 assert(b.subarray(0,8).equals(sig),"valid PNG signature");
 const w=b.readUInt32BE(16),h=b.readUInt32BE(20);
 assert.equal(b[24],8,"8-bit depth");assert.equal(b[25],3,"indexed palette");
 let off=8,idat=[],alpha=false,palette=false,end=false;
 while(off+12<=b.length){
  const n=b.readUInt32BE(off),tag=b.toString("ascii",off+4,off+8);
  assert(off+n+12<=b.length,"PNG chunk bounds");
  const d=b.subarray(off+8,off+8+n);
  if(tag==="PLTE")palette=true;
  if(tag==="tRNS")alpha=d[0]===0;
  if(tag==="IDAT")idat.push(d);
  if(tag==="IEND"){end=true;break}
  off+=12+n;
 }
 assert(palette&&alpha&&end&&idat.length>0,"complete indexed PNG with transparency");
 const rows=inflateSync(Buffer.concat(idat));
 assert.equal(rows.length,h*(w+1),"exact image data");
 for(let y=0;y<h;y++)assert.equal(rows[y*(w+1)],0,"unfiltered rows");
 assert(rows.some((v,i)=>i%(w+1)!==0&&v!==0),"not blank");
 return {b,w,h};
}
test("all original PNGs decode within bounded dimensions and sizes",()=>{
 for(const [name,w,h,limit] of expected){
  const f=readPNG(resolve(assetDir,name));
  assert.equal(f.w,w,name);assert.equal(f.h,h,name);
  assert(f.b.length<=limit,name+" exceeds asset budget");
 }
});
test("generator is deterministic and isolated from approved mixer assets",()=>{
 const paths=expected.map(([name])=>resolve(assetDir,name));
 const before=paths.map(p=>sha256(readFileSync(p)));
 execFileSync(process.execPath,["scripts/build_guitar_fx_assets.mjs"],{cwd:root,stdio:"pipe"});
 assert.deepEqual(paths.map(p=>sha256(readFileSync(p))),before,"byte-identical rerun");
 const src=readFileSync(resolve(root,"scripts/build_guitar_fx_assets.mjs"),"utf8");
 assert(src.includes("GUITAR_ASSETS"));
 assert(!src.includes("site/preview-buttons"));
});
test("editor prototype cannot claim PCM nor send production native control",()=>{
 const html=readFileSync(resolve(root,"design-references/guitar-fx/editor-prototype.html"),"utf8");
 for(const s of ["DESIGN ONLY","NO AUDIO","assets/amp-head-class-a.png","cabinet-dual-12.png","assets/knob-brushed-metal.png","data-power","reorder("])
  assert(html.includes(s),"expected design marker "+s);
 assert(!html.includes("arMixConsoleIntent"));
 assert(!html.includes("AudioContext"));
});

test("native JUCE editor uses the generated binary resource identifiers",()=>{
 const editor=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigEditor.h"),"utf8");
 const cmake=readFileSync(resolve(root,"CMakeLists.txt"),"utf8");
 const main=readFileSync(resolve(root,"src/Main.cpp"),"utf8");
 // JUCE BinaryData strips hyphens from stems: arsonmix-256.png is
 // BinaryData::arsonmix256_png, NOT BinaryData::arsonmix_256_png.
 assert(main.includes("BinaryData::arsonmix256_png"),"verified production JUCE naming");
 assert(cmake.includes("ARMIX_GUITAR_SKIN_PNGS"),"GFX images are embedded");
 assert(cmake.includes("juce_add_binary_data(ARMixAssets"),"one existing app resource target");
 for(const name of [
   "amp-head-class-a.png",
   "cabinet-dual-12.png",
   "pedal-gate.png",
   "pedal-overdrive.png",
   "knob-brushed-metal.png"
 ]){
   const id=name.replace(/\.png$/,"").replace(/[^A-Za-z0-9_]/g,"")+"_png";
   assert(editor.includes("BinaryData::"+id),name+" correct generated JUCE identifier");
   assert(editor.includes("BinaryData::"+id+"Size"),name+" corresponding size constant");
   assert(!editor.includes("BinaryData::"+name.replaceAll("-","_").replace(".png","_png")),
     name+" must not reference non-existent underscore-hyphen symbol");
 }
});

test("reopened native editor reduces prior accepted PCM completions before painting the tone",()=>{
 const editor=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigEditor.h"),"utf8");
 const start=editor.indexOf("explicit GuitarRigEditor(");
 const consume=editor.indexOf("processor_.serviceCompletions();",start);
 const accepted=editor.indexOf("draft_=processor_.acceptedProgram();",start);
 assert(start>=0 && consume>start && accepted>consume,
        "editor must display already acknowledged DSP state after close/reopen");
});
