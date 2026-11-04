// SPDX-License-Identifier: AGPL-3.0-only
// Native FAT: compile the user's APPROVED /preview/ Pages renderer, not a new UI.
// Preserve realtime/device/PE/ZIP protections while rejecting stale visual sources.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=path=>readFileSync(path,'utf8');
const workflow=read('.github/workflows/p1-windows.yml');
const packaging=read('scripts/package_native_windows.ps1');
const verify=read('scripts/verify_native_windows_artifact.ps1');
const nativeMain=read('src/Main.cpp');
const cmake=read('CMakeLists.txt');
const generator=read('scripts/build_approved_native_gui.py');
const adapter=read('web/approved-native-bridge.js');
const fat=read('docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md');

test('native FAT picks exact JUCE GUI target, not a test executable',()=>{
  assert.match(cmake,/juce_add_gui_app\(ARMixConsole/);
  assert.match(cmake,/PRODUCT_NAME "AR MIX Studio"/);
  assert.match(packaging,/ARMixConsole_artefacts\/Release\/AR MIX Studio\.exe/);
  assert.match(packaging,/minimumGuiExeBytes = 4MB/);
  assert.match(packaging,/0x8664/);
  assert.match(packaging,/0x020b/);
  assert.match(packaging,/\$subsystem -ne 2/);
  assert.match(verify,/\$bytes\.Length -lt 4MB/);
  assert.match(verify,/\$archiveBytes -lt 2MB/);
  assert.match(verify,/Compress-Archive/);
  assert.match(verify,/ZipFile\]::OpenRead/);
  assert.match(verify,/ComputeHash\(\$stream\)/);
  assert.match(verify,/manifest\.sourceCommit/);
  assert.match(workflow,/scripts\/package_native_windows\.ps1/);
  assert.match(workflow,/Independently verify release ZIP and embedded GUI\/engine before upload/);
  assert.ok(workflow.indexOf('scripts/verify_native_windows_artifact.ps1') <
            workflow.indexOf('uses: actions/upload-artifact@v4'));
});

test('JUCE embeds only generated approved Pages DOM, CSS, JS, and PNG button variant',()=>{
  const sourceBlock=cmake.split('juce_add_binary_data(ARMixAssets SOURCES')[1]?.split(')\n')[0]||'';
  assert.match(sourceBlock,/ARMIX_APPROVED_GUI_DIR/);
  assert.match(sourceBlock,/ARMIX_APPROVED_GUI_BINARIES/);
  assert.doesNotMatch(sourceBlock,/web\/index\.html|web\/console-native\.html/);
  assert.match(cmake,/build_approved_native_gui\.py/);
  assert.match(cmake,/site\/preview-v2/);
  assert.match(cmake,/site\/preview-buttons/);
  assert.match(nativeMain,/ApprovedGuiResources\.h/);
  assert.match(nativeMain,/resolveApprovedGuiResource \(path, data, size, mime\)/);
  assert.doesNotMatch(nativeMain,/BinaryData::consolenative_html|BinaryData::index_html|BinaryData::app_js/);
  assert.match(nativeMain,/withResourceProvider \(assetFor\)/);
  assert.match(generator,/approvedPagesHtmlSha256/);
  assert.match(generator,/authoritativePagesOutput/);
  assert.match(generator,/mute-solo-pfl-56x37\.png/);
});

test('independent release gate rejects old self-consistent web hash artifact',()=>{
  for(const field of [
    'approvedGuiRoute','approvedGuiOutput','approvedGuiPagesSha256',
    'approvedGuiNativeHtmlSha256','approvedGuiManifestSha256',
    'approvedGuiCanonicalSha256','approvedGuiResourceCount','approvedGuiAdapterStatus'
  ]) {
    assert.ok(packaging.includes(field+' = '), 'missing manifest field '+field);
    assert.ok(verify.includes('manifest.'+field), 'independent gate missing '+field);
  }
  assert.match(verify,/scripts\/build_approved_native_gui\.py --out \$approvedGuiDir --verify/);
  assert.match(verify,/APPROVED GUI BYTE RESOURCE NOT IN WINDOWS EXE/);
  assert.match(verify,/Test-EmbeddedBytes/);
  assert.match(verify,/guiCalibration/);
  assert.match(verify,/STABLE|NOT_PRODUCT_READY|NOT_LIVE_PCM/);
  assert.match(verify,/BinaryData::consolenative_html/);
  assert.match(verify,/BinaryData::index_html/);
  assert.doesNotMatch(verify,/Path="web\/index\.html"/);
  assert.doesNotMatch(verify,/Path="web\/app\.js"/);
});

test('native bridge authenticates UI readiness but never hallucinates PCM/VST3 LIVE',()=>{
  assert.match(cmake,/JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1/);
  assert.match(cmake,/NEEDS_WEBVIEW2 TRUE/);
  assert.match(nativeMain,/WebBrowserComponent::areOptionsSupported \(webViewOptions\(\)\)/);
  assert.match(nativeMain,/WEBVIEW2_GUI_READY_TIMEOUT/);
  assert.match(nativeMain,/withEventListener \("arMixGuiReady"/);
  assert.doesNotMatch(nativeMain,/pageFinishedLoading \(const juce::String&\) override \{ loaded = true; \}/);
  assert.match(adapter,/emitEvent\("arMixGuiReady"/);
  assert.match(adapter,/acceptedGraphConnected: false/);
  assert.match(adapter,/productionCallbackLive: false/);
  assert.match(packaging,/webView2LoaderMode = "JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1"/);
  assert.match(verify,/manifest\.webView2LoaderMode/);
  assert.match(verify,/WEBVIEW2_GUI_READY_TIMEOUT/);
});

test('physical 10CH+4AUX runtime remains hardware-unqualified, no fake UAT',()=>{
  assert.match(packaging,/NATIVE_TEN_INPUT_CHANNELS_FOUR_DEDICATED_STEREO_AUX_RETURNS_MASTER__EXPLICIT_PATCH_ACK/);
  assert.match(packaging,/HARDWARE_FAT_UNQUALIFIED/);
  assert.match(verify,/NATIVE_TEN_INPUT_CHANNELS_FOUR_DEDICATED_STEREO_AUX_RETURNS_MASTER__EXPLICIT_PATCH_ACK/);
  assert.doesNotMatch(packaging,/CHANNEL_1_ONLY__REMAINING_9_GUI_CHANNELS_NOT_REAL_AUDIO/);
  for(const marker of [
    /native 10CH \+ 4 AUX/i,/UNPATCHED \/ SILENT/i,/APPLY ROUTING/,
    /CLEAR PATCH/,/Experimental; not yet qualified for professional live audio/i,
    /OUTPUT_ONLY_TONE/,/CAPTURE_ONLY/,/DRY_THROUGH/,/44,100 Hz/,
    /48,000 Hz/,/RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE/
  ]) assert.match(fat,marker);
});

test('FAT bundle retains the reviewed first-sound guide and SHA protection',()=>{
  const quick=read('docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md');
  assert.match(packaging,/FIRST_SOUND_FAT_QUICKSTART_20261101\.md/);
  assert.match(verify,/manifest\.exeSha256/);
  assert.match(verify,/inZipHash -ne \$appHash/);
  for(const text of ['10 input channels + 4 AUX','OUTPUT_ONLY_TONE',
    'CAPTURE_ONLY','COPY FULL REPORT','30 minutes','no separate manual START AUDIO'])
    assert.ok(quick.includes(text),text);
});
