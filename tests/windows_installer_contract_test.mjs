import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const iss=read('installer/ArSonMix.iss');
const ps=read('scripts/build_verify_windows_installer.ps1');
const ci=read('.github/workflows/p1-windows.yml');
const main=read('src/Main.cpp');

test('Inno RC is per-user, 64-bit, safe update and one interactive launch',()=>{
  for(const item of [
    'PrivilegesRequired=lowest','DefaultDirName={localappdata}',
    'ArchitecturesAllowed=x64compatible','UNQUALIFIED',
    'verified-gui.exe','BUILD_PROVENANCE.json',
    'CloseApplications=yes','CloseApplicationsFilter=ArSonMix.exe',
    'RestartApplications=no','UsePreviousAppDir=yes',
    'UsePreviousTasks=yes','DirExistsWarning=no',
    '[Run]','Filename: "{app}\\ArSonMix.exe"',
    'Flags: postinstall nowait skipifsilent runasoriginaluser'
  ]) assert.ok(iss.includes(item),item);
  assert.doesNotMatch(iss,/^CloseApplications=force/m);
  assert.doesNotMatch(iss,/^\[(UninstallDelete|UninstallRun)\]/m);
  assert.doesNotMatch(iss,/^Source:.*portable_mode\.txt/m);
  assert.equal((iss.match(/^Filename: "\{app\}\\ArSonMix\.exe"; Description: "Launch ArSonMix"/gm)||[]).length,1,
    'ONE finish-screen launcher, never a second Restart Manager relaunch');
});

test('JUCE receives graceful app close and stores accepted session',()=>{
  assert.match(main,/void closeButtonPressed\(\) override \{ juce::JUCEApplication::getInstance\(\)->systemRequestedQuit\(\); \}/);
  assert.match(main,/~ConsoleWindow\(\) override[\s\S]*?mixerSessionWorker_\.finishForShutdown\(\);[\s\S]*?saveMixerSession\(resumeMain\)/);
  assert.match(main,/bool moreThanOneInstanceAllowed\(\) override \{ return false; \}/);
});

test('installer uses a SHA-verified FAT PE and tests fresh install + in-place update + uninstall',()=>{
  for(const item of [
    'manifest.sourceCommit','manifest.exeSha256','manifest.fatStatus',
    'assets/icon/arsonmix-app.ico','FactoryFX',
    'Start-Process -FilePath $setup',
    '$upgrade=Start-Process -FilePath $setup',
    '"/CLOSEAPPLICATIONS"','"/NORESTARTAPPLICATIONS"',
    'In-place update changed the verified GUI',
    'Start-Process -FilePath $uninstall','Test-Path -LiteralPath $sentinel',
    'PHYSICAL_FAT_PENDING','INSTALLER_PROVENANCE.json'
  ]) assert.ok(ps.includes(item),item);
  const verifyStep=ci.indexOf('      - name: Independently verify release ZIP and embedded GUI/engine before upload');
  const installerStep=ci.indexOf('      - name: Compile and validate per-user Windows RC installer from exact FAT PE');
  assert.ok(verifyStep>=0 && installerStep>verifyStep);
  assert.match(ci,/ArSonMix-Windows-Installer-UNQUALIFIED/);
});
