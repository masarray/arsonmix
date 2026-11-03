// SPDX-License-Identifier: AGPL-3.0-only
// Fail-closed source compatibility gate for the ONE integrated release branch.
// Contract self-tests can run on every PR. --release requires THREE owners'
// exact real-producer/consumer socket and refuses synthetic-only readiness.
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {execFileSync} from 'node:child_process';
const files={
  native:'src/ConsoleLiveBridgeV6.h',
  dispatcher:'src/Main.cpp',
  gui:'web/console-native.js',
  guiTest:'tests/operator_console_v6_smoke.py',
  fat:'scripts/console_v6_fat_gate.mjs',
  fatTest:'tests/console_v6_fat_gate_test.mjs',
  operatorHtml:'web/index.html',
  operatorApp:'web/index.html',
  cmake:'CMakeLists.txt'
};
const release=process.argv.includes('--release');
const read=path=>{try{return readFileSync(resolve(path),'utf8')}catch{return ''}};
const data=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,read(p)]));
const checks=[
 ['ENGINE real device pin masks',/setProperty\("enabledInputs"/.test(data.native)&&/setProperty\("enabledOutputs"/.test(data.native),'ENGINE #266: publish enabledInputs and enabledOutputs from active device pin mask'],
 ['ENGINE actual accepted stable ChannelId roster',!/graph->setProperty\("channels",juce::var\(juce::Array<juce::var>\{\}\)\)/.test(data.native)&&/setProperty\("channels"/.test(data.native),'ENGINE #266: serialize real accepted roster, not empty array'],
 ['ENGINE universal processing admitted',!/w\.productionCallbackLive=false;/.test(data.native) && !/setProperty\("productionCallbackLive",false\)/.test(data.native),'ENGINE #266: actual callback full fidelity must gate productionCallbackLive dynamically'],
 ['ENGINE real ConsoleIntent dispatch',!/consoleBridge_\.rejectUnsupportedIntent\(args,snap\)/.test(data.dispatcher),'ENGINE #266: route valid intents to prepared accepted native graph, preserve rollback'],
 ['GUI native ACK and snapshot listeners',/arMixConsoleAck/.test(data.gui)&&/arMixConsoleSnapshot/.test(data.gui),'GUI #271: consume real native ACK and snapshot'],
 ['GUI fixture from one canonical source',/console-v6\/fixtures\.json/.test(data.guiTest),'GUI #271: use shared fixture file, remove standalone MAIN/BUS-FX fake ids'],
 ['FAT checks native device endpoints and pin masks',/enabledInputs/.test(data.fat)&&/enabledOutputs/.test(data.fat),'FAT #270: validate native producer enabled pins, not hand-authored fantasy fields'],
 ['FAT fixture from one canonical source',/console-v6\/fixtures\.json/.test(data.fatTest),'FAT #270: use the SAME shared fixture as GUI tests, not duplicate schema'],
 // The legacy app shipped a source-exact, but WRONG, fixed-ten GUI.
 // The approved operator Console must own the embedded WebView assets.
 ['Approved current Console replaces old workbench in JUCE production entry',
  /id="nativeAuthority"/.test(data.operatorHtml) &&
  /src="console-native.js"/.test(data.operatorHtml) &&
  !/data-view=["']FX RACK["']/.test(data.operatorHtml) &&
  !/data-view=["']SIGNAL FLOW["']/.test(data.operatorHtml),
  'GUI: embed approved main M3 Console, never retired old MIX/FX RACK/PATCH'],
 ['Production BinaryData excludes all obsolete frontend DSP-mock sources',
  /web\/index\.html\s+web\/console-native\.js/.test(data.cmake) &&
  !/web\/app\.js/.test(data.cmake) &&
  !/web\/bridge\.js/.test(data.cmake),
  'Windows product: only M3 Console and native adapter may be compiled into JUCE'],
 ['GUI native ACK/snapshot and ChannelId renderer on approved page',
  /arMixConsoleSnapshot/.test(data.gui) &&
  /arMixConsoleAck/.test(data.gui) &&
  /arMixConsoleIntent/.test(data.gui) &&
  /function projectNative\(s\)/.test(data.operatorHtml) &&
  /s\.graph\.channels/.test(data.operatorHtml),
  'GUI: M3 Console must consume real JUCE ACK/snapshot and render accepted channels'],
 ['Native dynamic CREATE_CHANNEL implemented, not a disabled mock button',
  /CREATE_CHANNEL/.test(data.gui) &&
  /CREATE_CHANNEL/.test(data.native) &&
  !/w\.productionCallbackLive=false;/.test(data.native),
  'ENGINE: implement actual dynamic CH11+ through one installed JUCE renderer before promotion'],

];
execFileSync(process.execPath,['--test','tests/console_v6_puzzle_seam_test.mjs'],{stdio:'inherit'});
const blockers=checks.filter(x=>!x[1]);
for(const [name,pass,fix] of checks)
 console.log((pass?'[MATCH] ':'[MISMATCH] ')+name+(pass?'':' -> '+fix));
if(release){
  if(blockers.length){
    console.error('RELEASE PUZZLE GATE BLOCKED: '+blockers.length+' missing native/GUI/FAT sockets. No partial EXE/UAT.');
    process.exitCode=1;
  }else console.log('RELEASE SOURCE SOCKET MATCH: static preflight only; native CTest, UI Playwright and independent physical FAT still required.');
}else{
  console.log('G0 contract self-tests complete; --release will block until all three real owner branches match.');
}
