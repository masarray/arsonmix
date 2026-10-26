// SPDX-License-Identifier: AGPL-3.0-only
// Prevent packaging a native test executable as the GUI application again.
// Exact PE and source SHA verification happens at package time on Windows.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read = path => readFileSync(path, 'utf8');
const workflow = read('.github/workflows/p1-windows.yml');
const packaging = read('scripts/package_native_windows.ps1');
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
test('physical test protocol differentiates actual native channel 1 and GUI-only channels', () => {
  assert.match(fat, /native engine implements \\*\\*Channel 1\\*\\* only/i);
  assert.match(fat, /OUTPUT_ONLY_TONE/);
  assert.match(fat, /CAPTURE_ONLY/);
  assert.match(fat, /DRY_THROUGH/);
  assert.match(fat, /44,100 Hz/);
  assert.match(fat, /48,000 Hz/);
  assert.match(fat, /RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE/);
});
