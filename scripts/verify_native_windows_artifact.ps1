# SPDX-License-Identifier: AGPL-3.0-only
# Independent post-package acceptance. An EXE build and 24 CTests can pass
# while upload-artifact delivers a WRONG console executable: verify the
# DISTRIBUTION, its actual GUI resources and its compressed ZIP integrity.
param([string] $OutputDir = "dist")
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "windows_zip_member_paths.ps1")

$expected = @(
    "ArSonMix-GUI10CH-P012-Windows-x64.exe",
    "BUILD_PROVENANCE.json",
    "HARDWARE_FAT_GUIDE.md",
    "README.md",
    "portable_mode.txt",
    "ArSonMixStartupDiagnostics.ps1",
    "collect_hardware_fat_windows.ps1",
    "m3e_fat_evidence_gate.mjs",
    "FACTORYFX_STAGE_REPORT.json"
)
foreach ($name in $expected) {
    if (-not (Test-Path -LiteralPath (Join-Path $OutputDir $name) -PathType Leaf)) {
        throw "Distribution is missing required file: $name"
    }
}
# Verify FactoryFX only when an exact reviewed lock explicitly authorizes it.
# App-only engineering ZIPs remain unchanged; a missing approved module fails.
& python scripts/factoryfx_release_bundle.py verify --lock config/factoryfx-approved-releases.json --output $OutputDir --best-effort
if ($LASTEXITCODE -ne 0) { throw "FactoryFX resource, manifest or licence gate failed" }
$factoryPath = Join-Path $OutputDir "FactoryFX"
$topDirs = @(Get-ChildItem -LiteralPath $OutputDir -Directory | ForEach-Object Name)
if (@($topDirs | Where-Object { $_ -ne "FactoryFX" }).Count -gt 0) {
    throw "Unapproved directory in public release: $($topDirs -join ', ')"
}
$actual = @(Get-ChildItem -LiteralPath $OutputDir -File | ForEach-Object Name)
if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
    throw "Unapproved files inside release distribution: $($actual -join ', ')"
}

$manifest = Get-Content -LiteralPath (Join-Path $OutputDir "BUILD_PROVENANCE.json") -Raw -Encoding UTF8 | ConvertFrom-Json
# Compare the exact-source legal hashes recorded at packaging, and scan
# the REAL PE for all seven full original bytes (JUCE BinaryData).
if ($manifest.legalEmbedding -ne "JUCE_BINARYDATA_OFFLINE_LEGAL_CENTER_V1") {
    throw "Expected embedded legal center missing from provenance"
}
$legalSources = [ordered]@{
    "LICENSE" = "licenseTextSha256"
    "LICENSE-NOTICE.txt" = "licenseNoticeSha256"
    "docs/THIRD_PARTY_8D_SONKU_HRTF.md" = "hrirNoticeSha256"
    "third_party/signalsmith/LICENSE-STRETCH.txt" = "signalsmithStretchLicenseSha256"
    "third_party/signalsmith/LICENSE-LINEAR.txt" = "signalsmithLinearLicenseSha256"
    "web/assets/fonts/OFL-LICENSE-Inter.txt" = "interOflLicenseSha256"
    "installer/VB-CABLE-NOTICE.txt" = "vbCableNoticeSha256"
}
foreach ($source in $legalSources.Keys) {
    $key = $legalSources[$source]
    $actualHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($manifest.$key -ne $actualHash) {
        throw "Embedded legal source changed since packaging: $source"
    }
}
$agplText = Get-Content -LiteralPath "LICENSE" -Raw -Encoding UTF8
if (-not $agplText.StartsWith("GNU AFFERO GENERAL PUBLIC LICENSE") -or
    -not $agplText.Contains("END OF TERMS AND CONDITIONS")) {
    throw "Source AGPL text is incomplete"
}
& python scripts/verify_embedded_legal.py --binary (Join-Path $OutputDir $expected[0])
if ($LASTEXITCODE -ne 0) { throw "Exact-source embedded legal scan failed" }
# FAT tools match source bytes, distributed bytes AND the current SHA manifest.
$startupDiagnostics = "ArSonMixStartupDiagnostics.ps1"
foreach ($path in @((Join-Path "installer" $startupDiagnostics), (Join-Path $OutputDir $startupDiagnostics))) {
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.startupDiagnosticsSha256) {
        throw "Startup diagnostic script differs from exact-source manifest"
    }
}
& python scripts/verify_win10_pe_runtime.py --exe (Join-Path $OutputDir $expected[0])
if ($LASTEXITCODE -ne 0) { throw "Packaged PE dependency audit failed" }
$fatTools = @{
    "collect_hardware_fat_windows.ps1" = [string]$manifest.fatCollectorSha256
    "m3e_fat_evidence_gate.mjs" = [string]$manifest.fatGateSha256
}
foreach ($filename in $fatTools.Keys) {
    if ($fatTools[$filename] -notmatch '^[0-9a-f]{64}$') {
        throw "M3-E evidence tool hash missing: $filename"
    }
    $sourceFile = Join-Path "scripts" $filename
    $packageFile = Join-Path $OutputDir $filename
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Current exact SHA checkout lacks FAT tool: $filename"
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash.ToLowerInvariant()
    $distHash = (Get-FileHash -LiteralPath $packageFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sourceHash -ne $fatTools[$filename] -or $distHash -ne $fatTools[$filename]) {
        throw "FAT tool differs from exact source / manifest: $filename"
    }
}
& node scripts/m3e_fat_evidence_gate.mjs $OutputDir
if ($LASTEXITCODE -ne 0) { throw "Independent M3-E FAT bundle verifier failed" }
# No physical FAT journal in a build artifact is PENDING, never a false PASS.
# App-only ZIP does not ship an untracked dynamic WebView2Loader.dll.
if ($manifest.webView2LoaderMode -ne "JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1" -or
    $manifest.guiStartupVerification -ne "WEBVIEW2_PREFLIGHT_AND_AR_MIX_GUI_READY_HANDSHAKE") {
    throw "Missing Windows WebView2 startup / static loader provenance"
}
if ($manifest.nativeDSP -ne "NATIVE_TEN_INPUT_CHANNELS_FOUR_DEDICATED_STEREO_AUX_RETURNS_MASTER__EXPLICIT_PATCH_ACK") {
    throw "Native mixer routing provenance is not the current 10CH/4AUX production graph"
}
if ($manifest.storageMode -ne "PORTABLE_MARKER__USER_STUDIO_EMPTY_AT_RELEASE") {
    throw "Public ZIP missing portable workspace mode provenance"
}
$portableMarker = (Get-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Raw -Encoding UTF8).Trim()
if ($portableMarker -ne "ARSONMIX_PORTABLE_WORKSPACE_V1") { throw "Invalid portable marker" }
if (Test-Path -LiteralPath (Join-Path $OutputDir "Studio")) {
    throw "Public artifact MUST NOT bundle user-specific plugins, presets or sessions"
}
# Validate THIS exact distribution, not the CMake intent alone.
# A generic PE/CTest pass without ImmerSIB must never reach upload-artifact.
if ($manifest.spatialFxBuild -ne "SONKU_P99_AND_IMMERSIB_NATIVE_CATALOG_PLUS_SIX_EMBEDDED_SCENES_VERIFIED" -or
    $manifest.spatialFxCatalogId -ne "armix.builtin.imersip.v1") {
    throw "WINDOWS_FAT_SPATIAL_PROVENANCE_MISSING"
}
$exePath = Join-Path $OutputDir "ArSonMix-GUI10CH-P012-Windows-x64.exe"
& python scripts/verify_immersib_approved_pcm_sources.py
if ($LASTEXITCODE -ne 0) { throw "WINDOWS_FAT_IMMERSIB_APPROVED_SOURCE_CHANGED" }
& python scripts/verify_windows_spatial_fx_packaging.py --build-dir build --exe $exePath
if ($LASTEXITCODE -ne 0) { throw "WINDOWS_FAT_SPATIAL_BINARY_MISMATCH" }

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $exePath).Path)
if ($bytes.Length -lt 4MB) { throw "REJECT: suspiciously small executable ($($bytes.Length) bytes)" }
if ($bytes.Length -ne $manifest.guiExeBytes) { throw "Mismatch: actual EXE size and recorded size" }
if ([Text.Encoding]::ASCII.GetString($bytes,0,2) -ne "MZ") { throw "Invalid EXE MZ signature" }
$pe = [BitConverter]::ToInt32($bytes,0x3c)
if ([Text.Encoding]::ASCII.GetString($bytes,$pe,4) -ne ("PE" + [char]0 + [char]0)) {
    throw "Invalid EXE PE signature"
}
if ($pe -lt 64 -or $pe -ge $bytes.Length - 128) { throw "Invalid PE header offset" }
if ([BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($bytes,$pe+24) -ne 0x020b -or
    [BitConverter]::ToUInt16($bytes,$pe+24+68) -ne 2) {
    throw "REJECT: distribution EXE is not Windows x64 GUI PE32+ (console tests forbidden)"
}
$appHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($appHash -ne $manifest.exeSha256) { throw "EXE hash mismatch before ZIP upload" }
$sha = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sha -ne $manifest.sourceCommit) {
    throw "Distribution manifest Git sourceCommit does not match checkout"
}
# Release gate must verify the EXACT GUI shown by /preview/, not a
# self-consistent but WRONG legacy web/index.html or console-native renderer.
$approvedGuiDir = Join-Path "build" "approved_gui"
. (Join-Path $PSScriptRoot "resolve_approved_gui_python.ps1")
$approvedGuiPython = Resolve-ArSonMixApprovedGuiPython -BuildDir "build"
# Independent post-package check of the EXE that will go INSIDE the ZIP.
& $approvedGuiPython scripts/verify_windows_pe_brand_icon.py --exe $exePath --approved assets/icon/arsonmix-256.png
if ($LASTEXITCODE -ne 0) { throw "REJECT: distributed Windows PE icon does not match current approved brand" }
& $approvedGuiPython scripts/build_approved_native_gui.py --out $approvedGuiDir --verify
if ($LASTEXITCODE -ne 0) { throw "REJECT: native GUI is not built from current approved Pages" }
$approvedPath = Join-Path $approvedGuiDir "approved-gui-manifest.json"
$approved = Get-Content -LiteralPath $approvedPath -Raw -Encoding UTF8 | ConvertFrom-Json
$approvedHash = (Get-FileHash -LiteralPath $approvedPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($manifest.approvedGuiManifestSha256 -ne $approvedHash -or
    $manifest.approvedGuiRoute -ne "/preview/" -or
    $manifest.approvedGuiOutput -ne "preview/index.html" -or
    $manifest.approvedGuiPagesSha256 -ne $approved.approvedPagesHtmlSha256 -or
    $manifest.approvedGuiNativeHtmlSha256 -ne $approved.nativeHtmlSha256 -or
    $manifest.approvedGuiCanonicalSha256 -ne $approved.canonicalSourceSha256 -or
    $manifest.approvedGuiAdapterStatus -ne "SCAFFOLD_ONLY__NOT_LIVE_PCM" -or
    $manifest.guiCalibration -ne "APPROVED_PAGES_VISUAL_PROVENANCE_ONLY__M1_NOT_PRODUCT_READY" -or
    $manifest.approvedGuiResourceCount -ne @($approved.files).Count) {
    throw "REJECT: native release provenance does not match the approved Pages GUI"
}
$cmakeSource = Get-Content -LiteralPath "CMakeLists.txt" -Raw -Encoding UTF8
$nativeSource = Get-Content -LiteralPath "src/Main.cpp" -Raw -Encoding UTF8
if (-not $cmakeSource.Contains('juce_add_binary_data(ARMixAssets SOURCES') -or
    -not $cmakeSource.Contains('ARMIX_APPROVED_GUI_DIR') -or
    -not $nativeSource.Contains('resolveApprovedGuiResource (path, data, size, mime)') -or
    $nativeSource.Contains('BinaryData::consolenative_html') -or
    $nativeSource.Contains('BinaryData::index_html')) {
    throw "REJECT: competing legacy GUI renderer is compiled or served"
}
# PNG artwork is binary; CSS/JS/SVG/HTML are compared as exact UTF8 text,
# rather than mere marker matches. Each staged resource is also SHA-attested.
# Byte-identical GUI parity is verified against the DISTRIBUTED executable,
# not just source manifests. Python mmap.find is compiled/native and avoids
# the former PowerShell O(EXE_size * resource_count * resource_size) loop.
# Keep the PowerShell PE/header, icon and release provenance checks above.
& $approvedGuiPython scripts/verify_embedded_gui_resources.py --exe $exePath --bundle $approvedGuiDir --manifest $approvedPath
if ($LASTEXITCODE -ne 0) {
    throw "REJECT: APPROVED GUI BYTE RESOURCE NOT IN WINDOWS EXE"
}
# Human-readable startup markers are additional checks, never a substitute
# for the raw byte fingerprint of every HTML/CSS/JS/SVG/PNG resource.
$embeddedText = [Text.Encoding]::UTF8.GetString($bytes)
if ($embeddedText.IndexOf('APPROVED_PAGES_PREVIEW_BUTTONS', [StringComparison]::Ordinal) -lt 0 -or
    $embeddedText.IndexOf('M1_VISUAL_BUNDLE_ONLY__NOT_AUDIO_READY', [StringComparison]::Ordinal) -lt 0 -or
    $embeddedText.IndexOf('WEBVIEW2_GUI_READY_TIMEOUT', [StringComparison]::Ordinal) -lt 0 -or
    $embeddedText.IndexOf('AUDIO_GLITCH_WINDOW', [StringComparison]::Ordinal) -lt 0) {
    throw "REJECT: approved GUI bridge or native device safety markers absent"
}
Write-Host "APPROVED_GUI_EMBEDDED: /preview/ exact PNG skin and renderer resource hashes verified"
Write-Host "NOT_PRODUCT_READY: native accepted PCM/ACK + hardware/FAT remain separate release blockers"

# Probe a fresh compressed archive outside dist/ using the SAME dist folder
# that actions/upload-artifact will upload; reject the original 648KB class
# even when every source-level contract is green.
if (-not $env:RUNNER_TEMP) { throw "Missing RUNNER_TEMP for release ZIP audit" }
$archivePath = Join-Path $env:RUNNER_TEMP "arsonmix-release-audit-$PID.zip"
try {
    Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $archivePath -CompressionLevel Optimal -Force
    $archiveBytes = (Get-Item -LiteralPath $archivePath).Length
    if ($archiveBytes -lt 2MB) {
        throw "REJECT: hardware FAT ZIP unexpectedly small ($archiveBytes bytes)"
    }
    Add-Type -AssemblyName System.IO.Compression
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        # ZIP includes VST3 directories. Compare actual FILE entries to all
        # reviewed FactoryFX files; directory entries themselves are harmless.
        $expectedZipFiles = @($expected)
        $factoryZipFiles = @()
        if (Test-Path -LiteralPath $factoryPath -PathType Container) {
            $distRoot = (Resolve-Path -LiteralPath $OutputDir).Path
            foreach ($file in Get-ChildItem -LiteralPath $factoryPath -File -Recurse) {
                $rel = Get-ArSonMixZipRelativePath $distRoot $file.FullName
                $expectedZipFiles += $rel
                $factoryZipFiles += $rel
            }
        }
        Assert-ArSonMixZipFileSet $zip $expectedZipFiles
        $entry = $zip.GetEntry("ArSonMix-GUI10CH-P012-Windows-x64.exe")
        if ($null -eq $entry -or $entry.Length -ne $bytes.Length) {
            throw "Wrong/truncated GUI executable inside ZIP"
        }
        $stream = $entry.Open()
        $hashEngine = [Security.Cryptography.SHA256]::Create()
        try {
            $inZipHash = [BitConverter]::ToString($hashEngine.ComputeHash($stream)).Replace("-","").ToLowerInvariant()
        } finally {
            $hashEngine.Dispose()
            $stream.Dispose()
        }
        if ($inZipHash -ne $appHash) { throw "ZIP entry hash mismatch" }
        foreach ($filename in $fatTools.Keys) {
            $toolEntry = $zip.GetEntry($filename)
            if ($null -eq $toolEntry) { throw "FAT tool missing in verified ZIP: $filename" }
            $toolStream = $toolEntry.Open()
            $toolHasher = [Security.Cryptography.SHA256]::Create()
            try {
                $inZipToolHash = [BitConverter]::ToString($toolHasher.ComputeHash($toolStream)).Replace("-","").ToLowerInvariant()
            } finally {
                $toolStream.Dispose()
                $toolHasher.Dispose()
            }
            if ($inZipToolHash -ne $fatTools[$filename]) {
                throw "FAT tool ZIP payload digest differs: $filename"
            }
        }

        foreach ($rel in $factoryZipFiles) {
            $item = $zip.GetEntry($rel)
            if ($null -eq $item) { throw "FactoryFX file missing in ZIP: $rel" }
            $itemStream = $item.Open()
            $factoryHash = [Security.Cryptography.SHA256]::Create()
            try {
                $inside = [BitConverter]::ToString($factoryHash.ComputeHash($itemStream)).Replace("-", "").ToLowerInvariant()
            } finally {
                $itemStream.Dispose()
                $factoryHash.Dispose()
            }
            $outside = (Get-FileHash -LiteralPath (Join-Path $OutputDir $rel) -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($inside -ne $outside) { throw "FactoryFX ZIP file digest differs: $rel" }
        }
    } finally {
        $zip.Dispose()
    }
    Write-Host "FAT_RELEASE_ACCEPTED: source=$sha GUI_WIN64_PE32+=2"
    Write-Host "FAT_RELEASE_ACCEPTED: exeBytes=$($bytes.Length) testZipBytes=$archiveBytes sha256=$appHash"
    Write-Host "FAT_RESOURCE_ACCEPTED: approved Pages GUI HTML/CSS/JS/SVG/PNG are embedded exact bytes; native controls NOT YET QUALIFIED"
    Write-Host "FAT_RELEASE_ACCEPTED: P0-12 and static WebView2 startup contract verified"
} finally {
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
}
