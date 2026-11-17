// SPDX-License-Identifier: AGPL-3.0-only
// Fast no-compiler check of first-party licence and exact release boundaries.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = path => readFileSync(path, 'utf8');
const full = read('LICENSE');
const notice = read('LICENSE-NOTICE.txt');
const fx = read('src/NativeBuiltInFx.h');

test('the repository contains the complete canonical AGPLv3 text', () => {
  assert.match(full, /^GNU AFFERO GENERAL PUBLIC LICENSE\nVersion 3, 19 November 2007/);
  assert.match(full, /END OF TERMS AND CONDITIONS/);
  assert.ok(full.length > 30000, 'full AGPLv3 legal text required');
  assert.match(notice, /SPDX-License-Identifier: AGPL-3\.0-only/);
  assert.match(notice, /GNU Affero General Public License, version 3\nonly/);
  assert.match(read('README.md'), /AGPL-3\.0-only \(GNU Affero/);
});

test('native mixer and built-in effects are AGPL, vendor plugins remain separate', () => {
  for (const name of ['Parametric EQ','Compressor','Harmonice','Vocal Delay',
                      '8D-SonKu','Guitar FX Rig','ImmerSIB']) {
    assert.ok(notice.includes(name), 'missing native FX scope: '+name);
  }
  for (const source of [
    'src/Main.cpp', 'src/LiveAudioEngine.h', 'src/NativeBuiltInFx.h',
    'engine/builtin_fx/ChannelEqProcessor.h',
    'engine/builtin_fx/ChannelCompressorProcessor.h',
    'engine/builtin_fx/ChannelHarmonyProcessor.h',
    'engine/builtin_fx/VocalThrowProcessor.h',
    'engine/builtin_fx/SonKuSpatialProcessor.h',
    'engine/builtin_fx/GuitarRigProcessor.h',
    'engine/builtin_fx/ImerSipP92Processor.h'
  ]) assert.match(read(source).slice(0,300), /SPDX-License-Identifier: AGPL-3\.0-only/, source);
  assert.match(fx, /static_assert\(validNativeBuiltInFxCatalog\(nativeBuiltInFx\)\)/);
  assert.match(notice, /Third-party materials are NOT relicensed/);
  assert.match(read('third_party/signalsmith/README.md'), /MIT licensed/);
});

test('Windows portable, Inno installer and Linux/macOS embed licence notices', () => {
  const win=read('scripts/package_native_windows.ps1');
  const verify=read('scripts/verify_native_windows_artifact.ps1');
  const iss=read('installer/ArSonMix.iss');
  const fat=read('scripts/build_verify_windows_installer.ps1');
  const smoke=read('scripts/test_inno_graceful_update_windows.ps1');
  for (const name of ['LICENSE','LICENSE-NOTICE.txt','LICENSE-SIGNALSMITH-STRETCH.txt',
                      'LICENSE-SIGNALSMITH-LINEAR.txt','OFL-LICENSE-Inter.txt']) {
    assert.ok(win.includes('"'+name+'"'), 'Windows packager missing '+name);
    assert.ok(verify.includes('"'+name+'"'), 'Windows verifier missing '+name);
    assert.ok(iss.includes('\\'+name+'"'), 'Inno setup missing '+name);
    assert.ok(fat.includes('"'+name+'"'), 'installed FAT missing '+name);
    assert.ok(smoke.includes('"'+name+'"'), 'Inno smoke fixture missing '+name);
  }
  for (const key of ['licenseTextSha256','licenseNoticeSha256',
                    'signalsmithStretchLicenseSha256','signalsmithLinearLicenseSha256',
                    'interOflLicenseSha256']) {
    assert.ok(win.includes(key+' = '), 'manifest missing '+key);
    assert.ok(verify.includes('"'+key+'"'), 'independent verifier missing '+key);
    assert.ok(fat.includes('"'+key+'"'), 'installer FAT missing '+key);
  }
  assert.match(verify, /Full AGPL-3\.0 text absent from distribution/);
  assert.match(verify, /Compare-Object -ReferenceObject \$expected -DifferenceObject \$actual/);
  assert.match(fat, /Installed licence missing or altered/);
  for (const pkg of ['scripts/package_native_linux.sh','scripts/package_native_macos.sh']) {
    const s=read(pkg);
    assert.match(s, /cp LICENSE "\$tmp\/ArSonMix\/LICENSE"/);
    assert.match(s, /cp LICENSE-NOTICE\.txt/);
    assert.match(s, /THIRD_PARTY_8D_SONKU_HRTF\.md/);
    assert.match(s, /LICENSE-SIGNALSMITH-STRETCH\.txt/);
    assert.match(s, /LICENSE-SIGNALSMITH-LINEAR\.txt/);
    assert.match(s, /OFL-LICENSE-Inter\.txt/);
  }
  assert.match(read('site/index.html'), /AGPL-3\.0 license<\/a>/);
});
