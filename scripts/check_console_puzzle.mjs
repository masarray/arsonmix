// SPDX-License-Identifier: AGPL-3.0-only
// One shared v8 socket preflight (#279), maintained by FAT #270.
// OBSERVATION tiers are explicit. Source text can never attest actual PCM.
// No second schema; all JSON field validation lives in puzzle-seam.mjs.
import {readFileSync} from 'node:fs';
import {inspectCheckout as inspectApprovedConsole} from './check_pr278_console_authority.mjs';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';

export const SOCKET_PATHS=Object.freeze({
 native:'src/ConsoleLiveBridgeV6.h',
 dispatcher:'src/Main.cpp',
 gui:'web/mixer-layout.js',
 guiTest:'tests/operator_console_v6_smoke.py',
 fat:'scripts/console_v6_fat_gate.mjs',
 fatTest:'tests/console_v6_fat_gate_test.mjs',
 cxxFixture:'tests/console_puzzle_v8_fixture.cpp',
 portableGolden:'tests/fixtures/console-v8-native-unready.json',
 nativeRunner:'scripts/run_console_native_s0.mjs',
 nativeProof:'scripts/verify_console_s0_native_fixture.mjs',
 nativeSource:'src/LiveDryRoutingLab.h',
 nativeTest:'tests/live_channel_fx_graph_test.cpp',
 machine:'contracts/console-puzzle-v8.json'
});
const c=(id,passes,owner,action)=>({id,pass:Boolean(passes),owner,action});
// Pure source inspection ONLY; never return audioCertified or releaseReady=true.
export function inspectPuzzleSources(s,fixtureSha256=''){
 const native=s.native||'',dispatch=s.dispatcher||'',gui=s.gui||'';
 const guiTest=s.guiTest||'',cpp=s.cxxFixture||'',fat=s.fat||'';
 const fatTest=s.fatTest||'',runner=s.nativeRunner||'';
 const proof=s.nativeProof||'',nativeSource=s.nativeSource||'';
 const machine=s.machine||'';
 const unready=/\bw\.productionCallbackLive\s*=\s*false\s*;/.test(native);
 const strictLive=!unready&&/\bw\.productionCallbackLive\s*=/.test(native);
 // A C++ entrypoint recognizing --emit-console-s0-json is NOT necessarily
 // the actual JUCE producer. Historically the ENGINE test printed an offline
 // PuzzleV8Wire::fixture(s,ack), which contains no engine.status PCM/pins.
 // Restrict the check to the flag's actual branch, not a helper elsewhere.
 const testCpp=s.nativeTest||'';
 const flagAt=testCpp.indexOf('--emit-console-s0-json');
 let exportBranch='';
 if(flagAt>=0){
   const open=testCpp.indexOf('{',flagAt);
   if(open>=0){
     let depth=1;
     for(let i=open+1;i<testCpp.length;i++){
       if(testCpp[i]==='{')depth++;
       if(testCpp[i]==='}'&&--depth===0){exportBranch=testCpp.slice(open+1,i);break}
       if(testCpp[i]==='}'&&depth<0)break;
     }
   }
 }
 const nativeEmitterSource=exportBranch.length>0 &&
   !/PuzzleV8Wire::fixture\s*\(/.test(exportBranch) &&
   /(?:LiveConsoleBridgeV6|Bridge)::snapshot\s*\(/.test(exportBranch) &&
   /\bengine\.status\s*\(/.test(exportBranch);
 const acceptedRoster=/\bw\.channels\s*=|\.channels\.push_back|accepted.*ChannelId/.test(native) &&
    !/^\s*\/\/.*\bw\.channels\s*=/m.test(native);
 const scopes=[
   c('V8_MACHINE_CONTRACT',machine.includes('"schemaVersion"')&&
     machine.includes('"requiredSnapshotFields"')&&machine.includes('"intentRequired"')&&
     machine.includes('"ackRequired"'),
     'SHARED #279','Restore machine-readable v8 schema/required-key inventory'),
   c('S0_SINGLE_CPP_SERIALIZER',
     native.includes('PuzzleV8Wire::snapshot(w)')&&
     native.includes('PuzzleV8Wire::ack(ack)')&&
     cpp.includes('PuzzleV8Wire::fixture(snapshot,ack)'),
     'ENGINE #266','C++ runtime + portable producer must use one PuzzleV8Wire serializer'),
   c('S0_NATIVE_PIN_INVENTORY',
     /\bw\.inputs\.push_back/.test(native)&&/\bw\.outputs\.push_back/.test(native),
     'ENGINE #266','Project native enabled input/output pin masks from actual JUCE status'),
   c('S0_WEBVIEW_EVENTS',
     dispatch.includes('arMixConsoleSnapshot')&&dispatch.includes('arMixConsoleIntent')&&
     dispatch.includes('arMixConsoleAck')&&dispatch.includes('processProductionIntent'),
     'ENGINE #266','Dispatch real subscribe/intent/ACK/snapshot through active JUCE adapter'),
   c('S0_EXPLICIT_JUCE_EXPORTER',
     nativeEmitterSource &&
     runner.includes('assertExplicitS0Exporter') &&
     proof.includes('runActualNativeS0'),
     'ENGINE #266','Export exact JUCE LiveConsoleBridgeV6::snapshot(engine.status()) from native binary; PuzzleV8Wire::fixture(...) cannot qualify S0'),
   c('S0_ONE_CPP_GUI_FIXTURE',
     /^[a-f0-9]{64}$/.test(fixtureSha256)&&
     guiTest.includes('console-v8-native-unready.json')&&
     guiTest.includes(fixtureSha256)&&
     proof.includes('actual.equals(expected)'),
     'GUI #271 + FAT #270','Same C++ byte-for-byte golden file and digest must be checked by both owners'),
   c('GUI_NATIVE_ACK_SNAPSHOT',
     gui.includes('arMixConsoleAck')&&gui.includes('arMixConsoleSnapshot')&&
     gui.includes('arMixConsoleIntent')&&
     gui.includes('baseGraphRevision')&&gui.includes('baseRosterRevision')&&
     gui.includes('enabledInputs')&&gui.includes('enabledOutputs'),
     'GUI #271','No alternate GUI wire or missing pin masks; ACK requires exact accepted snapshot'),
   c('FAT_SHARED_MACHINE_VALIDATION',
     fat.includes('enabledInputs')&&fat.includes('enabledOutputs')&&
     fatTest.includes('integration/console-v6/fixtures.json')&&
     runner.includes('puzzle-seam.mjs')&&
     machine.includes('"requiredSnapshotFields"'),
     'FAT #270','Use merged v8 machine socket and keep synthetic fixtures non-production'),
   c('S1_INSTALLED_LEGACY_NATIVE_CONTROLS',
     dispatch.includes('processProductionIntent')&&
     native.includes('engine.switchDryRoutingLabSource(')&&
     native.includes('engine.setDryRoutingLabParameter(')&&
     nativeSource.includes('bool process('),
     'ENGINE #266','Verify actual callback IN1→CH07→MAIN PCM and native ACK on same source SHA'),
   c('G2_FULL_UNIVERSAL_CALLBACK',
     strictLive&&acceptedRoster&&
     /supportedOps/.test(native)&&
     /ChannelId/.test(native)&&
     !/supportedOps\s*:\s*\[\s*\]/.test(native),
     'ENGINE #266','Install real full-fidelity ChannelId callback (VST/Show/CH11/Group/FX Return/Monitor); do not set LIVE in G0'),
   c('G2_GUI_NATIVE_OPERATIONS',
     gui.includes('productionCallbackLive===true')&&
     gui.includes('supportedOps')&&
     gui.includes('arMixConsoleIntent'),
     'GUI #271','Enable visible controls only for supported working native ops from accepted state')
 ];
 const passes=id=>scopes.find(x=>x.id===id)?.pass===true;
 const portableOnly=passes('S0_SINGLE_CPP_SERIALIZER')&&
   passes('S0_ONE_CPP_GUI_FIXTURE');
 return {
   stage:!passes('S0_EXPLICIT_JUCE_EXPORTER')?'S0_NATIVE_PRODUCER_BLOCKED':
     !passes('G2_FULL_UNIVERSAL_CALLBACK')?'S1_OR_G0_ONLY_UNIVERSAL_BLOCKED':
     'SOURCE_CANDIDATE_ONLY_REQUIRES_NATIVE_PCM_CI_HARDWARE',
   portableFixtureMatch:portableOnly,
   actualNativeS0Verified:false, // source-only checker cannot execute native JUCE
   actualCallbackPCMVerified:false,
   hardwareFAT:'PENDING',
   productCertified:false,
   checks:scopes,
   releaseSourceBlockers:scopes.filter(x=>!x.pass).map(x=>x.id)
 };
}
const here=resolve(fileURLToPath(new URL('..',import.meta.url)));
function filesOnDisk(){
 const files={};
 for(const [k,p] of Object.entries(SOCKET_PATHS))
  try{files[k]=readFileSync(resolve(here,p),'utf8')}catch{files[k]=''}
 try{
  const bytes=readFileSync(resolve(here,SOCKET_PATHS.portableGolden));
  files.fixtureSha256=createHash('sha256').update(bytes).digest('hex');
 }catch{files.fixtureSha256=''}
 return files;
}
if(process.argv[1]&&fileURLToPath(import.meta.url)===resolve(process.argv[1])){
 const release=process.argv.includes('--release');
 try{
  execFileSync(process.execPath,['--test','tests/console_v6_puzzle_seam_test.mjs'],
    {cwd:here,stdio:'inherit'});
  const src=filesOnDisk();
  const result=inspectPuzzleSources(src,src.fixtureSha256);
  for(const check of result.checks)
   console.log((check.pass?'[MATCH] ':'[BLOCKED] ')+check.id+
     (check.pass?'':' -> '+check.owner+': '+check.action));
  console.log(JSON.stringify({stage:result.stage,
    goldenSha256:src.fixtureSha256||'NOT_AVAILABLE',
    actualNativeS0Verified:false,actualCallbackPCMVerified:false,
    hardwareFAT:'PENDING',releaseSourceBlockers:result.releaseSourceBlockers},null,2));
  if(release){
 const design=inspectApprovedConsole();
 console.log('PR278 approved GUI source matches '+(design.checks.length-design.blocked.length)+'/'+design.checks.length);
 for(const d of design.blocked)console.error('[PR278 GUI BLOCK] '+d.id+' -> GUI #271: '+d.action);
 if(design.blocked.length)process.exitCode=1;
   if(result.releaseSourceBlockers.length){
     console.error('RELEASE SOURCE GATE BLOCKED: '+result.releaseSourceBlockers.length+
       ' source discrepancies. Windows+Chromium/real callback PCM are independent mandatory checks.');
     process.exitCode=1;
   }else console.log('SOURCE SOCKET CANDIDATE ONLY: not release/UAT; native PCM+EXE+physical review still required');
  }
 }catch(e){console.error('SHARED PUZZLE PRECHECK FAILED: '+e.message);process.exitCode=1}
}
