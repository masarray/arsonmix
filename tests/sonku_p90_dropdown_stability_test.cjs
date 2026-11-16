// SPDX-License-Identifier: AGPL-3.0-only
// Exercise the ACTUAL SonKu render-mode DOM-update branch, not a hand-copied
// model. Regression: native schema 11 also advertises schema 10 capabilities;
// prior if(p90) / else if(p89) branches alternately rewrote the open select.
"use strict";
const assert=require("node:assert/strict");
const fs=require("node:fs");
const vm=require("node:vm");
const source=fs.readFileSync("site/preview-v2/assets/mixer-native-sonku.js","utf8");
const start=source.indexOf('  if(p90Capable && picker.dataset.nativeModeSet!=="latest"){');
const end=source.indexOf("  const v=p90Capable?",start);
assert(start!==-1&&end>start,"Locate actual native SonKu mode menu reconciliation");
const actualReconcile=source.slice(start,end);
const renderMenu=vm.runInNewContext(
  '(function(picker,root,cap){ const {'+
  'p90Capable,p91p92Capable,p94Capable,p89Capable,p88Capable,p87Capable,p86Capable,'+
  'p85Capable,p84Capable,p83Capable}=cap;\n'+
  actualReconcile+'\n})',{}
);
const syncMatch=source.match(/if\(picker\.value!==v\)picker\.value=v;/);
assert(syncMatch,"Native render must not reset an unchanged open popup value");
const syncSelection=vm.runInNewContext(
  '(function(picker,v){'+syncMatch[0]+'})',{}
);
const optionWrites=[];
const picker={dataset:{},isOpen:false,_markup:"",_value:"music",
  querySelector(){return null;},add(){},
  get innerHTML(){return this._markup;},
  set innerHTML(markup){
    assert.equal(this.isOpen,false,
      "Render must not replace <option> children while native dropdown is open");
    optionWrites.push(markup);
    this._markup=markup;
    this._value="music"; // Native select defaults to its first option on rebuild.
  },
  get value(){return this._value;},
  set value(value){
    assert.equal(this.isOpen,false,
      "Render must not reset the current selection of an open dropdown");
    this._value=value;
    this.valueWrites++;
  },
  valueWrites:0
};
const label={textContent:""};
const root={querySelector(selector){
  assert.equal(selector,".sk-render-label");return label;
}};
const full={p90Capable:true,p91p92Capable:true,p94Capable:false,p89Capable:true,p88Capable:true,
  p87Capable:true,p86Capable:true,p85Capable:true,p84Capable:true,
  p83Capable:false};
renderMenu(picker,root,full);
assert.equal(picker.dataset.nativeModeSet,"latest");
assert.match(picker.innerHTML,/value="external"/);
assert.match(picker.innerHTML,/LATEST · IMMERSIVE SPATIAL/);
assert.match(label.textContent,/SPATIAL ENGINE/);
syncSelection(picker,"external");
assert.equal(picker.value,"external");
const writesAfterChoice=picker.valueWrites;
picker.isOpen=true;
// Simulate every 250ms native readback + other synchronous UI render during
// a ten-second open dropdown. Both P9 and P8.9 flags remain true.
for(let tick=0;tick<40;++tick) {
  renderMenu(picker,root,full);
  syncSelection(picker,"external");
}
picker.isOpen=false;
assert.equal(optionWrites.length,1,"P9 options cannot oscillate to P8.9");
assert.equal(picker.valueWrites,writesAfterChoice,
  "Native ACK polling must not programmatically re-select the same option");
assert.equal(picker.value,"external");
assert.match(picker.innerHTML,/value="external"/);
// Capability loss: an older native v11 P9.0 build must NOT claim P9.1/9.2.
// Exactly one rebuild on a REAL capability change is allowed with popup closed.
const p90Only={...full,p91p92Capable:false};
renderMenu(picker,root,p90Only);
assert.equal(picker.dataset.nativeModeSet,"latest");
assert.equal(optionWrites.length,1);
assert.match(picker.innerHTML,/LATEST · IMMERSIVE SPATIAL/);
assert.doesNotMatch(picker.innerHTML,/P9\.1 \+ P9\.2/);
assert.match(label.textContent,/SPATIAL ENGINE/);
picker.isOpen=true;
for(let i=0;i<12;++i)renderMenu(picker,root,p90Only);
picker.isOpen=false;
assert.equal(optionWrites.length,2);
const p89={...p90Only,p90Capable:false};
renderMenu(picker,root,p89);
assert.equal(picker.dataset.nativeModeSet,"p89");
assert.equal(optionWrites.length,2);
assert.doesNotMatch(picker.innerHTML,/value="external"/);
for(let i=0;i<12;++i)renderMenu(picker,root,p89);
assert.equal(optionWrites.length,3);
const p88={...p89,p89Capable:false};
renderMenu(picker,root,p88);
assert.equal(picker.dataset.nativeModeSet,"p88");
assert.equal(optionWrites.length,3);
renderMenu(picker,root,full);
assert.equal(picker.dataset.nativeModeSet,"latest");
assert.equal(optionWrites.length,4);
// Native capability is authoritative; a UI-only version string is insufficient.
const nativeSource=fs.readFileSync("src/Main.cpp","utf8");
assert.match(nativeSource,/reply->setProperty\("p91p92MusicBassFoundation",true\)/);
assert.match(source,/p91p92Capable=p90Capable && r\.p91p92MusicBassFoundation===true;/);
assert.match(source,/p91p92Capable&&s\.renderMode===9/);
assert.match(source,/NATIVE P9\.1\+P9\.2 WORLD BASS/);
assert.match(nativeSource,/reply->setProperty\("p94FrontBackDepthCue",true\)/);
assert.match(source,/p94Capable=p91p92Capable && r\.p94FrontBackDepthCue===true;/);
const p94={...full,p94Capable:true};
renderMenu(picker,root,p94);
assert.equal(picker.dataset.nativeModeSet,"latest");
assert.equal(optionWrites.length,4);
assert.match(picker.innerHTML,/LATEST · IMMERSIVE SPATIAL/);
assert.match(label.textContent,/SPATIAL ENGINE/);
// The real render() immediately sets the accepted native mode after rebuilding.
syncSelection(picker,"external");
picker.isOpen=true;
for(let i=0;i<40;++i){renderMenu(picker,root,p94);syncSelection(picker,"external");}
picker.isOpen=false;
assert.equal(optionWrites.length,6,"P9.4 must not flicker in native readback");
renderMenu(picker,root,full);
assert.equal(picker.dataset.nativeModeSet,"p91p92");
assert.equal(optionWrites.length,4);
assert.doesNotMatch(picker.innerHTML,/P9\.4/);
console.log("PASS P95 latest-only dropdown: native capability-gated milestone, stable 40 readbacks, P9.0 fallback, correct one-time transitions");
