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

test('quantised LED meter avoids redundant raw CSS writes while preserving diagnostics',()=>{
 assert.ok(app.includes('meterRawStyleWrites=new URLSearchParams(location.search)'));
 assert.ok(app.includes('if(state.lit!==lit)'));
 assert.ok(app.includes('else if(meterRawStyleWrites&&levelChanged)'));
 assert.ok(app.includes("'meter-raw'"));
 assert.ok(monitor.includes('const setTextIfChanged='));
 assert.ok(bridge.includes('footer.textContent!==text'));
});

test('native 12Hz status uses one capped shared visual meter scheduler',()=>{
 assert.ok(app.includes('const NATIVE_METER_FRAME_MS=34;'));
 assert.ok(app.includes('const nativeMeterStates=new Map()'));
 assert.ok(app.includes('function nativeMeterStep('));
 assert.ok(app.includes('function scheduleNativeMeterFrame('));
 assert.ok(app.includes('if(nativeMeterTimer||nativeMeterRAF||document.hidden)return;'));
 assert.ok(app.includes("window.__arMixSetNativeMeterTarget=setNativeMeterTarget"));
 assert.ok(bridge.includes('window.__arMixSetNativeMeterTarget(el,level)'));
 assert.ok(!bridge.includes('liveInputLevel-5'), 'release must use elapsed time, not telemetry count');
 assert.ok(!bridge.includes('liveOutputLeft-5'));
 assert.ok(!app.includes('setInterval(animateNativeMeters'), 'no perpetual meter frame timer');
});

// Check the light-weight, source-labelled capture triage contract in the same
// existing Node test invocation; no second CI job or browser required.
import './audio_capture_triage_contract_test.mjs';
