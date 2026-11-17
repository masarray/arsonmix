// SPDX-License-Identifier: AGPL-3.0-only
// Fast no-compiler contract for an OFFLINE legal center and clean distributions.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = path => readFileSync(path, 'utf8');
const full = read('LICENSE');
const notice = read('LICENSE-NOTICE.txt');

test('canonical full GNU AGPLv3 and original native FX scope remain intact', () => {
  assert.match(full, /^GNU AFFERO GENERAL PUBLIC LICENSE\nVersion 3, 19 November 2007/);
  assert.match(full, /END OF TERMS AND CONDITIONS/);
  assert.ok(full.length > 30000);
  assert.match(notice, /AGPL-3\.0-only/);
  assert.match(notice, /Third-party materials are NOT relicensed/);
  for(const name of ['Parametric EQ','Compressor','Harmonice','Vocal Delay',
                     '8D-SonKu','Guitar FX Rig','ImmerSIB'])
    assert.ok(notice.includes(name), 'missing source scope '+name);
  for(const source of ['src/Main.cpp','src/LiveAudioEngine.h','src/NativeBuiltInFx.h',
        'engine/builtin_fx/ChannelEqProcessor.h',
        'engine/builtin_fx/ChannelCompressorProcessor.h',
        'engine/builtin_fx/ChannelHarmonyProcessor.h',
        'engine/builtin_fx/VocalThrowProcessor.h',
        'engine/builtin_fx/SonKuSpatialProcessor.h',
        'engine/builtin_fx/GuitarRigProcessor.h',
        'engine/builtin_fx/ImerSipP92Processor.h'])
    assert.match(read(source).slice(0,300), /SPDX-License-Identifier: AGPL-3\.0-only/,source);
  assert.match(read('engine/builtin_fx/sonku/SonKuMeasuredHrir.h').slice(0,100), /SPDX-License-Identifier: MIT/);
});

test('all exact-source legal documents live inside the one native JUCE binary', () => {
  const cmake=read('CMakeLists.txt');
  const hdr=read('src/EmbeddedLegalResources.h');
  const app=read('src/Main.cpp');
  const build=read('scripts/build_approved_native_gui.py');
  const ui=read('web/approved-native-legal.js');
  for(const symbol of ['armixagpl','armixnotice','armixhrirmit',
       'armixsignalsmithstretch','armixsignalsmithlinear','armixinterofl',
       'armixvbcablenotice']){
    assert.ok(cmake.includes('/'+symbol+'.txt'),symbol+' not staged for binary');
    assert.ok(hdr.includes(symbol+'_txt'),symbol+' not mapped to JUCE data');
  }
  assert.match(cmake, /\$\{ARMIX_LEGAL_RESOURCE_FILES\}/);
  assert.match(cmake, /ARMIX_LEGAL_RESOURCE_COUNT EQUAL 7/);
  assert.match(app, /resolveEmbeddedLegalResource \(path, data, size, mime\)/);
  assert.match(app, /arMixOpenLegalSource/);
  assert.match(app, /ARMIX_BUILD_SOURCE_SHA/);
  assert.match(build, /approved-native-legal\.js/);
  assert.match(ui, /#moreTools \.more-tools-panel/);
  assert.match(ui, /fetch\("\.\/legal\/"/);
  assert.match(ui, /aria-modal/);
  assert.doesNotMatch(ui, /AudioIODeviceCallback|nativeDSP|setInterval/);
  assert.match(read('scripts/verify_embedded_legal.py'), /source not in binary/);
});

test('Windows portable ZIP and Inno installer retain zero top-level legal files', () => {
  const win=read('scripts/package_native_windows.ps1');
  const verify=read('scripts/verify_native_windows_artifact.ps1');
  const iss=read('installer/ArSonMix.iss');
  const fat=read('scripts/build_verify_windows_installer.ps1');
  const smoke=read('scripts/test_inno_graceful_update_windows.ps1');
  assert.match(win,/verify_embedded_legal\.py --binary \$destination/);
  assert.match(verify,/verify_embedded_legal\.py --binary/);
  assert.match(verify,/Compare-Object -ReferenceObject \$expected -DifferenceObject \$actual/);
  assert.match(fat,/verify_embedded_legal\.py --binary \$installedExe/);
  assert.match(smoke,/Installer left redundant legal file/);
  assert.match(iss,/Source: "\.\.\\LICENSE"; Flags: dontcopy/);
  assert.match(iss,/Source: "\.\.\\docs\\THIRD_PARTY_8D_SONKU_HRTF\.md"; Flags: dontcopy/);
  assert.doesNotMatch(iss,/^LicenseFile=/m);
  assert.match(iss,/Flags: dontcopy/);
  for(const key of ['licenseTextSha256','licenseNoticeSha256','hrirNoticeSha256',
       'signalsmithStretchLicenseSha256','signalsmithLinearLicenseSha256',
       'interOflLicenseSha256','vbCableNoticeSha256']){
    assert.ok(win.includes(key+' = '),key+' absent from packager manifest');
    assert.ok(verify.includes('"'+key+'"'),key+' absent from verifier');
  }
  for(const name of ['LICENSE','LICENSE-NOTICE.txt','OFL-LICENSE-Inter.txt',
        'THIRD_PARTY_8D_SONKU_HRTF.md']){
    assert.doesNotMatch(win,new RegExp('Destination \\(Join-Path \\$OutputDir "'+name.replaceAll('.','\\.')+'"\\)'));
  }
  assert.match(read('README.md'),/About & Licenses/);
});

test('Linux/macOS scan the real native binary without loose AGPL texts', () => {
  for(const pkg of ['scripts/package_native_linux.sh','scripts/package_native_macos.sh']){
    const script=read(pkg);
    assert.match(script,/python3 scripts\/verify_embedded_legal\.py --binary/);
    assert.doesNotMatch(script,/cp LICENSE "\$tmp\/ArSonMix\/LICENSE"/);
    assert.doesNotMatch(script,/cp LICENSE-NOTICE\.txt/);
  }
  assert.match(read('site/index.html'), /AGPL-3\.0 license<\/a>/);
});
