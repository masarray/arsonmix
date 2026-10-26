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
    "README.md"
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
foreach ($item in @(
    @{Path="web/app.js"; Hash=$manifest.appJsSha256},
    @{Path="web/styles.css"; Hash=$manifest.stylesCssSha256}
)) {
    $real = (Get-FileHash -LiteralPath $item.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($real -ne $item.Hash) { throw "Compiled GUI source provenance mismatch: $($item.Path)" }
}
# These actual resource bytes must be in the Windows app, not just present
# as files in GitHub's source tree or asserted by an unrelated Node test.
$embeddedText = [Text.Encoding]::UTF8.GetString($bytes)
foreach ($marker in @(
    "function openChannelIdentity",   # latest mixer channel editor
    'class="master-analysis"',       # native-compatible master analyzer
    "--ari-master-cap",             # rose/silver MASTER styling
    "function syncNativeMixerControls", # JUCE WebView bridge
    "AUDIO_GLITCH_WINDOW"           # engine P0-12 compiled diagnostics
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
    Write-Host "FAT_RELEASE_ACCEPTED: embedded native GUI/bridge + engine P0-12 markers all verified"
} finally {
    if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
}
