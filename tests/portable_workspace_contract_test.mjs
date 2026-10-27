// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read = p=>readFileSync(p,'utf8');
const policy = read('src/PortableWorkspace.h');
const main = read('src/Main.cpp');
const catalog = read('src/PluginCatalog.h');
const preset = read('src/NativePresetArchive.h');
const cmake = read('CMakeLists.txt');
const packaging = read('scripts/package_native_windows.ps1');
const verify = read('scripts/verify_native_windows_artifact.ps1');
const workflow = read('.github/workflows/p1-windows.yml');
const architecture = read('docs/ARD_PORTABLE_STUDIO_V1_20261027.md');

test('one executable, marker chooses storage not feature gates',()=>{
  assert.match(policy,/portable_mode\.txt/);
  assert.match(policy,/dataRootFor/);
  assert.match(policy,/installedDataRoot/);
  assert.match(policy,/getChildFile \("Studio"\)/);
  assert.match(main,/return armix::storage::PortableWorkspace::dataRoot\(\)/);
  assert.match(main,/PortableWorkspace::browserDataRoot\(\)/);
  assert.match(main,/machineSettingsDirectory\(\)\.getChildFile \("audio-devices\.xml"\)/);
  assert.doesNotMatch(main,/portable.*disable.*vst/i);
});

test('plugin catalog and native vendor ZIP persist workspace refs without changing read API',()=>{
  assert.match(policy,/workspace:\/\//);
  assert.match(policy,/safeRelativePath/);
  assert.match(policy,/part == "\.\."/);
  assert.match(catalog,/PortableWorkspace::encode \(dataFolder_, juce::File \(entry\.path\)\)/);
  assert.match(catalog,/PortableWorkspace::decode \(dataFolder_, entry\.path\)/);
  assert.match(preset,/PortableWorkspace::encode \(workspaceRoot_, juce::File \(fx\.path\)\)/);
  assert.match(preset,/PortableWorkspace::decode \(workspaceRoot_, storedPath\)/);
  assert.match(preset,/output = std::move \(temp\)/);
  assert.match(preset,/schemaVersion", 1/); // backward-compatible v1 payload
  assert.match(main,/presets_ \(settingsDirectory\(\)\.getChildFile \("presets"\), settingsDirectory\(\)\)/);
});
test('real Windows-native USB move test is built in existing CTest and release includes marker',()=>{
  assert.match(cmake,/ARMixWorkspaceTests/);
  assert.match(cmake,/PortableStudioUsbRelocation/);
  assert.match(workflow,/ARMixWorkspaceTests/);
  assert.match(workflow,/portable_workspace_contract_test\.mjs/);
  assert.match(packaging,/ARSONMIX_PORTABLE_WORKSPACE_V1/);
  assert.match(packaging,/portable_mode\.txt/);
  assert.match(verify,/Public artifact MUST NOT bundle user-specific plugins/);
  assert.match(verify,/portable_mode\.txt/);
  assert.match(architecture,/Backup \/ Restore|Backup\/Restore|BACKUP STUDIO/);
  assert.match(architecture,/one input channel|One live input channel/i);
});
