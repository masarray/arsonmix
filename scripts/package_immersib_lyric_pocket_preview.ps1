# SPDX-License-Identifier: AGPL-3.0-only
# Strict, separately branded A/B listening preview. Never a stable release
# package, and never a relaxation of alpha.26 source/provenance controls.
param(
 [string]$BuildDir="build",
 [string]$OutputDir="immersib_listening_preview",
 [Parameter(Mandatory=$true)][string]$ExpectedSha
)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$actual=(& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $ExpectedSha -notmatch '^[0-9a-f]{40}$' -or $actual -ne $ExpectedSha) {
 throw "IMMERSIB_P100_SOURCE_SHA_MISMATCH expected=$ExpectedSha actual=$actual"
}
$lines=@(Get-Content -LiteralPath (Join-Path $BuildDir "CMakeCache.txt") -Encoding UTF8)
foreach($flag in @("ARMIX_IMMERSIB_RELEASE_CANDIDATE","ARMIX_IMMERSIB_LYRIC_POCKET_RESEARCH","ARMIX_SONKU_APPROVED_P99_MAIN")){
 if ("$($flag):BOOL=ON" -notin $lines) {throw "IMMERSIB_P100_REQUIRED_FLAG_NOT_ON:$flag"}
}
$exe=Join-Path $BuildDir "ARMixConsole_artefacts/Release/AR MIX Studio.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or (Get-Item -LiteralPath $exe).Length -lt 4MB) {
 throw "IMMERSIB_P100_NOT_REAL_MIXER_GUI_EXE"
}
& python scripts/verify_windows_spatial_fx_packaging.py --build-dir $BuildDir --exe $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_P100_COMPILED_SPATIAL_CATALOG_NOT_PROVEN"}
& python scripts/verify_embedded_legal.py --binary $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_P100_LEGAL_NOT_EMBEDDED"}
& python scripts/verify_windows_pe_brand_icon.py --exe $exe --approved assets/icon/arsonmix-256.png
if($LASTEXITCODE -ne 0){throw "IMMERSIB_P100_BRAND_ICON_NOT_APPROVED"}
$guiDir=Join-Path $BuildDir "approved_gui"
. (Join-Path "scripts" "resolve_approved_gui_python.ps1")
$approvedGuiPython = Resolve-ArSonMixApprovedGuiPython -BuildDir $BuildDir
& $approvedGuiPython scripts/build_approved_native_gui.py --out $guiDir --verify
if($LASTEXITCODE -ne 0){throw "IMMERSIB_P100_GUI_PROVENANCE_FAILED"}
$gui=Get-Content -LiteralPath (Join-Path $guiDir "approved-gui-manifest.json") -Raw | ConvertFrom-Json
if($gui.authoritativePagesRoute -ne "/preview/" -or $gui.authoritativePagesOutput -ne "preview/index.html") {
 throw "IMMERSIB_P100_NOT_APPROVED_GUI"
}
# Must be real 64-bit Windows GUI, not the passing console test executable.
$stream=[System.IO.File]::OpenRead((Resolve-Path -LiteralPath $exe).Path)
try {
 $reader=[System.IO.BinaryReader]::new($stream)
 if($reader.ReadUInt16() -ne 0x5a4d){throw "IMMERSIB_P100_NO_MZ"}
 $stream.Position=0x3c
 $off=$reader.ReadInt32()
 if($off -lt 64 -or $off -gt $stream.Length-128){throw "IMMERSIB_P100_BAD_PE_OFFSET"}
 $stream.Position=$off
 if($reader.ReadUInt32() -ne 0x4550){throw "IMMERSIB_P100_NO_PE"}
 if($reader.ReadUInt16() -ne 0x8664){throw "IMMERSIB_P100_NOT_AMD64"}
 $stream.Position=$off+24
 if($reader.ReadUInt16() -ne 0x020b){throw "IMMERSIB_P100_NOT_PE32PLUS"}
 $stream.Position=$off+24+68
 if($reader.ReadUInt16() -ne 2){throw "IMMERSIB_P100_NOT_WINDOWS_GUI"}
} finally {$stream.Dispose()}
if(Test-Path -LiteralPath $OutputDir){throw "IMMERSIB_P100_STALE_PACKAGE_DIR"}
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$name="ArSonMix-ImmerSIB-P100-Lyric-Pocket-Windows-x64.exe"
$dest=Join-Path $OutputDir $name
Copy-Item -LiteralPath $exe -Destination $dest
"ARSONMIX_PORTABLE_WORKSPACE_V1" | Set-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Encoding UTF8
Copy-Item -LiteralPath "docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md" -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")
Copy-Item -LiteralPath "installer/ArSonMixStartupDiagnostics.ps1" -Destination $OutputDir
@'
# ImmerSIB P100 — Lyric Front Pocket
Unapproved headphone A/B, not a release. Extract to a NEW directory and back up sessions.
Based on main 527e0216, not the separate #858 fixed-stage experiment.
The sole added audio operation is a fixed broad -2 dB peak reduction at 1.8 kHz
on the existing ImmerSIB diffuse FDN return MID. The SIDE difference is invariant
at this stage. Direct HRIR, early images, original P99, bass and scene presets unchanged.
The existing linked makeup and limiter still operate; output is not claimed bit identical.
Compare against alpha.31 with the same session, source, scene and loudness.
First disable the diagnostic external EQ in BOTH versions; then compare your preferred EQ.
Listen for lyric transcription, warmth, stage depth and fatigue. Do not use for live production.
'@ | Set-Content -LiteralPath (Join-Path $OutputDir "README_LISTENING_TEST.md") -Encoding UTF8
$manifest=[ordered]@{
 schema="arsonmix.immersib-unapproved-lyric-pocket.v1"
 sourceCommit=$actual
 parentMain="527e0216d226904288c4fe2dd8a76169dfb4417d"
 candidateAudioRevision="p100-static-wet-mid-1800hz-q0.60-minus2db"
 sourceLockRewritten=$false
 status="UNAPPROVED_HEADPHONE_AB_ONLY"
 executable=$name
 exeBytes=(Get-Item -LiteralPath $dest).Length
 exeSha256=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
 pe="AMD64_PE32+_WINDOWS_GUI"
 cmakeFlags=[ordered]@{
  ARMIX_IMMERSIB_RELEASE_CANDIDATE="ON"
  ARMIX_IMMERSIB_LYRIC_POCKET_RESEARCH="ON"
  ARMIX_SONKU_APPROVED_P99_MAIN="ON"
 }
 legalAndBrandVerifier="PASS"
 approvedGuiRoute=$gui.authoritativePagesRoute
 approvedGuiSha256=$gui.canonicalSourceSha256
 headphoneAB="NOT_TESTED"
 ASIOFAT="NOT_TESTED"
}
$manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDir "BUILD_PROVENANCE.json") -Encoding UTF8
Write-Host "IMMERSIB_P100_EXPERIMENTAL_PORTABLE_PASS source=$actual exeSHA256=$($manifest.exeSha256)"
Write-Host "NOT_A_PUBLIC_RELEASE_APPROVAL"
