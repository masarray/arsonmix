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

// Regression for a real WebView2-hosted desktop: JUCE DocumentWindow's
// activeWindowStatusChanged() disables caption buttons whenever its focus
// bookkeeping reports inactive. A WebView2 child HWND may own Windows focus,
// so these three real native controls must stay enabled and responsive to
// their existing over/down transitions even on focus handoff.
const nativeWindowConstructor=main.slice(main.indexOf('explicit ConsoleWindow ('),
                                          main.indexOf('~ConsoleWindow()'));
assert.match(nativeWindowConstructor,/setLookAndFeel \(&mixerCaption_\);[\s\S]*?setUsingNativeTitleBar \(false\);[\s\S]*?setTitleBarButtonsRequired \(allButtons, false\);/);
const activation=main.slice(main.indexOf('void activeWindowStatusChanged() override'),
                            main.indexOf('~ConsoleWindow()'));
assert.match(activation,/juce::DocumentWindow::activeWindowStatusChanged\(\);/);
assert.match(activation,/for \(auto\* control : \{ getMinimiseButton\(\), getMaximiseButton\(\), getCloseButton\(\) \}\)/);
assert.match(activation,/control->setEnabled \(true\)/);
assert.doesNotMatch(activation,/startTimer|setAlwaysOnTop|toFront|SetForegroundWindow|engine_/);
assert.match(chrome,/void paintButton \(juce::Graphics& g, bool over, bool down\) override/);
assert.match(chrome,/if \(over \|\| pressed\)/);

assert.match(main, /setUsingNativeTitleBar \(false\)/);
assert.match(main, /setTitleBarHeight \(31\)/);
assert.match(main, /setLookAndFeel \(nullptr\)/);
assert.match(chrome, /M6\.4,19L5,17\.6L10\.6,12/);
assert.match(chrome, /M6,21V19H18v2H6Z/);
assert.match(chrome, /M3,21V3H21V21H3Z/);
assert.match(chrome, /constexpr int buttonWidth = 34/);
assert.match(chrome, /12\.0f, 12\.0f/);
// Exact semantic hover/pressed palette of the REAL JUCE caption, not WebView.
const palette = chrome.slice(chrome.indexOf('static CaptionPalette paletteFor'),
                             chrome.indexOf('static std::unique_ptr<juce::Drawable> load'));
assert.ok(palette.length > 300, 'native caption must own one state palette');
assert.match(chrome, /const auto palette = paletteFor \(kind_\)/);
assert.match(chrome, /pressed \? palette\.pressedFace : palette\.hoverFace/);
assert.match(chrome, /pressed \? palette\.pressedEdge : palette\.hoverEdge/);
assert.match(palette, /closeButton[\s\S]*?0xff5c3039[\s\S]*?0xffab5864[\s\S]*?0xffef3e50[\s\S]*?0xffff8993/);
assert.match(palette, /minimiseButton[\s\S]*?0xff244e3a[\s\S]*?0xff4ead7a[\s\S]*?0xff1fb76b[\s\S]*?0xff8be2b4/);
assert.match(palette, /0xff584929[\s\S]*?0xffbc9544[\s\S]*?0xffe5ae2b[\s\S]*?0xffffdf81/);
assert.doesNotMatch(palette, /0xff50d4e7|0xff67b9ff|0xff168fa7|0xff287abd/);
assert.match(chrome, /drawRoundedRectangle/);
assert.match(chrome, /parent->isFullScreen\(\)/);

/* JUCE 8 Button::mouseUp runs its listener before the message-loop's paint:
 * Close/minimize vanish and maximize changes geometry before the bright click
 * face is drawn. Test a real bounded, one-shot paint->dispatch handoff, not
 * just the existence of red/green/amber palette hex values. */
assert.match(chrome,/class MixerCaptionButton final : public juce::Button, private juce::Timer/);
assert.match(chrome,/const bool pressed = down \|\| pendingClick_/);
assert.match(chrome,/g\.setColour \(pressed \? palette\.pressedFace : palette\.hoverFace\)/);
assert.match(chrome,/g\.setColour \(pressed \? palette\.pressedEdge : palette\.hoverEdge\)/);
assert.match(chrome,/pressed && pressedRestoreIcon_ != nullptr/);
assert.match(chrome,/pressed && pressedIcon_ != nullptr/);
const defer=chrome.slice(chrome.indexOf('void internalClickCallback ('),
                         chrome.indexOf('private:\n    struct CaptionPalette'));
assert.match(defer,/if \(!isEnabled\(\) \|\| pendingClick_\)\s*return/);
assert.match(defer,/pendingModifiers_ = modifiers/);
assert.match(defer,/pendingClick_ = true;\s*repaint\(\);\s*startTimer \(160\)/);
assert.match(defer,/void timerCallback\(\) override/);
assert.match(defer,/stopTimer\(\);\s*pendingClick_ = false;\s*repaint\(\);/);
assert.match(defer,/juce::Button::internalClickCallback \(pendingModifiers_\)/);
assert.equal((defer.match(/juce::Button::internalClickCallback/g)||[]).length,1,
 'a caption click must dispatch once, through the original JUCE listener');
assert.doesNotMatch(defer,/engine_|WebView|setAlwaysOnTop|toFront|sleep|Thread::sleep|std::thread/);


/* Dark-ink pressed glyphs are prebuilt once, not reparsed/recoloured when
   paintButton receives a hover or pointer-down transition. Maintain WCAG
   non-text graphic contrast against the luminous green/amber pressed faces. */
assert.match(chrome, /pressedIcon_ = load \(iconPath, kind == juce::DocumentWindow::minimiseButton/);
assert.match(chrome, /pressedRestoreIcon_ = load \(restorePath/);
assert.match(chrome, /const bool restoring = kind_ == juce::DocumentWindow::maximiseButton/);
assert.match(chrome, /down && pressedRestoreIcon_ != nullptr/);
assert.match(chrome, /down && pressedIcon_ != nullptr/);
assert.match(chrome, /static std::unique_ptr<juce::Drawable> load \(const char\* path, const char\* ink/);
const paint = chrome.slice(chrome.indexOf('void paintButton '), chrome.indexOf('private:\n    struct CaptionPalette'));
assert.doesNotMatch(paint, /Drawable::createFromSVG|XmlDocument::parse|std::make_unique|new juce::/);
const luminance = hex => {
  const rgb = hex.match(/[a-f0-9]{2}/gi).map(x=>parseInt(x,16)/255);
  return rgb.map(v=>v<=0.04045?v/12.92:((v+0.055)/1.055)**2.4)
    .reduce((sum,v,i)=>sum+v*[0.2126,0.7152,0.0722][i],0);
};
const contrast = (a,b) => {
  const [lighter,darker] = [luminance(a),luminance(b)].sort((x,y)=>y-x);
  return (lighter+0.05)/(darker+0.05);
};
assert.ok(contrast('1fb76b','103322')>=4.5,'green pressed minimise icon contrast');
assert.ok(contrast('e5ae2b','37280e')>=4.5,'amber pressed maximise/restore icon contrast');

assert.match(chrome, /GlyphArrangement::getStringWidthInt \(brandFont, word\)/);
assert.match(chrome, /paintPart \("Ar", juce::Colour/);
assert.match(chrome, /paintPart \("Son", juce::Colour/);
assert.match(chrome, /paintPart \("MiX", juce::Colour/);
assert.match(chrome, /Open Source Premium Digital Mixer/);
// The caption must expose the binary's actual version, not a copied release
// string or the historical fake 01.00. Narrow desktop widths keep the
// version ahead of the lower-priority description.
assert.match(chrome, /JUCE_APPLICATION_VERSION_STRING/);
assert.doesNotMatch(chrome, /Version 01\.00/);
assert.match(chrome, /0xffe8f6fc/);
assert.match(chrome, /getStringWidthInt \(subtitleFont, label\)/);
const cmake = read('CMakeLists.txt');
assert.match(cmake, /set\(ARMIX_RELEASE_TAG/);
assert.match(cmake, /ARMIX_EXACT_SOURCE_TAG/);
assert.match(cmake, /\$\{ARMIX_APP_VERSION\}/);
assert.doesNotMatch(cmake, /0\.5\.0-p2/);
assert.match(chrome, /charToString \(\(juce::juce_wchar\) 0x00a9\)/);
assert.match(chrome, /charToString \(\(juce::juce_wchar\) 0x00b7\)/);
assert.match(chrome, /const auto advance = juce::GlyphArrangement::getStringWidthInt/);
assert.match(chrome, /2026 SonKuPik/);
assert.doesNotMatch(chrome, /NATIVE SESSION/);
assert.ok(bridge.includes('classList?.add?.("armix-native-desktop")'));
const operatorCopy=require('../web/approved-native-contract.js').operatorMessage;
assert.match(bridge, /const operatorMessage=protocol\.operatorMessage/);
assert.match(bridge, /const NEEDS_REMAP="Audio connection lost"/);
assert.match(bridge, /MutationObserver\(sanitize\)/);
assert.match(bridge, /const nodes=\[transport,info,warning\]\.filter\(Boolean\)/);
assert.match(bridge, /const operatorTransport=operatorMessage\(transport\)/);
assert.equal(operatorCopy('HARDWARE FAT PENDING'),'Mixer ready');
assert.doesNotMatch(bridge, /FAT PENDING/);
assert.doesNotMatch(bridge, /HARDWARE FAT PENDING/);
assert.match(bridge, /"OUTPUT ENABLED"/);
assert.match(bridge, /status\("Mixer settings updated"\)/);
assert.match(bridge, /element\.textContent=operatorMessage\(message\)/);
assert.match(bridge, /nativeToolbarGroup=name/);
assert.match(bridge, /toolbar\.insertBefore\(divider,element\)/);
assert.match(css, /html\.armix-native-desktop \.app \.command-bar\{\s*height:43px!important/);
assert.match(css, /\.armix-toolbar-divider\{display:none!important\}/);
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
// Reconciled main Player M3 controls must survive the native caption branch.
const playerHtml = read('site/preview-v2/assets/mixer-contextual-buttons.js');
assert.match(main, /struct PlayerTrackSettings/);
assert.match(main, /action=="playerKeyStep" \|\| action=="playerTempoStep"/);
assert.match(main, /setPlayerPitchTempo\(next\.key,next\.tempo\)/);
assert.match(playerHtml, /id="playerKeyMinus"/);
assert.match(playerHtml, /id="playerTempoPlus"/);
assert.match(playerHtml, /armix-brand-mix/);
console.log('Native titlebar + supplied SVG + WIRE geometry contract PASS');

// Runtime scheduler test with fake DOM; no WebView2, no sound engine and no timer.
const vm = require('node:vm');
// User-visible status copy must never expose internal admission/QA or revision
// codes even if the preview or native error path emits one later.
assert.equal(typeof operatorCopy,'function','native presentation helper must be exported');
for(const raw of [
  'NATIVE PCM · HARDWARE FAT PENDING',
  'NATIVE PCM GRAPH ACCEPTED · FAT PENDING · device 3 · graph 3 · roster 94',
  'NATIVE PENDING_ACCEPTED_SNAPSHOT · awaiting authoritative readback',
  'WIRE not applied · native admission timed out; previous audio preserved',
  'NATIVE: STALE_NATIVE_WIRE_GRAPH · previous controls unchanged',
  'SIMULATED METERS · NO REAL AUDIO',
  'DEVICE_INIT_FAILED',
  'STALE_NATIVE_GRAPH_REVISION',
  'JUCE_CALLBACK_DIAGNOSTIC_FAT_PENDING',
  'HARDWARE FAT PENDING',
  'FAT PENDING',
  'NATIVE PCM GRAPH ACCEPTED · FAT PENDING · device 1',
  'MONITOR ON · OUT Headphones · NATIVE PCM · HARDWARE FAT PENDING'
]){
  const shown=operatorCopy(raw);
  assert.ok(shown.length>0);
  assert.doesNotMatch(shown,/\b(FAT|PCM|ACK|readback|graph|roster|epoch|revision|native|simulated|demo|admission)\b|NATIVE_/i);
}
assert.equal(operatorCopy('Mixer saved as .smix'),'Mixer saved as .smix');
assert.equal(operatorCopy('HARDWARE FAT PENDING'),'Mixer ready');
assert.equal(operatorCopy('MONITOR ON · NATIVE PCM · FAT PENDING'),'Output enabled');
console.log('Operator-facing footer and toast copy isolation PASS');
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
