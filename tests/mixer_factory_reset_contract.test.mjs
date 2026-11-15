// SPDX-License-Identifier: AGPL-3.0-only
// Lightweight, hardware-independent reset integration regression.
import test from "node:test";
import assert from "node:assert/strict";
import {readFileSync} from "node:fs";

const read=path=>readFileSync(new URL("../"+path,import.meta.url),"utf8");
const host=read("src/Main.cpp");
const store=read("src/NativeMixerSessionStore.h");
const ui=read("site/preview-v2/index.html");
const bridge=read("web/approved-native-bridge.js");
const logical=read("src/NativeLogicalMixerControl.h");
const hotkeys=read("src/NativeHotkeyRegistry.h");

test("factory reset is an explicit confirmed native action, never demo-only",()=>{
  assert.match(ui, /id="reset"[^>]*>Reset Mixer/);
  assert.match(ui, /id="mixerResetDialog"/);
  assert.match(ui, /id="mixerResetConfirm"/);
  assert.match(bridge,/target\.id==="reset"/);
  assert.match(bridge,/if\(!dialog\?\.open\)return/);
  assert.match(bridge,/action:"nativeMixerFactoryReset",confirmation:"RESET_MIXER"/);
  assert.match(host,/action=="nativeMixerFactoryReset"/);
  assert.match(host,/confirmation",""\)\.toString\(\)!="RESET_MIXER"/);
  assert.doesNotMatch(bridge,/reset:"Demo reset must not erase a live mixer"/);
});

test("durable reset invalidates both saved HEAD and PREVIOUS before restart",()=>{
  assert.match(store,/juce::String discardForFactoryReset\(\) const/);
  assert.match(store,/root_\.moveFileTo\(retired\)/);
  assert.match(store,/retired\.deleteRecursively\(\)/);
  assert.match(host,/\.discardForFactoryReset\(\)/);
  assert.match(host,/logicalMixer_\.resetForFactory\(\)/);
  assert.match(logical,/void resetForFactory\(\)/);
  assert.match(host,/lastMixerSessionFingerprint_\.clear\(\)/);
  assert.match(host,/mixerSessionRecoveryPending_=false/);
});

test("reset is transactional and automatically readies verified MAIN without persistent mute",()=>{
  const start=host.indexOf("void resetMixerToFactory()");
  const end=host.indexOf("juce::String resolveMixerSessionCatalog",start);
  const body=host.slice(start,end);
  assert.ok(start>=0&&end>start);
  const journal=body.indexOf("discardForFactoryReset()");
  const transition=body.indexOf("engine_.setOutputMuted(true)");
  const stop=body.indexOf("engine_.stop()");
  const restart=body.indexOf("engine_.start(saved.get())");
  const rearm=body.indexOf("requestAutomaticMain(false)");
  assert.ok(journal>=0&&transition>journal&&stop>transition&&restart>stop&&rearm>restart);
  assert.equal(body.lastIndexOf("engine_.setOutputMuted(true)"),transition,
    "output transition must not be left latched AFTER engine startup");
  assert.match(body,/FACTORY_RESET_READY/);
  assert.match(body,/engine_\.setMasterGainLinear\(1\.f\)/);
  assert.match(body,/engine_\.setMasterMuted\(false\)/);
  assert.doesNotMatch(body,/operatorOutputMuted_/);
  assert.doesNotMatch(body,/deleteRecursively\(\).*Plugins/);
});

test("no user-facing OUTPUT Mute/Unmute button or persistent operator latch",()=>{
  assert.doesNotMatch(bridge,/Unmute Output|Mute Output|data-native-monitor|muteBtn|armBtn/);
  assert.doesNotMatch(bridge,/action:"monitor",value:false/);
  assert.doesNotMatch(host,/operatorOutputMuted_/);
  assert.match(host,/void serviceAutomaticMain\(\)/);
  assert.match(host,/engine_\.liveOutputArmFault\(requireInput\)/);
  assert.match(host,/else if\(startupError_\.isEmpty\(\)\)requestAutomaticMain\(false\)/);
  assert.match(host,/OUTPUT_MUTE_REMOVED/);
  assert.match(ui,/MAIN audio becomes active automatically/);
  assert.doesNotMatch(ui,/Audio output will be muted/);
});

test("preview reset is explicitly FX-free and WIRE-free, not demo-init",()=>{
  assert.match(ui,/function init\(factoryBlank=false\)/);
  assert.match(ui,/if\(factoryBlank\)\{/);
  assert.match(ui,/t\.fx=Array\(8\)\.fill\('None'\)/);
  assert.match(ui,/wireEdges=\[\];nextWireId=1/);
  assert.match(ui,/setDensity\('comfort'\);init\(true\)/);
});

test("bounded control-thread automatic MAIN self-heals without an output toggle",()=>{
  const start=host.indexOf("void serviceAutomaticMain()");
  const end=host.indexOf("void requestAutomaticMain(",start);
  const gate=host.slice(start,end);
  assert.ok(start>=0&&end>start);
  assert.match(gate,/timerTicks_%12!=0/);
  assert.match(gate,/if\(autoMainEpoch_==0\)/);
  assert.match(gate,/!engine_\.hasBoundMixerRoute\(\)/);
  assert.match(gate,/engine_\.isGraphSwitchPending\(\)/);
  assert.match(gate,/engine_\.secondaryArmed\(\)/);
  assert.match(gate,/engine_\.diagnosticRoute\(\)!=armix::diagnostics::Route::normal/);
  assert.match(gate,/autoMainRequiresInput_=false/);
  assert.match(gate,/autoMainCheckedRevision_\.reset\(\)/);
  assert.match(gate,/engine_\.liveOutputArmFault\(requireInput\)/);
  assert.match(gate,/engine_\.setMonitorEnabled\(true\)/);
  assert.match(host,/serviceAutomaticMain\(\);\s*\/\/ Autosave/);
});

test("PANIC shortcut is recoverable from visible MASTER MUTE, never a hidden hardware latch",()=>{
  const start=host.indexOf('if(mapping.operation=="PANIC_MUTE")');
  const end=host.indexOf('if(mapping.scope!="SESSION"',start);
  const panic=host.slice(start,end);
  assert.ok(start>0&&end>start);
  assert.match(panic,/engine_\.setMasterMuted\(true\)/);
  assert.doesNotMatch(panic,/setOutputMuted\(true\)/);
  assert.match(bridge,/sendM4Control\("masterMute",t,!t\.mute\)/);
});

test("Show/secondary/refresh maintenance never require permanent output MUTE",()=>{
  assert.match(host,/struct ScopedMainTransaction final/);
  assert.match(host,/~ScopedMainTransaction\(\)\{resume\(\);\}/);
  assert.match(host,/owner\.requestAutomaticMain\(false\)/);
  assert.match(host,/ScopedMainTransaction mainGate\(\*this\)/);
  assert.match(host,/mainGate\.resume\(\)/);
  assert.match(host,/std::optional<ScopedMainTransaction> recallGate/);
  assert.match(host,/recallGate\.reset\(\)/);
  assert.match(bridge,/showRecall\.disabled=showBusy\|\|!entry\|\|entry\.needsRecovery/);
  assert.match(bridge,/showRecover\.disabled=showBusy\|\|!entry\|\|!entry\.hasPrevious/);
  assert.doesNotMatch(bridge,/SHOW SAVE requires MUTED output/);
  assert.doesNotMatch(bridge,/mute output before recall\/recovery/);
  assert.match(host,/Stop the current STREAM before changing its output endpoint/);
  assert.match(host,/if\(error\.isEmpty\(\)\)requestAutomaticMain\(false\)/);
});

test("emergency CLI reset runs before WebView and only clears mixer data",()=>{
  assert.match(host,/commandLine\.trim\(\)=="--reset-mixer"/);
  assert.match(host,/appearance\.resetForFactory\(\)/);
  assert.match(host,/hotkeys\.resetMixerMappings\(\)/);
  assert.match(hotkeys,/juce::String resetMixerMappings\(\)/);
  assert.match(hotkeys,/m\.scope=="SESSION"/);
  assert.match(host,/!isAutoUpdateHealthProbe\(\)/);
  assert.match(host,/\.withButton\("Cancel"\)/);
  assert.match(host,/\.withButton\("Reset Mixer"\)/);
  assert.match(host,/if\(choice!=1\)/);
  assert.match(host,/void startNormalMixerStartup\(\)/);
  assert.match(host,/discardPlayerSourceForFactory\(\)/);
  assert.match(host,/resetLegacyMixerControlsForFactory\(\)/);
  const installer=read("installer/ArSonMix.iss");
  assert.match(installer,/Reset ArSonMix Mixer/);
  assert.match(installer,/Parameters: "--reset-mixer"/);
});

test("native bridge clears stale channel appearance on reset readback",()=>{
  assert.match(bridge,/factoryResetProjectionPending=true/);
  assert.match(bridge,/t\.color=appearance\.color/);
  assert.match(bridge,/t\.icon=appearance\.icon/);
  assert.match(bridge,/factoryVisualSeed\[inputOrdinal-1\]/);
  assert.match(bridge,/factoryResetProjectionPending=false/);
  assert.match(bridge,/ack\?\.operation==="RESET"/);
});
