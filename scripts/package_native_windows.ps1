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
$exeName = "ArSonMix-NativeConsole-CH11-EXPERIMENTAL-Windows-x64.exe"
$destination = Join-Path $OutputDir $exeName
Copy-Item -LiteralPath $app -Destination $destination -Force
$readme = "README.md"
$fatGuide = "docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md"
if (-not (Test-Path -LiteralPath $fatGuide -PathType Leaf)) {
    throw "Missing hardware FAT guide"
}
Copy-Item -LiteralPath $readme -Destination (Join-Path $OutputDir "README.md")
Copy-Item -LiteralPath $fatGuide -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")
# Same executable, same features. Portable ZIP selects the adjacent Studio/
# data root; installed apps without this file continue using legacy AppData.
"ARSONMIX_PORTABLE_WORKSPACE_V1" |
    Set-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Encoding utf8

$manifest = [ordered]@{
    schema = "arsonmix.native-artifact-provenance.v1"
    sourceCommit = $gitSha
    gui = "APPROVED SINGLE LIVE CONSOLE · NATIVE SNAPSHOT ONLY · UNIVERSAL FEATURES GATED"
    engine = "CHANNELID_CH11_NATIVE_PCM__LEGACY_10CH4AUX_COMPAT__HARDWARE_FAT_UNQUALIFIED"
    nativeDSP = "NATIVE_CHANNELID_CH11_DYNAMIC_INPUT__10CH4AUX_COMPAT_FALLBACK__NOT_FULL_UNIVERSAL"
    storageMode = "PORTABLE_MARKER__USER_STUDIO_EMPTY_AT_RELEASE"
    guiExeBytes = $appBytes
    guiExeMinimumBytes = $minimumGuiExeBytes
    runtimeDependency = "MICROSOFT_EDGE_WEBVIEW2_RUNTIME_REQUIRED_ON_HARDWARE"
    webView2LoaderMode = "JUCE_WEBVIEW2_LOADER_STATIC_LINKED_V1"
    guiStartupVerification = "WEBVIEW2_PREFLIGHT_AND_AR_MIX_GUI_READY_HANDSHAKE"
    appTarget = "ARMixConsole"
    expectedTargetPath = $appRelativePath
    executable = $exeName
    peMachine = "AMD64 (0x8664)"
    peFormat = "PE32+"
    peSubsystem = "WINDOWS_GUI (2)"
    exeSha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    # Production runtime is the approved Console; legacy index is no longer the WebView2 root.
    consoleNativeHtmlSha256 = (Get-FileHash -LiteralPath "web/console-native.html" -Algorithm SHA256).Hash.ToLowerInvariant()
    consoleNativeCssSha256 = (Get-FileHash -LiteralPath "web/console-native.css" -Algorithm SHA256).Hash.ToLowerInvariant()
    consoleNativeJsSha256 = (Get-FileHash -LiteralPath "web/console-native.js" -Algorithm SHA256).Hash.ToLowerInvariant()
    activeGuiEntrypoint = "web/console-native.html__JUCE_BINARYDATA_ROOT"
    guiSource = "web/console-native.html + web/console-native.css + web/console-native.js (JUCE BinaryData)"
    nativeConsoleOnly = $true
    retiredUiEmbedded = $false
    guiCalibration = "GAIN_10_TO_MINUS60_BLACK_DETENT_AT_38PX__VU_DBFS_INDEPENDENT_TICKS"
    fatStatus = "EXPERIMENTAL_NOT_HARDWARE_QUALIFIED"
    note = "No assertion of clean 0-FX sound, measured round-trip latency or native render-hardware XRUN."
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $OutputDir "BUILD_PROVENANCE.json") -Encoding utf8
Write-Host "Verified JUCE Windows GUI executable (not a test binary): $app"
Write-Host "Package source commit: $gitSha"
Write-Host "App SHA256: $($manifest.exeSha256)"
