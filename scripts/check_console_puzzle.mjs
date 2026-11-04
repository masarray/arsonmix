// SPDX-License-Identifier: AGPL-3.0-only
// Milestone A: fail-closed STATIC convergence gate for the one approved
// /preview/ -> JUCE WebView2 -> native ChannelId PCM product.
// This deliberately does not certify a Windows executable or physical sound.
// CTest, screenshot parity, packaged SHA and hardware FAT are separate gates.
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';

export const PATHS=Object.freeze({
  cmake:'CMakeLists.txt',
  entry:'src/Main.cpp',
  engine:'src/LiveAudioEngine.h',
  nativeG0:'src/ApprovedConsoleNativeTransportV6.h',
  nativeV8:'src/ConsoleLiveBridgeV6.h',
  projection:'src/ApprovedNativeV8Projection.h',
  renderOwner:'engine/core/NativeRenderAuthority.h',
  gui:'web/approved-native-bridge.js',
  protocol:'web/approved-native-contract.js',
  assembler:'scripts/build_approved_native_gui.py',
  pages:'scripts/build_pages.py',
  nativeTests:'tests/console_native_integration_test.cpp',
  uiTests:'tests/approved_native_m2_contract.test.cjs',
  fat:'scripts/verify_native_windows_artifact.ps1',
  windows:'.github/workflows/p1-windows.yml'
});
export function collectSources(root=resolve('.')) {
  const out={};
  for(const [key,path] of Object.entries(PATHS)) {
    try {out[key]=readFileSync(resolve(root,path),'utf8');}
    catch {out[key]='';}
  }
  return out;
}
// Result ids are stable so negative regression tests never rely on log wording.
// "release" checks are necessary source evidence, NOT sufficient audio proof.
export function inspectMilestoneA(s) {
  const has=(key,pattern)=>pattern.test(s[key]||'');
  const yes=(id,ok,reason,release=false)=>({id,pass:Boolean(ok),reason,release});
  const approved=has('cmake',/build_approved_native_gui\.py/) &&
    has('cmake',/ARMIX_APPROVED_GUI_DIR/) &&
    has('assembler',/build_pages\.py/) &&
    has('pages',/APPROVED_SINGLE_GUI/);
  const resource=has('entry',/ApprovedGuiResources\.h/) &&
    has('entry',/resolveApprovedGuiResource/) &&
    has('entry',/withResourceProvider\s*\(\s*assetFor\s*\)/);
  const socket=['arMixConsoleSubscribe','arMixConsoleIntent',
    'arMixConsoleSnapshot','arMixConsoleAck'];
  const nativeWire=socket.every(name=>(s.entry||'').includes(name));
  const guiWire=socket.every(name=>((s.gui||'')+'\n'+(s.protocol||'')).includes(name));
  const oldEmbedded=/juce_add_binary_data\(ARMixAssets\s+SOURCES\s*web\//.test(s.cmake||'') ||
    /web\/console-native\.(?:html|css|js)/.test(s.cmake||'') ||
    /BinaryData::(?:consolenative_html|index_html)/.test(s.entry||'');
  const controls=has('gui',/consumeField/) && has('gui',/consumeWire/) &&
    has('gui',/onSnapshot/) && has('gui',/onAck/) &&
    has('protocol',/readbackProvesIntent/) &&
    has('protocol',/baseGraphRevision/) &&
    has('protocol',/baseRosterRevision/);
  const currentG0=has('nativeG0',/productionCallbackLive",\s*false/) &&
    has('nativeG0',/supportedOps",\s*juce::var\s*\(\s*juce::Array/);
  const tests=has('windows',/ctest --test-dir build -C Release --output-on-failure/) &&
    has('windows',/check_approved_native_gui_release\.mjs/) &&
    has('windows',/verify_native_windows_artifact\.ps1/);
  const baseline=[
    yes('single-approved-pages-source',approved,
      'CMake, Pages and native assembly must use the sole approved /preview/'),
    yes('juce-serves-approved-resources',resource,
      'The JUCE WebView must dispatch generated approved GUI resources'),
    yes('no-retired-console-renderer',!oldEmbedded,
      'Do not compile web/index or web/console-native as another mixer'),
    yes('native-four-event-socket',nativeWire,
      'Main.cpp must carry Subscribe/Intent/Snapshot/Ack'),
    yes('gui-four-event-consumer',guiWire&&controls,
      'Existing /preview/ adapter must consume real native ACK/readback'),
    yes('canonical-uint64-protocol',has('protocol',/BigInt/) &&
      has('protocol',/normalizeSnapshot/) &&
      has('protocol',/createController/),
      'Stable native ChannelId/EdgeId, not JS Number or optimistic UI'),
    yes('ci-keeps-native-qualification',tests,
      'One Windows pipeline must build, CTest, verify approved GUI and package')
  ];
  const producerJoined=has('entry',/ConsoleLiveBridgeV6\.h/) &&
    has('nativeV8',/PuzzleV8Wire/) &&
    has('projection',/mainChannelId/) &&
    has('projection',/processingFormat/);
  const dispatched=has('entry',/processNativeBusIntent|processConsoleIntent|dispatchConsoleIntent/) &&
    !has('entry',/native_console_m2::reject\s*\(\s*args,\s*accepted\s*\)/);
  const pcmReadback=has('engine',/nativeChannelIdPcmActive|acceptedChannelId/) &&
    has('engine',/audioDeviceIOCallbackWithContext/) &&
    has('nativeV8',/nativeBus|acceptedRouting/);
  const integration=[
    yes('real-v8-producer-installed',producerJoined,
      'ENGINE #266 must connect its real accepted native V8 graph producer to Main.cpp',true),
    yes('intent-dispatch-not-g0-rejection',dispatched,
      'Valid admitted native intents must execute on the ONE engine; G0 blanket reject cannot ship',true),
    yes('actual-channelid-callback-readback',pcmReadback,
      'One installed audio callback must expose native ChannelId PCM and true accepted graph state',true),
    yes('native-graph-impulse-regression',has('nativeTests',/DynamicAcceptedSession/) &&
      has('nativeTests',/render\(/) && has('nativeTests',/ack/) &&
      has('nativeTests',/projectAcceptedConsoleV6/),
      'Keep native PCM and graph ACK regression in CTest; synthetic Pages data is insufficient',true),
    yes('fat-verified-packaging-wired',has('fat',/approved.gui|approved.gui.manifest|approved-gui|preview/i) &&
      has('windows',/package_native_windows\.ps1/) &&
      has('windows',/verify_native_windows_artifact\.ps1/),
      'Exact approved GUI bundle, Windows EXE and FAT package verifier must be wired',true)
  ];
  // Baseline G0 is ALLOWED during development, never advertised as a product.
  // Kept separately to prevent either fake LIVE or spurious failures on main.
  return {baseline,integration,g0Truthful:currentG0};
}
async function cli() {
  const release=process.argv.includes('--release');
  execFileSync(process.execPath,['--test','tests/console_v6_puzzle_seam_test.mjs',
    'tests/milestone_a_native_contract_test.mjs',
    'tests/m4_operator_routing.test.mjs'],{stdio:'inherit'});
  const report=inspectMilestoneA(collectSources());
  const selected=release?[...report.baseline,...report.integration]:report.baseline;
  for(const x of selected)
    console.log((x.pass?'[MATCH] ':'[BLOCKED] ')+x.id+(x.pass?'':' — '+x.reason));
  const blocked=selected.filter(x=>!x.pass);
  if(blocked.length){
    console.error((release?'MILESTONE A RELEASE':'BASELINE')+
      ' SOURCE GATE BLOCKED: '+blocked.length+' failed checks.');
    process.exitCode=1;
  } else if(release) {
    console.log('MILESTONE A SOURCE CONVERGENCE ONLY: run exact-SHA JUCE/CTest, '+
      'generated /preview/ BinaryData verifier, Windows screenshot parity and physical FAT.');
  } else {
    console.log('Approved GUI/native M2 source baseline PASS. G0 may still reject '+
      'all ChannelId actions. Use --release for strict A convergence preflight.');
  }
}
if(process.argv[1] && resolve(process.argv[1])===fileURLToPath(import.meta.url))
  await cli();
