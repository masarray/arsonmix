# SPDX-License-Identifier: AGPL-3.0-only
# Independent post-package acceptance. An EXE build and 24 CTests can pass
# while upload-artifact delivers a WRONG console executable: verify the
# DISTRIBUTION, its actual GUI resources and its compressed ZIP integrity.
param([string] $OutputDir = "dist")
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expected = @(
    "ArSonMix-GUI10CH-P012-Windows-x64.exe",
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
if ($manifest.storageMode -ne "PORTABLE_MARKER__USER_STUDIO_EMPTY_AT_RELEASE") {
    throw "Public ZIP missing portable workspace mode provenance"
}
$portableMarker = (Get-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Raw -Encoding UTF8).Trim()
if ($portableMarker -ne "ARSONMIX_PORTABLE_WORKSPACE_V1") { throw "Invalid portable marker" }
if (Test-Path -LiteralPath (Join-Path $OutputDir "Studio")) {
    throw "Public artifact MUST NOT bundle user-specific plugins, presets or sessions"
}
$exePath = Join-Path $OutputDir "ArSonMix-GUI10CH-P012-Windows-x64.exe"
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
$resources = @(
    @{Path="web/index.html"; Hash=$manifest.indexHtmlSha256},
    @{Path="web/app.js"; Hash=$manifest.appJsSha256},
    @{Path="web/styles.css"; Hash=$manifest.stylesCssSha256},
    @{Path="web/bridge.js"; Hash=$manifest.bridgeJsSha256},
    @{Path="web/monitor.js"; Hash=$manifest.monitorJsSha256}
)
$embeddedText = [Text.Encoding]::UTF8.GetString($bytes)
foreach ($item in $resources) {
    $real = (Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($real -ne $item.Hash) {
        throw "GUI provenance SHA256 mismatch: $($item.Path)"
    }
    $sourceText = [System.IO.File]::ReadAllText(
        (Resolve-Path -LiteralPath $item.Path).Path, [Text.Encoding]::UTF8)
    if ($sourceText.Length -eq 0 -or
        $embeddedText.IndexOf($sourceText, [StringComparison]::Ordinal) -lt 0) {
        throw "STALE NATIVE GUI: exact JUCE BinaryData resource absent from EXE: $($item.Path)"
    }
}
# Assert current approved calibration is present inside the actual shipped binary.
if ($manifest.guiCalibration -ne "GAIN_10_TO_MINUS60_BLACK_DETENT_AT_38PX__VU_DBFS_INDEPENDENT_TICKS") {
    throw "GUI calibration release contract is missing"
}
# Marker checks below supplement, not replace, full resource byte correspondence.
foreach ($marker in @(
    "function openChannelIdentity",   # latest mixer channel editor
    'class="master-analysis"',       # native-compatible master analyzer
    "--ari-master-cap",             # rose/silver MASTER styling
    "gain-scale-travel",            # calibrated +10..-60 dB scale
    "vu-scale-travel",              # independent dBFS meter ruler
    "meter-signal-area",            # VU activity region distinct from CLIP LED
    "fader-travel",                 # PNG black detent in 38px inset travel
    "function syncNativeMixerControls", # JUCE WebView bridge
    "arMixGuiReady",               # Native GUI JS ready handshake
    "WEBVIEW2_GUI_READY_TIMEOUT",  # Visible failure, never a blank IE page
    "AUDIO_GLITCH_WINDOW",          # engine P0-12 compiled diagnostics
    "--b180-on-mute",              # current harmonized BTN-180 palette
    "--b180-on-solo",              # MUTE / SOLO lightness calibration
    "--b180-on-stereo",            # STEREO blue lightness calibration
    "--b180-on-green",             # native device-connected green
    "inset 5px 5px 2px -1px",      # owner-approved hardware button shadow
    "gain-scale-travel",           # calibrated fader detent
    "function syncNativeMixerControls" # latest native GUI state bridge
)) {
    if ($embeddedText.IndexOf($marker, [StringComparison]::Ordinal) -lt 0) {
        throw "Compiled GUI/engine marker absent from shipped EXE: $marker"
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
        if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $names).Count -ne 0) {
            throw "ZIP does not contain exactly the verified distribution files"
        }
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
    } finally {
        $zip.Dispose()
    }
    Write-Host "FAT_RELEASE_ACCEPTED: source=$sha GUI_WIN64_PE32+=2"
    Write-Host "FAT_RELEASE_ACCEPTED: exeBytes=$($bytes.Length) testZipBytes=$archiveBytes sha256=$appHash"
    Write-Host "FAT_RELEASE_ACCEPTED: full embedded GUI HTML/CSS/JS/bridge/monitor + calibrated VU/fader matched exactly"
    Write-Host "FAT_RELEASE_ACCEPTED: P0-12 and static WebView2 startup contract verified"
} finally {
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
}
