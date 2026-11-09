// Zero-dependency structural regression: desktop caption owns native buttons;
// wire remains a geometry projection and never mutates the audio graph on resize.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8');
const main = read('src/Main.cpp'), chrome = read('src/ArSonWindowChrome.h');
const page = read('site/preview-v2/index.html');
const css = read('site/preview-v2/assets/mixer-chrome-material.css');
const bridge = read('web/approved-native-bridge.js');
assert.match(main, /setLookAndFeel \(&mixerCaption_\)/);
assert.match(main, /setUsingNativeTitleBar \(false\)/);
assert.match(main, /setTitleBarHeight \(31\)/);
assert.match(main, /setLookAndFeel \(nullptr\)/);
assert.match(chrome, /M6\.4,19L5,17\.6L10\.6,12/);
assert.match(chrome, /M6,21V19H18v2H6Z/);
assert.match(chrome, /M3,21V3H21V21H3Z/);
assert.match(chrome, /paintPart \("Ar", 16/);
assert.match(chrome, /paintPart \("Son", 27/);
assert.match(chrome, /paintPart \("MiX", 27/);
assert.ok(bridge.includes('classList?.add?.("armix-native-desktop")'));
assert.match(css, /html\.armix-native-desktop \.app > \.window-title\{display:none!important\}/);
assert.match(page, /function scheduleWireGeometryReflow\(\)/);
assert.match(page, /let wireFragmentGeometryDirty=false/);
assert.match(page, /wireMotion\.token;wireMotion\.key=null/);
assert.match(page, /if\(layer\?\.dataset\.edgeId===String\(edge\.id\)&&!wireFragmentGeometryDirty\)return/);
assert.match(page, /new ResizeObserver\(scheduleWireGeometryReflow\)/);
assert.match(page, /if\(drawn&&required!==drawn\)\{/);
assert.match(page, /if\(wireGesture\)\{/);
assert.match(page, /wireGesture=null;wireSuppressClick=true/);
assert.match(page, /if\(faderGesture\)\{/);
assert.match(page, /render\(\); \/\/ structural bank change only/);
assert.match(page, /renderWireRouting\(\); \/\/ geometry-only/);
assert.match(page, /document\.addEventListener\('visibilitychange'/);
assert.doesNotMatch(page, /resizeQueued=false;render\(\)/);
console.log('Native titlebar + supplied SVG + WIRE geometry contract PASS');

// Runtime scheduler test with fake DOM; no WebView2, no sound engine and no timer.
const vm = require('node:vm');
const begin=page.indexOf('// Desktop maximise, restore, DPI changes');
const end=page.indexOf('restorePluginPreferences();init();',begin);
assert.ok(begin>=0&&end>begin,'geometry scheduler source must exist');
let drawn=9, required=9, width=800, hidden=false;
let renderCount=0, wireCount=0;
const raf=[], events=new Map(), docEvents=new Map();
const node={clientWidth:width,clientHeight:600};
const dom={
 get hidden(){return hidden;},
 getElementById:()=>{node.clientWidth=width;return node;},
 querySelector:()=>({style:{getPropertyValue:()=>String(drawn)}}),
 addEventListener:(name,handler)=>docEvents.set(name,handler)
};
const browser={addEventListener:(name,handler)=>events.set(name,handler)};
const motion={token:7,key:'stale-edge',offset:3};
const changed={graph:0,cancelled:0,captureReleased:0};
class FakeResizeObserver{constructor(handler){this.handler=handler}observe(){}}
const sandbox={
 document:dom,window:browser,ResizeObserver:FakeResizeObserver,
 wireGesture:null,faderGesture:null,wireSuppressClick:false,
 setWireSnapTarget:()=>changed.cancelled++,
 wireMotion:motion,wireFragmentGeometryDirty:false,
 requestAnimationFrame:fn=>{raf.push(fn);return raf.length;},
 bankCapacity:()=>required,render:()=>{renderCount++},
 renderWireRouting:()=>{wireCount++;assert.equal(sandbox.wireFragmentGeometryDirty,true);sandbox.wireFragmentGeometryDirty=false;}
};
vm.runInNewContext(page.slice(begin,end),sandbox,{filename:'wire-reflow-runtime.js'});
const frame=()=>{assert.equal(raf.length,1);raf.shift()();};
events.get('resize')();events.get('resize')();
frame();
assert.equal(wireCount,1,'two resize events need just one geometry pass');
assert.equal(motion.token,8,'in-flight wire animation cancels once');
assert.equal(motion.key,null,'stale wire coordinates are retired');
assert.equal(renderCount,0,'no full mixer rebuild within a bank');
required=8;events.get('resize')();frame();
assert.equal(renderCount,1,'bank boundary requires structural render');
assert.equal(motion.token,9,'structural resize must also cancel old WIRE motion');
assert.equal(changed.graph,0,'resize must never commit graph operations');
// A captured WIRE drag must be retired before a bank's DOM is replaced.
const oldRoot=dom.querySelector;
dom.querySelector=()=>({
 style:{getPropertyValue:()=>String(drawn)},
 classList:{remove:()=>{}},
 querySelectorAll:()=>[{classList:{remove:()=>{}}}],
 querySelector:()=>({hasPointerCapture:()=>true,releasePointerCapture:()=>changed.captureReleased++})
});
sandbox.wireGesture={pointerId:88,sourceId:5,originDestId:null,active:true};
required=7;events.get('resize')();frame();
assert.equal(renderCount,2,'second bank change must still redraw');
assert.equal(sandbox.wireGesture,null,'detached WIRE drag must be cancelled');
assert.equal(sandbox.wireSuppressClick,true,'late click must not create a route');
assert.equal(changed.cancelled,1,'snap outline must be cleared once');
assert.equal(changed.captureReleased,1,'native pointer capture must be released');
dom.querySelector=oldRoot;
drawn=7; // simulate the structural render committing its new bank geometry
hidden=true;events.get('resize')();
assert.equal(raf.length,0,'hidden window must not repaint');
hidden=false;docEvents.get('visibilitychange')();frame();
assert.equal(wireCount,2,'restore repaints live pin geometry');
width=0;events.get('focus')();frame();
assert.equal(wireCount,2,'zero viewport must not paint stale SVG');
width=800;events.get('pageshow')();frame();
assert.equal(wireCount,3,'recovered WebView viewport repaints');
console.log('Coalesced runtime WIRE resize / restore scheduler PASS');
