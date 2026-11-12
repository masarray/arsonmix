// SPDX-License-Identifier: AGPL-3.0-only
// Native FAT: compile the user's APPROVED /preview/ Pages renderer, not a new UI.
// Preserve realtime/device/PE/ZIP protections while rejecting stale visual sources.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
// FAT regression tests share the existing Windows Node contract runner.
import './m3e_fat_evidence_gate.test.mjs';
const read=path=>readFileSync(path,'utf8');
const workflow=read('.github/workflows/p1-windows.yml');
const packaging=read('scripts/package_native_windows.ps1');
const pythonProvenance=read('scripts/resolve_approved_gui_python.ps1');
const iconVerifier=read('scripts/verify_windows_pe_brand_icon.py');
const verify=read('scripts/verify_native_windows_artifact.ps1');
const nativeMain=read('src/Main.cpp');
const cmake=read('CMakeLists.txt');
const generator=read('scripts/build_approved_native_gui.py');
const adapter=read('web/approved-native-bridge.js');
const fat=read('docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md');


test('packaging and independent ZIP verifier use the exact CMake Python, never PATH python for icon recompression',()=>{
  for(const script of [packaging,verify]){
    assert.match(script,/resolve_approved_gui_python\.ps1/);
    assert.match(script,/Resolve-ArSonMixApprovedGuiPython/);
    assert.match(script,/& \$approvedGuiPython scripts\/build_approved_native_gui\.py --out \$approvedGuiDir --verify/);
    assert.doesNotMatch(script,/& python scripts\/build_approved_native_gui\.py/);
  }
  assert.match(cmake,/approved-gui-python\.txt/);
  assert.match(pythonProvenance,/approved-gui-python\.txt/);
  assert.match(pythonProvenance,/Test-Path -LiteralPath \$selected -PathType Leaf/);
  assert.match(pythonProvenance,/refusing unverified GUI regeneration/);
});

test('EXE and independently ZIP-distributed PE icons must match approved source RGBA',()=>{
  for(const script of [packaging,verify]){
    assert.match(script,/& \$approvedGuiPython scripts\/verify_windows_pe_brand_icon\.py --exe/);
    assert.match(script,/--approved assets\/icon\/arsonmix-256\.png/);
    assert.match(script,/LASTEXITCODE -ne 0/);
  }
  assert.match(iconVerifier,/RT_GROUP_ICON\/RT_ICON/);
  assert.match(iconVerifier,/read_png\(expected\)/);
  assert.match(iconVerifier,/actual_rows\s*=\s*read_png/);
  assert.match(iconVerifier,/old\[pos\+3\]!=actual\[pos\+3\]/);
  assert.match(iconVerifier,/required=\{16,32,48,256\}/);
});

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
  // Read the one authoritative Pages sprite, rather than pinning a
  // long-retired MUTE/SOLO/PFL asset name in the native installer gate.
  // Native CMake must embed the same approved sprite selected by Pages CSS.
  const builder=read('scripts/build_pages.py');
  const sprite=builder.match(/^sprite='([^']+\.png)'$/m)?.[1];
  assert.ok(sprite?.startsWith('harmonice-thin-flat-'), 'approved PNG atlas missing from Pages');
  assert.ok(read('site/preview-buttons/assets/mixer-control-keys.css')
    .includes('background-image:url("'+sprite+'")'),
    'native and Pages must use one approved physical PNG atlas');
  assert.ok(readFileSync('site/preview-buttons/assets/'+sprite).byteLength>1000,
    'approved PNG atlas must exist and have non-empty raster bytes');
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
  // Windows still requires WebView2, but other native platforms must not.
  assert.ok(cmake.includes('NEEDS_WEBVIEW2 ${WIN32}') || cmake.includes('NEEDS_WEBVIEW2 TRUE'));
  assert.ok(cmake.includes('if(WIN32)') && cmake.includes('JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1'));
  assert.match(nativeMain,/WebBrowserComponent::areOptionsSupported \(webViewOptions\(\)\)/);
  assert.match(nativeMain,/WEBVIEW2_GUI_READY_TIMEOUT/);
  assert.match(nativeMain,/withEventListener \("arMixGuiReady"/);
  assert.doesNotMatch(nativeMain,/pageFinishedLoading \(const juce::String&\) override \{ loaded = true; \}/);
  assert.match(adapter,/emitEvent\("arMixGuiReady"/);
  assert.match(adapter,/acceptedGraphConnected:false/);
  assert.match(adapter,/productionCallbackLive:false/);
  assert.match(packaging,/webView2LoaderMode = "JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1"/);
  assert.match(verify,/manifest\.webView2LoaderMode/);
  assert.match(verify,/WEBVIEW2_GUI_READY_TIMEOUT/);
});

test('physical 10CH+4AUX Preview build does not invent independent hardware certification',()=>{
  assert.match(packaging,/NATIVE_TEN_INPUT_CHANNELS_FOUR_DEDICATED_STEREO_AUX_RETURNS_MASTER__EXPLICIT_PATCH_ACK/);
  assert.match(packaging,/PREVIEW/);
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

test('M3-E same-SHA native ZIP carries SHA-pinned FAT tools and no fake hardware claims',()=>{
  const p=read('scripts/package_native_windows.ps1');
  const v=read('scripts/verify_native_windows_artifact.ps1');
  const collector=read('scripts/collect_hardware_fat_windows.ps1');
  const checker=read('scripts/m3e_fat_evidence_gate.mjs');
  assert.match(p,/fatCollectorSha256 =/);
  assert.match(p,/fatGateSha256 =/);
  assert.match(v,/FAT tool differs from exact source \/ manifest/);
  assert.match(v,/FAT tool ZIP payload digest differs/);
  assert.match(v,/scripts\/m3e_fat_evidence_gate\.mjs/);
  assert.match(collector,/FAT_RUNS\.json/);
  assert.match(collector,/SOFTWARE_VERIFIED_PREVIEW_CANDIDATE/);
  assert.match(collector,/physicalAudibilityVerifiedBySoftware=\$false/);
  assert.match(checker,/PENDING_NATIVE_CALLBACK_PROOF/);
  assert.match(checker,/EVIDENCE_READY_FOR_HUMAN_REVIEW/);
  assert.doesNotMatch(checker,/hardwareCertified:true/);
});

test('FAT PowerShell tools parse on real Windows PowerShell before native build',()=>{
  if (process.platform !== 'win32') return;
  for (const file of [
    'scripts/collect_hardware_fat_windows.ps1',
    'scripts/package_native_windows.ps1',
    'scripts/verify_native_windows_artifact.ps1'
  ]) {
    const command="$tokens=$null;$parseErrors=$null;" +
      "[System.Management.Automation.Language.Parser]::ParseFile('"+file+"',[ref]$tokens,[ref]$parseErrors)|Out-Null;" +
      "if($parseErrors.Count -gt 0){$parseErrors|ForEach-Object{Write-Output $_.Message};exit 1}";
    assert.doesNotThrow(()=>execFileSync('powershell.exe',
      ['-NoProfile','-NonInteractive','-Command',command],
      {cwd:process.cwd(),encoding:'utf8',timeout:15000}),
      file+' must have a complete parseable PowerShell AST');
  }
});

test('FAT collector requires an exact rate and real endpoints before timers, plus both-rate honest summary',()=>{
  const source=read('scripts/collect_hardware_fat_windows.ps1');
  const verifier=read('scripts/verify_native_windows_artifact.ps1');
  assert.match(source,/SampleRate -notin @\(44100,48000\)/);
  assert.match(source,/Real physical capture\/render endpoint identity required before timer starts/);
  assert.match(source,/\$_.mode -eq 'DRY_THROUGH_0FX' -and \[int\]\$_.sampleRate -eq \$SampleRate/);
  assert.match(source,/foreach \(\$rate in @\(44100,48000\)\)/);
  assert.match(source,/verified CLEAR observation and human witness required/);
  assert.match(source,/native report SHA256 mismatch/);
  assert.match(verifier,/M3-E evidence tool hash missing/);
  assert.match(verifier,/FAT tool differs from exact source \/ manifest/);
  assert.match(verifier,/FAT tool ZIP payload digest differs/);
  assert.doesNotMatch(verifier,/\n\) \{\n\s*throw "M3-E evidence tool hash missing/);
});
