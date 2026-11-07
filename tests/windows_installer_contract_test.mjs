import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const read = path => readFileSync(path, 'utf8');
const iss = read('installer/ArSonMix.iss');
const ps = read('scripts/build_verify_windows_installer.ps1');
const fixture = read('tests/fixtures/installer_graceful_close.cs');
const ci = read('.github/workflows/p1-windows.yml');

test('per-user Inno Setup upgrades the same app and retains private Studio data', () => {
  for (const item of [
    'AppId=ArSonMix.LiveStudio', 'PrivilegesRequired=lowest',
    'DefaultDirName={localappdata}', 'UsePreviousAppDir=yes',
    'ArchitecturesAllowed=x64compatible', 'MinVersion=10.0.17763',
    'DisableWelcomePage=yes', 'DisableDirPage=yes',
    'DisableReadyPage=yes', 'DisableFinishedPage=yes',
    'SetupMutex=ArSonMix.PerUserInstaller.SingleInstance',
    'UNQUALIFIED', 'verified-gui.exe', 'BUILD_PROVENANCE.json'
  ]) assert.ok(iss.includes(item), item);

  assert.doesNotMatch(iss, /^\[UninstallDelete\]/m);
  assert.doesNotMatch(iss, /^Source:.*portable_mode\.txt/m);
  assert.doesNotMatch(iss, /taskkill|Stop-Process|TerminateProcess|CloseApplications=force/i);
  assert.match(iss, /Name: "\{autoprograms\}\\ArSonMix"/);
  assert.match(iss, /FactoryFX\\\*.*skipifsourcedoesntexist/);
});

test('Restart Manager requests graceful shutdown, not data-loss force-close', () => {
  assert.match(iss, /^CloseApplications=yes$/m);
  assert.match(iss, /^CloseApplicationsFilter=ArSonMix\.exe$/m);
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
    'assets/icon/arsonmix-app.ico', 'FactoryFX',
    'Start-Process -FilePath $setup', 'Start-Process -FilePath $installedExe',
    'installer_graceful_close.cs', 'WM_CLOSE_SAVED', 'WINDOW_READY',
    '/CLOSEAPPLICATIONS', '/NOFORCECLOSEAPPLICATIONS',
    'INNO_GRACEFUL_UPDATE_PASS', 'Test-Path -LiteralPath $installSentinel',
    'Test-Path -LiteralPath $sentinel', 'INSTALLER_PROVENANCE.json',
    'PHYSICAL_FAT_PENDING'
  ]) assert.ok((ps + fixture).includes(item), item);
  assert.equal((ps.match(/Start-Process -FilePath \$setup/g) ?? []).length, 2);
  assert.match(fixture, /FormClosing/);
  assert.match(fixture, /Application\.Run\(window\)/);
  // No fixture is packaged: only the verified native EXE ships.
  assert.doesNotMatch(iss, /installer_graceful_close|\.cs"/);
});

test('installer packaging is gated by the exact SHA FAT verifier', () => {
  const verify = ci.indexOf('      - name: Independently verify release ZIP and embedded GUI/engine before upload');
  const installer = ci.indexOf('      - name: Compile and validate per-user Windows RC installer from exact FAT PE');
  assert.ok(verify >= 0 && installer > verify);
  assert.match(ci, /ArSonMix-Windows-Installer-UNQUALIFIED/);
  assert.match(ci, /tests\/windows_installer_contract_test\.mjs/);
});
