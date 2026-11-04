// SPDX-License-Identifier: AGPL-3.0-only
// Native RELEASE gate: user-approved /preview/ is the ONLY allowable Windows GUI.
// Necessary static provenance, NOT proof of rendered EXE screenshots or PCM FAT.
import {readFileSync, existsSync} from 'node:fs';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
const root=resolve('.');
const abs=p=>resolve(root,p);
const exists=p=>existsSync(abs(p));
const read=p=>readFileSync(abs(p));
const digest=b=>createHash('sha256').update(b).digest('hex');
const fail=s=>{console.error('RELEASE BLOCKED — approved /preview/ GUI: '+s);process.exitCode=1;};
const entry='web/approved-preview/index.html';
const manifest='web/approved-preview/native-manifest.json';
const source='site/preview-v2/index.html'; // INTERNAL HTML seed, NOT /preview-v2/ product
const buttons='site/preview-buttons/assets/';
if(!exists(entry)||!exists(manifest)){
  fail('Windows approved GUI bundle missing. Generate the exact /preview/ assembled HTML, CSS/JS/PNG, then embed it into JUCE.');
  process.exit(1);
}
let m;try{m=JSON.parse(read(manifest).toString('utf8'));}catch(e){fail('Invalid manifest: '+e.message);process.exit(1);}
if(m.canonicalUrl!=='https://masarray.github.io/arsonmix/preview/'||m.nativeEntrypoint!==entry)fail('Canonical GUI identity is NOT /preview/');
if(m.entrySha256!==digest(read(entry)))fail('Native approved HTML digest mismatch');
const sourceHtml=read(source).toString('utf8');
let expected=sourceHtml;
for(const name of ['mixer-control-keys.css','mixer-control-keys.js']){
  const key=buttons+name;
  if(!exists(key)){fail('Missing approved PNG button asset '+key);continue;}
  const rev=digest(read(key)).slice(0,16);
  const token='assets/'+name;
  const pattern=name.endsWith('.css') ? /assets\/mixer-control-keys\.css(?:\?[^"'\s>]*)?/g : /assets\/mixer-control-keys\.js(?:\?[^"'\s>]*)?/g;
  const matches=[...expected.matchAll(pattern)];
  if(matches.length!==1)fail('Expected exactly one PNG-button source reference '+name);
  expected=expected.replace(pattern,token+'?rev='+rev);
}
if(read(entry).toString('utf8')!==expected)fail('Native HTML does not match EXACT approved /preview/ assembly');
const linked=[...new Set([...sourceHtml.matchAll(/(?:src|href)=["'](assets\/[^"'?]+)(?:\?[^"']*)?["']/g)].map(v=>v[1]))];
for(const rel of linked){
  const name=rel.slice('assets/'.length);
  const sourceAsset=['mixer-control-keys.css','mixer-control-keys.js','mute-solo-pfl-56x37.png'].includes(name)?buttons+name:'site/preview-v2/'+rel;
  const nativeAsset='web/approved-preview/'+rel;
  if(!exists(sourceAsset)||!exists(nativeAsset)||digest(read(sourceAsset))!==digest(read(nativeAsset)))fail('Native CSS/JS/art missing/changed: '+rel);
}
for(const name of ['mute-solo-pfl-56x37.png','mixer-wire-dots.css','knob-ICON.svg']){
  const ref=name==='mute-solo-pfl-56x37.png'?buttons+name:'site/preview-v2/assets/'+name;
  const native='web/approved-preview/assets/'+name;
  if(!exists(native)||!exists(ref)||digest(read(native))!==digest(read(ref)))fail('Mandatory GUI art mismatch: '+name);
}
const cmake=read('CMakeLists.txt').toString('utf8'),host=read('src/Main.cpp').toString('utf8');
if(!cmake.includes(entry)||!cmake.includes('web/approved-preview/assets/'))fail('CMake BinaryData not built from approved /preview/ GUI and assets');
if(!/approved-preview|approvedPreview/i.test(host))fail('JUCE ResourceProvider not serving approved /preview/ GUI');
if(cmake.includes('web/console-native.html')||cmake.includes('web/index.html'))fail('Compiled alternate native/legacy HTML renderer');
if(!process.exitCode)console.log('APPROVED /preview/ static source/asset+JUCE contract PASS (actual Windows GUI/PCM FAT still required)');
