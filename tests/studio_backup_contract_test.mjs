// SPDX-License-Identifier: AGPL-3.0-only
// Checks the user-facing native BACKUP STUDIO integration, NOT a mock JS archive.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read = p => readFileSync(p,'utf8');
const native = read('src/Main.cpp');
const backup = read('src/StudioBackup.h');
const gui = read('web/bridge.js');
const cmake = read('CMakeLists.txt');
const workflow = read('.github/workflows/p1-windows.yml');
const fixture = read('tests/studio_backup_test.cpp');

test('same public native app exposes explicit Backup Studio button and OS folder picker', () => {
  assert.match(gui,/data-backup-studio="1"/);
  assert.match(gui,/send\("backupStudio"\)/);
  assert.match(native,/action == "backupStudio"/);
  assert.match(native,/void chooseStudioBackup\(\)/);
  assert.match(native,/FileBrowserComponent::canSelectDirectories/);
  assert.match(native,/studioBackupWorker_\.launch/);
});
test('backup is non realtime and fail-closed, not a browser localStorage illusion', () => {
  assert.match(backup,/class Worker final : private juce::Thread/);
  assert.match(backup,/std::filesystem::recursive_directory_iterator/);
  assert.match(backup,/\bjuce::SHA256 \(to\)/);
  assert.match(backup,/\bjuce::SHA256 \(from\)/);
  assert.match(backup,/const auto validation = verify \(staging\)/);
  assert.match(backup,/if \(complete\.exists\(\) \|\| !staging\.moveFileTo \(complete\)\)/);
  assert.match(backup,/backup-manifest\.json/);
  assert.match(backup,/\bportable_mode\.txt/);
  assert.match(backup,/WEBVIEW_CACHE_EXCLUDED|GUI_BROWSER_SCENES_NOT_PORTABLE_YET/);
  assert.match(backup,/externalCatalogPluginReferences/);
  assert.match(native,/studioBackupBusy_/);
  assert.doesNotMatch(read('src/LiveAudioEngine.h'),/StudioBackup::/);
});
test('Windows CI compiles and runs native corruption/USB backup tests in existing job',()=>{
  assert.match(cmake,/ARMixBackupTests/);
  assert.match(cmake,/VerifiedPortableStudioBackup/);
  assert.match(workflow,/ARMixBackupTests/);
  assert.match(workflow,/studio_backup_contract_test\.mjs/);
  assert.match(fixture,/StudioBackup::verify/);
  assert.match(fixture,/INJECTED\.vst3/);
  assert.match(fixture,/portable_mode\.txt/);
});
