import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const iss=read('installer/ArSonMix.iss');
const ps=read('scripts/build_verify_windows_installer.ps1');
const ci=read('.github/workflows/p1-windows.yml');
test('Inno RC is per-user, 64-bit, no automatic activation or Studio deletion',()=>{
  for(const item of ['PrivilegesRequired=lowest','DefaultDirName={localappdata}',
    'ArchitecturesAllowed=x64compatible','CloseApplications=no','RestartApplications=no',
    'UNQUALIFIED','verified-gui.exe','BUILD_PROVENANCE.json']) assert.ok(iss.includes(item),item);
  assert.doesNotMatch(iss,/^\[(Run|UninstallDelete)\]/m);
  assert.doesNotMatch(iss,/^Source:.*portable_mode\.txt/m);
});
test('installer only derives from verified FAT PE and tests uninstall safely',()=>{
  for(const item of ['manifest.sourceCommit','manifest.exeSha256','manifest.fatStatus',
    'assets/icon/arsonmix-app.ico','FactoryFX','Start-Process -FilePath $setup',
    'Start-Process -FilePath $uninstall','Test-Path -LiteralPath $sentinel',
    'PHYSICAL_FAT_PENDING','INSTALLER_PROVENANCE.json'])assert.ok(ps.includes(item),item);
  const verifyStep=ci.indexOf('      - name: Independently verify release ZIP and embedded GUI/engine before upload');
  const installerStep=ci.indexOf('      - name: Compile and validate per-user Windows RC installer from exact FAT PE');
  assert.ok(verifyStep>=0 && installerStep>verifyStep);
  assert.match(ci,/ArSonMix-Windows-Installer-UNQUALIFIED/);
});
