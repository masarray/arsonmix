// SPDX-License-Identifier: AGPL-3.0-only
// ONE approved /preview/ Windows WebView2 resource bundle, not an old
// web/approved-preview staging fiction. Source proof != product PCM/hardware FAT.
import {readFileSync, existsSync} from 'node:fs';
import {resolve, join, sep} from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';

const root=resolve('.');
const argv=process.argv.slice(2);
const value=flag=>{
  const at=argv.indexOf(flag);
  return at<0?null:argv[at+1] || null;
};
const sourceOnly=argv.includes('--source-only');
const release=argv.includes('--release');
const bundle=resolve(value('--bundle') || process.env.ARMIX_NATIVE_GUI_BUNDLE || 'build/approved_gui');
const read=path=>readFileSync(path);
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const block=reason=>{throw Error('APPROVED /preview/ native bundle BLOCKED: '+reason);};
const requireFile=(path,why)=>{
  if(!existsSync(path))block(why+': '+path);
  return read(path);
};
try {
  if(!sourceOnly&&!release)block('Pass --source-only or --release explicitly');
  const manifestPath=join(bundle,'approved-gui-manifest.json');
  const manifest=JSON.parse(requireFile(manifestPath,'Missing actual CMake-generated manifest').toString('utf8'));
  if(manifest.schema!=='arsonmix.approved-native-pages-bundle.v2' ||
     manifest.authoritativePagesRoute!=='/preview/' ||
     manifest.authoritativePagesOutput!=='preview/index.html')
    block('Wrong public GUI identity/manifest schema');
  const html=requireFile(join(bundle,'index.html'),'Missing native Pages entry');
  if(sha(html)!==manifest.nativeHtmlSha256)block('Approved native HTML hash mismatch');
  if(!Array.isArray(manifest.files)||manifest.files.length<4)
    block('Native BinaryData resource inventory incomplete');
  const paths=new Set();
  for(const item of manifest.files) {
    if(typeof item.path!=='string' || !/^[a-zA-Z0-9_./-]+$/.test(item.path) ||
       item.path.startsWith('/') || item.path.split('/').includes('..') ||
       paths.has(item.path))
      block('Unexpected, repeated, or unsafe resource path');
    paths.add(item.path);
    const resolved=resolve(bundle,item.path);
    if(!resolved.startsWith(bundle+sep))block('Resource escapes approved bundle');
    if(sha(requireFile(resolved,'Missing approved native resource'))!==item.sha256)
      block('Native resource hash mismatch: '+item.path);
  }
  for(const name of ['index.html','assets/approved-native-contract.js',
                      'assets/approved-native-bridge.js',
                      'assets/mixer-control-keys.css',
                      'assets/mixer-control-keys.js',
                      'assets/mixer-keys-thin-flat-v9-approved-off-shadow-57x30.png','assets/mixer-wire-dots.css'])
    if(!paths.has(name))block('Mandatory approved GUI/V8 transport resource missing: '+name);
  const cmake=requireFile(join(root,'CMakeLists.txt'),'Missing native CMake').toString('utf8');
  const host=requireFile(join(root,'src/Main.cpp'),'Missing JUCE host').toString('utf8');
  if(!cmake.includes('scripts/build_approved_native_gui.py') ||
     !cmake.includes('ARMIX_APPROVED_GUI_DIR') ||
     !cmake.includes('juce_add_binary_data(ARMixAssets SOURCES'))
    block('JUCE BinaryData does not use approved Pages source generator');
  if(!host.includes('resolveApprovedGuiResource') ||
     !host.includes('withResourceProvider (assetFor)'))
    block('JUCE WebView ResourceProvider does not serve approved Pages assets');
  if(/juce_add_binary_data\(ARMixAssets SOURCES\s*web\//.test(cmake) ||
     host.includes('BinaryData::consolenative_html'))
    block('Forbidden alternative GUI compiled in Windows executable');
  // CMake's FindPython3 may pick a different interpreter than PATH's
  // `python` on Windows. PNG/favicon bytes can differ across zlib versions.
  // Reproduce the GUI with the exact Python that made the CMake bundle.
  const interpreterNote=join(bundle,'..','approved-gui-python.txt');
  const cmakePython=existsSync(interpreterNote)
    ? read(interpreterNote).toString('utf8').trim() : '';
  const py=cmakePython || process.env.PYTHON || 'python';
  const result=spawnSync(py,['scripts/build_approved_native_gui.py',
      '--out',bundle,'--verify'],{cwd:root,encoding:'utf8',timeout:60000});
  if(result.error || result.status!==0)
    block('Pages→native byte-for-byte regeneration failed: '+
      String(result.error?.message||result.stderr||result.stdout).slice(0,700));
  if(release &&
     host.includes('native_console_m2::reject (args, accepted)') &&
     !host.includes('consoleBridge_.processNativeBusIntent'))
    block('G0 rejects all PCM/WIRE operations; ENGINE #266 + native FAT not yet installed');
  // This gate proves the CMake bundle; it cannot replace the separate exact
  // Windows executable screenshot, PCM/VST3, or ASIO/WASAPI hardware FAT gate.
  console.log('PASS: exact /preview/ GUI CMake resources, native v8 bridge and SHA256 manifest');
  console.log('NOT hardware/PCM FAT — product promotion still requires #270 and #15 evidence');
} catch(error) {
  console.error(error.message);
  process.exitCode=1;
}
