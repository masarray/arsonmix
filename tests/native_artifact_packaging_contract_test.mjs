// SPDX-License-Identifier: AGPL-3.0-only
// Prevent packaging a native test executable as the GUI application again.
// Exact PE and source SHA verification happens at package time on Windows.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read = path => readFileSync(path, 'utf8');
const workflow = read('.github/workflows/p1-windows.yml');
const packaging = read('scripts/package_native_windows.ps1');
const verify = read('scripts/verify_native_windows_artifact.ps1');
const nativeMain = read('src/Main.cpp');
const cmake = read('CMakeLists.txt');
const fat = read('docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md');

test('native FAT artifact picks an EXACT JUCE GUI target path, never wildcard test EXEs', () => {
  assert.match(cmake, /juce_add_gui_app\(ARMixConsole/);
  assert.match(cmake, /PRODUCT_NAME "AR MIX Studio"/);
  assert.match(packaging, /ARMixConsole_artefacts\/Release\/AR MIX Studio\.exe/);
  assert.match(packaging, /Missing JUCE GUI executable at expected target/);
  assert.doesNotMatch(workflow, /Get-ChildItem -Path build -Filter/);
  assert.doesNotMatch(workflow, /Select-Object -First 1/);
  assert.match(workflow, /scripts\/package_native_windows\.ps1/);
});
test('native FAT artifact proves AMD64 PE32+ WINDOWS GUI, rejects console-subsystem tests', () => {
  assert.match(packaging, /0x8664/);
  assert.match(packaging, /0x020b/);
  assert.match(packaging, /\$subsystem -ne 2/);
  assert.match(packaging, /a test console EXE is NOT the app/);
  assert.match(packaging, /exeSha256/);
  assert.match(packaging, /sourceCommit/);
  assert.match(packaging, /appJsSha256/);
  assert.match(packaging, /stylesCssSha256/);
  assert.match(packaging, /BUILD_PROVENANCE\.json/);
  assert.match(packaging, /HARDWARE_FAT_GUIDE\.md/);
  assert.match(packaging, /EXPERIMENTAL_NOT_HARDWARE_QUALIFIED/);
});
test('production Windows workflow rejects a tiny 648KB zip, test EXE, or missing GUI/engine runtime data', () => {
  assert.match(packaging, /minimumGuiExeBytes = 4MB/);
  assert.match(verify, /\$bytes\.Length -lt 4MB/);
  assert.match(verify, /\$archiveBytes -lt 2MB/);
  assert.match(verify, /Compress-Archive/);
  assert.match(verify, /ZipFile\]::OpenRead/);
  assert.match(verify, /ComputeHash\(\$stream\)/);
  assert.match(verify, /manifest\.sourceCommit/);
  assert.match(verify, /manifest\.appJsSha256/);
  assert.match(verify, /manifest\.stylesCssSha256/);
  for (const marker of [
    'function openChannelIdentity',
    'class="master-analysis"',
    '--ari-master-cap',
    'function syncNativeMixerControls',
    'AUDIO_GLITCH_WINDOW',
  ]) {
    assert.ok(verify.includes(marker), 'release audit must search compiled artifact for '+marker);
  }
  assert.match(nativeMain, /BinaryData::app_js/);
  assert.match(nativeMain, /BinaryData::styles_css/);
  assert.match(nativeMain, /withResourceProvider \(assetFor\)/);
  assert.match(workflow, /Independently verify release ZIP and embedded GUI\/engine before upload/);
  assert.match(workflow, /scripts\/verify_native_windows_artifact\.ps1/);
  assert.ok(workflow.indexOf('scripts/verify_native_windows_artifact.ps1') <
            workflow.indexOf('uses: actions/upload-artifact@v4'));
});
test('physical test protocol differentiates actual native channel 1 and GUI-only channels', () => {
  assert.match(fat, /native engine implements \*\*Channel 1\*\* only/i);
  assert.match(fat, /OUTPUT_ONLY_TONE/);
  assert.match(fat, /CAPTURE_ONLY/);
  assert.match(fat, /DRY_THROUGH/);
  assert.match(fat, /44,100 Hz/);
  assert.match(fat, /48,000 Hz/);
  assert.match(fat, /RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE/);
});
