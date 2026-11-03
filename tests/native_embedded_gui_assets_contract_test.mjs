// SPDX-License-Identifier: AGPL-3.0-only
// Integration FAT gate: the physical Windows JUCE executable must serve
// the exact GUI assets used by the joined operator console, OFFLINE.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,existsSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const cmake=read('CMakeLists.txt');
const host=read('src/Main.cpp');
const html=read('web/index.html');
const css=read('web/mixer-layout.css');
const packageScript=read('scripts/package_native_windows.ps1');
const verify=read('scripts/verify_native_windows_artifact.ps1');
const images=[
  'assets/fader-silver2.png',
  'assets/faders/FADER-ARI-SILVER.png',
  'assets/faders/FADER-ARI-ROSEGOLD.png',
  'assets/faders/FADER-ARI-GOLD.png',
  'assets/pan/PAN-SLIDER.png',
  'assets/knob-ICON.svg'
];
test('joined native executable embeds actual operator hardware-control JS',()=>{
  assert.match(html,/<script src="hardware-keys\.js"><\/script>/);
  assert.ok(existsSync('web/hardware-keys.js'));
  assert.ok(cmake.includes('web/hardware-keys.js'));
  assert.ok(host.includes('path == "/hardware-keys.js"'));
  assert.ok(host.includes('BinaryData::hardwarekeys_js'));
  assert.match(host,/mime = "text\/javascript"/);
  for(const [file,hash] of [
    ['web/mixer-io.js','mixerIoJsSha256'],
    ['web/mixer-layout.js','mixerLayoutJsSha256'],
    ['web/hardware-keys.js','hardwareKeysJsSha256']
  ]) {
    assert.ok(packageScript.includes(hash+' = '),hash+' missing manifest');
    assert.ok(verify.includes('manifest.'+hash),hash+' not independently verified');
    assert.ok(verify.includes('Path="'+file+'"'),file+' not verified inside EXE');
  }
});
test('native WebView resolves every referenced physical fader/PAN graphic without disk or network',()=>{
  assert.match(host,/BinaryData::namedResourceListSize/);
  assert.match(host,/BinaryData::getNamedResourceOriginalFilename/);
  assert.match(host,/BinaryData::getNamedResource\(id,size\)/);
  assert.match(host,/static constexpr const char\* allowed\[\]/);
  assert.match(host,/if \(!permitted\) return std::nullopt/);
  const urls=[...css.matchAll(/url\(\s*["']?(assets\/[^"')\s]+)["']?\s*\)/g)]
    .map(x=>x[1]);
  assert.ok(urls.length>=4,'fader and PAN artwork not linked from joined CSS');
  for(const path of new Set(urls)) {
    assert.ok(images.includes(path),'unrecognized external GUI graphic '+path);
    assert.ok(existsSync('web/'+path),'missing GUI graphic '+path);
  }
  for(const path of images) {
    assert.ok(cmake.includes('web/'+path),'missing JUCE BinaryData: '+path);
    assert.ok(host.includes('"/'+path+'"'),'WebView route absent: '+path);
  }
  assert.doesNotMatch(host.slice(host.indexOf('else if (path.startsWith("/assets/"))'),
    host.indexOf('else return std::nullopt;',host.indexOf('else if (path.startsWith("/assets/"))')),
    /FileInputStream|File::|getChildFile|http:|https:/);
});
