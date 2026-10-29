// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const app=read('web/app.js'), css=read('web/styles.css'), html=read('web/index.html');
const bridge=read('web/bridge.js'), src=read('src/LiveAudioEngine.h'), main=read('src/Main.cpp'), offline=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
test('GUI v0.8 remains the canonical host, not an older v0.5 reset',()=>{
 assert.match(app,/function openChannelIdentity\(/);
 assert.match(app,/function masterFxRows\(/);
 assert.match(app,/class="master-analysis"/);
 assert.ok(app.includes('<span>LUFS</span>'));
 assert.match(app,/dBTP/);
 assert.match(html,/id="channelIdentity"/);
 assert.match(css,/rail-input-footer/);
 assert.match(css,/master-analysis/);
 assert.match(html,/src="bridge\.js"/);
 assert.match(html,/src="monitor\.js"/);
});
test('offline standalone is the byte-matched MAIN v0.8 demo',()=>{
 assert.ok(offline.includes('<script>'+app+'</script>'));
 assert.ok(offline.includes('<style>'+css+'</style>'));
});
test('no fake oscillating VUs inside native JUCE',()=>{
 const scheduler=app.indexOf('function scheduleDemoMeter()');
 const a=app.indexOf('function meters(t)');
 const guard=app.indexOf('if(window.__JUCE__?.backend?.emitEvent||document.hidden||',a);
 const firstSynth=app.indexOf('channels.forEach((ch,i)=>{const wave=',a);
 const schedulerGuard=app.indexOf('if(window.__JUCE__?.backend?.emitEvent||document.hidden||',scheduler);
 const scheduled=app.indexOf('meterTimer=setTimeout(',scheduler);
 assert.ok(a>scheduler && guard>a && guard<firstSynth);
 assert.ok(schedulerGuard>scheduler && schedulerGuard<scheduled);
 assert.match(bridge,/paintMeter\(document\.querySelector\('\[data-meter="masterL"\]'\)/);
 assert.match(bridge,/paintMeter\(document\.querySelector\('\[data-meter="masterR"\]'\)/);
 assert.ok(bridge.includes('percent(d.peakLeft)') && bridge.includes('percent(d.peakRight)'));
 assert.ok(!bridge.includes('liveOutputLeft-5'),'do not smooth on the 12Hz source callback');
});
test('native mixer always displays DSP gain, pan, and mute instead of stale demo session',()=>{
 assert.match(src,/setProperty \("channelDb", juce::Decibels::gainToDecibels/);
 assert.match(src,/setProperty \("masterDb", juce::Decibels::gainToDecibels/);
 assert.match(bridge,/function syncNativeMixerControls\(d\)/);
 assert.match(bridge,/syncNativeMixerControls\(d\);/);
 assert.match(bridge,/channel\.mute=d\.channelMute/);
 assert.match(bridge,/masterMute=d\.masterMute/);
 assert.match(bridge,/Math\.abs\(channel\.fader-channelDb\)/);
 assert.match(bridge,/const editing=Date\.now\(\)<controlEditUntil/);
 assert.match(bridge,/!editing && Math\.abs\(channel\.fader-channelDb\)/);
 assert.match(bridge,/controlEditUntil=Date\.now\(\)\+350/);
});
test('master native preview restrictions run before source channel rack lookup',()=>{
 const a=bridge.indexOf('if(track.classList.contains("master-strip"))');
 const b=bridge.indexOf('const rack=track.querySelector(".rack")',a);
 assert.ok(a>0 && b>a);
 assert.match(bridge,/pan\.style\.pointerEvents="none"/);
 assert.match(bridge,/observe\(mixer,\{childList:true,subtree:true\}\)/);
 assert.match(bridge,/marker\.textContent!=="NATIVE PEAKS"/);
});
test('editor opening cannot race with pending graph retirement',()=>{
 const a=main.indexOf('else if (action == "fxEditor")');
 const guard=main.indexOf('if (engine_.isGraphSwitchPending())',a);
 const open=main.indexOf('engine_.pluginAt (slot)',a);
 assert.ok(a>0 && guard>a && guard<open);
});
test('native plugin state and graph preserved',()=>{
 assert.match(src,/handoff_\.beginCycle\(/);
 assert.match(src,/handoff_\.stage \(/);
 assert.match(bridge,/arMixNativePresetLoaded/);
 assert.match(bridge,/Master PAN DSP not yet implemented/);
 assert.match(bridge,/data-fxpower/);
});

test('startup never opens an audio device before the WebView2 GUI handshake',()=>{
 const constructor=main.slice(main.indexOf('explicit ConsoleWindow('),main.indexOf('~ConsoleWindow()'));
 const ready=constructor.indexOf('browser_.goToURL');
 assert.ok(ready>=0,'GUI navigation must be started from constructor');
 assert.ok(!constructor.includes('engine_.start ('),'synchronous audio startup must not block the first WebView frame');
 const timer=main.slice(main.indexOf('void timerCallback() override'));
 const readyGate=timer.indexOf('if (!audioEngineStarted_)');
 const startup=timer.indexOf('engine_.start (saved.get())');
 assert.ok(readyGate>=0&&startup>readyGate);
 assert.ok(timer.includes('browser_.loaded')&&timer.includes('guiReadyTick_ < 150u'));
});
test('master readout remains native-unavailable until measured BS.1770/true peak exists',()=>{
 assert.match(app,/updateMasterAnalysisPreview\(-31\+/);
 assert.match(app,/\['masterLufs',lufs,-12\]/);
 assert.match(app,/\['masterDbtp',dbtp,-1\]/);
 assert.match(bridge,/lufs\.textContent="—"/);
 assert.match(bridge,/tp\.textContent="—"/);
 assert.doesNotMatch(app, /STREAM LEVELS <span>SIM<\/span>/);
});

test('startup uses splash before constructing WebView2 and native engine',()=>{
 const splash=read('src/ArSonMixStartupSplash.h');
 assert.match(splash,/class ArSonMixStartupSplash/);
 assert.match(splash,/setOpaque \(true\)/);
 assert.doesNotMatch(splash,/WebBrowserComponent|AudioDeviceManager|repaintTimer/);
 const startup=main.slice(main.indexOf('class ConsoleApp final'));
 assert.ok(startup.indexOf('make_unique<ArSonMixStartupSplash>') <
           startup.indexOf('make_unique<ConsoleWindow>'));
 assert.match(startup,/startTimer \(90\)/);
 assert.match(startup,/if \(stage >= 2\)/);
 assert.match(main,/onStartupStage_ \(1\)/);
 assert.match(main,/onStartupStage_ \(2\)/);
});
