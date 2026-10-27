import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const html=read('web/index.html'),app=read('web/app.js'),css=read('web/styles.css'),bridge=read('web/bridge.js'),monitor=read('web/monitor.js'),cpp=read('src/Main.cpp');
test('native tools open directly without Setup workspace',()=>{
 for(const id of ['audioSetupBtn','arSignalOpen'])assert.equal(html.split('id="'+id+'"').length-1,1);
 for(const s of ['data-native-action="audioSetup"','data-plugin-manager="1"','data-my-sounds="1"'])assert.ok(html.includes(s));
 assert.ok(bridge.includes('if(action==="audioSetup"){send("audioSetup");return;}'));
 assert.ok(!bridge.includes('nativeAudioSetup')&&!bridge.includes('renderSetup'));
 const action=cpp.indexOf('if (action == "audioSetup")');
 const open=cpp.indexOf('showDeviceSetup();',action);
 const nextAction=cpp.indexOf('else if (action == "diagnosticRoute")',action);
 assert.ok(action>=0 && open>action && nextAction>open, 'SETUP opens directly after restoring any temporary audio test route');
 assert.ok(monitor.includes("querySelector('#audioSetupBtn')"));
});
test('demo EQ/Setup code is gone; fader double-click returns unity',()=>{
 for(const name of ['editorGraph','renderEditor','data-activeband','data-band',"setView('EDIT')",'renderSetup'])assert.ok(!app.includes(name),name);
 assert.ok(!html.includes('id="editor"')&&!css.includes('content:"FX CHAIN"'));
 assert.ok(app.includes('updateFader(f.dataset.fader,0)'));
});
test('status bar is informational and not an action rack',()=>{
 const footer=html.match(/<footer[^>]*>[\s\S]*?<\/footer>/)?.[0];
 assert.ok(footer&&!footer.includes('<button'));
 assert.ok(!bridge.includes('nativeP2')&&!bridge.includes('addPluginManagerToolbar'));
 assert.ok(!monitor.includes('insertAdjacentHTML'));
 assert.ok(html.includes('id="arSignalBadge"'));
 assert.ok(bridge.includes('getElementById("headerRate")'));
});

test('native meter bridge drives fixed-palette LED reveal by level',()=>{
  for(const src of [bridge,read('web/native-bridge.js')]){
    assert.ok(src.includes('function paintMeter('), 'missing native-meter adapter');
    assert.ok(src.includes('"--level"'), 'native meter requires level percent for stationary clip');
    assert.ok(!src.includes('"--meter-scale"'), 'obsolete compositor-scale property creates redundant writes');
    assert.ok(!src.includes('transition:height 90ms linear'),'height animation triggers needless reflow');
  }
  assert.ok(css.includes('clip-path:inset(calc(100% - var(--led-lit,var(--level,40%))) 0 0)'));
  assert.ok(!css.includes('transform:scaleY(var(--meter-scale,.4))'));
  assert.ok(css.includes('repeating-linear-gradient(to top,transparent 0 5px,#071014 5px 7px)'));
  assert.ok(app.includes('setLedMeterLevel(meter.fill,levelStep/10)'));
  assert.ok(app.includes('const LED_PITCH_PX=7'));
  assert.ok(app.includes('new WeakMap()'));
  assert.ok(bridge.includes('window.__arMixSetLedMeterLevel'));
  assert.ok(read('web/native-bridge.js').includes('window.__arMixSetLedMeterLevel'));
  assert.ok(!bridge.includes('backdrop-filter:blur(5px)'));
  assert.ok(!app.includes("meter.fill.style.setProperty('--meter-scale'"));
});

test('native bridge does not inspect idle pointer movement',()=>{
 const a=bridge.indexOf('let controlEditUntil=0;');
 const b=bridge.indexOf('function syncNativeMixerControls',a);
 assert.ok(a>=0 && b>a);
 const guard=bridge.slice(a,b);
 assert.ok(!guard.includes('"pointermove"'), 'idle pointer listener forbidden');
 assert.ok(guard.includes('typeof drag') && guard.includes('"pointerup"') && guard.includes('"pointercancel"'));
});


test('P0 FX Rack allows selection of every channel and MASTER without falsely loading VST3',()=>{
 for(const marker of ['function rackTarget','function rackOptions','function renderPendingRack','function openRackTarget','data-native-rack-open','data-native-routing-pending','id="nativeRackChannel"']){
   assert.ok(bridge.includes(marker),marker);
 }
 assert.ok(bridge.includes('if(rackTarget()!==0)'));
 assert.ok(bridge.includes('openRackTarget("master")'));
 assert.ok(bridge.includes('rack.dataset.nativeSig="p0-master-pending"'));
 assert.ok(bridge.includes('const signature="p0-unrouted-"+i'));
 assert.ok(app.includes('function renderMasterRackWorkspace'));
 assert.ok(app.includes('masterSelected=false;currentChannel=i;renderMixer()'));
 assert.ok(cpp.includes('channel == 1'), 'P0 must preserve truthful Channel 1-only native DSP');
});
test('P0 selected toolbar pad and fader recess are paint-only refinements',()=>{
 const btn=read('web/styles-src/20-button-180.css');
 const fader=read('web/styles-src/42-mixer-premium.css');
 assert.ok(btn.includes('--b180-color:#b9cf77;--b180-border:#708942;--b180-ink:#263319'));
 assert.ok(fader.includes('width:min(44px,100%)'));
 assert.ok(fader.includes('width:6px'));
 assert.ok(fader.includes('top:7px;bottom:7px'));
 assert.ok(fader.includes('pointer-events:none'));
 assert.ok(css.includes('transform:translate(-50%,-38px)'));
 assert.ok(!fader.includes('backdrop-filter:blur('));
});
