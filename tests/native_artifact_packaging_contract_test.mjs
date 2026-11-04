// SPDX-License-Identifier: AGPL-3.0-only
// Windows product packaging must never attest the retired GUI by coincidence.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(p,'utf8'),workflow=read('.github/workflows/p1-windows.yml');
const pack=read('scripts/package_native_windows.ps1'),verify=read('scripts/verify_native_windows_artifact.ps1');
const host=read('src/Main.cpp'),cmake=read('CMakeLists.txt');
test('packaged EXE is the actual JUCE x64 Windows GUI, not a console test',()=>{
 assert.match(cmake,/juce_add_gui_app\(ARMixConsole/);
 assert.match(pack,/ARMixConsole_artefacts\/Release\/AR MIX Studio\.exe/);
 assert.match(pack,/minimumGuiExeBytes = 4MB/);
 assert.match(pack,/0x8664/);assert.match(pack,/0x020b/);
 assert.match(pack,/\$subsystem -ne 2/);
 assert.match(pack,/exeSha256/);
 assert.match(verify,/\$bytes\.Length -lt 4MB/);
 assert.match(verify,/\$archiveBytes -lt 2MB/);
 assert.match(verify,/ComputeHash\(\$stream\)/);
 assert.match(verify,/manifest\.sourceCommit/);
});
test('real M3 Console and its complete 24-asset set are attested INSIDE the executable',()=>{
 const name='ArSonMix-M3-NativeConsole-ENGINEERING-NOT-HARDWARE-QUALIFIED-Windows-x64.exe';
 assert.ok(pack.includes(name)&&verify.includes(name));
 assert.match(pack,/guiResources = \$guiResources/);
 assert.match(pack,/guiAuthority = "APPROVED_M3_NATIVE_CONSOLE_V1"/);
 assert.match(pack,/web\/index\.html/);
 assert.match(pack,/web\/console-native\.js/);
 assert.match(pack,/expected HTML \+ native JS \+ 24 visual assets/);
 assert.match(verify,/\$embeddedText\.IndexOf\(\$sourceText, \[StringComparison\]::Ordinal\)/);
 assert.match(verify,/STALE NATIVE GUI: approved JUCE BinaryData resource absent/);
 assert.match(verify,/WRONG GUI: the abandoned ten-strip frontend was packaged/);
 assert.match(verify,/manifest\.guiAuthority/);
 assert.match(verify,/arMixConsoleSnapshot/);
 assert.match(verify,/arMixConsoleIntent/);
 assert.match(host,/BinaryData::index_html/);
 assert.match(host,/BinaryData::consolenative_js/);
 assert.doesNotMatch(host,/BinaryData::app_js|BinaryData::styles_css/);
 assert.doesNotMatch(cmake,/web\/app\.js|web\/bridge\.js/);
});
test('product status and immutable provenance do NOT claim universal ChannelId ready',()=>{
 assert.match(pack,/M4_10CH4AUX_COMPAT_CHANNELID_CUTOVER_PENDING__NOT_PRODUCT_READY/);
 assert.match(pack,/REAL_M4_10CH4AUX__UNIVERSAL_CH11_VST3_BUS_FX_NOT_READY/);
 assert.match(verify,/REAL_M4_10CH4AUX__UNIVERSAL_CH11_VST3_BUS_FX_NOT_READY/);
 assert.match(pack,/EXPERIMENTAL_NOT_HARDWARE_QUALIFIED/);
 assert.match(pack,/sourceCommit/);
 assert.match(pack,/portable_mode\.txt/);
 assert.match(verify,/ARSONMIX_PORTABLE_WORKSPACE_V1/);
});
test('one qualified, exact-SHA product release gate after native CTest; no ENGINE-only artifact',()=>{
 assert.match(workflow,/ENGINE_EXACT_SOURCE_SHA_VERIFIED/);
 assert.ok(workflow.indexOf('ctest --test-dir build -C Release --output-on-failure')<
  workflow.indexOf('Stop incomplete integrated product before Windows packaging'));
 assert.ok(workflow.indexOf('Stop incomplete integrated product before Windows packaging')<
  workflow.indexOf('Package verified JUCE GUI for hardware FAT'));
 assert.match(workflow,/node scripts\/check_console_puzzle\.mjs --release/);
 const upload=workflow.split('uses: actions/upload-artifact@v4')[1]||'';
 assert.ok(upload.includes("github.event_name == 'pull_request' && startsWith(github.head_ref, 'integration/product-')"));
 assert.ok(!upload.includes('ArSonMix-ENGINE-10CH4AUX'));
 assert.ok(upload.includes('if-no-files-found: error'));
 assert.ok(workflow.indexOf('scripts/verify_native_windows_artifact.ps1')<
  workflow.indexOf('uses: actions/upload-artifact@v4'));
});
test('JUCE startup is native WebView2 with genuine JS ready event',()=>{
 assert.match(cmake,/JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1/);
 assert.match(cmake,/NEEDS_WEBVIEW2 TRUE/);
 assert.match(host,/WebBrowserComponent::areOptionsSupported \(webViewOptions\(\)\)/);
 assert.match(host,/WEBVIEW2_GUI_READY_TIMEOUT/);
 assert.match(host,/withEventListener \("arMixGuiReady"/);
 assert.match(host,/withEventListener \("arMixConsoleIntent"/);
 assert.match(host,/withEventListener \("arMixConsoleSubscribe"/);
 const native=read('web/console-native.js');
 assert.match(native,/backend\.emitEvent\('arMixGuiReady'/);
 assert.match(native,/backend\.emitEvent\('arMixConsoleSubscribe'/);
});
