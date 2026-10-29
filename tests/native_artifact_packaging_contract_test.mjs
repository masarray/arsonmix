// SPDX-License-Identifier: AGPL-3.0-only
// Prevent packaging a native test executable as the GUI application again.
// Exact PE and source SHA verification happens at package time on Windows.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read = path => readFileSync(path, 'utf8');
const workflow = read('.github/workflows/p1-windows.yml');
const packaging = read('scripts/package_native_windows.ps1');
const verify = read('scripts/verify_native_windows_artifact.ps1');
const nativeMain = read('src/Main.cpp');
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
test('production Windows workflow rejects a tiny 648KB zip, test EXE, or missing GUI/engine runtime data', () => {
  assert.match(packaging, /minimumGuiExeBytes = 4MB/);
  assert.match(verify, /\$bytes\.Length -lt 4MB/);
  assert.match(verify, /\$archiveBytes -lt 2MB/);
  assert.match(verify, /Compress-Archive/);
  assert.match(verify, /ZipFile\]::OpenRead/);
  assert.match(verify, /ComputeHash\(\$stream\)/);
  assert.match(verify, /manifest\.sourceCommit/);
  assert.match(verify, /manifest\.appJsSha256/);
  assert.match(verify, /manifest\.stylesCssSha256/);
  for (const marker of [
    'function openChannelIdentity',
    'class="master-analysis"',
    '--ari-master-cap',
    'function syncNativeMixerControls',
    'AUDIO_GLITCH_WINDOW',
  ]) {
    assert.ok(verify.includes(marker), 'release audit must search compiled artifact for '+marker);
  }
  assert.match(nativeMain, /BinaryData::app_js/);
  assert.match(nativeMain, /BinaryData::styles_css/);
  assert.match(nativeMain, /withResourceProvider \(assetFor\)/);
  assert.match(workflow, /Independently verify release ZIP and embedded GUI\/engine before upload/);
  assert.match(workflow, /scripts\/verify_native_windows_artifact\.ps1/);
  assert.ok(workflow.indexOf('scripts/verify_native_windows_artifact.ps1') <
            workflow.indexOf('uses: actions/upload-artifact@v4'));
});
test('physical test protocol differentiates actual native channel 1 and GUI-only channels', () => {
  assert.match(fat, /native engine implements \*\*Channel 1\*\* only/i);
  assert.match(fat, /OUTPUT_ONLY_TONE/);
  assert.match(fat, /CAPTURE_ONLY/);
  assert.match(fat, /DRY_THROUGH/);
  assert.match(fat, /44,100 Hz/);
  assert.match(fat, /48,000 Hz/);
  assert.match(fat, /RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE/);
});

test('release proves exact calibrated GUI bytes are embedded in the executable, not just copied beside it', () => {
  for (const field of ['indexHtmlSha256','appJsSha256','stylesCssSha256','bridgeJsSha256','monitorJsSha256','guiCalibration']) {
    assert.ok(packaging.includes(field+' = '), 'provenance missing '+field);
    assert.ok(verify.includes('manifest.'+field), 'verifier missing '+field);
  }
  for (const file of ['web/index.html','web/app.js','web/styles.css','web/bridge.js','web/monitor.js']) {
    assert.ok(verify.includes('Path="'+file+'"'), 'native resource not checked: '+file);
  }
  assert.match(verify,/IndexOf\(\$sourceText, \[StringComparison\]::Ordinal\)/);
  assert.match(verify,/STALE NATIVE GUI: exact JUCE BinaryData resource absent from EXE/);
  for (const marker of ['gain-scale-travel','vu-scale-travel','meter-signal-area','fader-travel']) {
    assert.ok(verify.includes('"'+marker+'"'), 'calibration marker absent: '+marker);
  }
  assert.match(packaging,/GAIN_10_TO_MINUS60_BLACK_DETENT_AT_38PX__VU_DBFS_INDEPENDENT_TICKS/);
});

test('latest BTN-180 GUI palette is an attested embedded native release requirement', () => {
  const current=read('web/styles-src/20-button-180.css');
  const integrated=read('web/styles.css');
  for(const token of ['--b180-on-mute','--b180-on-solo','--b180-on-stereo','--b180-on-green']) {
    assert.ok(current.includes(token) && integrated.includes(token),token);
    assert.ok(verify.includes('"'+token+'"'), 'native verified executable missing acceptance marker '+token);
  }
  assert.match(verify,/inset 5px 5px 2px -1px/);
  assert.match(cmake,/web\/index\.html web\/styles\.css web\/plugin-ui\.css web\/app\.js web\/plugin-ui\.js web\/bridge\.js web\/monitor\.js/);
  assert.match(nativeMain,/BinaryData::index_html/);
  assert.match(nativeMain,/BinaryData::bridge_js/);
  assert.match(nativeMain,/BinaryData::pluginui_js/);
  assert.match(nativeMain,/BinaryData::pluginui_css/);
});

test('native startup requires static WebView2 loader and JS-backed UI readiness', () => {
  const monitor=read('web/monitor.js');
  assert.match(cmake,/JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1/);
  assert.match(cmake,/NEEDS_WEBVIEW2 TRUE/);
  assert.match(nativeMain,/WebBrowserComponent::areOptionsSupported \(webViewOptions\(\)\)/);
  assert.match(nativeMain,/WEBVIEW2_GUI_READY_TIMEOUT/);
  assert.match(nativeMain,/withEventListener \("arMixGuiReady"/);
  assert.doesNotMatch(nativeMain,/pageFinishedLoading \(const juce::String&\) override \{ loaded = true; \}/);
  assert.match(monitor,/api\.emitEvent\("arMixGuiReady"/);
  assert.match(packaging,/webView2LoaderMode = "JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1"/);
  assert.match(verify,/manifest\.webView2LoaderMode/);
  assert.match(verify,/WEBVIEW2_GUI_READY_TIMEOUT/);
  assert.match(fat,/Microsoft Edge WebView2 Runtime/);
});
