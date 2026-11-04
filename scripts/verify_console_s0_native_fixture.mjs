// SPDX-License-Identifier: AGPL-3.0-only
// Puzzle v8 S0 independent byte-parity preflight on the ONE integrated tree.
// Actually compiles and executes the C++ ENGINE serializer in this checkout,
// compares emitted BYTES to GUI's checked-in native fixture, then uses the
// single #279 wire and optional Playwright GUI consumer on those same bytes.
// NEVER implies actual JUCE audio callback, physical output or hardware FAT.
import {execFileSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdtempSync,readFileSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {resolve,join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {generateS0} from './run_console_native_s0.mjs';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const hash=x=>createHash('sha256').update(x).digest('hex');
const fail=s=>{throw Error('S0 PRODUCER/GUI BYTE-PARITY BLOCKED: '+s)};
const assert=(ok,s)=>{if(!ok)fail(s)};
export function verifySourceBindings({read=readFileSync}={}){
 const producer=read(join(root,'tests/console_puzzle_v8_fixture.cpp'),'utf8');
 const native=read(join(root,'src/ConsoleLiveBridgeV6.h'),'utf8');
 const dispatcher=read(join(root,'src/Main.cpp'),'utf8');
 const gui=read(join(root,'tests/operator_console_v6_smoke.py'),'utf8');
 assert(producer.includes('PuzzleV8Wire::fixture(snapshot,ack)'),
   'ENGINE fixture must use the runtime shared serializer, not a separate JSON schema');
 assert(native.includes('PuzzleV8Wire::snapshot(w)')&&
   native.includes('PuzzleV8Wire::ack(ack)'),
   'ENGINE native JUCE bridge not using identical C++ snapshot and ACK serializers');
 assert(dispatcher.includes('arMixConsoleSnapshot')&&
   dispatcher.includes('arMixConsoleIntent')&&
   dispatcher.includes('arMixConsoleAck'),
   'JUCE dispatcher missing one of the canonical native WebView event paths');
 assert(gui.includes('tests/fixtures/console-v8-native-unready.json')||
   gui.includes('console-v8-native-unready.json'),
   'GUI smoke must load the exact same native C++ golden fixture');
 return true;
}
// Real G0 S0 admission: the ENGINE-owned JUCE binary must explicitly
// implement --emit-console-s0-json (and reject unsupported flags). The
// same *actual* emitted bytes are passed to both shared FAT wire and GUI.
export function runActualNativeS0({generator,output,playwright=true}={}){
 assert(generator,'ENGINE JUCE exporter executable required for native S0');
 verifySourceBindings();
 const current=execFileSync('git',['rev-parse','HEAD'],{
   cwd:root,encoding:'utf8'}).trim();
 assert(/^[a-f0-9]{40}$/.test(current),'exact checkout SHA required');
 const dir=mkdtempSync(join(tmpdir(),'arsonmix-s0-juce-'));
 const nativePath=output?resolve(output):join(dir,'juce-native-s0.json');
 try{
   const proof=generateS0({generator,output:nativePath,sourceSha:current});
   if(playwright)execFileSync(process.env.PYTHON||'python',[
     join(root,'tests/console_native_s0_playwright.py'),'--fixture',nativePath
   ],{cwd:root,stdio:'inherit',timeout:120000});
   return {status:playwright?'S0_NATIVE_CONTROL_PLANE_GUI_MATCH':
     'S0_NATIVE_CONTROL_PLANE_ONLY',
     ...proof,playwright:playwright?'PASS':'NOT_RUN',
     nativeCallbackAudio:'NOT_PROVEN',fullMixerReady:false,hardwareFAT:'PENDING'};
 }finally{rmSync(dir,{recursive:true,force:true})}
}
export function run({output,playwright=false}={}){
 verifySourceBindings();
 const current=execFileSync('git',['rev-parse','HEAD'],{
   cwd:root,encoding:'utf8'}).trim();
 assert(/^[a-f0-9]{40}$/.test(current),'git HEAD cannot be resolved');
 const expectedPath=join(root,'tests/fixtures/console-v8-native-unready.json');
 const expected=readFileSync(expectedPath);
 const expectedSha=hash(expected);
 const gui=readFileSync(join(root,'tests/operator_console_v6_smoke.py'),'utf8');
 assert(gui.includes(expectedSha),
   'GUI locked fixture digest differs from C++-checked-in golden file bytes');
 const dir=mkdtempSync(join(tmpdir(),'arsonmix-s0-cxx-'));
 const generated=output?resolve(output):join(dir,'cpp-produced.json');
 try{
   const cpp=join(root,'tests/console_puzzle_v8_fixture.cpp');
   const exe=join(dir,process.platform==='win32'?'s0-producer.exe':'s0-producer');
   // Ubuntu Chromium runner and Linux developer machines have a C++20
   // compiler; use CXX override for other POSIX toolchains. We do NOT mock
   // compiler output. Native Windows MSVC is checked by CTest separately.
   assert(process.platform!=='win32',
     'this portable S0 C++ compile gate runs on Chromium/Linux; Windows MSVC CTest is separate');
   const compiler=process.env.CXX||'c++';
   execFileSync(compiler,[
     '-std=c++20','-O2','-Wall','-Wextra','-Werror','-pedantic',
     cpp,'-o',exe
   ],{cwd:root,stdio:'pipe',timeout:120000});
   // Portable fixture: execute the compiled C++ main with NO argv. It is
   // explicitly not the live JUCE exporter and cannot qualify S0 runtime.
   const actual=execFileSync(exe,[],{cwd:root,encoding:null,timeout:15000,
     maxBuffer:8*1024*1024});
   writeFileSync(generated,actual);
   assert(actual.equals(expected),
     'ENGINE compiled stdout byte mismatch with checked-in GUI native JSON fixture');
   assert(hash(actual)===expectedSha,
     'portable C++ producer digest differs from one shared GUI golden SHA256');
   if(playwright){
     execFileSync(process.env.PYTHON||'python',[
       join(root,'tests/console_native_s0_playwright.py'),
       '--fixture',generated
     ],{cwd:root,stdio:'inherit',timeout:120000});
   }
   return {status:'PORTABLE_CPP_SERIALIZER_PARITY_ONLY_NOT_S0',
     sourceSha:current,sharedFixtureSha256:expectedSha,
     events:4,sourceBindings:'STATIC_MATCH',nativeCallbackAudio:'NOT_PROVEN',
     nativeEventExporter:'NOT_EXECUTED',nativeControlPlaneS0:'BLOCKED',
     PlaywrightPortableGolden:playwright?'PASS':'NOT_RUN',
     fullMixerReady:false,hardwareFAT:'PENDING'};
 }finally{rmSync(dir,{recursive:true,force:true})}
}
if(process.argv[1]&&fileURLToPath(import.meta.url)===resolve(process.argv[1])){
 try{
   const args=process.argv.slice(2),i=args.indexOf('--output');
   const output=i>=0?args[i+1]:undefined;
   assert(i<0||output,'--output requires path');
   const nativeIndex=args.indexOf('--native-generator');
   const native=nativeIndex>=0?args[nativeIndex+1]:undefined;
   assert(nativeIndex<0||native,'--native-generator requires built JUCE executable');
   const actual=native?runActualNativeS0({generator:native,output,
       playwright:!args.includes('--skip-playwright')}):
     run({output,playwright:args.includes('--playwright')});
   console.log(JSON.stringify(actual,null,2));
 }catch(e){console.error(e.stack||e.message);process.exitCode=1}
}
