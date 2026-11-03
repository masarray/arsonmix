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
  gui:'web/mixer-layout.js',
  guiTest:'tests/operator_console_v6_smoke.py',
  fat:'scripts/console_v6_fat_gate.mjs',
  fatTest:'tests/console_v6_fat_gate_test.mjs',
  operatorHtml:'web/console-native.html',
  operatorApp:'web/console-native.js',
  nativeEntry:'src/Main.cpp',
  cmake:'CMakeLists.txt'
};
const release=process.argv.includes('--release');
const read=path=>{try{return readFileSync(resolve(path),'utf8')}catch{return ''}};
const data=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,read(p)]));
const checks=[
 ['ENGINE real device pin masks',/setProperty\("enabledInputs"/.test(data.native)&&/setProperty\("enabledOutputs"/.test(data.native),'ENGINE #266: publish enabledInputs and enabledOutputs from active device pin mask'],
 ['ENGINE actual accepted stable ChannelId roster',!/graph->setProperty\("channels",juce::var\(juce::Array<juce::var>\{\}\)\)/.test(data.native)&&/setProperty\("channels"/.test(data.native),'ENGINE #266: serialize real accepted roster, not empty array'],
 ['ENGINE universal processing admitted',!/setProperty\("productionCallbackLive",false\)/.test(data.native),'ENGINE #266: actual callback full fidelity must gate productionCallbackLive dynamically'],
 ['ENGINE real ConsoleIntent dispatch',!/consoleBridge_\.rejectUnsupportedIntent\(args,snap\)/.test(data.dispatcher),'ENGINE #266: route valid intents to prepared accepted native graph, preserve rollback'],
 ['GUI native ACK and snapshot listeners',/arMixConsoleAck/.test(data.gui)&&/arMixConsoleSnapshot/.test(data.gui),'GUI #271: consume real native ACK and snapshot'],
 ['GUI fixture from one canonical source',/console-v6\/fixtures\.json/.test(data.guiTest),'GUI #271: use shared fixture file, remove standalone MAIN/BUS-FX fake ids'],
 ['FAT checks native device endpoints and pin masks',/enabledInputs/.test(data.fat)&&/enabledOutputs/.test(data.fat),'FAT #270: validate native producer enabled pins, not hand-authored fantasy fields'],
 ['FAT fixture from one canonical source',/console-v6\/fixtures\.json/.test(data.fatTest),'FAT #270: use the SAME shared fixture as GUI tests, not duplicate schema'],
 // The legacy app shipped a source-exact, but WRONG, fixed-ten GUI.
 // The approved operator Console must own the embedded WebView assets.
 ['GUI retired ten-strip demo removed',
  !/\bconst\s+raw\s*=\s*\[/.test(data.operatorApp) &&
  !/\bconst\s+channels\s*=\s*raw\.map\s*\(/.test(data.operatorApp),

  'GUI #271: replace hard-coded 10-channel demo array with native accepted ChannelId roster'],
 ['GUI approved single Console is packaged, not retired multi-workspace shell',
  !/data-view=["']FX RACK["']/.test(data.operatorHtml) &&
  !/data-view=["']SIGNAL FLOW["']/.test(data.operatorHtml) &&
  !/Live FX Host GUI Preview/.test(data.operatorHtml),
  'GUI #271: remove abandoned MIX / FX RACK / SIGNAL FLOW shell; follow approved one-Console workflow'],
 ['GUI direct universal channel operation originates from native accepted state',
  /arMixConsoleSnapshot/.test(data.gui) &&
  /CREATE_CHANNEL/.test(data.gui) &&
  (data.gui.includes('graph.channels') || data.gui.includes('graph?.channels')),
  'ENGINE #266 + GUI #271: CREATE_CHANNEL native ACK then accepted graph.channels renders CH11+, no browser-created strips'],
 ['Windows WebView2 root serves native approved Console, NOT legacy index',
  /data = BinaryData::consolenative_html/.test(data.nativeEntry) &&
  /console-native\.css/.test(data.nativeEntry) &&
  /console-native\.js/.test(data.nativeEntry) &&
  !/AR MIX Studio UI-only demonstration/.test(data.operatorApp),
  'ENGINE #266: native / root must embed approved native operator Console and never the legacy fixed-ten HTML'],
 ['Console listens to native ACK/snapshot and never fabricates metering',
  /arMixConsoleSubscribe/.test(data.operatorApp) &&
  /arMixConsoleIntent/.test(data.operatorApp) &&
  /arMixConsoleAck/.test(data.operatorApp) &&
  /channelPeaksL/.test(data.operatorApp) &&
  !/Math\.sin/.test(data.operatorApp),
  'GUI #271: actual native event bus and PCM; no browser animated meters'],
 ['JUCE BinaryData embeds release-gated operator HTML and JS',
  /web\/console-native\.html\s+web\/console-native\.css\s+web\/console-native\.js/.test(data.cmake),
  'FAT #270: CMake BinaryData must embed audited Console HTML/JS'],
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
