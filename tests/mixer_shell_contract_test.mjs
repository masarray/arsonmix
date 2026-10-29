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
});
