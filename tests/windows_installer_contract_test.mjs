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

test('official standard VB-CABLE ZIP is pinned and opt-in without elevating ArSonMix', () => {
  const m = JSON.parse(read('config/vbcable-official-package.json'));
  const pack = read('scripts/prepare_vbcable_package.ps1');
  const bootstrap = read('installer/ArSonMixVBCableInstall.ps1');
  const notice = read('installer/VB-CABLE-NOTICE.txt');
  assert.equal(m.filename, 'VBCABLE_Driver_Pack45.zip');
  assert.match(m.url, /^https:\/\/download\.vb-audio\.com\/Download_CABLE\//);
  assert.match(m.sha256, /^[a-f0-9]{64}$/);
  assert.match(m.proof, /actions\/runs\/43079930924/);
  assert.match(iss, /^PrivilegesRequired=lowest$/m);
  assert.match(iss, /Name: "vbcable_driver";.*Flags: unchecked/);
  assert.match(iss, /VBCABLE_Driver_Pack45\.zip/);
  assert.match(iss, /ArSonMixVBCableInstall\.ps1/);
  assert.match(iss, /Tasks: vbcable_driver; Flags: runasoriginaluser skipifsilent waituntilterminated/);
  assert.match(iss, /-ExpectedSha256 ""\{#VBCableSha256\}""/);
  assert.match(ps, /prepare_vbcable_package\.ps1/);
  assert.match(ps, /DVBCableSha256=/);
  assert.match(pack, /Get-FileHash[\s\S]*?-cne \$m\.sha256/);
  assert.match(pack, /OFFICIAL_VBCABLE_BUNDLE_VERIFIED/);
  assert.match(bootstrap, /Get-AuthenticodeSignature/);
  assert.match(bootstrap, /\$sig\.Status -ne "Valid"/);
  // Official 2024 Pack45 signs as "BUREL VINCENT Entrepreneur individuel",
  // while older VB-Audio signatures used "Vincent Burel". Both must be
  // recognized, but an unsigned or invalid executable is NEVER accepted.
  assert.match(bootstrap, /Vincent\\s\+Burel/);
  assert.match(bootstrap, /Burel\\s\+Vincent/);
  assert.match(bootstrap, /Get-PnpDevice -PresentOnly/);
  assert.match(bootstrap, /-Verb RunAs/);
  assert.match(bootstrap, /PENDING_REBOOT/);
  assert.doesNotMatch(bootstrap + iss, /certutil\s+-addstore|Add-TrustedPublisher\s*\(|SetDefaultEndpoint\s*\(|pnputil\s+\/delete-driver|Uninstall-PnpDevice\s|LocalMachine\\TrustedPublisher/i);
  assert.match(notice, /donationware/i);
  assert.match(notice, /professional/i);
  assert.match(notice, /www\.vb-cable\.com/);
  assert.match(notice, /A\+B and C\+D/);
  assert.match(iss, /Flags: runasoriginaluser skipifsilent waituntilterminated/);
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
  assert.match(updater, /Test-StagedAsset \$newPath \$state\.newAsset/);
  assert.match(updater, /Test-StagedAsset \$oldPath \$state\.previousAsset/);
  assert.doesNotMatch(updater, /foreach\(\$pair in @\(@\(/);
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

test('one-click recovery can restore main EXE without a live app', () => {
  const updater = read('installer/ArSonMixAutoUpdate.ps1');
  assert.match(iss, /Name: "\{autoprograms\}\\Repair ArSonMix"/);
  assert.match(iss, /-RecoveryMode/);
  assert.match(iss, /function IsArSonMixRecovery: Boolean;/);
  assert.match(iss, /if not FileExists\(Target\) and not IsArSonMixRecovery then/);
  assert.match(updater, /\[switch\]\$RecoveryMode/);
  assert.match(updater, /Restore-InstalledRelease/);
  assert.match(updater, /-AllowMissingExeRecovery/);
  assert.match(updater, /\/ARSONMIXRECOVERY/);
  assert.match(updater, /ABANDONED_MUTEX_RECLAIMED/);
  assert.match(updater, /Test-NoActiveMixer/);
  assert.match(isolatedSmoke, /INNO_MISSING_EXE_RECOVERY_PASS/);
  assert.doesNotMatch(updater, /\bStop-Process\b|\btaskkill\b/);
});

test('updater script has one coherent main try/finally and no duplicated tail', () => {
  const script = read('installer/ArSonMixAutoUpdate.ps1').replace(/\r\n/g, '\n');
  const tail = [
    '} finally {',
    '    if($locked -and $null -ne $mutex){$mutex.ReleaseMutex()}',
    '    if($null -ne $mutex){$mutex.Dispose()}',
    '}',
  ].join('\n');
  assert.equal(script.split(tail).length - 1, 1, 'duplicate main-script terminator');
  assert.ok(script.trimEnd().endsWith(tail), 'script has garbage after terminator');
  assert.equal(script.split('function Restore-InstalledRelease').length - 1, 1);
  assert.equal(script.split('if($PolicySelfTest)').length - 1, 1);
});

test('healthy install, repair and rollback require real EXE SHA, not provenance commit alone', () => {
  const script = read('installer/ArSonMixAutoUpdate.ps1');
  const smoke = read('scripts/test_inno_graceful_update_windows.ps1');
  assert.match(script, /function Test-InstalledRelease\(/);
  assert.match(script, /Get-InstalledImageStatus \$manifest/);
  assert.match(script, /Repair did not restore exact authenticated EXE and source manifest/);
  assert.match(script, /if\(\$upgradeOK -and \(Test-InstalledRelease/);
  assert.match(script, /if\(Test-InstalledRelease \$installedSha\)/);
  assert.match(script, /GUI_READY' -and\s*\n\s*\(Test-InstalledRelease \$expectedSha\)/);
  assert.match(script, /Same source with changed EXE accepted/);
  assert.match(smoke, /ARSONMIX_UPDATER_EXACT_IMAGE_POLICY_PASS/);
});

test('two genuine release installers upgrade and roll back only in disposable Windows FAT', () => {
  const real = read('scripts/test_two_genuine_inno_releases_windows.ps1');
  const ci = read('.github/workflows/p1-windows.yml');
  assert.match(real, /REAL_RELEASE_FAT_REQUIRES_DISPOSABLE_GITHUB_WINDOWS_RUNNER/);
  assert.match(real, /v0\.5\.0-alpha\.4/);
  assert.match(real, /v0\.5\.0-alpha\.5/);
  assert.match(real, /GENUINE_RELEASE_BASELINE_PASS/);
  assert.match(real, /GENUINE_RELEASE_UPGRADE_PASS/);
  assert.match(real, /GENUINE_RELEASE_ROLLBACK_PASS/);
  assert.match(real, /INSTALLER_PROVENANCE\.json/);
  assert.match(real, /SHA256SUMS\.txt/);
  assert.match(real, /sourceCommit/);
  assert.match(real, /exeSha256/);
  assert.match(real, /guiExeBytes/);
  assert.match(real, /proof-user-session\.smix/);
  assert.match(ci, /Prove two authentic historical Inno releases upgrade and roll back/);
  assert.match(ci, /scripts\/test_two_genuine_inno_releases_windows\.ps1/);
  // API requests from shared runner IPs must not silently fail at 60/hour.
  // The workflow already has contents:read. Only the metadata lookup gets
  // the scoped Actions token; downloadable release assets stay hash checked.
  assert.match(real, /GENUINE_RELEASE_METADATA_AUTH_REQUIRED/);
  assert.match(real, /Authorization="Bearer \$env:GITHUB_TOKEN"/);
  assert.match(real, /Invoke-RestMethod -Uri \$uri -Headers \$releaseHeaders/);
  assert.match(ci, /GITHUB_TOKEN:\s*\$\{\{ github\.token \}\}/);
  assert.match(ci, /permissions:\s*\n\s*contents: read/);
  assert.doesNotMatch(real, /Invoke-WebRequest -Uri \$url -Headers \$releaseHeaders/);
  assert.doesNotMatch(real, /(?:Stop-Process|taskkill|Remove-Item\s+-LiteralPath\s+\$install\s+-Recurse)/);
});

test('auto apply authenticates the published rollback asset as well as the new release', () => {
  const source = read('installer/ArSonMixAutoUpdate.ps1');
  assert.match(source, /\$approvedOld=Approved-Installer \$release/);
  assert.match(source, /\$release\.tag_name -ceq \$state\.previousTag/);
  assert.match(source, /\$release\.target_commitish -ceq \$state\.previousSha/);
  assert.match(source, /\$approvedOld\.name -cne \$state\.previousAsset\.name/);
  assert.match(source, /\$approvedOld\.digest -cne \$state\.previousAsset\.digest/);
  assert.match(source, /ROLLBACK_RELEASE_UNAVAILABLE_UPDATE_DEFERRED/);
  assert.ok(source.indexOf('ROLLBACK_RELEASE_UNAVAILABLE_UPDATE_DEFERRED') <
            source.indexOf("$state.phase='installing'"));
});

test('updater enumerates GitHub JSON release arrays correctly in Windows PowerShell 5.1', () => {
  const script=read('installer/ArSonMixAutoUpdate.ps1');
  assert.doesNotMatch(script, /@\(\s*Invoke-RestMethod -Uri \$api/);
  for (const name of ['releases','liveReleases']) {
    assert.match(script, new RegExp('\\$'+name+'=Invoke-RestMethod -Uri \\$api'));
  }
  assert.match(script, /foreach\(\$release in \$releases\)/);
  assert.match(script, /foreach\(\$release in \$liveReleases\)/);
});
test('WebView2 error screen cannot acknowledge healthy GUI; staging retries after 20m', () => {
  const script=read('installer/ArSonMixAutoUpdate.ps1');
  const native=read('src/Main.cpp');
  assert.match(native,/if \(stage == 2 && probe\.length\(\) == 36/);
  assert.match(native,/if \(stage >= 2\)/); // still dismiss error splash
  assert.match(native,/onStartupStage_ \(3\)/); // error stage must not set marker
  assert.match(script, /\$state\.phase='staging-retry'\s*\n\s*\$state\.lastCheckUtc=''/);
  assert.match(script, /\$state\.phase='staged'\s*\n\s*\$state\.lastCheckUtc=\[DateTime\]::UtcNow\.ToString\('o'\)/);
  assert.match(script, /TotalMinutes -lt 20/);
});
