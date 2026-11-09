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
assert.match(bridge, /classList\.add\("armix-native-desktop"\)/);
assert.match(css, /html\.armix-native-desktop \.app > \.window-title\{display:none!important\}/);
assert.match(page, /function scheduleWireGeometryReflow\(\)/);
assert.match(page, /new ResizeObserver\(scheduleWireGeometryReflow\)/);
assert.match(page, /if\(drawn&&required!==drawn\)render\(\)/);
assert.match(page, /else renderWireRouting\(\)/);
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
class FakeResizeObserver{constructor(handler){this.handler=handler}observe(){}}
vm.runInNewContext(page.slice(begin,end),{
 document:dom,window:browser,ResizeObserver:FakeResizeObserver,
 requestAnimationFrame:fn=>{raf.push(fn);return raf.length;},
 bankCapacity:()=>required,render:()=>{renderCount++},
 renderWireRouting:()=>{wireCount++}
},{filename:'wire-reflow-runtime.js'});
const frame=()=>{assert.equal(raf.length,1);raf.shift()();};
events.get('resize')();events.get('resize')();
frame();
assert.equal(wireCount,1,'two resize events need just one geometry pass');
assert.equal(renderCount,0,'no full mixer rebuild within a bank');
required=8;events.get('resize')();frame();
assert.equal(renderCount,1,'bank boundary requires structural render');
drawn=8; // simulate the structural render committing its new bank geometry
hidden=true;events.get('resize')();
assert.equal(raf.length,0,'hidden window must not repaint');
hidden=false;docEvents.get('visibilitychange')();frame();
assert.equal(wireCount,2,'restore repaints live pin geometry');
width=0;events.get('focus')();frame();
assert.equal(wireCount,2,'zero viewport must not paint stale SVG');
width=800;events.get('pageshow')();frame();
assert.equal(wireCount,3,'recovered WebView viewport repaints');
console.log('Coalesced runtime WIRE resize / restore scheduler PASS');
