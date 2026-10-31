// SPDX-License-Identifier: AGPL-3.0-only
// No dependencies; catches regressions between the legacy v0.3 GUI and native host.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const app=readFileSync('web/app.js','utf8');
const bridge=readFileSync('web/bridge.js','utf8');
const engine=readFileSync('src/LiveAudioEngine.h','utf8');
const main=readFileSync('src/Main.cpp','utf8');
const recorder=readFileSync('src/DiagnosticsRecorder.h','utf8');
test('native app never runs demo sine meters',()=>{
  const scheduler=app.indexOf('function scheduleDemoMeter()');
  const fn=app.indexOf('function meters(t)');
  const scheduleGuard=app.indexOf('if(window.__JUCE__?.backend?.emitEvent||document.hidden||',scheduler);
  const timer=app.indexOf('meterTimer=setTimeout(',scheduler);
  const meterGuard=app.indexOf('if(window.__JUCE__?.backend?.emitEvent||document.hidden||',fn);
  const synth=app.indexOf('channels.forEach((ch,i)=>{const wave=',fn);
  assert.ok(scheduler>=0 && scheduleGuard>scheduler && scheduleGuard<timer);
  assert.ok(fn>scheduler && meterGuard>fn && meterGuard<synth);
  assert.ok(app.indexOf('meterRAF=requestAnimationFrame(t=>{meterRAF=0;meters(t)});',scheduler)>scheduler);
});
test('native meter reads DSP and hides only the nine unrouted input channels',()=>{
  assert.ok(bridge.includes('percent(d.inputPeak)'),'CH1 uses native audio input peak');
  assert.ok(bridge.includes('percent(d.peakLeft)') && bridge.includes('percent(d.peakRight)'),'real output L/R peak inputs');
  assert.ok(bridge.includes('percent(d.peak)'),'master aux uses native output peak');
  assert.ok(bridge.includes('window.__arMixSetNativeMeterTarget(el,level)'),'native data is fed to bounded visual presenter');
  assert.ok(!bridge.includes('liveInputLevel-5'),'remove callback-cadence smoothing');
  assert.match(bridge,/track\.dataset\.preview="1"/);
  assert.ok(bridge.includes("meter-fill{opacity:0!important}"));
});
test('quick FX bypass is wired to native slot instead of demo mock',()=>{
  assert.match(bridge,/data-native-fx="bypass" data-slot=/);
  assert.match(bridge,/aria-pressed/);
  assert.match(bridge,/send\("fxBypass",\{slot,value:!state\.chain\[slot\]\?\.bypass\}\)/);
  assert.match(engine,/graphOwned_->bypassFlags\[physical\]\.store/);
  assert.match(engine,/bypassEnvelope\[physical\]/);
});
test('GUI pan event reaches actual callback pan matrix',()=>{
  assert.match(app,/send\(master\?'masterPan':'pan',c\.pan,master\?-1:idx\)/);
  assert.match(main,/name == "pan" && channel == 1/);
  assert.match(engine,/StereoPanMatrix::fromNormalized/);
});
test('health logger has room for complete JSON snapshots',()=>{
  assert.match(recorder,/char data\[4096\]/);
  assert.match(recorder,/truncatedRecords\(\)/);
});

test('MASTER MONO/STEREO is delivered to RT audio as equal post-pan L+R summing',()=>{
  assert.match(app,/case'masterMode':setMasterMono\(!masterMono\)/);
  assert.match(app,/send\('masterMono',masterMono,-1\)/);
  assert.match(app,/masterGain,masterPan,masterMute,masterMono,masterInserts/);
  assert.match(main,/name == "masterMono" && value\.isBool\(\)/);
  assert.match(main,/engine_\.setMasterMono \(bool \(value\)\)/);
  assert.match(engine,/masterMono_ \{ false \}/);
  assert.match(engine,/const bool mono = masterMono_\.load/);
  assert.match(engine,/const float monoL = 0\.5f \* \(matrix\.ll \+ matrix\.rl\)/);
  assert.match(engine,/const float monoR = 0\.5f \* \(matrix\.lr \+ matrix\.rr\)/);
  assert.match(engine,/const float fromL = mono \? monoL/);
  assert.match(engine,/const float fromR = mono \? monoR/);
  assert.match(bridge,/setMasterMono\(d\.masterMono,false\)/);
});

// Regression: MASTER has preview-only FX, not preview-only AUDIO. Do not
// let the generic "data-preview" selector suppress real master output peaks.
test('MASTER stereo meters are never hidden by native preview CSS',()=>{
  assert.ok(bridge.includes('.strip:not(.master-strip)[data-preview=\\\"1\\\"] .meter-fill{opacity:0!important}'));
  assert.ok(bridge.includes('.strip:not(.master-strip)[data-preview=\\\"1\\\"] .meter{opacity:.36}'));
  const start=bridge.indexOf('if(track.classList.contains("master-strip"))');
  const end=bridge.indexOf('const rack=track.querySelector(".rack")',start);
  assert.ok(start>=0&&end>start);
  const masterBranch=bridge.slice(start,end);
  assert.ok(masterBranch.includes('track.removeAttribute("data-preview")'));
  assert.ok(masterBranch.includes('track.dataset.fxPreview="1"'));
  assert.ok(!masterBranch.includes('track.dataset.preview="1"'));
  assert.ok(bridge.includes('percent(d.peakLeft)')&&bridge.includes('percent(d.peakRight)'));
});

// M4 verified-source, all-channel FX contract: a native channel must be
// selected by its ACKed physical route, not by a CH1-only UI fallback.
test('M4 FX console keeps channel routing and plugin identity per native lane',()=>{
  const routes=readFileSync('web/monitor.js','utf8');
  assert.match(bridge,/const nativePatched=!master/);
  assert.match(bridge,/const routed=nativePatched&&!legacy/);
  assert.match(routes,/rackFxDraft\.channels\.includes\(channel\)/);
  assert.match(routes,/channels:\[\.\.\.channels\]\.sort/);
  assert.match(routes,/for\(const channel of rackFxDraft\.channels\)/);
  assert.match(routes,/draftRoutes\[channel\]\.fxIds=current/);
  assert.match(routes,/action:"routingDryApply"/);
  assert.match(bridge,/window\.__arMixOpenChannelPatch\?\.\(id,"input"\)/);
  assert.match(bridge,/data-routed-fx=/);
  assert.match(bridge,/data-routed-bypass=/);
  assert.match(bridge,/data-routed-editor=/);
  assert.match(bridge,/lab\.channelPeaksL/);
  assert.match(bridge,/lab\.channelPeaksR/);
  assert.doesNotMatch(bridge,/Native VST3 is currently available on Channel 1 only/);
});
