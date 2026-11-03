// SPDX-License-Identifier: AGPL-3.0-only
// The release artefact MUST be the exact native Console, never the retired fixed-ten HTML.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8');
const cmake=read('CMakeLists.txt');
const main=read('src/Main.cpp');
const packageScript=read('scripts/package_native_windows.ps1');
const verify=read('scripts/verify_native_windows_artifact.ps1');
const workflow=read('.github/workflows/p1-windows.yml');
const consoleHtml=read('web/console-native.html');
const consoleJs=read('web/console-native.js');
const name='ArSonMix-NativeConsole-CH11-EXPERIMENTAL-Windows-x64.exe';

test('only the approved Console resources are compiled/served by JUCE',()=>{
 assert.match(cmake,/juce_add_gui_app\(ARMixConsole/);
 assert.match(cmake,/web\/console-native\.html web\/console-native\.css web\/console-native\.js/);
 assert.doesNotMatch(cmake,/web\/index\.html|web\/app\.js|web\/monitor\.js/);
 assert.match(main,/BinaryData::consolenative_html/);
 assert.match(main,/BinaryData::consolenative_css/);
 assert.match(main,/BinaryData::consolenative_js/);
 assert.doesNotMatch(main,/BinaryData::index_html|BinaryData::app_js|BinaryData::monitor_js/);
 assert.match(main,/withResourceProvider \(assetFor\)/);
 assert.match(consoleHtml,/ArSonMix — Native Live Console/);
 assert.doesNotMatch(consoleHtml,/data-view="FX RACK"/);
 assert.doesNotMatch(consoleJs,/\bconst\s+channels\s*=\s*raw\.map/);
});
test('actual single Console uses native snapshot ACK, real PCM and dynamic ChannelId',()=>{
 for(const value of ['arMixConsoleSubscribe','arMixConsoleIntent','arMixConsoleSnapshot','arMixConsoleAck','channelPeaksL','nativeAddAudio','NATIVE_PATCH_CHANNEL','NATIVE_SET_CHANNEL_MIX','NATIVE_SET_CHANNEL_MUTE_SOLO']) {
   assert.ok(consoleJs.includes(value)||consoleHtml.includes(value),'missing native operator flow '+value);
 }
 assert.match(consoleJs,/graph\?\.channels|graph\.channels/);
 assert.doesNotMatch(consoleJs,/Math\.sin\(/);
});
test('package is a real PE32+ x64 GUI, not a console test binary or old 10CH artifact',()=>{
 assert.match(cmake,/PRODUCT_NAME "AR MIX Studio"/);
 assert.match(packageScript,/ARMixConsole_artefacts\/Release\/AR MIX Studio\.exe/);
 for(const marker of ['0x8664','0x020b','minimumGuiExeBytes = 4MB','$subsystem -ne 2','exeSha256','sourceCommit','EXPERIMENTAL_NOT_HARDWARE_QUALIFIED','BUILD_PROVENANCE.json','HARDWARE_FAT_GUIDE.md'])
   assert.ok(packageScript.includes(marker),'package field missing '+marker);
 assert.ok(packageScript.includes(name)&&verify.includes(name));
 assert.ok(!packageScript.includes('ArSonMix-GUI10CH-P012-Windows-x64.exe'));
 assert.match(packageScript,/nativeConsoleOnly = \$true/);
 assert.match(packageScript,/retiredUiEmbedded = \$false/);
 assert.match(verify,/manifest\.nativeConsoleOnly/);
 assert.match(verify,/manifest\.retiredUiEmbedded/);
 assert.match(packageScript,/NATIVE_CHANNELID_CH11_DYNAMIC_INPUT__10CH4AUX_COMPAT_FALLBACK__NOT_FULL_UNIVERSAL/);
 assert.match(verify,/NATIVE_CHANNELID_CH11_DYNAMIC_INPUT__10CH4AUX_COMPAT_FALLBACK__NOT_FULL_UNIVERSAL/);
});
test('release verifier hashes ALL active GUI bytes inside actual EXE and ZIP',()=>{
 for(const f of ['consoleNativeHtmlSha256','consoleNativeCssSha256','consoleNativeJsSha256','activeGuiEntrypoint']) {
  assert.ok(packageScript.includes(f+' = '));
  assert.ok(verify.includes('manifest.'+f));
 }
 for(const file of ['web/console-native.html','web/console-native.css','web/console-native.js'])
  assert.ok(verify.includes('Path="'+file+'"'));
 for(const old of ['Path="web/index.html"','Path="web/app.js"','Path="web/monitor.js"'])
  assert.ok(!verify.includes(old),'retired asset still required in distribution '+old);
 for(const marker of ['STALE NATIVE GUI: exact JUCE BinaryData resource absent from EXE','IndexOf($sourceText, [StringComparison]::Ordinal)','ZipFile]::OpenRead','ComputeHash($stream)','manifest.sourceCommit','manifest.exeSha256','WEBVIEW2_GUI_READY_TIMEOUT','AUDIO_GLITCH_WINDOW'])
  assert.ok(verify.includes(marker),'archive or binary attestation missing: '+marker);
 assert.match(verify,/\$bytes\.Length -lt 4MB/);
 assert.match(verify,/\$archiveBytes -lt 2MB/);
 assert.match(verify,/Get-FileHash/);
});
test('native startup uses WebView2 readiness and producer-owned output controls',()=>{
 assert.match(cmake,/NEEDS_WEBVIEW2 TRUE/);
 assert.match(cmake,/JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1/);
 assert.match(main,/WebBrowserComponent::areOptionsSupported \(webViewOptions\(\)\)/);
 assert.match(main,/withEventListener \("arMixGuiReady"/);
 assert.match(main,/WEBVIEW2_GUI_READY_TIMEOUT/);
 assert.match(consoleJs,/api\.emitEvent\('arMixGuiReady'/);
});
test('CI validates exact HEAD before upload and cannot ship ENGINE-only old GUI',()=>{
 assert.match(workflow,/ref: \$\{\{ github\.event_name == 'pull_request' && github\.event\.pull_request\.head\.sha \|\| github\.sha \}\}/);
 assert.match(workflow,/ENGINE_EXACT_SOURCE_SHA_VERIFIED/);
 assert.match(workflow,/ctest --test-dir build -C Release --output-on-failure/);
 assert.match(workflow,/scripts\/package_native_windows\.ps1/);
 assert.match(workflow,/scripts\/verify_native_windows_artifact\.ps1/);
 const upload=workflow.split('uses: actions/upload-artifact@v4')[1]||'';
 assert.ok(upload.includes("startsWith(github.head_ref, 'integration/product-')"));
 assert.ok(!upload.includes('milestone/m5-universal-channelid-vst3-lease-migration-20261101'));
 assert.ok(workflow.indexOf('scripts/verify_native_windows_artifact.ps1')<workflow.indexOf('uses: actions/upload-artifact@v4'));
});
test('physical crackle remains unqualified until independent 0FX/1FX FAT',()=>{
 const guide=read('docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md');
 for(const marker of ['OUTPUT_ONLY_TONE','CAPTURE_ONLY','DRY_THROUGH_0FX','30 minutes','44,100','48,000'])
  assert.ok(guide.includes(marker),'hardware FAT guide missing '+marker);
 assert.match(packageScript,/HARDWARE_FAT_UNQUALIFIED/);
 assert.match(packageScript,/EXPERIMENTAL_NOT_HARDWARE_QUALIFIED/);
});
