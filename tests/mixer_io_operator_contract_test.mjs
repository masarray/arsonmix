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

test('M1 AUX MASTER and selected-channel workflow share one native graph and ACK',()=>{
  const native=read('engine/core/PreparedMixerBlock.h');
  const graph=read('engine/core/LiveChannelFxGraph.h');
  const lab=read('src/LiveDryRoutingLab.h');
  const engine=read('src/LiveAudioEngine.h');
  const css=read('web/mixer-io.css');
  assert.match(native,/bool setAuxBusGain\(int bus,float gain\)/);
  assert.match(native,/auxBusTarget_\[b\]\.load\(std::memory_order_acquire\)/);
  assert.match(graph,/block_->setAuxBusGain\(bus,gain\)/);
  assert.match(lab,/publisher_\.updateActiveControl\(expectedEpoch,expectedRevision/);
  assert.match(engine,/setDryRoutingLabAuxBusGain/);
  assert.match(main,/routingAuxBusGain/);
  assert.match(monitor,/arMixAuxBusGainAck/);
  assert.match(gui,/__arMixSetLiveAuxMasterGain/);
  assert.match(gui,/arChannelSignalFlow/);
  assert.match(gui,/refreshFocus\(s\)/);
  assert.match(gui,/data-focus-step="source"/);
  assert.match(gui,/data-focus-step="fx"/);
  assert.match(gui,/data-focus-step="sends"/);
  assert.match(gui,/data-focus-step="output"/);
  assert.match(css,/\.arChannelSignalFlow/);
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

test('DEVICE1 existing matrix drives real JUCE manager with gated native readback, no selector popup',()=>{
 const io=read('web/mixer-io.js'),native=read('src/Main.cpp'),cm=read('CMakeLists.txt');
 for(const flag of ['JUCE_ASIO=1','JUCE_WASAPI=1','JUCE_DIRECTSOUND=1'])assert.ok(cm.includes(flag));
 assert.match(native,/manager, 1, 64, 1, 64, false, false, false, false/); // hidden fallback only
 assert.match(native,/action == "audioDeviceApply"/);
 assert.match(native,/setAudioDeviceSetup\(candidate,true\)/);
 assert.match(native,/beginManualAudioSetup\(\)/);
 assert.match(native,/finishManualAudioSetup\(\)/);
 assert.match(native,/RESTORED_MUTED/);
 assert.match(native,/RECOVERY_REQUIRED/);
 assert.match(native,/arMixAudioDeviceApplyAck/);
 assert.match(io,/function openIO\(tab='device',channel=null\)/);
 for(const name of ['arIODeviceBackend','arIODeviceCapture','arIODevicePlayback',
                    'arIODeviceRate','arIODeviceBuffer','arIODevicePin','arIODeviceApply'])
   assert.ok(io.includes(name));
 assert.match(io,/backend\.emitEvent\('arMixAction',\{action:'audioDeviceApply'/);
 assert.doesNotMatch(io,/backend\.emitEvent\('arMixAction',\{action:'audioSetup'\}\)/);
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


test('SOURCE quick switch is live only for verified same format; structural fallback remains muted',()=>{
 const io=read('web/mixer-io.js'),monitor=read('web/monitor.js');
 assert.match(io,/__arMixCommitSelectedSource\?\.\(channel,mode,left,right\)/);
 assert.match(io,/quickSourcePending/);
 assert.match(io,/PENDING · NATIVE ACK/);
 assert.match(monitor,/window\.__arMixCommitMutedSource=/);
 assert.match(monitor,/if\(state\.monitor\)/);
 assert.match(monitor,/window\.__arMixApplyMixerDraft\(\)/);
 assert.match(monitor,/window\.__arMixRevertMixerDraft\(\)/);
 assert.doesNotMatch(io,/new AudioContext\(/);
});


test('P0 FIRST SOUND raw capture has per-pin native callback truth BEFORE route and output ARM',()=>{
  const native=read('src/LiveAudioEngine.h');
  const pcm=read('engine/core/CaptureBoundaryEvidence.h');
  const ui=read('web/mixer-io.js');
  const html=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
  const css=read('web/mixer-io.css');
  const start=native.indexOf('// P0: SILENT output must not mean blind capture diagnostics.');
  assert.ok(start>0,'unpatched native callback must retain actual PCM observation');
  const end=native.indexOf('const std::uint64_t beginUs',start);
  assert.ok(end>start,'early mute-only callback path must precede full DSP renderer');
  const early=native.slice(start,end);
  assert.match(early,/captureBoundary_\.observe\(inputs/);
  assert.match(early,/captureObservationBlocks_\.fetch_add\(1/);
  assert.match(early,/callbackInputs_\.store\(inputChannels/);
  assert.match(early,/inputPeak_\.store\(juce::jmin\(1\.f,observedPeak\)/);
  assert.doesNotMatch(early,/processLegacyChannelOne|processBlock\(/);
  assert.match(native,/captureCallbackPeaks/);
  assert.match(native,/captureCallbackSignalBlocks/);
  assert.match(native,/capturePortProvenance/);
  assert.match(native,/captureObservationBlocks/);
  assert.match(native,/captureBoundary_\.reset\(\); \/\/ no stale pin peaks after disconnect/);
  assert.match(pcm,/maxObservedPorts = 64/);
  assert.match(pcm,/float portPeak\(int callbackPort\) const noexcept/);
  assert.match(pcm,/portSignalBlocks\(int callbackPort\) const noexcept/);
  assert.match(pcm,/perPortPeak_\[port\]\.store\(channelPeak/);
  assert.match(ui,/const capturePinSummary=\(\)=>/);
  assert.match(ui,/a\.deviceGeneration\)!==Number\(d\.deviceGeneration\)/);
  assert.match(ui,/a\.inputNames\?\.\[port\]/);
  assert.match(ui,/RAW INPUTS SILENT/);
  assert.match(ui,/RAW PCM NOW/);
  assert.match(ui,/arIODeviceCaptureEvidence/);
  const jsTag='<script id="arsonmix-mixer-io-js">';
  const cssTag='<style id="arsonmix-mixer-io-css">';
  assert.equal(html.slice(html.indexOf(jsTag)+jsTag.length,html.indexOf('</script>',html.indexOf(jsTag))),
               ui,'WebView + standalone exact raw PCM UI parity');
  assert.equal(html.slice(html.indexOf(cssTag)+cssTag.length,html.indexOf('</style>',html.indexOf(cssTag))),
               css,'WebView + standalone exact raw PCM style parity');
});
test('P0 MAONO: staged ASIO switch never advertises live audio and cannot APPLY while output armed',()=>{
  const js=read('web/mixer-io.js');
  const main=read('src/Main.cpp');
  const engine=read('src/LiveAudioEngine.h');
  const start=js.indexOf('const deviceSwitchBlocker=s=>');
  const end=js.indexOf('function deviceApply()',start);
  assert.ok(start>0&&end>start,'device safety decision must be shared by render and action');
  const guard=js.slice(start,end);
  assert.match(guard,/lastStatus\.secondaryOutputArmed/);
  assert.match(guard,/s\?\.monitor\|\|lastStatus\.monitor/);
  assert.match(guard,/PRESS MUTE OUTPUT above before APPLY DEVICE/);
  assert.match(guard,/s\?\.pending/);
  assert.match(guard,/Maono MIC or VC In 3\+4/);
  assert.match(js,/ASIO DEVICE SELECTED · NOT ACTIVE · current engine still uses/);
  assert.match(js,/const blocked=deviceSwitchBlocker\(route\(\)\)/);
  assert.match(js,/const deviceBlocker=deviceSwitchBlocker\(s\)/);
  assert.match(js,/disabled=!deviceDirty\(\)\|\|!!deviceBlocker/);
  assert.match(js,/deviceNote=ack\?\.ok\?'DEVICE ACCEPTED · OUTPUT MUTED · '\+devicePinNextStep\(\)/);
  assert.match(main,/!engine_\.allOutputsDisarmed\(\)/);
  assert.match(main,/opened->getActiveInputChannels\(\)!=actual\.inputChannels/);
  assert.match(main,/opened->getCurrentSampleRate\(\)-actual\.sampleRate/);
  assert.match(engine,/return isOutputMuted\(\) && !secondaryOutput_\.armed\(\)/);
  const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
  const jsTag='<script id="arsonmix-mixer-io-js">';
  const cssTag='<style id="arsonmix-mixer-io-css">';
  const inlineJs=standalone.slice(standalone.indexOf(jsTag)+jsTag.length,
                                  standalone.indexOf('</script>',standalone.indexOf(jsTag)));
  const inlineCss=standalone.slice(standalone.indexOf(cssTag)+cssTag.length,
                                   standalone.indexOf('</style>',standalone.indexOf(cssTag)));
  assert.equal(inlineJs,js,'standalone and native mixer I/O must ship identical device gates');
  assert.equal(inlineCss,read('web/mixer-io.css'),'standalone and native device layout must match');
  assert.doesNotMatch(guard,/emitEvent\(/,'guard must never secretly switch or rearm hardware');
});

test('DEVICE2 native device list is cached on overlay open; explicit scan is fail-closed',()=>{
 const native=read('src/Main.cpp'),gui=read('web/mixer-io.js');
 const section=native.slice(native.indexOf('else if (action == "audioDeviceInventory")'),
                            native.indexOf('else if (action == "audioDeviceApply")'));
 assert.match(section,/emitDeviceInventory\(false\)/);
 assert.match(section,/action == "audioDeviceRefresh"/);
 assert.match(section,/emitDeviceInventory\(true\)/);
 assert.doesNotMatch(section,/scanForDevices/);
 const inventory=native.slice(native.indexOf('void emitDeviceInventory('),
                              native.indexOf('juce::var currentDeviceSnapshot()'));
 assert.match(inventory,/explicitRescan && scanSafe/);
 assert.match(inventory,/engine_\.allOutputsDisarmed\(\)/);
 assert.match(inventory,/type->scanForDevices\(\)/);
 assert.match(inventory,/scanDurationMs/);
 assert.match(gui,/rescan\?'audioDeviceRefresh':'audioDeviceInventory'/);
 assert.match(gui,/SCAN_ERROR|BLOCKED_ARMED/);
 const apply=native.slice(native.indexOf('void applyCustomAudioDevice('),
                          native.indexOf('void showDeviceSetup()',native.indexOf('void applyCustomAudioDevice(')));
 assert.match(apply,/opened->getActiveInputChannels\(\)!=actual\.inputChannels/);
 assert.match(apply,/opened->getActiveOutputChannels\(\)!=actual\.outputChannels/);
 assert.match(apply,/opened->getCurrentBufferSizeSamples\(\)!=actual\.bufferSize/);
});

test('SOURCE2 selected-channel popover selects verified physical ports without a second router',()=>{
 const js=read('web/mixer-io.js');
 assert.match(js,/function refreshFocus\(s\)/);
 assert.match(js,/const openSourceChooser=channel=>/);
 assert.match(js,/window\.__arMixCommitSelectedSource\?\.\(/);
 assert.match(js,/sourceNote\.textContent/);
 assert.match(js,/sourceChooser\.addEventListener\('click'/);
 assert.match(js,/portsFor\(s,r\|\|/);
 assert.match(js,/NONADJACENT L\/R/);
 assert.doesNotMatch(js,/new AudioContext\(/);
});


test('M1 faders send continuous coalesced ACK-backed params without structural APPLY',()=>{
 const gui=read('web/mixer-io.js'),monitor=read('web/monitor.js'),html=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 assert.match(gui,/sofControls\.addEventListener\('input',sendAuxMaster\)/);
 assert.match(gui,/const now=performance\.now\(\)/);
 assert.match(gui,/Math\.abs\(d\.db-d\.lastSentDb\)>=\.25/);
 assert.match(gui,/window\.__arMixSetLiveAuxSendGain\?\.\(/);
 assert.match(monitor,/const liveSendLatest=new Map\(\)/);
 assert.match(monitor,/const auxMasterLatest=new Map\(\)/);
 assert.match(monitor,/LIVE SEND COALESCED/);
 assert.match(monitor,/AUX MASTER COALESCED/);
 assert.match(monitor,/if\(matched&&latest!==undefined/);
 assert.match(monitor,/if\(ok&&latest!==undefined/);
 assert.match(html,/Math\.abs\(d\.db-d\.lastSentDb\)>=\.25/);
});


test('M1 named input sources are persisted only for native device and enabled pin identity',()=>{
 const ui=read('web/mixer-io.js');
 const html=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 for(const src of [ui,html]){
  assert.match(src,/const sourceIdentity=s=>/);
  assert.match(src,/d\.backend,d\.captureDevice,pins\.map/);
  assert.match(src,/const validAlias=\(a,pins\)=>/);
  assert.match(src,/localStorage\.setItem\(key,JSON\.stringify\(items\)\)/);
  assert.match(src,/const aliasValue=a=>/);
  assert.match(src,/window\.__arMixCommitSelectedSource\?\.\(/);
  assert.match(src,/data-source-alias-save/);
  assert.match(src,/if\(!sourceIdentity\(route\(\)\)\)deviceRefresh\(\)/);
 }
});


test('M1 SOURCE safe unmuted handoff uses native input crossfade with immutable FX graph',()=>{
 const ui=read('web/mixer-io.js'),monitor=read('web/monitor.js');
 const main=read('src/Main.cpp'),engine=read('src/LiveAudioEngine.h');
 const lab=read('src/LiveDryRoutingLab.h'),block=read('engine/core/PreparedMixerBlock.h');
 assert.match(ui,/window\.__arMixCommitSelectedSource\?\.\(/);
 assert.match(ui,/arMixSourceSwitchResult/);
 assert.match(monitor,/window\.__arMixCommitSelectedSource=/);
 assert.match(monitor,/action:"routingSourceSwitch"/);
 assert.match(monitor,/arMixSourceSwitchAck/);
 assert.match(main,/action=="routingSourceSwitch"/);
 assert.match(engine,/switchDryRoutingLabSource\(/);
 assert.match(lab,/switchPhysicalSource\(/);
 assert.match(block,/physicalTarget_\[strip\]\.store/);
 assert.match(block,/fade\.remaining=64/);
 assert.doesNotMatch(ui,/new AudioContext\(/);
});

test('M1 focused SEND editor uses native live gain for accepted POST and AUX RETURN without structural APPLY',()=>{
 const gui=read('web/mixer-io.js'),html=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 for(const src of [gui,html]){
  assert.match(src,/function sendEditorLiveReady\(focus\)/);
  assert.match(src,/__arMixSetLiveAuxMasterGain\?\.\(/);
  assert.match(src,/__arMixSetLiveAuxSendGain\?\.\(/);
  assert.match(src,/field==='gain'&&tap==='POST'&&sendEditorLiveReady/);
  assert.match(src,/x\.tap==='POST'/);
  assert.match(src,/__arMixStageAuxSendEdit\?\.\(/);
  assert.match(src,/__arMixStageAuxReturnEdit\?\.\(/);
  assert.match(src,/PRE and structural edits: MUTE \+ APPLY/);
 }
});


test('DEVICE3 studio toggle opens only verified hardware crosspoints and preserves active endpoint truth',()=>{
 const io=read('web/mixer-io.js'),css=read('web/mixer-io.css');
 assert.match(io,/function selectDeviceBackend\(name\)/);
 assert.match(io,/data\.?ioBackendSelect|dataset\.ioBackendSelect/);
 assert.match(io,/arIODeviceDriverToggle/);
 assert.match(io,/arIODevicePatchMatrix/);
 assert.match(io,/makeMatrix\(s,deviceMatrixView\)/);
 assert.match(io,/if\(!live\)/);
 assert.match(io,/CURRENT AUDIO DEVICE · NATIVE READBACK/);
 assert.match(io,/const playback=hasActive\?active\.playbackDevice/);
 assert.match(io,/const capture=hasActive\?active\.captureDevice/);
 assert.match(css,/\.arIODeviceDriverButton\[aria-pressed="true"\]/);
 assert.doesNotMatch(css,/backdrop-filter\s*:/);
});


test('DEVICE4 Studio matrix names map actual JUCE pins, reuse SOURCE2/native ACK, never create fake buses',()=>{
 const io=read('web/mixer-io.js'),style=read('web/mixer-io.css');
 const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 assert.match(io,/function makeSoftwareInputs\(s\)/);
 assert.match(io,/function saveSoftwareInput\(\)/);
 assert.match(io,/saveSourceAliases\(s,next\)/);
 assert.match(io,/sourceIdentity\(s\)/);
 assert.match(io,/__arMixCommitSelectedSource\?\.\(/);
 assert.match(io,/DIRECT MIXER CHANNEL PATCH · CH01–10/);
 assert.match(io,/directMatrixOpen=direct.open/);
 assert.match(io,/previousDirect.open/);
 assert.match(io,/dataset\.ioSoftwarePin/);
 assert.doesNotMatch(io,/new AudioContext\(/);
 assert.match(style,/\.arIOSoftwareGrid/);
 assert.doesNotMatch(style,/backdrop-filter\s*:/);
 assert.ok(standalone.includes(io),'Standalone embeds same JS');
 assert.ok(standalone.includes(style),'Standalone embeds same CSS');
});


test('FAST1 one gesture uses native routing ACK then fenced physical ARM, with permanent panic MUTE',()=>{
 const io=read('web/mixer-io.js'),control=read('web/monitor.js');
 const native=read('src/Main.cpp'),engine=read('src/LiveAudioEngine.h');
 const html=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 assert.match(io,/START AUDIO/);
 assert.match(io,/dataset\.ioStartAudio/);
 assert.match(io,/LIVE ON /);
 assert.match(io,/ASSIGN CH/);
 assert.match(io,/function startAudioBlocked\(\)/);
 assert.match(io,/start\.disabled=startAudioBlocked\(\)/);
 assert.match(control,/window\.__arMixStartLiveAudio=/);
 assert.match(control,/fastAudioPending=\{phase:"patch"/);
 assert.match(control,/if\(verified\)fastAudioArm\(result\)/);
 assert.match(control,/expectedEpoch:Number\(d.epoch\)/);
 assert.match(control,/arMixAudioArmAck/);
 assert.match(control,/const output=!d.monitor\?"OUTPUT SAFE-MUTED"/);
 assert.match(control,/window\.__arMixAudioPathView=/);
 assert.match(native,/engine_\.liveOutputArmFault\(\)/);
 assert.match(native,/arMixAudioArmAck/);
 assert.match(engine,/juce::String liveOutputArmFault\(\)/);
 assert.match(engine,/dryRoutingLab_\.hasExplicitRoute\(\)/);
 assert.doesNotMatch(control,/new AudioContext\(/);
 assert.ok(html.includes(io),'Standalone JS must be identical');
});

test('P0 actual raw PCM before MUTE and native route/output state remain independent',()=>{
 const cpp=read('src/LiveAudioEngine.h'),ui=read('web/monitor.js'),io=read('web/mixer-io.js');
 const standalone=read('AR-MIX-Live-FX-Host-Standalone-v0.3.html');
 assert.match(cpp,/const float rawCapturePeak=captureBoundary_\.observe\(/);
 assert.match(cpp,/captureZeroHeadProvenance/);
 assert.match(cpp,/captureSignalBlocks_\.fetch_add/);
 assert.match(cpp,/RAW_PCM_BEFORE_HARDWARE_MUTE/);
 assert.match(cpp,/inputPeak_\.store\(juce::jmin\(1\.f,rawCapturePeak\)/);
 assert.match(ui,/window\.__arMixAudioPathView=/);
 assert.match(ui,/CABLE Input/);
 assert.match(ui,/arInputBar"\)\.style\.width=percent\(d\.inputPeak\)/);
 for(const f of [io,standalone]){
  assert.match(f,/data-io-signal-check/);
  assert.match(f,/refreshAudioPath\(\)/);
  assert.match(f,/OPEN SIGNAL CHECK/);
 }
 assert.doesNotMatch(io,/new AudioContext\(/);
});


test('P0 First Sound separates RAW capture, post-fader PCM and physical output ARM',()=>{
  const css=read('web/mixer-io.css');
  const engine=read('src/LiveAudioEngine.h');
  const core=read('engine/core/PreparedMixerBlock.h');
  const main=read('src/Main.cpp');
  assert.match(gui,/data-audio-path-mixer/);
  assert.match(gui,/data-io-start-audio disabled/);
  assert.match(gui,/function startAudioBlocked\(\)/);
  assert.match(gui,/start\.disabled=startAudioBlocked\(\)/);
  assert.match(gui,/start\.disabled=startAudioBlocked\(\)/);
  assert.match(gui,/window\.__arMixStartLiveAudio\?\.\(\)/);
  assert.match(monitor,/window\.__arMixStartLiveAudio=/);
  assert.match(monitor,/action:"monitor",value:true/);
  assert.match(monitor,/expectedEpoch:Number\(d\.epoch\),expectedRevision:Number\(d\.revision\)/);
  assert.match(main,/engine_\.liveOutputArmFault\(\)/);
  assert.match(monitor,/const mixed=patched\.some\(i=>Number\(meter\[i\]\)>0\.0002\)/);
  assert.match(monitor,/const mainPcm=Number\(d\.peak\)>0\.0002/);
  assert.match(monitor,/ArSonMix physical MAIN is SAFE-MUTED/);
  assert.match(monitor,/if sound persists|sound must stop|If sound persists/i);
  assert.match(engine,/const bool outputPermitted=outputGate\.renderMain/);
  assert.match(engine,/if\(!outputGate\.hardware\)/);
  assert.match(core,/observed->channelPeak\[strip\]=peak/);
  assert.match(core,/observed->channelPeakL\[strip\]=peakL/);
  assert.match(core,/observed->channelPeakR\[strip\]=peakR/);
  assert.match(css,/\.arIOAudioPath>\.arIOPathStart/);
  assert.doesNotMatch(gui,/new AudioContext|AudioWorklet|processBlock|createPluginInstance/);
});
