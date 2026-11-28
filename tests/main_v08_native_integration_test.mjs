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
 const constructor=main.slice(main.indexOf('explicit ConsoleWindow ('),main.indexOf('~ConsoleWindow()'));
 const ready=constructor.indexOf('browser_.goToURL');
 assert.ok(ready>=0,'GUI navigation must be started from constructor');
 assert.ok(!constructor.includes('engine_.start ('),'synchronous audio startup must not block the first WebView frame');
 const timer=main.slice(main.indexOf('void timerCallback() override'));
 const readyGate=timer.indexOf('if (!audioEngineStarted_)');
 const startup=timer.indexOf('engine_.start (saved.get())');
 assert.ok(readyGate>=0&&startup>readyGate);
 assert.ok(timer.includes('browser_.loaded')&&timer.includes('guiReadyTick_ < 150u'));
});
test('native LUFS/true peak are shown only with qualified live measurement provenance',()=>{
 assert.match(app,/updateMasterAnalysisPreview\(-31\+/); // preview remains demo-only
 assert.match(bridge,/MEASURED_48K_BS1770_K_4X_FIR/);
 assert.match(bridge,/element\.classList\.toggle\("over-limit",measured&&value>limit\)/);
 // The former synchronous DSP is now analysis-only: exact render PCM is
 // published to a bounded queue and the unchanged meter runs on its worker.
 assert.match(src,/NativeMasterAnalysisTap masterLoudness_/);
 assert.match(src,/const float\* measured\[2\] = \{nativeMain\[0\],nativeMain\[1\]\};\s*if\(!independent_\.active\(\)\)masterLoudness_\.publish\(measured,2,samples\)/);
 assert.match(src,/masterLoudness_\.publish\(measured,juce::jmin\(outputChannels,2\),samples\)/);
 assert.doesNotMatch(src,/masterLoudness_\.process\(/);
 const tap=read('engine/dsp/NativeMasterAnalysisTap.h');
 const producer=tap.slice(tap.indexOf('void publish('),tap.indexOf('Reading read() const')).replace(/\/\/[^\r\n]*/g, ""); // inspect code, not explanatory comments
 assert.match(producer,/queue_->push\(gen,pcm,count,n\)/);
 assert.match(producer,/gapGeneration_\.store\(gen,std::memory_order_release\)/);
 assert.doesNotMatch(producer,/meter\.process|lock_guard|sleep_for|new |make_unique/);
 const worker=tap.slice(tap.indexOf('void run() noexcept'));
 assert.match(worker,/meter\.process\(pcm,b\.channels,b\.frames\)/);
 assert.match(tap,/State::gap/);
 assert.match(tap,/State::stale/);
 assert.match(src,/masterLoudness_\.prepare \(sampleRateHz_\)/);
 assert.match(src,/masterLoudness_\.prepare \(0\.0\)/);
 assert.match(src,/masterAnalysisProvenance/);
 assert.doesNotMatch(bridge,/lufs\.textContent="—"/);
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

test('startup handoff raises mixer once after splash, without permanent topmost or probe focus',()=>{
 const app=main.slice(main.indexOf('class ConsoleApp final'));
 const warm=main.slice(main.indexOf('if (!webViewPeerWarmed_)'),
                       main.indexOf('// Existing message-thread 12 Hz timer',main.indexOf('if (!webViewPeerWarmed_)')));
 assert.match(warm,/setVisible \(true\);/);
 assert.doesNotMatch(warm,/\btoBack\s*\(/,'warm compositor must not bury the mixer');
 assert.match(app,/else if \(stage == 2\)\s*presentMixerOnStartup\(\);/);
 const handoff=app.slice(app.indexOf('void presentMixerOnStartup()'),
                         app.indexOf('void serviceSmartUpdateAcknowledgement()'));
 assert.match(handoff,/if \(startupMixerPresented_ \|\| !window_ \|\| isAutoUpdateHealthProbe\(\)\)/);
 assert.match(handoff,/startupMixerPresented_ = true/);
 assert.match(handoff,/window_->setVisible \(true\);/);
 assert.match(handoff,/window_->setMinimised \(false\);/);
 assert.match(handoff,/window_->toFront \(true\);/);
 assert.match(handoff,/SetWindowPos \(hwnd, HWND_TOPMOST/);
 assert.match(handoff,/SetWindowPos \(hwnd, HWND_NOTOPMOST/);
 assert.match(handoff,/::SetForegroundWindow \(hwnd\)/);
 assert.match(handoff,/window_->grabKeyboardFocus\(\)/);
 assert.doesNotMatch(handoff,/startTimer|engine_\.start|setAlwaysOnTop\s*\(/);
 assert.match(app,/if \(isAutoUpdateHealthProbe\(\)\)\s*\{\s*if \(window_\) window_->setVisible \(false\);/);
});

test('native splash embeds preblurred JPEG and product PNG, preserves guard and responsive centering',()=>{
 const splash=read('src/ArSonMixStartupSplash.h');
 const cmake=read('CMakeLists.txt');
 const artwork=readFileSync('assets/startup/arsonmix-splash-background.jpg');
 assert.match(cmake,/ARMIX_BRAND_DIR\}\/arsonmix-256\.png/);
 assert.match(cmake,/assets\/startup\/arsonmix-splash-background\.jpg/);
 assert.match(splash,/BinaryData::arsonmix256_png,/);
 assert.match(splash,/BinaryData::arsonmix256_pngSize/);
 assert.match(splash,/BinaryData::arsonmixsplashbackground_jpg,/);
 assert.match(splash,/BinaryData::arsonmixsplashbackground_jpgSize/);
 assert.equal((splash.match(/juce::ImageFileFormat::loadFrom/g)||[]).length,2);
 assert.match(splash,/g\.drawImageWithin \(background_, 0, 0, getWidth\(\), getHeight\(\),\s*juce::RectanglePlacement::fillDestination/);
 assert.match(splash,/g\.drawImageWithin \(brand_, 265, 22, 90, 90,\s*juce::RectanglePlacement::centred/);
 assert.match(splash,/g\.drawText \("ArSonMix", 24, 116, 572, 50, juce::Justification::centred\)/);
 assert.match(splash,/width \/ \(cardWidth \+ 24\.0f\), height \/ \(cardHeight \+ 24\.0f\)/);
 assert.match(splash,/\(width - cardWidth \* scale\) \* 0\.5f,\s*\(height - cardHeight \* scale\) \* 0\.5f/);
 assert.match(splash,/g\.fillAll \(juce::Colour \(0xff0b1823\)\)/);
 assert.doesNotMatch(splash,/ColourGradient|g\.drawText \("Ar",|g\.drawText \("SonMix",/);
 assert.doesNotMatch(splash.slice(splash.indexOf('void paint')),/loadFrom|ImageCache|startTimer|repaint\(\)|ImageConvolutionKernel|GaussianBlur/);
 assert.ok(artwork.length<=8*1024,'soft startup JPEG size budget');
 assert.equal(artwork[0],0xff);
 assert.equal(artwork[1],0xd8);
 assert.equal(artwork[artwork.length-2],0xff);
 assert.equal(artwork[artwork.length-1],0xd9);
 let dimensions;
 for(let i=2;i+8<artwork.length;){
   assert.equal(artwork[i],0xff);
   const marker=artwork[i+1];
   const length=artwork[i+2]*256+artwork[i+3];
   assert.ok(length>=2 && i+2+length<=artwork.length);
   if(marker===0xc0){
     dimensions=[artwork[i+7]*256+artwork[i+8],artwork[i+5]*256+artwork[i+6]];
     break;
   }
   assert.notEqual(marker,0xda,'SOF precedes scan');
   i+=2+length;
 }
 assert.deepEqual(dimensions,[384,216]);
 const layout=splash.match(/cardWidth = ([\d.]+)f, cardHeight = ([\d.]+)f/);
 const cap=splash.match(/const float scale = juce::jmin \(([\d.]+)f/);
 assert.ok(layout && cap);
 const cw=Number(layout[1]), ch=Number(layout[2]), maxScale=Number(cap[1]);
 for(const [w,h] of [[740,420],[1366,768],[1646,928],[1920,1080],[3440,1440],[320,200],[1,1]]){
   const s=Math.min(maxScale,w/(cw+24),h/(ch+24));
   const x=(w-cw*s)/2,y=(h-ch*s)/2;
   assert.ok(s>0 && x>=0 && y>=0 && x+cw*s<=w && y+ch*s<=h);
   assert.ok(Math.abs(x+cw*s/2-w/2)<1e-7);
   assert.ok(Math.abs(y+ch*s/2-h/2)<1e-7);
 }
});
