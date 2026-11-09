import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = path => readFileSync(path, 'utf8');
const iss = read('installer/ArSonMix.iss');
const legacyShutdown = read('installer/ArSonMixUpdateShutdown.ps1');
const ps = read('scripts/build_verify_windows_installer.ps1');
const isolatedSmoke = read('scripts/test_inno_graceful_update_windows.ps1');
const fixture = read('tests/fixtures/installer_graceful_close.cs');
const ci = read('.github/workflows/p1-windows.yml');

test('per-user Inno Setup upgrades the same app and retains private Studio data', () => {
  for (const item of [
    'AppId=ArSonMix.LiveStudio', 'PrivilegesRequired=lowest',
    'AppVersion={#SourceAppVersion}', 'AppVerName=ArSonMix {#SourceAppVersion}',
    'DefaultDirName={localappdata}', 'UsePreviousAppDir=yes',
    'ArchitecturesAllowed=x64compatible', 'MinVersion=10.0.17763',
    'DisableWelcomePage=yes', 'DisableDirPage=yes',
    'DisableReadyPage=yes', 'DisableFinishedPage=yes',
    'SetupMutex=ArSonMix.PerUserInstaller.SingleInstance',
    'Preview', 'verified-gui.exe', 'BUILD_PROVENANCE.json'
  ]) assert.ok(iss.includes(item), item);

  assert.doesNotMatch(iss, /^\[UninstallDelete\]/m);
  assert.doesNotMatch(iss, /^Source:.*portable_mode\.txt/m);
  assert.doesNotMatch(iss, /taskkill|Stop-Process|TerminateProcess|CloseApplications=force/i);
  assert.match(iss, /Name: "\{autoprograms\}\\ArSonMix"/);
  assert.match(iss, /FactoryFX\\\*.*skipifsourcedoesntexist/);
});

test('single installer preflight requests graceful shutdown; Restart Manager cannot force old mixer', () => {
  assert.match(iss, /^CloseApplications=no$/m);
  assert.doesNotMatch(iss, /^CloseApplicationsFilter=/m);
  assert.match(iss, /^RestartApplications=no$/m);
  // Never prevent upgrades while the mixer is open: no AppMutex.
  assert.doesNotMatch(iss, /^AppMutex=/m);
  assert.match(iss, /Source: "\{#SourceRoot\}\\verified-gui\.exe"; DestDir: "\{app\}"; DestName: "ArSonMix\.exe"/);
});

test('successful interactive install or update launches exactly one new mixer', () => {
  assert.equal((iss.match(/^\[Run\]$/gm) ?? []).length, 1);
  assert.equal((iss.match(/^Filename: "\{app\}\\ArSonMix\.exe"/gm) ?? []).length, 1);
  const runLine = iss.split('\n').find(s => s.startsWith('Filename: "{app}\\ArSonMix.exe"'));
  assert.ok(runLine, '[Run] must invoke the installed mixer');
  for (const flag of ['nowait', 'runasoriginaluser', 'skipifsilent']) {
    assert.ok(runLine.includes(flag), flag);
  }
  assert.doesNotMatch(runLine, /\bpostinstall\b|\brunhidden\b/);
  // Interactive: launch immediately when installation completes, not a checkbox.
  // Silent enterprise/CI installs: never open an audio device in Session 0.
  assert.match(iss, /^DisableFinishedPage=yes$/m);
  assert.match(iss, /^RestartApplications=no$/m);
});

test('Windows FAT actually repeats the installer while a graceful WM_CLOSE test mixer is running', () => {
  for (const item of [
    'manifest.sourceCommit', 'manifest.exeSha256', 'manifest.fatStatus',
    '/DSourceAppVersion=$appVersion', 'project\\(ARMixConsole VERSION',
    'assets/icon/arsonmix-app.ico', 'FactoryFX',
    'Start-Process -FilePath $setup', 'Start-Process -FilePath $installedExe',
    'installer_graceful_close.cs', 'WM_CLOSE_SAVED', 'WINDOW_READY',
    '/NOCLOSEAPPLICATIONS',
    'INNO_GRACEFUL_UPDATE_PASS', 'Test-Path -LiteralPath $installSentinel',
    'Test-Path -LiteralPath $sentinel', 'INSTALLER_PROVENANCE.json',
    'OWNER_FIELD_AUDIO_LISTENING_REPORTED'
  ]) assert.ok((ps + fixture).includes(item), item);
  assert.equal((ps.match(/Start-Process -FilePath \$setup/g) ?? []).length, 2);
  assert.match(fixture, /FormClosing/);
  assert.match(fixture, /WM_QUERYENDSESSION/);
  assert.match(fixture, /WM_ENDSESSION/);
  assert.match(ps, /RM_QUERY_SAVED/);
  assert.match(ps, /RM_ENDSESSION_SAVED/);
  assert.match(ps, /INSTALLER_HANDSHAKE_SAVED/);
  assert.match(iss, /PrepareToInstall/);
  assert.match(iss, /OpenEventW/);
  assert.match(iss, /CheckForMutexes/);
  assert.match(fixture, /ArSonMix\.InstallShutdown/);
  assert.match(fixture, /ArSonMix\.MixerRunning/);
  assert.match(fixture, /Application\.Run\(window\)/);
  // No fixture is packaged: only the verified native EXE ships.
  assert.doesNotMatch(iss, /installer_graceful_close|\.cs"/);
});

test('Legacy pre-handshake mixer process is shut down before Inno touches its EXE', () => {
  // No taskkill/force; close only an exact path-matched ArSonMix process.
  assert.match(iss, /^Source: "ArSonMixUpdateShutdown\.ps1"; Flags: dontcopy$/m);
  assert.match(iss, /ExtractTemporaryFile\('ArSonMixUpdateShutdown\.ps1'\)/);
  assert.match(iss, /PrepareToInstall/);
  assert.match(iss, /OpenExclusiveFile/);
  assert.match(iss, /InstalledExeReplaceable\(Target\)/);
  assert.match(iss, /if ExitCode <> 0 then/);
  assert.match(legacyShutdown, /GetProcessesByName\('ArSonMix'\)/);
  assert.match(legacyShutdown, /\.MainModule\.FileName/);
  assert.match(legacyShutdown, /OrdinalIgnoreCase/);
  assert.match(legacyShutdown, /CloseMainWindow/);
  assert.match(legacyShutdown, /EnumWindows/);
  assert.match(legacyShutdown, /GetWindowThreadProcessId/);
  assert.match(legacyShutdown, /PostMessage/);
  assert.doesNotMatch(legacyShutdown + iss, /(?:^|\\n)\\s*(?:taskkill|Stop-Process|TerminateProcess|CloseApplications=force)\\b/im);
  assert.match(isolatedSmoke, /INNO_LEGACY_NO_HANDSHAKE_UPGRADE_PASS/);
  assert.match(isolatedSmoke, /INNO_BLOCKED_FAIL_CLOSED_PASS/);
  assert.match(fixture, /args\[2\] == "legacy"/);
  assert.match(fixture, /args\[2\] == "blocked"/);
  assert.doesNotMatch(iss, /DestName:\s*"ArSonMixUpdateShutdown\.ps1"/);
});

test('installer packaging is gated by the exact SHA FAT verifier', () => {
  const verify = ci.indexOf('      - name: Independently verify release ZIP and embedded GUI/engine before upload');
  const installer = ci.indexOf('      - name: Compile and validate per-user Windows RC installer from exact FAT PE');
  assert.ok(verify >= 0 && installer > verify);
  assert.match(ci, /ArSonMix-Windows-Installer-Preview/);
  assert.match(ci, /tests\/windows_installer_contract_test\.mjs/);
});


test('Smart Auto Updater uses an independent process and strict release gates', () => {
  const updater = read('installer/ArSonMixAutoUpdate.ps1');
  const native = read('src/Main.cpp');
  assert.match(iss, /^Source: "ArSonMixAutoUpdate\.ps1"; DestDir: "\{app\}"; Flags: ignoreversion$/m);
  assert.match(native, /getChildFile \("ArSonMixAutoUpdate\.ps1"\)/);
  assert.match(native, /juce::ChildProcess supervisor/);
  assert.match(native, /ARSONMIX_UPDATE_PROBE/);
  assert.match(native, /GUI_READY/);
  // JUCE 8.0.15 Uuid::toString() is un-dashed; PS .NET Guid.ToString("D") is dashed.
  assert.match(native, /juce::Uuid \(probe\)\.toDashedString\(\) == probe/);
  assert.doesNotMatch(native, /juce::Uuid \(probe\)\.toString\(\) == probe/);
  assert.match(updater, /api\.github\.com\/repos\/masarray\/arsonmix\/releases\?per_page=100/);
  assert.match(updater, /Download-Release-Verified/);
  assert.match(updater, /INSTALLER_PROVENANCE\.json/);
  assert.match(updater, /SHA256SUMS\.txt/);
  assert.match(updater, /Get-FileHash -LiteralPath \$part -Algorithm SHA256/);
  assert.match(updater, /WaitForExit\(120000\)/);
  assert.match(native, /launchSmartUpdater \(true\)/);
  assert.match(native, /launchSmartUpdater \(false\)/);
  assert.match(updater, /\[switch\]\$ApplyStaged/);
  assert.match(updater, /exit 0 # Checker ends here/);
  assert.match(updater, /DEFER_NEW_SESSION/);
  assert.match(updater, /Local\\ArSonMix\.AutoUpdater\.v1/);
  assert.match(updater, /PREVIOUS_FAILURE_BLOCKED/);
  assert.match(updater, /ROLLED_BACK/);
  assert.match(updater, /Never terminate a hung GUI/);
  assert.match(updater, /ARSONMIX_UPDATER_POLICY_PASS/);
  assert.match(isolatedSmoke, /ARSONMIX_UPDATER_POLICY_PASS/);
  assert.doesNotMatch(updater, /\b(?:taskkill|Stop-Process|TerminateProcess)\b/);
  assert.doesNotMatch(updater, /ArSonMix\.InstallShutdown\.v1|SetEvent\(/);
});


test('automatic Inno update never closes a newly active mixer', () => {
  assert.match(iss, /function IsArSonMixAutoUpdate: Boolean;/);
  assert.match(iss, /CompareText\(ParamStr\(I\), '\/ARSONMIXAUTOUPDATE'\)/);
  assert.match(iss, /if IsArSonMixAutoUpdate then/);
  assert.match(iss, /-ProbeOnly/);
  assert.match(iss, /automatic update deferred/i);
  assert.match(legacyShutdown, /\[switch\]\$ProbeOnly/);
  assert.match(legacyShutdown, /if\(\$ProbeOnly\) \{/);
  assert.match(legacyShutdown, /if\(\$count -ne 0\)\{exit 21\}/);
  assert.match(read('installer/ArSonMixAutoUpdate.ps1'), /\/ARSONMIXAUTOUPDATE/);
  assert.match(isolatedSmoke, /INNO_AUTO_UPDATE_LIVE_SESSION_PRESERVED_PASS/);
});


test('auto updater cannot race a still-running installer or Windows shutdown', () => {
  const updater = read('installer/ArSonMixAutoUpdate.ps1');
  const native = read('src/Main.cpp');
  assert.match(updater, /PriorityClass\s*=\s*\n\s*\[Diagnostics\.ProcessPriorityClass\]::BelowNormal/);
  assert.match(updater, /Installer is still running after timeout; no relaunch or rollback/);
  assert.match(updater, /UPDATED_EXE_FAILED_TO_START/);
  assert.match(native, /GetSystemMetrics \(SM_SHUTTINGDOWN\) == 0/);
});

test('Windows updater executes byte-level offline stage integrity fixtures', () => {
  const updater = read('installer/ArSonMixAutoUpdate.ps1');
  const smoke = read('scripts/test_inno_graceful_update_windows.ps1');
  assert.match(updater, /function Test-StagedAsset\(/);
  assert.match(updater, /if\(-not \(Test-StagedAsset \$path \$asset\)\)/);
  assert.match(updater, /Tampered staged EXE accepted/);
  assert.match(updater, /Short staged EXE accepted/);
  assert.match(updater, /Missing staged EXE accepted/);
  assert.match(updater, /ARSONMIX_UPDATER_STAGED_INTEGRITY_PASS/);
  assert.match(smoke, /ARSONMIX_UPDATER_STAGED_INTEGRITY_PASS/);
});

test('updater recovers after crash and offers only verified same-version repair', () => {
  const s = read('installer/ArSonMixAutoUpdate.ps1');
  for (const rule of [
    'Get-InstalledImageStatus', 'exeSha256', 'guiExeBytes',
    'Classify-InterruptedTransaction', 'previous-retained',
    'candidate-unverified', 'INVALID_UPDATER_STATE_QUARANTINED',
    "operation='repair'", 'STAGING_VERIFIED_SAME_VERSION_REPAIR',
    'Clear-StagedVendorFiles', 'ARSONMIX_UPDATER_SELF_REPAIR_POLICY_PASS',
    'TotalMinutes -lt 20', 'TotalHours -lt 6'
  ]) assert.ok(s.includes(rule), rule);
  assert.doesNotMatch(s, /Remove-Item -LiteralPath \$stage -Recurse/);
  assert.doesNotMatch(s, /\bunins000\.exe\b/);
});

test('cached installers are byte-verified and revoked releases cannot loop indefinitely', () => {
  const ps = read('installer/ArSonMixAutoUpdate.ps1');
  const smoke = read('scripts/test_inno_graceful_update_windows.ps1');
  for (const marker of [
    'Test-CompleteStagedTransaction', 'STAGED_CACHE_INVALID_REDOWNLOAD',
    'STAGED_READY_FOR_NORMAL_EXIT', 'RELEASE_REVOKED_UPDATE_DEFERRED',
    'ARSONMIX_UPDATER_CACHE_AND_RECOVERY_PASS'
  ]) assert.ok(ps.includes(marker), marker);
  assert.match(smoke, /ARSONMIX_UPDATER_CACHE_AND_RECOVERY_PASS/);
});

test('transient Inno failure allows one delayed retry, never a reinstall loop', () => {
  const ps = read('installer/ArSonMixAutoUpdate.ps1');
  const smoke = read('scripts/test_inno_graceful_update_windows.ps1');
  for (const x of ['Should-RetryInstaller','installAttempts','retryAfterUtc','AddMinutes(30)',
    'INSTALLER_RETRY_DEFERRED_COOLDOWN','INSTALLER_TRANSIENT_FAILURE_RETRY_NEXT_EXIT',
    'ARSONMIX_UPDATER_RETRY_POLICY_PASS']) assert.ok(ps.includes(x), x);
  assert.match(smoke,/ARSONMIX_UPDATER_RETRY_POLICY_PASS/);
});

test('Windows Inno fixture proves unattended in-place repair after binary corruption', () => {
  const smoke = read('scripts/test_inno_graceful_update_windows.ps1');
  assert.match(smoke,/INNO_AUTO_REPAIR_CORRUPT_EXE_PASS/);
  assert.match(smoke,/Same-version repair did not restore verified EXE/);
  assert.match(smoke,/Same-version repair erased Studio or install-local user files/);
});
