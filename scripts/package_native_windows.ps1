# SPDX-License-Identifier: AGPL-3.0-only
# Windows CI distribution boundary: ONLY the actual JUCE GUI application,
# never a random ARMix* test executable returned by filesystem traversal.
param(
    [string] $BuildDir = "build",
    [string] $OutputDir = "dist"
)
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# JUCE's PRODUCT_NAME is set in CMakeLists.txt. Fail closed if its build path
# changes rather than silently packaging an ASIO/CTest console application.
$appRelativePath = "ARMixConsole_artefacts/Release/AR MIX Studio.exe"
$app = Join-Path $BuildDir $appRelativePath
if (-not (Test-Path -LiteralPath $app -PathType Leaf)) {
    throw "Missing JUCE GUI executable at expected target: $app"
}
# An earlier 648KB artifact was a test console binary, not the mixer.
# Require a realistic lower bound in addition to exact path and PE subtype:
# small/missing embedded assets or a wrong target must fail the release.
$minimumGuiExeBytes = 4MB
$appBytes = (Get-Item -LiteralPath $app).Length
if ($appBytes -lt $minimumGuiExeBytes) {
    throw "GUI EXE too small ($appBytes bytes). Expected >= $minimumGuiExeBytes bytes with embedded JUCE/WebView UI."
}

# Structural PE check: AMD64 + PE32+ + Windows GUI subsystem (2).
# Our previous wildcard packaging accidentally shipped a valid x64 CONSOLE
# test binary (subsystem 3). File names, build success and CTest alone are
# NOT sufficient proof of the distributable executable's target identity.
$stream = [System.IO.File]::OpenRead((Resolve-Path -LiteralPath $app).Path)
try {
    if ($stream.Length -lt 512) { throw "Truncated Windows executable" }
    $reader = [System.IO.BinaryReader]::new($stream)
    if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a DOS/PE executable" }
    $stream.Position = 0x3c
    $peOffset = $reader.ReadInt32()
    if ($peOffset -lt 64 -or $peOffset -gt $stream.Length - 128) {
        throw "Invalid PE header offset"
    }
    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature" }
    $machine = $reader.ReadUInt16()
    if ($machine -ne 0x8664) { throw "Expected AMD64 executable, machine=0x$($machine.ToString('x'))" }
    $stream.Position = $peOffset + 24
    $magic = $reader.ReadUInt16()
    if ($magic -ne 0x020b) { throw "Expected PE32+ optional header" }
    $stream.Position = $peOffset + 24 + 68
    $subsystem = $reader.ReadUInt16()
    if ($subsystem -ne 2) {
        throw "Expected Windows GUI subsystem 2, got $subsystem (a test console EXE is NOT the app)"
    }
} finally {
    $stream.Dispose()
}

$gitSha = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $gitSha -notmatch '^[0-9a-f]{40}$') {
    throw "Cannot attest packaged source Git commit"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$exeName = "ArSonMix-GUI10CH-P012-Windows-x64.exe"
$destination = Join-Path $OutputDir $exeName
Copy-Item -LiteralPath $app -Destination $destination -Force
$readme = "README.md"
$fatGuide = "docs/HARDWARE_FAT_GUI10_ENGINE_P012_20261027.md"
if (-not (Test-Path -LiteralPath $fatGuide -PathType Leaf)) {
    throw "Missing hardware FAT guide"
}
Copy-Item -LiteralPath $readme -Destination (Join-Path $OutputDir "README.md")
Copy-Item -LiteralPath $fatGuide -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")

$manifest = [ordered]@{
    schema = "arsonmix.native-artifact-provenance.v1"
    sourceCommit = $gitSha
    gui = "10 SOURCE CHANNELS + STEREO MASTER (PR #73)"
    engine = "P0-12 BOUNDED PRE-MARK HARDWARE EVIDENCE"
    nativeDSP = "CHANNEL_1_ONLY__REMAINING_9_GUI_CHANNELS_NOT_REAL_AUDIO"
    guiExeBytes = $appBytes
    guiExeMinimumBytes = $minimumGuiExeBytes
    runtimeDependency = "MICROSOFT_EDGE_WEBVIEW2_RUNTIME_REQUIRED_ON_HARDWARE"
    appTarget = "ARMixConsole"
    expectedTargetPath = $appRelativePath
    executable = $exeName
    peMachine = "AMD64 (0x8664)"
    peFormat = "PE32+"
    peSubsystem = "WINDOWS_GUI (2)"
    exeSha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    indexHtmlSha256 = (Get-FileHash -LiteralPath "web/index.html" -Algorithm SHA256).Hash.ToLowerInvariant()
    appJsSha256 = (Get-FileHash -LiteralPath "web/app.js" -Algorithm SHA256).Hash.ToLowerInvariant()
    stylesCssSha256 = (Get-FileHash -LiteralPath "web/styles.css" -Algorithm SHA256).Hash.ToLowerInvariant()
    bridgeJsSha256 = (Get-FileHash -LiteralPath "web/bridge.js" -Algorithm SHA256).Hash.ToLowerInvariant()
    monitorJsSha256 = (Get-FileHash -LiteralPath "web/monitor.js" -Algorithm SHA256).Hash.ToLowerInvariant()
    guiSource = "web/index.html + web/app.js + web/styles.css + web/bridge.js + web/monitor.js (JUCE BinaryData)"
    guiCalibration = "GAIN_10_TO_MINUS60_BLACK_DETENT_AT_38PX__VU_DBFS_INDEPENDENT_TICKS"
    fatStatus = "EXPERIMENTAL_NOT_HARDWARE_QUALIFIED"
    note = "No assertion of clean 0-FX sound, measured round-trip latency or native render-hardware XRUN."
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $OutputDir "BUILD_PROVENANCE.json") -Encoding utf8
Write-Host "Verified JUCE Windows GUI executable (not a test binary): $app"
Write-Host "Package source commit: $gitSha"
Write-Host "App SHA256: $($manifest.exeSha256)"
