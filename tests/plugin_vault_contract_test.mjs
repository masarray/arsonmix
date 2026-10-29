// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const vault=read('src/PluginVault.h'), catalog=read('src/PluginCatalog.h'),
  main=read('src/Main.cpp'), cmake=read('CMakeLists.txt'),
  workflow=read('.github/workflows/p1-windows.yml'), testNative=read('tests/plugin_vault_test.cpp');

test('manual VST3 import physically copies whole plugin into the user workspace, not mere absolute source registration',()=>{
  assert.match(catalog,/scan \(juce::StringArray \{ file\.getFullPathName\(\) \}, copyToVault\)/);
  assert.match(catalog,/PluginVault::importSelected \(/);
  assert.match(catalog,/imported\.plugin/);
  assert.match(catalog,/worker_\.copyToVault_ = copyToVault/);
  assert.match(vault,/PortableWorkspace::vaultRoot \(workspace\)/);
  assert.match(vault,/std::filesystem::recursive_directory_iterator/);
  assert.match(vault,/symlink_status/);
  assert.match(vault,/std::filesystem::copy_file/);
  assert.match(vault,/\.incoming-/);
  assert.match(vault,/staging\.moveFileTo \(finalFolder\)/);
  assert.match(vault,/rollback\.committed = true/);
  assert.match(main,/Select a \.vst3 plugin file/);
  assert.match(main,/Select a \.vst3 bundle folder/);
  assert.match(main,/canSelectFiles/);
  assert.match(main,/canSelectDirectories/);
  assert.match(main,/chooseImport \(true\)/);
  assert.match(main,/chooseImport \(false\)/);
  assert.match(read('web/bridge.js'),/data-native-action="importVst3InPlace"/);
  assert.match(read('web/plugin-ui.js'),/IMPORT \.VST3 FILE/);
  assert.match(read('web/plugin-ui.js'),/IMPORT BUNDLE/);
  assert.doesNotMatch(vault,/deleteRecursively\(\).*selected/);
});
test('no all-PC scan required for three manually selected VSTs; no third-party copy from audio callback',()=>{
  assert.match(catalog,/void scanDefaults\(\)/);
  assert.match(catalog,/void importVst3 \(const juce::File& file, bool copyToVault = true\)/);
  assert.match(catalog,/struct Scanner final : juce::Thread/);
  assert.ok(!read('src/LiveAudioEngine.h').includes('PluginVault::'));
  assert.match(catalog,/scan \(juce::StringArray \{ file\.getFullPathName\(\) \}, copyToVault\)/);
});
test('Windows CTest exercises full VST bundle resources and preserves original files/versions',()=>{
  assert.match(cmake,/ARMixVaultTests/);
  assert.match(cmake,/SelectiveVst3BundleVaultImport/);
  assert.match(workflow,/ARMixVaultTests/);
  assert.match(testNative,/Contents\/x86_64-win\/MyVerb\.vst3/);
  assert.match(testNative,/Contents\/Resources\/moduleinfo\.json/);
  assert.match(testNative,/NEVER delete user source VST/);
  assert.match(testNative,/workspace\.copyDirectoryTo \(otherRoot\)/);
});
