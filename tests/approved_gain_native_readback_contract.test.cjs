// SPDX-License-Identifier: AGPL-3.0-only
// Deterministic native GAIN readback test: no fake PCM authority or WebView2 runtime.
const test = require("node:test");
const assert = require("node:assert/strict");
const {readFileSync} = require("node:fs");
const vm = require("node:vm");
const source = readFileSync("web/approved-native-bridge.js","utf8");
const html = readFileSync("site/preview-v2/index.html","utf8");
const begin = source.indexOf("  function paintAcceptedInputGain(t,valueDb){");
const end = source.indexOf("  function projectNativeGraphWithoutFocusLoss(next) {",begin);
assert.ok(begin>0 && end>begin,"extract source-owned scalar renderer, not test copy");
assert.match(html,/if\(gainDialGesture\)finishGainDialGesture\(\{pointerId:gainDialGesture\.id\},true\);/);
assert.match(html,/lostpointercapture/);
assert.match(html,/visibilitychange/);
assert.match(html,/window\.addEventListener\('blur'/);
assert.match(source,/const settledGain=pendingNativeGainIntent&&!state\.pending/);
assert.match(source,/if\(!settledGain&&!activeGain&&!scope\.wireGainDragging/);

function fixture(){
 const writes={slider:0,rotation:0,label:0};
 let sliderValue="4.2";
 const dial={style:{
  value:"23.63deg",
  getPropertyValue(k){return this.value;},
  setProperty(k,v){this.value=v;writes.rotation++;}
 }};
 const slider={
  dataset:{},closest(k){assert.equal(k,".gain-dial");return dial;},
  get value(){return sliderValue;},
  set value(v){sliderValue=String(v);writes.slider++;}
 };
 let label="+4.2 dB";
 const digits={
  get textContent(){return label;},
  set textContent(v){label=String(v);writes.label++;}
 };
 const root={querySelector(s){
  if(s==='input[data-field="trim"]')return slider;
  if(s===".gain-lcd-digits")return digits;
  return null;
 }};
 const channel={id:"9007199254740993",role:"INPUT",controls:{inputGainDb:0}};
 const next={admitted:true,graph:{
  channels:[channel],capabilities:{supportedOps:["SET_INPUT_GAIN"]}
 }};
 const track={id:5,trim:0};
 let pending=null;
 const ctx={
  Number,Math,String,Map,document:{querySelector(s){
   assert.equal(s,'.strip[data-id="5"]');return root;
  }},
  controller:{state(){return {current:next,pending};}},
  uiByNativeId:new Map([[channel.id,5]]),
  scope:{tracks:[track]},
  window:{}
 };
 vm.createContext(ctx);
 vm.runInContext(source.slice(begin,end),ctx);
 return {
  ctx,slider,digits,dial,writes,track,channel,next,
  setPending(v){pending=v;},
  setAccepted(v){channel.controls.inputGainDb=v;},
  paint(){ctx.reconcileAcceptedInputGainSnapshot(next);},
  cancel(){ctx.window.__ARMIX_NATIVE_GAIN_RESTORE__();}
 };
}
test("unchanged native accepted gain repairs stale preview LCD/needle",()=>{
 const f=fixture();
 f.paint();
 assert.equal(f.slider.value,"0");
 assert.equal(f.digits.textContent,"0.0 dB");
 assert.equal(f.dial.style.value,"0.00deg");
 assert.deepEqual(f.writes,{slider:1,rotation:1,label:1});
 f.paint();
 assert.deepEqual(f.writes,{slider:1,rotation:1,label:1},
  "unchanged readback must not allocate/repaint each telemetry update");
});
test("pending SET_INPUT_GAIN cannot be painted as a rejected edit",()=>{
 const f=fixture();
 f.setPending({operation:"SET_INPUT_GAIN",requestId:"r1"});
 f.paint();
 assert.equal(f.slider.value,"4.2");
 f.setPending(null);
 f.cancel();
 assert.equal(f.slider.value,"0");
 assert.equal(f.track.trim,0);
});
test("active pointer capture defers LCD paint; cancellation restores only accepted scalar",()=>{
 const f=fixture();
 f.slider.dataset.gainDragging="true";
 f.paint();
 assert.equal(f.slider.value,"4.2");
 assert.equal(f.digits.textContent,"+4.2 dB");
 delete f.slider.dataset.gainDragging;
 f.cancel();
 assert.equal(f.slider.value,"0");
 assert.equal(f.digits.textContent,"0.0 dB");
});
test("real accepted change paints exact ChannelId gain and keeps same DOM",()=>{
 const f=fixture();
 f.setAccepted(6.5);
 f.paint();
 assert.equal(f.track.trim,6.5);
 assert.equal(f.slider.value,"6.5");
 assert.equal(f.digits.textContent,"+6.5 dB");
 assert.equal(f.dial.style.value,"36.56deg");
 f.setAccepted(-3.1);f.paint();
 assert.equal(f.digits.textContent,"-3.1 dB");
 assert.equal(f.slider.value,"-3.1");
});
test("unadmitted and unsupported snapshots cannot forge gain readback",()=>{
 const f=fixture();
 f.next.admitted=false;
 f.paint();
 assert.equal(f.slider.value,"4.2");
 f.next.admitted=true;
 f.next.graph.capabilities.supportedOps=[];
 f.paint();
 assert.equal(f.slider.value,"4.2");
});
