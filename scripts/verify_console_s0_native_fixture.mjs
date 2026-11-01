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
   const result=generateS0({generator:exe,output:generated,sourceSha:current});
   const actual=readFileSync(generated);
   assert(actual.equals(expected),
     'ENGINE compiled stdout byte mismatch with checked-in GUI native JSON fixture');
   assert(result.nativeFixtureSha256===expectedSha,
     'FAT C++ producer digest differs from one shared GUI fixture SHA256');
   if(playwright){
     execFileSync(process.env.PYTHON||'python',[
       join(root,'tests/console_native_s0_playwright.py'),
       '--fixture',generated
     ],{cwd:root,stdio:'inherit',timeout:120000});
   }
   return {status:'S0_CPP_SERIALIZER_TO_SHARED_GUI_FIXTURE_MATCH',
     sourceSha:current,sharedFixtureSha256:expectedSha,
     events:4,sourceBindings:'STATIC_MATCH',nativeCallbackAudio:'NOT_PROVEN',
     Playwright:playwright?'PASS':'NOT_RUN',
     fullMixerReady:false,hardwareFAT:'PENDING'};
 }finally{rmSync(dir,{recursive:true,force:true})}
}
if(process.argv[1]&&fileURLToPath(import.meta.url)===resolve(process.argv[1])){
 try{
   const args=process.argv.slice(2),i=args.indexOf('--output');
   const output=i>=0?args[i+1]:undefined;
   assert(i<0||output,'--output requires path');
   console.log(JSON.stringify(run({output,playwright:args.includes('--playwright')}),null,2));
 }catch(e){console.error(e.stack||e.message);process.exitCode=1}
}
