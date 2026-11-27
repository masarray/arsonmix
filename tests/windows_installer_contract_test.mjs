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
    'Preview', 'verified-gui.exe', 'BUILD_PROVENANCE.json',
    'THIRD_PARTY_8D_SONKU_HRTF.md'
  ]) assert.ok(iss.includes(item), item);

  assert.doesNotMatch(iss, /^\[UninstallDelete\]/m);
  assert.doesNotMatch(iss, /^Source:.*portable_mode\.txt/m);
  assert.doesNotMatch(iss, /taskkill|Stop-Process|TerminateProcess|CloseApplications=force/i);
  assert.match(iss, /Name: "\{autoprograms\}\\ArSonMix"/);
  assert.match(iss, /FactoryFX\\\*.*skipifsourcedoesntexist/);
  assert.match(ps, /verify_embedded_legal\.py --binary \$installedExe/);
  assert.match(ps, /Installer left a redundant top-level legal document/);
  assert.match(iss, /Source: "\.\.\\docs\\THIRD_PARTY_8D_SONKU_HRTF\.md"; Flags: dontcopy/);
});

test('interactive Setup cannot be held by optional signed VB-CABLE, which remains an opt-in action', () => {
  const m = JSON.parse(read('config/vbcable-official-package.json'));
  const pack = read('scripts/prepare_vbcable_package.ps1');
  const bootstrap = read('installer/ArSonMixVBCableInstall.ps1');
  const notice = read('installer/VB-CABLE-NOTICE.txt');
  assert.equal(m.filename, 'VBCABLE_Driver_Pack45.zip');
  assert.match(m.url, /^https:\/\/download\.vb-audio\.com\/Download_CABLE\//);
  assert.match(m.sha256, /^[a-f0-9]{64}$/);
  assert.match(m.proof, /actions\/runs\/43079930924/);
  assert.match(iss, /^PrivilegesRequired=lowest$/m);
  assert.doesNotMatch(iss, /^\[Tasks\]$/m);
  assert.match(iss, /VBCABLE_Driver_Pack45\.zip/);
  const run = iss.slice(iss.indexOf('[Run]'), iss.indexOf('[Code]'));
  const entries = run.split('\n').filter(line => line.startsWith('Filename:'));
  assert.equal(entries.length, 2);
  assert.match(entries[0], /ArSonMixWebView2Prerequisite\.ps1/);
  assert.match(entries[0], /Flags: runasoriginaluser skipifsilent waituntilterminated runhidden/);
  assert.match(entries[1], /^Filename: "\{app\}\\ArSonMix\.exe"/);
  assert.doesNotMatch(run, /ArSonMixVBCableInstall\.ps1/);
  assert.match(iss, /Install optional VB-CABLE \(Administrator approval required\)/);
  assert.match(iss, /-File ""\{app\}\\ThirdParty\\VB-Audio\\ArSonMixVBCableInstall\.ps1""/);
  for (const entry of entries) assert.doesNotMatch(entry, /;\s*(Check|Tasks):/i);
  assert.match(iss, /^RestartIfNeededByRun=no$/m);
  assert.doesNotMatch(iss + bootstrap, /VBCableRestart|RestartMarker|CanLaunchMixer|function NeedRestart/);
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
  assert.match(bootstrap, /Get-PnpDevice -ErrorAction Stop/);
  assert.match(bootstrap, /Get-CimInstance Win32_OperatingSystem/);
  assert.match(bootstrap, /previousState -eq 'PENDING_REBOOT'/);
  assert.match(bootstrap, /previous\.PSObject\.Properties\['bootUtc'\]/);
  assert.match(bootstrap, /PENDING_REBOOT.*Restart Windows|Record "PENDING_REBOOT"/);
  assert.match(bootstrap, /Record "REPAIR_REQUIRED"/);
  // Interrupted vendor/UAC attempts must not be treated as fresh installs;
  // rerunning the vendor GUI can switch into destructive REMOVE mode.
  assert.match(bootstrap, /previousState -notin/);
  assert.match(bootstrap, /\$driverAttemptUncertain\)\{/);
  assert.match(bootstrap, /\$vendorHandoffAttempted=\$true/);
  assert.match(bootstrap, /if\(\$vendorHandoffAttempted\)/);
  assert.match(bootstrap, /Vendor handoff\/install result uncertain/);
  assert.ok(bootstrap.indexOf('Record "AWAITING_UAC"') <
            bootstrap.indexOf('$vendorHandoffAttempted=$true') &&
            bootstrap.indexOf('$vendorHandoffAttempted=$true') <
            bootstrap.indexOf('Start-Process -FilePath $setups[0].FullName'),
            'vendor handoff must become fail-closed before invoking ShellExecute');
  assert.match(isolatedSmoke, /interrupted-uac-missing/);
  assert.match(isolatedSmoke, /interrupted-uac-present/);
  assert.match(isolatedSmoke, /corrupt-status-but-healthy/);
  assert.match(isolatedSmoke, /cable-vendor-handoff/);
  assert.match(isolatedSmoke, /ArSonMixTestVendorHandoffCount -eq 1/);
  assert.match(bootstrap, /if\(\$all.Count -gt 0\)\{[\s\S]*?\$present \| Where-Object[\s\S]*?Record "ALREADY_PRESENT"[\s\S]*?exit 0/);
  assert.ok(bootstrap.indexOf("previousState -eq 'PENDING_REBOOT'") <
            bootstrap.indexOf('Start-Process -FilePath $setups[0].FullName'),
            'pending reboot must be checked before launching a setup that can remove installed cable');
  assert.match(bootstrap, /-Verb RunAs/);
  assert.match(bootstrap, /WaitForExit\(180000\)/);
  assert.match(bootstrap, /\$vendorStillRunning/);
  assert.doesNotMatch(bootstrap, /-Verb RunAs -Wait/);
  assert.match(bootstrap, /PENDING_REBOOT/);
  assert.doesNotMatch(bootstrap + iss, /certutil\s+-addstore|Add-TrustedPublisher\s*\(|SetDefaultEndpoint\s*\(|pnputil\s+\/delete-driver|Uninstall-PnpDevice\s|LocalMachine\\TrustedPublisher/i);
  assert.match(notice, /donationware/i);
  assert.match(notice, /professional/i);
  assert.match(notice, /www\.vb-cable\.com/);
  assert.match(notice, /A\+B and C\+D/);
  assert.match(iss, /Flags: runasoriginaluser skipifsilent waituntilterminated/);
});

test('WebView2 GUI prerequisite runs only on interactive setup from Microsoft-signed bootstrapper', () => {
  const helper=read('installer/ArSonMixWebView2Prerequisite.ps1');
  assert.match(iss, /Source: "ArSonMixWebView2Prerequisite\.ps1"; DestDir: "\{app\}"/);
  assert.match(helper, /ARSONMIX_WEBVIEW2_PREREQUISITE_POLICY_PASS/);
  assert.match(helper, /go\.microsoft\.com\/fwlink\/p\/\?LinkId=2124703/);
  assert.match(helper, /Get-AuthenticodeSignature/);
  assert.match(helper, /Microsoft-Signer/);
  assert.match(helper, /Microsoft-Uri \$response\.ResponseUri/);
  assert.match(helper, /-ArgumentList @\('\/silent','\/install'\)/);
  assert.match(helper, /WaitForExit\(120000\)/);
  assert.match(helper, /\$bootstrapperStillRunning/);
  assert.doesNotMatch(helper, /-ArgumentList @\('\/silent','\/install'\) -Wait/);
  assert.match(helper, /ARSONMIX_WEBVIEW2_ALREADY_PRESENT/);
  assert.match(helper, /ARSONMIX_WEBVIEW2_INSTALLED/);
  assert.match(helper, /NEEDS_ATTENTION/);
  assert.ok(helper.indexOf('if($installed)') < helper.indexOf('$request.GetResponse()'));
  assert.ok(helper.indexOf('Get-AuthenticodeSignature') < helper.indexOf('Start-Process -FilePath $file'));
  assert.doesNotMatch(helper, /vcredist|vc_redist|TrustedPublisher|SetDefaultEndpoint|--safe-webview-profile/i);
  assert.match(read('scripts/test_inno_graceful_update_windows.ps1'), /ARSONMIX_WEBVIEW2_PREREQUISITE_POLICY_PASS/);
});

test('Start Menu diagnostics distinguish host loader, vendor VST3, WebView2 and unsupported OS without mutation', () => {
  const helper=read('installer/ArSonMixStartupDiagnostics.ps1');
  assert.match(iss, /Diagnose ArSonMix Startup/);
  assert.match(helper, /ARSONMIX_STARTUP_DIAGNOSTIC_POLICY_PASS/);
  for (const cls of [
    'UNSUPPORTED_WINDOWS', 'HOST_EXE_MISSING', 'VC_RUNTIME_RELATED_ERROR',
    'THIRD_PARTY_PLUGIN_CRASH_SUSPECTED', 'EARLY_DLL_LOADER_FAILURE_SUSPECTED',
    'WEBVIEW2_OR_PROFILE_STARTUP_FAILURE', 'WEBVIEW2_RUNTIME_NOT_VERIFIED',
    'WINDOWS_APP_FAILURE_NEEDS_FAULT_MODULE', 'NO_FATAL_STARTUP_CAUSE_IDENTIFIED'
  ]) assert.ok(helper.includes(cls), cls);
  assert.match(helper, /Faulting module name: VCRUNTIME140_1\.dll/);
  assert.match(helper, /0xc0000135/);
  assert.match(helper, /msedgewebview2\.exe/);
  assert.match(helper, /-MaxEvents 1200/);
  assert.match(helper, /STARTUP_DIAGNOSIS:/);
  assert.match(helper, /RECOMMENDED_ACTION:/);
  assert.match(helper, /No diagnostics are uploaded/);
  assert.match(helper, /if \(\$PolicySelfTest\)/);
  assert.match(isolatedSmoke, /ARSONMIX_STARTUP_DIAGNOSTIC_POLICY_PASS/);
  assert.doesNotMatch(helper, /Invoke-WebRequest|Invoke-RestMethod|Start-BitsTransfer|Remove-Item|Stop-Process|taskkill|SetDefaultEndpoint|certutil/i);
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
  assert.match(native, /GUI_READY_NO_AUDIO/);
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
  assert.match(updater, /function Test-InstallableAssetPair\(/);
  assert.match(updater, /Test-StagedAsset \$newPath \$newAsset/);
  assert.match(updater, /Test-StagedAsset \$oldPath \$oldAsset/);
  assert.match(updater, /Test-InstallableAssetPair \$newPath \$state\.newAsset \$oldPath \$state\.previousAsset/);
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
    'TotalMinutes -lt $attemptCooldown', 'TotalHours -lt 6'
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
    '    if($null -ne $rollbackGuard){$rollbackGuard.Dispose()}',
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
  assert.match(script, /GUI_READY_NO_AUDIO' -and\s*\n\s*\(Test-InstalledRelease \$expectedSha\)/);
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

test('updater paginates published releases, matching exact legacy source across pages', () => {
  const script=read('installer/ArSonMixAutoUpdate.ps1');
  assert.match(script, /function Read-PublishedReleases\(\[hashtable\]\$headers\)/);
  assert.match(script, /for\(\[int\]\$page=1; \$page -le 10; \$page\+\+\)/);
  assert.match(script, /\$uri=if\(\$page -eq 1\)\{\$api\}else\{\$api\+'&page='\+\$page\}/);
  assert.match(script, /\$batch=Invoke-RestMethod -Uri \$uri/);
  assert.match(script, /\$rows=@\(\$batch\)/);
  assert.equal(script.split('$releases=Read-PublishedReleases $headers').length - 1, 2);
  assert.match(script, /\$liveReleases=Read-PublishedReleases \$headers/);
  assert.match(script, /GitHub release history exceeds 1000 entries/);
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
  assert.match(script, /TotalMinutes -lt \$attemptCooldown/);
});


test('acknowledged one-click upgrade is deferred until the exact candidate is approved', () => {
  const updater = read('installer/ArSonMixAutoUpdate.ps1');
  const native = read('src/Main.cpp');
  assert.match(updater, /\$approvalPath=Join-Path \$root 'approved\.json'/);
  assert.match(updater, /UPDATE_WAITING_FOR_USER_OK/);
  assert.match(updater, /function Test-UserApproval\(/);
  assert.match(updater, /ARSONMIX_UPDATER_USER_ACK_POLICY_PASS/);
  assert.match(updater, /if\(-not \(Test-UserApproval \$approval \$state\)\)/);
  assert.match(updater, /\$state\.operation -ceq 'upgrade'/);
  for (const name of ['candidateSha','candidateTag','previousSha'])
    assert.ok(updater.includes('[string]$approval.'+name+' -ceq [string]$transaction.'+name));
  assert.ok(updater.indexOf('UPDATE_WAITING_FOR_USER_OK') < updater.indexOf("    $state.phase='installing'"),
    'consent must gate the installer before any replacement');
  assert.match(native, /void serviceSmartUpdateAcknowledgement\(bool explicitReview=false\)/);
  assert.match(native, /if \(\+\+updatePromptTicks_ % 40 == 0\)/);
  assert.match(native, /class ArSonMixUpdateNotice final : public juce::DocumentWindow/);
  assert.match(native, /ArSonNoticeButton ok \{"OK",true\}/);
  assert.match(native, /class ArSonNoticeButton final : public juce::TextButton/);
  assert.match(native, /void closeButtonPressed\(\) override \{ setVisible \(false\); \}/);
  assert.match(native, /std::make_unique<ArSonMixUpdateNotice>/);
  assert.doesNotMatch(native, /juce::NativeMessageBox::showAsync \(options/);
  assert.match(native, /approved\.json/);
  assert.match(native, /lastUpdatePromptedSha_ == candidate/);
  assert.doesNotMatch(updater, /(?:Stop-Process|taskkill|TerminateProcess)/);
});

test('GUI-only updater probe does not strand pre-window WebView2 startup failures', () => {
  const native = read('src/Main.cpp');
  const app=native.indexOf('class ConsoleApp final');
  const init=native.indexOf('void initialise (const juce::String& commandLine) override',app);
  const shutdown=native.indexOf('void shutdown() override',init);
  assert.ok(app>=0&&init>app&&shutdown>init);
  const startup=native.slice(init,shutdown);
  const failures = [
    'Cannot create the browser-cache folder:',
    'The native browser backend could not be initialized on this machine.'
  ];
  for (const message of failures) {
    const failureAt = startup.indexOf(message);
    assert.ok(failureAt > 0, message);
    const preceding = startup.slice(Math.max(0, failureAt - 285), failureAt);
    assert.match(preceding, /if \(isAutoUpdateHealthProbe\(\)\)\s*\{\s*systemRequestedQuit\(\);\s*return;\s*\}/);
  }
});

test('silent post-install GUI probe cannot automatically arm ASIO or save a user session', () => {
  const main = read('src/Main.cpp');
  const updater = read('installer/ArSonMixAutoUpdate.ps1');
  assert.match(main, /static bool isAutoUpdateHealthProbe\(\)/);
  assert.match(main, /if \(updaterProbeMode_ && audioEngineStarted_\)\s*return;/);
  const guard=main.indexOf('diagnostics_.record ("UPDATER_GUI_PROBE"');
  const engine=main.indexOf('startupError_ = engine_.start (saved.get());', guard);
  assert.ok(guard >= 0 && engine > guard, 'GUI-only return must precede audio device startup');
  assert.match(main.slice(guard,engine), /return;\s*\}\s*auto saved/);
  assert.match(main, /if \(!updaterProbeMode_ &&\s*\(static_cast<bool>\(lab\.getProperty/);
  assert.match(main, /if \(!isAutoUpdateHealthProbe\(\)\)\s*launchSmartUpdater \(false\)/);
  assert.match(main, /if \(hadMixerWindow && !recoveryOnly_ && !innoIsClosing && !isAutoUpdateHealthProbe\(\) &&/);
  assert.match(main, /updateProbeCompleteTick_ >= 3500u/);
  assert.match(updater, /GUI_READY_NO_AUDIO/);
  assert.match(updater, /PREVIOUS_EXE_RETAINED_FOR_NEXT_MANUAL_LAUNCH/);
  // Explicit manual Repair mode may still relaunch on request.
  const afterApply = updater.slice(updater.indexOf('# Post-close supervisor'));
  assert.doesNotMatch(afterApply, /\[Diagnostics\.Process\]::Start\(\$exe\)/);
});

test('updater notice is nonmodal, dismissal cannot approve and shutdown cleans up', () => {
  const s=read('src/Main.cpp');
  const notice=s.slice(s.indexOf('class ArSonMixUpdateNotice final'),s.indexOf('class ConsoleApp final'));
  assert.match(notice, /juce::DocumentWindow/);
  assert.match(notice, /panel_\.ok\.onClick = \[this\]/);
  assert.match(notice, /if \(onAccept_ && onAccept_\(\)\)/);
  assert.match(notice, /panel_\.ok\.setButtonText \("Retry"\)/);
  assert.match(s, /return approvalFile\.replaceWithText \(/);
  assert.match(notice, /void closeButtonPressed\(\) override \{ setVisible \(false\); \}/);
  assert.doesNotMatch(notice, /enterModalState|runModalLoop|NativeMessageBox/);
  assert.match(s, /updateNotice_\.reset\(\);\s*#endif\s*window_\.reset\(\)/);
});

test('updater rechecks both verified installer binaries before execution and rollback', () => {
  const script=read('installer/ArSonMixAutoUpdate.ps1');
  assert.match(script, /function Test-InstallableAssetPair\(/);
  assert.match(script, /ARSONMIX_UPDATER_LAST_MILE_PAIR_PASS/);
  const lastCheck=script.indexOf('# Reverify after GitHub re-query');
  const transition=script.indexOf("$state.phase='installing'",lastCheck);
  const execution=script.indexOf('$upgradeOK=Run-Installer $newPath',transition);
  assert.ok(lastCheck>=0 && transition>lastCheck && execution>transition);
  assert.match(script.slice(lastCheck,transition), /Test-InstallableAssetPair \$newPath \$state.newAsset \$oldPath \$state.previousAsset/);
  assert.match(script, /if\(-not \(Test-StagedAsset \$oldPath \$oldAsset\)\)/);
  assert.match(script, /INSTALLER_BYTES_CHANGED_UPDATE_DEFERRED/);
});


test('P0 Windows 10 fresh install needs no VC redistributable and reports missing WebView2',()=>{
  const cmake=read('CMakeLists.txt'),native=read('src/Main.cpp');
  assert.match(cmake,/if \(MSVC AND WIN32 AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY\)/);
  assert.match(cmake,/set\(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded\$<\$<CONFIG:Debug>:Debug>"\)/);
  assert.ok(cmake.indexOf('set(CMAKE_MSVC_RUNTIME_LIBRARY')<
    cmake.indexOf('add_subdirectory("${JUCE_ROOT}"'),
    'static CRT must apply to JUCE and app as one ABI');
  const emergency=native.slice(native.indexOf('class ArSonRecoveryNotice final'),native.indexOf('class ConsoleWindow final'));
  assert.match(emergency, /"Repair WebView2","Close",\[\]\(bool accepted\)/);
  assert.match(emergency, /if\(accepted\)juce::URL\("https:\/\/developer\.microsoft\.com\/en-us\/microsoft-edge\/webview2\/"\)\s*\.launchInDefaultBrowser\(\)/);
  assert.match(emergency, /panel_\.primary\.onClick=\[this\]\{finish\(true\);\}/);
  assert.match(emergency, /panel_\.cancel\.onClick=\[this\]\{finish\(false\);\}/);
  assert.match(emergency, /void closeButtonPressed\(\) override \{finish\(false\);\}/);
  assert.doesNotMatch(emergency.slice(0,emergency.indexOf('void showWebViewStartupError')), /NativeMessageBox|runModalLoop|enterModalState/);
  assert.match(iss,/^RestartIfNeededByRun=no$/m);
  const packaged=read('scripts/build_verify_windows_installer.ps1');
  const pe=read('scripts/verify_win10_pe_runtime.py');
  assert.match(packaged,/verify_win10_pe_runtime\.py --exe \$exe/);
  assert.match(pe,/WIN10_STATIC_CRT_PE_PASS/);
  assert.match(pe,/delay-import descriptors|delay-import|Delay-load descriptors/i);
  assert.match(pe,/vcruntime/);
  assert.match(pe,/msvcp/);
  assert.match(pe,/WEBVIEW2_RUNTIME_REQUIRED_SEPARATELY_NOT_PROVEN/);
});

test('ZBSH08 all installer execution requires independent cryptographic identity',()=>{
  const source=read('installer/ArSonMixAutoUpdate.ps1');
  const run=source.slice(source.indexOf('function Run-Installer('),source.indexOf('function Launch-WithHealth('));
  assert.match(run,/Assert-InstallerAttestation \$file \$tag \$sha/);
  assert.ok(run.indexOf('Assert-InstallerAttestation')<run.indexOf('[Diagnostics.Process]::Start'));
  assert.match(run,/\[IO.FileShare\]::Read/);
  assert.match(source,/--signer-workflow masarray\/arsonmix\/\.github\/workflows\/alpha-preview-publish\.yml/);
  assert.match(source,/--source-digest/);
  assert.match(source,/--signer-digest/);
  assert.match(source,/--deny-self-hosted-runners/);
  assert.match(source,/Test-ReleaseTransition \$state.operation \$baselineRelease \$candidateRelease/);
  assert.match(source,/ARSONMIX_UPDATER_ATTESTATION_POLICY_PASS/);
  assert.doesNotMatch(source,/AllowAutoRedirect=\$true/);
  assert.match(source,/Assert-InstallerAttestation \$oldPath/);
});


test("cold-start mixer archive requires confirmation and bypasses normal startup and updater",()=>{
  const main=read("src/Main.cpp"),store=read("src/NativeMixerSessionStore.h"),iss=read("installer/ArSonMix.iss");
  const at=main.indexOf('if(commandLine.trim()=="--archive-mixer-session" && !isAutoUpdateHealthProbe())');
  const end=main.indexOf('// Paint the approved dark native splash',at);
  assert.ok(at>0&&end>at);
  const recovery=main.slice(at,end);
  assert.match(recovery,/recoveryOnly_=true/);
  assert.match(recovery,/"Archive session","Cancel",\[this\]\(bool accepted\)/);
  assert.match(recovery,/if\(!accepted\)\{systemRequestedQuit\(\);return;\}/);
  assert.ok(recovery.indexOf('if(!accepted)') < recovery.indexOf('quarantineForStartupRecovery(archived)'));
  assert.match(recovery,/quarantineForStartupRecovery\(archived\)/);
  assert.match(recovery,/\}\);\s*return;\s*\}/);
  assert.doesNotMatch(recovery,/engine_\.start|startTimer|make_unique<ConsoleWindow>|launchSmartUpdater/);
  const method=store.slice(store.indexOf("juce::String quarantineForStartupRecovery"),store.indexOf("static constexpr juce::int64 maxJson"));
  assert.match(method,/root_\.getSiblingFile\("MixerSession-Recovery-"\+id\)/);
  assert.match(method,/!root_\.moveFileTo\(target\)/);
  assert.doesNotMatch(method,/deleteRecursively|deleteFile|commit\(/);
  assert.match(iss,/Parameters: "--archive-mixer-session"/);
});

test('Autonomous updater bundles a pinned verifier and never uses user GitHub credentials', () => {
  const u=read('installer/ArSonMixAutoUpdate.ps1');
  const iss=read('installer/ArSonMix.iss');
  const build=read('scripts/build_verify_windows_installer.ps1');
  const intake=read('scripts/prepare_update_verifier.ps1');
  const pin=JSON.parse(read('config/update-verifier-package.json'));
  assert.equal(pin.version,'2.106.0');
  assert.equal(pin.sha256,'d406f442ca0baa3d3e9c695e9cded1aa48e67dad3294b8947e5ab84f980b4844');
  assert.ok(u.includes(pin.sha256));
  assert.ok(u.includes(pin.exeSha256));
  assert.match(intake,/test_public_update_attestation\.ps1/);
  assert.match(read('scripts/test_public_update_attestation.ps1'),/ARSONMIX_ANONYMOUS_REAL_ATTESTATION_PASS/);
  assert.match(u,/function Read-PublicAttestation/);
  assert.match(u,/sourceRepositoryIdentifier/);
  assert.match(u,/--custom-trusted-root/);
  assert.match(u,/GH_CONFIG_DIR/);
  assert.match(u,/EnvironmentVariables\.Remove\(\$key\)/);
  assert.doesNotMatch(u,/ProgramFiles|Get-Command gh|Trusted GitHub CLI missing/);
  assert.match(u,/ARSONMIX_UPDATER_AUTONOMOUS_POLICY_PASS/);
  assert.match(iss,/UpdateVerifier\\gh\.exe/);
  assert.match(iss,/UpdateVerifier\\THIRD-PARTY-LICENSES\.txt/);
  assert.match(build,/prepare_update_verifier\.ps1/);
  assert.match(intake,/Get-FileHash/);
  assert.match(intake,/\.ExtractToFile|::ExtractToFile/);
});

test('Update Center is a bounded message-thread presentation and scheduler, never audio intent',()=>{
  const native=read('src/Main.cpp'), bridge=read('web/approved-native-bridge.js'), u=read('installer/ArSonMixAutoUpdate.ps1');
  assert.match(native,/withEventListener \("arMixUpdateCenterIntent"/);
  assert.match(native,/lastSmartUpdateCheckTick_ >= 6u \* 60u \* 60u \* 1000u/);
  assert.match(native,/launchSmartUpdater\(false,true\)/);
  assert.match(native,/serviceSmartUpdateAcknowledgement\(true\)/);
  assert.match(native,/!explicitReview && lastUpdatePromptedSha_ == candidate/);
  assert.match(native,/command\.add \("-CheckNow"\)/);
  assert.match(u,/\[switch\]\$CheckNow/);
  assert.match(u,/if\(\$CheckNow -and \(\$ApplyStaged -or \$RecoveryMode -or \$PolicySelfTest\)\)/);
  assert.match(u,/\$attemptCooldown=if\(\$CheckNow\)\{1\}else\{20\}/);
  assert.match(u,/if\(-not \$CheckNow -and \$imageStatus/);
  const center=bridge.slice(bridge.indexOf('function installUpdateCenter('),bridge.indexOf('function installFirstSoundMenu('));
  assert.match(center,/dialog\.show\(\)/);
  assert.doesNotMatch(center,/showModal|arMixAction|arMixConsoleIntent|fetch\(/);
  assert.match(center,/window\.clearInterval\(poll\)/);
  assert.match(center,/state\?\.requestId!==latest/);
});

test('Resume uses digest-scoped partials, exact HTTP range, and full hash before promotion',()=>{
  const u=read('installer/ArSonMixAutoUpdate.ps1');
  assert.match(u,/function Get-ResumeResponseMode/);
  assert.match(u,/Get-PartialAssetPath \$destination \$expectedHash/);
  assert.match(u,/\$request\.AddRange\(\$offset\)/);
  assert.match(u,/Content-Range/);
  assert.match(u,/if\(\$mode -ceq 'restart'\)/);
  assert.match(u,/if\(\$hash -cne \$expectedHash\)\{\$discard=\$true;Fail/);
  assert.match(u,/ARSONMIX_UPDATER_RESUME_POLICY_PASS/);
});
test('Rollback survives verifier replacement and offline network only under same-process verified lease',()=>{
  const u=read('installer/ArSonMixAutoUpdate.ps1');
  const at=u.indexOf('$rollbackGuard=[IO.File]::Open($oldPath');
  const sign=u.indexOf('Assert-InstallerAttestation $oldPath',at);
  const proof=u.indexOf('$rollbackProof=[pscustomobject]',sign);
  const install=u.indexOf('$upgradeOK=Run-Installer $newPath',proof);
  assert.ok(at>0&&sign>at&&proof>sign&&install>proof);
  assert.match(u,/Run-Installer \$oldPath .* -UseVerifiedRollback/);
  assert.match(u,/Test-RollbackProof \$rollbackProof \$rollbackGuard/);
  assert.match(u,/ARSONMIX_UPDATER_OFFLINE_ROLLBACK_LEASE_PASS/);
  assert.match(u,/if\(\$null -ne \$rollbackGuard\)\{\$rollbackGuard\.Dispose\(\)\}/);
});

test('Portable release handoff downloads an attested ZIP but cannot apply or modify Studio',()=>{
  const u=read('installer/ArSonMixAutoUpdate.ps1'),n=read('src/Main.cpp'),bridge=read('web/approved-native-bridge.js');
  const work=u.slice(u.indexOf('function Invoke-PortableUpdateCheck'),u.indexOf('function Test-StagedAsset'));
  assert.match(work,/Download-PortableVerified/);
  assert.doesNotMatch(work,/Run-Installer|Expand-Archive|Start-Process|Remove-Item|Studio/);
  assert.match(u,/if\(\$portable -and \(\$ApplyStaged -or \$RecoveryMode\)\)\{exit 0\}/);
  assert.match(u,/Assert-InstallerAttestation \$destination \$tag \$sha -PortableZip/);
  assert.match(u,/ARSONMIX_PORTABLE_DOWNLOAD_POLICY_PASS/);
  assert.match(n,/if \(portable && applyStaged\) return false/);
  assert.match(bridge,/Portable download only/);
  assert.match(read('.github/workflows/alpha-preview-publish.yml'),/-IncludePortableUpdater/);
  assert.match(read('scripts/build_verify_windows_installer.ps1'),/PORTABLE_RELEASE_UPDATE_DOWNLOADER_STAGED/);
});
