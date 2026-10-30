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
  assert.match(css,/--ar-upper-height:208px/);
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
test('gain tracking lights lower slit on channel and MASTER without heavy visual effects',()=>{
  const app=read('web/app.js'),css=read('web/styles-src/42-mixer-premium.css');
  assert.equal((app.match(/style="--fader-open:\$\{\+\(100-levelToTop/g)||[]).length,2);
  assert.ok(app.includes("host.style.setProperty('--fader-open'"));
  assert.ok(app.includes("el.style.setProperty('--fader-open'"));
  const finish=css.slice(css.indexOf('/* M4: Keep dBFS ticks'));
  assert.ok(finish.includes('background:linear-gradient(to top,'));
  assert.doesNotMatch(finish,/(?:^|\s)(?:filter|backdrop-filter|animation|transition):/);
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
test('native readback updates blue lower slit with cap on all channel/master paths',()=>{
  const native=read('web/bridge.js');
  assert.equal((native.match(/style\.setProperty\("--fader-open",\(100-top\)\.toFixed\(3\)\+"%"\)/g)||[]).length,3);
  assert.match(native,/element\.getAttribute\("aria-label"\)!==accessible/);
});
