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
 const bankA=editor.indexOf("bankDrafts_[0]=processor_.acceptedBankProgram(0);",start);
 const bankB=editor.indexOf("bankDrafts_[1]=processor_.acceptedBankProgram(1);",start);
 const chosen=editor.indexOf("draft_=bankDrafts_[selectedBank_];",start);
 assert(start>=0 && consume>start && bankA>consume && bankB>bankA &&
        chosen>bankB,"editor must restore both native A/B snapshots after ACK");
 assert(editor.includes("processor_.queueProgram(draft_,id,selectedBank_)"),
        "A/B selection must name the native target bank during control ACK");
});

test("bypassed Guitar FX editor may enqueue tone without a live wet meter",()=>{
 const editor=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigEditor.h"),"utf8");
 const processor=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigProcessor.h"),"utf8");
 assert(editor.includes("processor_.queueProgram(draft_,id,selectedBank_)"));
 assert(!editor.includes("processor_.isReady()&&serial>0"),
        "guitar bypass must not disable native tone edits");
 assert(processor.includes("pending_.fetch_add(1,std::memory_order_release)"),
        "unacknowledged tone must block stale session save");
 assert(editor.includes("~GuitarRigEditor() override") &&
        editor.includes("processor_.queueProgram(bankDrafts_[target],id,target)"),
        "closing editor must preserve queued native A/B switch");
});

test("native hardware PNG decoding is cached across editor close/reopen",()=>{
 const editor=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigEditor.h"),"utf8");
 assert(editor.includes("static const std::array<juce::Image,5> skins"),
        "native editor must not decode all five PNG resources for every window");
 for(const key of ["amp_","cab_","gate_","pedal_","knob_"])
  assert(editor.includes(key+"=skins["),key+" shares cached image resource");
});

test("Guitar FX stereo-linked gate and visible hold/release are real native controls",()=>{
 const core=readFileSync(resolve(root,"engine/builtin_fx/guitar/GuitarRigCore.h"),"utf8");
 const editor=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigEditor.h"),"utf8");
 assert(core.includes("linkedGateGain(") &&
        core.includes("std::max(std::fabs(leftIn),std::fabs(rightIn))"),
        "stereo-linked guitar gate must use max level without summing to mono");
 assert(core.includes("gateHoldSamples") && core.includes("gateCloseSmooth"),
        "Hold/Release parameters must change DSP timing");
 assert(editor.includes('{"THRESHOLD","HOLD","RELEASE"}'),
        "visible controls must match three real gate parameters");
 for(const value of ["-80.f+50.f*normalized","120.f*normalized",
                     "12.f+228.f*normalized"])
  assert(editor.includes(value),"gate values must use engineering dB/ms labels");
});
