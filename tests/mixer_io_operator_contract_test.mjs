// G1/G2: reject UI-only hardware claims and controller duplication.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(new URL('../'+p,import.meta.url),'utf8');
const gui=read('web/mixer-io.js'),monitor=read('web/monitor.js');
const main=read('src/Main.cpp'),cmake=read('CMakeLists.txt'),html=read('web/index.html');
test('single native controller stages MONO/STEREO through muted APPLY + revision ACK',()=>{
  assert.match(gui,/__arMixStageChannelInput/);
  assert.match(gui,/__arMixApplyMixerDraft/);
  assert.match(monitor,/patchPreflight\(\)/);
  assert.match(monitor,/window\.__arMixApplyMixerDraft/);
  assert.match(monitor,/routingDryApply/);
  assert.match(monitor,/arMixRoutingLabAck/);
  assert.match(monitor,/draftEpoch!==epoch/);
  assert.match(monitor,/const active=new Set\(patchPorts\(\)/);
  assert.match(monitor,/if\(state\.monitor\)/);
});
test('FX rack stays mounted, mixer bank only changes presentation',()=>{
  assert.match(gui,/\.insertAdjacentElement\('afterend',bank\)/);
  assert.match(gui,/mixer\.dataset\.ioMode=mode/);
  assert.doesNotMatch(gui,/new AudioContext|AudioWorklet|createPluginInstance|processBlock/);
  assert.match(read('web/mixer-io.css'),/\.rack\{display:none!important\}/);
});
test('MS3A MAIN physical output patch stages through existing muted native route ACK; AUX is internal',()=>{
  assert.match(gui,/s\.outputPorts/);
  assert.match(gui,/__arMixStageMainOutputs/);
  assert.match(gui,/\.arIOMainOutput/);
  assert.match(gui,/Only native-ACKed MAIN\/MONITOR pairs can carry sound/);
  assert.match(gui,/internal stereo FX returns/);
  assert.match(monitor,/window\.__arMixStageMainOutputs/);
  assert.match(monitor,/mainOutputs:draftMainOutputs/);
  assert.match(monitor,/routingDryApply/);
  assert.doesNotMatch(gui,/audioSelectDevice|setCurrentAudioDeviceType/);
});
test('native device inventory and packaged WebView2 assets are wired',()=>{
  assert.match(main,/action == "audioDeviceInventory"/);
  assert.match(main,/getAvailableDeviceTypes\(\)/);
  assert.match(main,/arMixAudioDeviceInventory/);
  for(const f of ['mixer-io.js','mixer-io.css']){
    assert.ok(html.includes(f));
    assert.ok(cmake.includes('web/'+f));
    assert.ok(main.includes('/'+f));
  }
});

test('G3 inline AUX sends mutate only one native routing draft with bounded PRE/POST semantics',()=>{
  assert.match(gui,/__arMixStageAuxSendEdit/);
  assert.match(gui,/__arMixStageAuxReturnEdit/);
  assert.match(gui,/data-send-focus/);
  assert.match(gui,/data-send-gain/);
  assert.match(gui,/data-send-pan/);
  assert.match(gui,/data-send-enable/);
  assert.match(gui,/data-send-apply/);
  assert.match(gui,/data-send-revert/);
  assert.match(monitor,/window\.__arMixStageAuxSendEdit=/);
  assert.match(monitor,/window\.__arMixStageAuxReturnEdit=/);
  assert.match(monitor,/window\.__arMixRevertMixerDraft=/);
  assert.match(monitor,/if\(draftSends\.length>=32\)/);
  assert.match(monitor,/original\.gain=0|item\.gain=0/);
  assert.match(monitor,/drawAuxSendMatrix\(\);drawSends\(\)/);
  assert.match(monitor,/window\.dispatchEvent\(new Event\("arMixRoutingDraftChanged"\)\)/);
  assert.match(monitor,/if\(patchPending\)return/);
  assert.match(monitor,/if\(state\.monitor\)/);
  assert.match(monitor,/api\.emitEvent\("arMixAction",\{action:"routingDryApply"/);
  assert.doesNotMatch(gui,/createPluginInstance|AudioContext|AudioWorklet|processBlock/);
});
test('G3 is computed from authoritative PRE/POST native renderer, not a visual-only mixer',()=>{
  const native=read('tests/dedicated_aux_bus_test.cpp');
  assert.match(native,/SendTap::preFader/);
  assert.match(native,/SendTap::postFader/);
  assert.match(native,/Zero source fader: PRE-FADER send stays audible/);
  assert.match(native,/duplicateSend/);
  assert.match(native,/invalidCount/);
  const css=read('web/mixer-io.css');
  assert.match(css,/\.arSendEditor\[hidden\]/);
  assert.match(css,/max-height:calc\(100dvh - 16px\)/);
  assert.doesNotMatch(css,/backdrop-filter\s*:|filter\s*:\s*blur/);
});

test('MS5/M1 Sends-on-Faders live POST gain uses native ACK; topology remains guarded',()=>{
  const css=read('web/mixer-io.css');
  assert.match(gui,/data-sof-bus/);
  assert.match(gui,/data-sof-toggle/);
  assert.match(gui,/function sofToggle\(\)/);
  assert.match(gui,/function sofStage\(channel,action,value\)/);
  assert.match(gui,/__arMixSetLiveAuxSendGain/);
  assert.match(monitor,/arMixAuxSendGainAck/);
  assert.match(monitor,/routingAuxSendGain/);
  assert.match(gui,/tap:'POST',action/);
  assert.match(gui,/s\.epoch!==sofEpoch/);
  assert.match(gui,/s\.pending/);
  assert.match(gui,/if\(next!=='SENDS'&&sofBus>=0\)leaveSof\(\)/);
  assert.match(gui,/function sofMove\(event\)/);
  assert.match(gui,/document\.addEventListener\('pointerup'/);
  assert.match(gui,/mixer\.dataset\.sofActive=String\(active\)/);
  assert.match(css,/\.arSofLayer\{/);
  assert.match(css,/\[data-sof-active="true"\]/);
  assert.doesNotMatch(css,/backdrop-filter\s*:|filter\s*:\s*blur/);
  assert.doesNotMatch(gui,/new AudioContext|AudioWorklet|requestAnimationFrame/);
});

test('MS1 typed native processing format stays independent of physical source and bounded to existing L2 bank',()=>{
  const decoder=read('engine/core/NativeDryRouteCommand.h');
  const lab=read('src/LiveDryRoutingLab.h');
  const css=read('web/mixer-io.css');
  assert.match(decoder,/schemaVersion/);
  assert.match(decoder,/sourceKind.*expectedKind/);
  assert.match(decoder,/sourceFormat.*expectedSourceFormat/);
  assert.match(decoder,/channelFormat.*"MONO"/);
  assert.match(monitor,/window\.__arMixStageChannelFormat=/);
  assert.match(monitor,/schemaVersion:2/);
  assert.match(monitor,/channels:draftRoutes\.map\(r=>\(\{\.\.\.typedSource\(r\),fxIds:laneIds\(r\)\}\)\)/);
  assert.match(monitor,/data-patch-channel-format/);
  assert.match(monitor,/draft\.channelFormat/);
  assert.match(gui,/class="arBankChannelFormat"/);
  assert.match(gui,/__arMixStageChannelFormat/);
  assert.match(css,/\.arBankFormatRow\{display:flex/);
  assert.match(lab,/strip\.source==armix::mixer::SourceMode::mono/);
});

test('IO-MATRIX uses only verified pins, dual-step stereo draft and native ACK without new DSP',()=>{
  const css=read('web/mixer-io.css');
  assert.match(gui,/matrixPorts\(s,kind\)/);
  assert.match(gui,/matrixPageSize=8/);
  assert.match(gui,/const matrixDraft=new Map\(\)/);
  assert.match(gui,/candidate\.left===candidate\.right/);
  assert.match(gui,/__arMixStageChannelInput\?\./);
  assert.match(gui,/__arMixStageMainOutputs\?\./);
  assert.match(gui,/!hardwareReady\(s\)/);
  assert.match(gui,/\.dataset\.ioMatrixKind/);
  assert.match(gui,/s\.mainOutputs/);
  assert.match(gui,/\.dataset\.ioRowMode/);
  assert.match(css,/\.arIOMatrixCell\[data-assigned="true"\]/);
  assert.doesNotMatch(css,/backdrop-filter\s*:|filter\s*:\s*blur/);
  assert.match(main,/assets\/audio-interface\.svg/);
  assert.match(cmake,/web\/assets\/audio-interface\.svg/);
});

test('MS3B independent physical MONITOR publishes in existing native ACK transaction',()=>{
  const native=read('src/LiveDryRoutingLab.h');
  const graph=read('engine/core/LiveChannelFxGraph.h');
  const block=read('engine/core/PreparedMixerBlock.h');
  assert.match(gui,/matrixOutputPin\(row,pin\)/);
  assert.match(gui,/arIOMonitorSendGrid/);
  assert.match(gui,/__arMixStageMonitorPatch/);
  assert.match(gui,/__arMixStageMonitorSend/);
  assert.match(monitor,/window\.__arMixStageMonitorPatch/);
  assert.match(monitor,/window\.__arMixStageMonitorSend/);
  assert.match(monitor,/monitorOutputs:draftMonitorOutputs\.slice\(\)/);
  assert.match(native,/setMonitorOutputBus/);
  assert.match(native,/monitorPeakL_/);
  assert.match(graph,/monitorEnabled_/);
  assert.match(block,/monitorGains/);
  assert.doesNotMatch(gui,/AudioContext|AudioWorklet|createPluginInstance|processBlock/);
});


test('PATCH1 operator-first mono and nonadjacent stereo stage into ONE native transaction',()=>{
  const css=read('web/mixer-io.css');
  assert.match(gui,/function makeQuickInputs\(s\)/);
  assert.match(gui,/function stageQuickInput\(button\)/);
  assert.match(gui,/__arMixStageChannelInput\?\.\(channel,mode/);
  assert.match(gui,/mode==='STEREO'.*left===right/);
  assert.match(gui,/window\.__arMixOpenAudioIO=openIO/);
  assert.match(gui,/if\(b\.dataset\.mixPatch==='input'\)openIO\('inputs',channel\)/);
  assert.match(gui,/matrixPorts\(s,'inputs'\)/);
  assert.match(gui,/SAME NATIVE ROUTING/);
  assert.match(gui,/A separate Windows capture endpoints cannot be silently combined|Separate Windows capture endpoints cannot be silently combined/);
  assert.match(css,/\.arIOQuickPatch\{/);
  assert.match(css,/\.arIOQuickRow\[data-state="draft"\]/);
  assert.match(css,/\.arIOQuickRow\[data-state="live"\]/);
  assert.doesNotMatch(css,/backdrop-filter\s*:|filter\s*:\s*blur/);
});

test('SETUP1 actual JUCE device picker and available ASIO WASAPI DirectSound backend',()=>{
 const io=read('web/mixer-io.js'),native=read('src/Main.cpp'),cm=read('CMakeLists.txt');
 for(const flag of ['JUCE_ASIO=1','JUCE_WASAPI=1','JUCE_DIRECTSOUND=1'])assert.ok(cm.includes(flag));
 assert.match(native,/manager, 1, 64, 1, 64, false, false, false, false/);
 assert.match(native,/action == "audioSetup"/);
 assert.match(native,/action == "audioDeviceInventory"/);
 assert.match(io,/function openIO\(tab='device',channel=null\)/);
 assert.match(io,/arIODeviceChoose/);
 assert.match(io,/arIOBackendBadge/);
 assert.match(io,/DirectSound · legacy/);
 assert.match(io,/backend\.emitEvent\('arMixAction',\{action:'audioSetup'\}\)/);
 assert.doesNotMatch(io,/audioSelectDevice|setCurrentAudioDeviceType/);
});
test('SETUP1 honest bus patch has real AUX/MAIN/MONITOR navigation, no fake GROUP DCA',()=>{
 const patch=read('web/monitor.js');
 assert.match(patch,/AUX \/ BUSSING/);
 assert.match(patch,/arBusJumpSends/);
 assert.match(patch,/arBusJumpReturns/);
 assert.match(patch,/arBusJumpOutputs/);
 assert.match(patch,/NOT IMPLEMENTED: AUDIO GROUP · DCA · MATRIX · CUE/);
 assert.match(patch,/window\.__arMixOpenAudioIO\?\.\("outputs"\)/);
 assert.match(patch,/drawAuxSendMatrix\(\)/);
});


test('SOURCE1 mute-only instant SOURCE uses one native ACK; armed output is rejected',()=>{
 const io=read('web/mixer-io.js'),monitor=read('web/monitor.js');
 assert.match(io,/__arMixCommitMutedSource\?\.\(channel,mode,left,right\)/);
 assert.match(io,/quickSourcePending/);
 assert.match(io,/PENDING · NATIVE ACK/);
 assert.match(monitor,/window\.__arMixCommitMutedSource=/);
 assert.match(monitor,/if\(state\.monitor\)/);
 assert.match(monitor,/window\.__arMixApplyMixerDraft\(\)/);
 assert.match(monitor,/window\.__arMixRevertMixerDraft\(\)/);
 assert.doesNotMatch(io,/new AudioContext\(/);
});
