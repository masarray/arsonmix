# SPDX-License-Identifier: AGPL-3.0-only
# Independent post-package acceptance. An EXE build and 24 CTests can pass
# while upload-artifact delivers a WRONG console executable: verify the
# DISTRIBUTION, its actual GUI resources and its compressed ZIP integrity.
param([string] $OutputDir = "dist")
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expected = @(
    "ArSonMix-M3-NativeConsole-ENGINEERING-NOT-HARDWARE-QUALIFIED-Windows-x64.exe",
    "BUILD_PROVENANCE.json",
    "HARDWARE_FAT_GUIDE.md",
    "README.md",
    "portable_mode.txt"
)
foreach ($name in $expected) {
    if (-not (Test-Path -LiteralPath (Join-Path $OutputDir $name) -PathType Leaf)) {
        throw "Distribution is missing required file: $name"
    }
}
# Verify FactoryFX only when an exact reviewed lock explicitly authorizes it.
# App-only engineering ZIPs remain unchanged; a missing approved module fails.
& python scripts/factoryfx_release_bundle.py verify --lock config/factoryfx-approved-releases.json --output $OutputDir
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
# App-only ZIP does not ship an untracked dynamic WebView2Loader.dll.
if ($manifest.webView2LoaderMode -ne "JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1" -or
    $manifest.guiStartupVerification -ne "WEBVIEW2_PREFLIGHT_AND_AR_MIX_GUI_READY_HANDSHAKE") {
    throw "Missing Windows WebView2 startup / static loader provenance"
}
if ($manifest.nativeDSP -ne "REAL_M4_10CH4AUX__UNIVERSAL_CH11_VST3_BUS_FX_NOT_READY") {
    throw "Engine provenance is not truthful about universal ChannelId readiness"
}
if ($manifest.guiAuthority -ne "APPROVED_M3_NATIVE_CONSOLE_V1" -or
    $manifest.approvedVisualSource -notmatch "aba0e2879fbc6cbfe42be42197aa249accb08abf") {
    throw "WRONG GUI: release requires the user-approved M3 operator Console"
}
if ($manifest.storageMode -ne "PORTABLE_MARKER__USER_STUDIO_EMPTY_AT_RELEASE") {
    throw "Public ZIP missing portable workspace mode provenance"
}
$portableMarker = (Get-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Raw -Encoding UTF8).Trim()
if ($portableMarker -ne "ARSONMIX_PORTABLE_WORKSPACE_V1") { throw "Invalid portable marker" }
if (Test-Path -LiteralPath (Join-Path $OutputDir "Studio")) {
    throw "Public artifact MUST NOT bundle user-specific plugins, presets or sessions"
}
$exePath = Join-Path $OutputDir "ArSonMix-M3-NativeConsole-ENGINEERING-NOT-HARDWARE-QUALIFIED-Windows-x64.exe"
$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $exePath).Path)
if ($bytes.Length -lt 4MB) { throw "REJECT: suspiciously small executable ($($bytes.Length) bytes)" }
if ($bytes.Length -ne $manifest.guiExeBytes) { throw "Mismatch: actual EXE size and recorded size" }
$pe = [BitConverter]::ToInt32($bytes,0x3c)
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
# Verify the exact full resource text shipped by JUCE BinaryData, not merely
# hashes recorded next to an unrelated executable. This catches packaging an
# older GUI with the latest source files and avoids brittle marker-only checks.
$resources = $manifest.guiResources
if ($null -eq $resources -or $resources.PSObject.Properties.Count -ne 26) {
    throw "WRONG GUI: missing complete M3 Console resource manifest"
}
$embeddedText = [Text.Encoding]::UTF8.GetString($bytes)
foreach ($resource in $resources.PSObject.Properties) {
    $sourcePath = $resource.Name
    $expectedHash = [string]$resource.Value
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Missing source asset in approved Console: $sourcePath"
    }
    $actualHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "GUI provenance SHA256 mismatch: $sourcePath"
    }
    # Exact embedded text for every HTML/JS/CSS/SVG resource. PNG binary
    # bytes are separately hashed at source and included in JUCE BinaryData;
    # do not decode binary images into a lossy UTF8 string.
    if ($sourcePath -match '\.(html|js|css|svg)$') {
        $sourceText = [System.IO.File]::ReadAllText(
            (Resolve-Path -LiteralPath $sourcePath).Path, [Text.Encoding]::UTF8)
        if ($sourceText.Length -eq 0 -or
            $embeddedText.IndexOf($sourceText, [StringComparison]::Ordinal) -lt 0) {
            throw "STALE NATIVE GUI: approved JUCE BinaryData resource absent from EXE: $sourcePath"
        }
    }
}
$html = Get-Content -LiteralPath "web/index.html" -Raw -Encoding UTF8
$cmake = Get-Content -LiteralPath "CMakeLists.txt" -Raw -Encoding UTF8
if ($html -match 'data-view="FX RACK"' -or
    $html -notmatch 'id="nativeAuthority"' -or
    $html -notmatch 'src="console-native.js"' -or
    $cmake -match 'web/app\.js' -or
    $cmake -match 'web/bridge\.js') {
    throw "WRONG GUI: the abandoned ten-strip frontend was packaged"
}
# Product identity is verified against actual compiled source bytes above.
if ($manifest.guiCalibration -ne "APPROVED_M3_DENSE_FADER_METER_WIRE_NATIVE_V1") {
    throw "Latest approved M3 channel hardware calibration provenance missing"
}
foreach ($marker in @("ArSonMixNativeModel", "projectNative(s)", 
    "arMixConsoleSnapshot", "arMixConsoleIntent", "arMixGuiReady",
    "id=`"nativeAuthority`"", "console-native.js", "AUDIO_GLITCH_WINDOW",
    "WEBVIEW2_GUI_READY_TIMEOUT")) {
    if ($embeddedText.IndexOf($marker, [StringComparison]::Ordinal) -lt 0) {
        throw "Compiled GUI/engine marker absent from Windows EXE: $marker"
    }
}
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
        $names = @($zip.Entries | ForEach-Object FullName)
        # ZIP includes VST3 directories. Compare actual FILE entries to all
        # reviewed FactoryFX files; directory entries themselves are harmless.
        $expectedZipFiles = @($expected)
        $factoryZipFiles = @()
        if (Test-Path -LiteralPath $factoryPath -PathType Container) {
            $distRoot = (Resolve-Path -LiteralPath $OutputDir).Path
            foreach ($file in Get-ChildItem -LiteralPath $factoryPath -File -Recurse) {
                $rel = [IO.Path]::GetRelativePath($distRoot, $file.FullName).Replace('\\', '/')
                $expectedZipFiles += $rel
                $factoryZipFiles += $rel
            }
        }
        $names = @($zip.Entries | Where-Object { -not $_.FullName.EndsWith('/') } |
            ForEach-Object { $_.FullName.Replace('\\', '/') })
        if (@(Compare-Object -ReferenceObject $expectedZipFiles -DifferenceObject $names).Count -ne 0) {
            throw "ZIP does not contain exactly the verified distribution files"
        }
        $entry = $zip.GetEntry("ArSonMix-M3-NativeConsole-ENGINEERING-NOT-HARDWARE-QUALIFIED-Windows-x64.exe")
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
    Write-Host "FAT_RELEASE_ACCEPTED: actual approved M3 HTML/native adapter/24 visual assets + native runtime provenance verified"
    Write-Host "FAT_RELEASE_ACCEPTED: P0-12 and static WebView2 startup contract verified"
} finally {
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
}
