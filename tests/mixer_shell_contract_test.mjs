// Pure source wiring checks for L2 mixer shell. Visual assertions run in Playwright.
import {readFileSync} from 'node:fs';
import test from 'node:test';
import assert from 'node:assert/strict';
const read=p=>readFileSync(new URL('../'+p,import.meta.url),'utf8');
test('mixer shell is a presentation module, not a second audio engine',()=>{
  const script=read('web/mixer-layout.js'),css=read('web/mixer-layout.css');
  assert.match(script,/rail\.insertBefore\(modeSection,document\.getElementById\('channelList'\)\)/);
  assert.match(script,/grid\.appendChild\(button\)/);
  assert.match(script,/nav\.querySelector\('\[data-view="MIX"\]'\)\?\.click/);
  assert.match(css,/grid-template-rows:/);
  assert.match(css,/minmax\(110px,1fr\)/);
  assert.match(css,/--ar-upper-height:224px/);
  assert.match(css,/--ar-upper-height:195px/);
  assert.match(css,/#app #mixer\{--ar-upper-height:165px\}/);
  assert.doesNotMatch(css,/\[data-io-mode="(?:ROUTE|SENDS)"\]\{--ar-upper-height:/);
  assert.doesNotMatch(script,/routingDryApply|createPluginInstance|AudioContext|processBlock|setInterval|requestAnimationFrame/);
});
test('new shell resources are in the exact native WebView2 asset list',()=>{
  const cmake=read('CMakeLists.txt'),main=read('src/Main.cpp'),html=read('web/index.html');
  for(const name of ['mixer-layout.js','mixer-layout.css']){
    assert.ok(cmake.includes('web/'+name),name+' not packaged');
    assert.ok(main.includes('"/'+name+'"'),name+' not served');
    assert.ok(html.includes(name),name+' not referenced');
  }
  assert.ok(html.indexOf('mixer-io.js')<html.indexOf('mixer-layout.js'));
});

test('route status adapter never consumes the device epoch before the native controller',()=>{
  const monitor=read('web/monitor.js');
  const start=monitor.indexOf('window.__arMixRouteView=()=>');
  const finish=monitor.indexOf('\n  };',start);
  assert.ok(start>=0 && finish>start,'pure native route projection missing');
  const projection=monitor.slice(start,finish+5);
  assert.ok(!projection.includes('hydrateAcceptedRoute('),'status snapshot mutates native route draft');
  assert.ok(!projection.includes('invalidateDraftForNewDevice('),'status snapshot resets device epoch');
  assert.ok(monitor.includes('if(draftEpoch!==(Number(d.dryRoutingLab?.epoch)||0))'),'epoch refresh gate missing');
  assert.ok(monitor.includes('if(!routeOverlay.classList.contains("open") &&'),'status must initialize MIX when PATCH is closed');
});

test('VU dBFS ruler is housed with mono/stereo/master LCD bars',()=>{
  const app=read('web/app.js'),css=read('web/styles-src/42-mixer-premium.css');
  assert.match(app,/<div class="vu-bars">\$\{ch\.stereo\?[^\n]*\$\{meterScaleMarkup\(\)\}<\/div><\/div>/);
  assert.match(app,/<div class="vu-bars">\$\{meterUnit\('masterL','L'\)\}[^\n]*\$\{meterScaleMarkup\(\)\}<\/div>/);
  assert.match(css,/#app #mixer \.fader-zone \.vu-bank \.vu-bars \.vu-scale\{/);
  assert.match(css,/\.vu-bank\.is-mono \.vu-bars\{\s*flex:0 0 35px/);
  assert.match(css,/\.vu-bank\.is-stereo \.vu-bars/);
});
test('RAIL3 black upper slot and cyan lower guide with black-foot fade, PNG detent preserved',()=>{
 const app=read('web/app.js'),native=read('web/bridge.js');
 const css=read('web/styles-src/42-mixer-premium.css');
 const begin=css.indexOf('/* RAIL3 — operator-approved');
 const end=css.indexOf('/* M5 — names start',begin);
 assert.ok(begin>0&&end>begin);
 const rail=css.slice(begin,end);
 assert.match(rail,/linear-gradient\(90deg,#010508/);
 assert.match(rail,/linear-gradient\(to bottom,/);
 assert.match(rail,/#12191e calc\(var\(--fader-cut,50%\) \+ 26px\)/);
 assert.match(rail,/#27bfd8 calc\(var\(--fader-cut,50%\) \+ 68px\)/);
 assert.match(rail,/transparent 62%/);
 assert.match(rail,/rgba\(1,6,10,\.52\) 86%/);
 assert.match(rail,/rgba\(1,6,10,\.85\) 94%/);
 assert.match(rail,/#01060a 100%/);
 assert.equal((rail.match(/linear-gradient\(/g)||[]).length,3);
 assert.ok(rail.indexOf('rgba(1,6,10,.52) 86%')<rail.indexOf('#01060a 100%'));
 assert.match(rail,/inset 1px 0 #010406,inset -1px 0 #010406/);
 assert.doesNotMatch(rail,/filter:|blur\(|animation:|transition:|backdrop-filter:/);
 assert.equal((app.match(/style="--fader-cut:\$\{levelToTop/g)||[]).length,2);
 assert.match(app,/host\.style\.setProperty\('--fader-cut'/);
 assert.match(app,/el\.style\.setProperty\('--fader-cut'/);
 assert.equal((native.match(/\.style\.setProperty\("--fader-cut",top\+"%"\)/g)||[]).length,3);
 assert.doesNotMatch(app,/--fader-open/);
 assert.doesNotMatch(native,/--fader-open/);
});
test('native mixer rack reconcile ignores meter/loudness text node replacement',()=>{
  const native=read('web/bridge.js');
  assert.match(native,/const needsMixerReconcile=records=>records\.some/);
  assert.match(native,/target===mixer/);
  assert.match(native,/target\.id==="masterLeft"/);
  assert.match(native,/target\.classList\.contains\("rack"\)/);
  assert.match(native,/if\(needsMixerReconcile\(records\)\)syncView\(\)/);
  assert.doesNotMatch(native,/new MutationObserver\(\(\)=>syncView\(\)\)\.observe\(mixer/);
});
test('native gain readback moves all PNG caps with matched RAIL2 optical guide',()=>{
  const native=read('web/bridge.js');
  assert.equal((native.match(/\.querySelector\("\.cap"\)\?\.style\.setProperty\("--pos",top\+"%"\)/g)||[]).length,3);
  assert.equal((native.match(/\.style\.setProperty\("--fader-cut",top\+"%"\)/g)||[]).length,3);
  assert.match(native,/element\.getAttribute\("aria-label"\)!==accessible/);
});

test('routing FX draft explicitly reconciles the native mixer without telemetry DOM work',()=>{
  const native=read('web/bridge.js'),routes=read('web/monitor.js');
  assert.match(native,/window\.addEventListener\("arMixRoutingDraftChanged",\(\)=>\{/);
  assert.match(native,/if\(currentTab\(\)==="FX RACK"\)renderRack\(\);/);
  assert.match(native,/syncView\(\);/);
  assert.match(routes,/window\.dispatchEvent\(new Event\("arMixRoutingDraftChanged"\)\)/);
});

test('P1 channel selection retains all strip and meter nodes',()=>{
 const src=read('web/app.js');
 assert.match(src,/function chooseChannel\(i\)\{[\s\S]*?focusChannel\(i\);/);
 assert.match(src,/if\(e\.target\.id==='rackChannel'\)chooseChannel\(\+e\.target\.value\)/);
 assert.doesNotMatch(src,/currentChannel=i;renderMixer\(\)/);
});
test('P1 UI-only session save is bounded dirty-event based, not a 5s poll',()=>{
 const src=read('web/app.js');
 assert.match(src,/function markUiSessionDirty\(\)/);
 assert.match(src,/function flushUiAutosave\(\)/);
 assert.match(src,/Math\.min\(1800,Math\.max\(0,8500-/);
 assert.match(src,/window\.addEventListener\('pagehide',flushUiAutosave\)/);
 assert.doesNotMatch(src,/lastPersist/);
});

test('M5 CLIP light is resettable per channel and MASTER; real PCM peak is sole native latch source',()=>{
 const app=read('web/app.js'),bridge=read('web/bridge.js');
 assert.match(app,/window\.__arMixLatchNativeClip=\(key,peak\)=>/);
 assert.match(app,/Number\.isFinite\(peak\)&&peak>=1/);
 assert.match(app,/if\(b\.dataset\.clip!==undefined\)\{resetClip\(b\.dataset\.clip\);return\}/);
 assert.match(bridge,/window\.__arMixLatchNativeClip\?\.\(el\.dataset\.meter,rawPeak\)/);
 assert.match(bridge,/percent\(d\.peakLeft\),rawL/);
 assert.match(bridge,/percent\(d\.peakRight\),rawR/);
});
test('M5 fader light is recessed, VST text aligned, no heavyweight hover visual',()=>{
 const css=read('web/styles-src/42-mixer-premium.css');
 const m5=css.slice(css.indexOf('/* RAIL3 — operator-approved'));
 assert.doesNotMatch(m5,/linear-gradient\(to top,transparent/);
 assert.match(m5,/linear-gradient\(90deg,#010508/);
 assert.match(m5,/linear-gradient\(to bottom,/);
 assert.match(m5,/padding-left:17px/);
 assert.match(m5,/font:700 11px/);
 assert.match(m5,/\.clip-led\{[\s\S]*?height:12px/);
 assert.doesNotMatch(m5,/(?:^|\s)(?:filter|backdrop-filter|animation|transition|will-change):/);
});

test('L2 route and VST inserts use one consistent six-row native rack with locked Master',()=>{
 const src=read('web/bridge.js'),css=read('web/mixer-layout.css');
 assert.match(src,/Array\.from\(\{length:6\},\(_,slot\)=>/);
 assert.match(src,/master-locked-six/);
 assert.match(src,/class="master-insert insert empty" disabled aria-disabled="true"/);
 assert.match(src,/slot===0\?"FX UNAVAILABLE":"ADD FX"/);
  assert.match(src,/class="fx-power /);
  assert.match(src,/function fxSlotMarkup\(lane,slot,compact=false\)/);
 assert.match(css,/@media\(max-height:690px\)\{[\s\S]*?--ar-upper-height:165px/);
});

test('GUI-M1 MASTER stays pinned and channel focus never remounts',()=>{
 const app=read('web/app.js'),css=read('web/mixer-layout.css');
 assert.ok(css.includes('position:sticky;right:0'));
 assert.ok(app.includes('function revealMixerChannel(i)'));
 assert.ok(app.includes('revealMixerChannel(i);'));
 const slice=app.slice(app.indexOf('function revealMixerChannel('),app.indexOf('function focusChannel('));
 for(const forbidden of ['renderMixer(', 'setInterval(', 'requestAnimationFrame(', 'emitEvent('])assert.ok(!slice.includes(forbidden),forbidden);
});
test('GUI-M1 PATCH uses real physical columns only',()=>{
 const src=read('web/monitor.js');
 assert.ok(src.includes('const visibleCount=shown.length'));
 assert.ok(!src.includes('for(let k=shown.length;k<patchPageSize;k++)'));
});

test('operator-first console keyboard and contextual workflow stay presentation-only',()=>{
 const app=read('web/app.js'),shell=read('web/mixer-layout.js'),css=read('web/mixer-layout.css');
 assert.match(app,/data-choose="\$\{i\}" role="button" tabindex="0"/);
 assert.match(app,/aria-current="\$\{i===currentChannel\?'true':'false'\}"/);
 assert.match(shell,/window\.addEventListener\('arMixChannelFocus',syncStripContext\)/);
 assert.match(shell,/channelList\?\.addEventListener\('keydown'/);
 assert.match(shell,/channelList\?\.addEventListener\('dblclick'/);
 assert.match(shell,/const tabs=\{'Digit1':'MIX','Digit2':'FX RACK','Digit3':'SIGNAL FLOW'\}/);
 assert.match(shell,/const layers=\{'KeyF':'FX','KeyR':'ROUTE','KeyS':'SENDS'\}/);
 assert.match(css,/\.toolbar \.scene-select\{display:none!important\}/);
 assert.match(css,/\.rail-presets\{\s*display:grid;grid-template-columns:repeat\(2,minmax\(0,1fr\)\)/);
 assert.doesNotMatch(shell,/AudioContext|processBlock|setInterval|addEventListener\(['"]pointermove/);
});

test('standalone preview uses the same source-owned app and mixer shell',()=>{
 const builder=read('scripts/build_ui_css.py'),standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 for(const file of ['web/app.js','web/mixer-layout.css','web/mixer-layout.js']){
   const content=read(file);
   assert.ok(standalone.includes(content),'standalone preview drift: '+file);
 }
 assert.match(builder,/script_start=built\.index\('<script>'\)/);
 assert.match(builder,/\(ROOT\/'web\/app\.js'\)\.read_text/);
});

// Always run PR#278 V6 production material/identity/parity gate in the existing UI suite.
import './operator_visual_identity_v6_contract_test.mjs';
