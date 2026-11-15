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

test("stop old callback graph and restart with preserved device, silent MAIN",()=>{
  const start=host.indexOf("void resetMixerToFactory()");
  const end=host.indexOf("juce::String resolveMixerSessionCatalog",start);
  const body=host.slice(start,end);
  assert.ok(start>=0&&end>start);
  assert.ok(body.indexOf("engine_.setOutputMuted(true)")<body.indexOf("engine_.stop()"));
  assert.ok(body.indexOf("discardForFactoryReset()")<body.indexOf("engine_.stop()"));
  assert.match(body,/engine_\.stop\(\)/);
  assert.match(body,/engine_\.start\(saved\.get\(\)\)/);
  assert.match(body,/engine_\.setMasterGainLinear\(1\.f\)/);
  assert.match(body,/engine_\.setMasterMuted\(false\)/);
  assert.match(body,/engine_\.setOutputMuted\(true\)/);
  assert.doesNotMatch(body,/deleteRecursively\(\).*Plugins/);
});

test("preview reset is explicitly FX-free and WIRE-free, not demo-init",()=>{
  assert.match(ui,/function init\(factoryBlank=false\)/);
  assert.match(ui,/if\(factoryBlank\)\{/);
  assert.match(ui,/t\.fx=Array\(8\)\.fill\('None'\)/);
  assert.match(ui,/wireEdges=\[\];nextWireId=1/);
  assert.match(ui,/setDensity\('comfort'\);init\(true\)/);
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
