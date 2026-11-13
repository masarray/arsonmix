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
   "gfx-amp-head.png","gfx-cabinet.png",
   "gfx-pedal-gate-body.png","gfx-pedal-drive-body.png",
   "gfx-knob.png","gfx-switch-off.png","gfx-switch-on.png",
   "gfx-button-off.png","gfx-button-on.png","gfx-panel.png",
   "gfx-stomp-off.png","gfx-stomp-on.png"
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
 assert(editor.includes("static const std::array<juce::Image,12> skins"),
        "native editor must cache all 12 layered PNG resources per app");
 for(const key of ["amp_","cab_","gate_","pedal_","knob_","switchOff_",
                   "switchOn_","buttonOff_","buttonOn_","panel_",
                   "stompOff_","stompOn_"])
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

test("Guitar FX 2x polyphase processing preserves an authoritative fixed PDC",()=>{
 const core=readFileSync(resolve(root,"engine/builtin_fx/guitar/GuitarRigCore.h"),"utf8");
 const proc=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigProcessor.h"),"utf8");
 const overs=readFileSync(resolve(root,"engine/builtin_fx/guitar/GuitarOversampling2x.h"),"utf8");
 const portable=readFileSync(resolve(root,"tests/guitar_fx_core_pcm_test.cpp"),"utf8");
 assert(overs.includes("static constexpr int latencySamples=16"));
 assert(core.includes("processingLatencySamples=") &&
        core.includes("cabinetLatencySamples+oversamplingLatencySamples"));
 assert(proc.includes("setLatencySamples(armix::guitarfx::GuitarRigCore::processingLatencySamples)"));
 assert(overs.includes("upsamplePair(") && overs.includes("decimateEven("));
 assert(core.includes("const float matchedDry=oversampling_[ch].bypassDelay(input)"),
        "clean path must have same fixed PDC and warm dry history");
 assert(portable.includes("assert(aliasedThirdDb < -35.0)"),
        "folded 21kHz alias must remain a hard regression gate");
});

test("production guitar hardware is layered and old prototype art is not embedded",()=>{
 const cmake=readFileSync(resolve(root,"CMakeLists.txt"),"utf8");
 const editor=readFileSync(resolve(root,"engine/builtin_fx/GuitarRigEditor.h"),"utf8");
 assert(cmake.includes("build_guitar_fx_layered_assets.mjs"));
 assert(cmake.includes("assets/gfx-*.png"),
        "production binary must exclude deprecated old baked-footpedal PNGs");
 assert(editor.includes("juce::Rectangle<int> knobBounds("),
        "independent physical knob hit targets must follow artwork anchors");
 assert(editor.includes("stomp_.onClick=")&&editor.includes("power_[focus_].triggerClick()"),
        "stomp must affect the same PCM program as the chain power toggle");
 assert(editor.includes("setLookAndFeel(&switchLook_)") &&
        editor.includes("setLookAndFeel(&buttonLook_)"),
        "native controls must use the same layered material language");
 for(const path of ["pedalgate_png","pedaloverdrive_png","ampheadclassa_png"])
   assert(!editor.includes("BinaryData::"+path),
          "obsolete baked bitmap must not be loaded: "+path);
});
test("generated ON and OFF PNGs have byte-identical silhouettes and frame pixels",async()=>{
 const {SKINS,render}=await import("../scripts/build_guitar_fx_layered_assets.mjs");
 const decode=spec=>{
  const b=render(spec),w=b.readUInt32BE(16),h=b.readUInt32BE(20);
  assert.equal(b[25],6,"RGBA PNG (not opaque image with fake black alpha)");
  let i=8,idat=[];
  while(i<b.length){
   const n=b.readUInt32BE(i),tag=b.toString("ascii",i+4,i+8);
   if(tag==="IDAT")idat.push(b.subarray(i+8,i+8+n));
   i+=12+n;
   if(tag==="IEND")break;
  }
  const raw=inflateSync(Buffer.concat(idat));
  assert.equal(raw.length,(w*4+1)*h);
  return {w,h,raw};
 };
 const find=name=>SKINS.find(s=>s[0]===name);
 assert.equal(SKINS.length,12,"only active GFX modules and UI chrome");
 for(const [a,b] of [["gfx-switch-off.png","gfx-switch-on.png"],
                       ["gfx-button-off.png","gfx-button-on.png"],
                       ["gfx-stomp-off.png","gfx-stomp-on.png"]]){
  const A=decode(find(a)),B=decode(find(b));
  assert.equal(A.w,B.w);assert.equal(A.h,B.h);
  let stateChanges=0;
  for(let y=0;y<A.h;y++)for(let x=0;x<A.w;x++){
   const offset=y*(A.w*4+1)+1+x*4;
   assert.equal(A.raw[offset+3],B.raw[offset+3],a+" vs "+b+
     ": alpha/frame misregistration at "+x+","+y);
   // Match the immutable hardware shell. The circular stomp's lit
   // ring extends close to the canvas top but is an intentional state pixel.
   const d=Math.hypot(x/A.w-.5,y/A.h-.5);
   const outside=a.includes("stomp")?d>.43:
                 x<8||x>=A.w-8||y<10||y>=A.h-10;
   if(outside)
    for(let c=0;c<4;c++)assert.equal(A.raw[offset+c],B.raw[offset+c],
      "outer frame cannot flicker across ON/OFF");
   if(!outside&&A.raw[offset]!==B.raw[offset])stateChanges++;
  }
  assert(stateChanges>100,"state must visibly change while frame stays fixed");
 }
 // These are true independent body PNGs, with empty wells. The only
 // draggable controls are JUCE child widgets, not burned-in knob pixels.
 for(const name of ["gfx-pedal-gate-body.png","gfx-pedal-drive-body.png"]){
  const body=decode(find(name));
  const x=Math.floor(body.w*.5),y=Math.floor(body.h*.82);
  const k=y*(body.w*4+1)+1+x*4;
  assert(body.raw[k]<75,"stomp well must not contain a baked shiny switch");
 }
});
