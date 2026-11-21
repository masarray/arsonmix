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
 throw "IMMERSIB_AB_SOURCE_SHA_MISMATCH expected=$ExpectedSha actual=$actual"
}
$lines=@(Get-Content -LiteralPath (Join-Path $BuildDir "CMakeCache.txt") -Encoding UTF8)
foreach($flag in @("ARMIX_IMMERSIB_RELEASE_CANDIDATE","ARMIX_IMMERSIB_CONCERT_CANDIDATE","ARMIX_SONKU_APPROVED_P99_MAIN")){
 if ("$($flag):BOOL=ON" -notin $lines) {throw "IMMERSIB_AB_REQUIRED_FLAG_NOT_ON:$flag"}
}
$exe=Join-Path $BuildDir "ARMixConsole_artefacts/Release/AR MIX Studio.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or (Get-Item -LiteralPath $exe).Length -lt 4MB) {
 throw "IMMERSIB_AB_NOT_REAL_MIXER_GUI_EXE"
}
& python scripts/verify_windows_spatial_fx_packaging.py --build-dir $BuildDir --exe $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_AB_COMPILED_SPATIAL_CATALOG_NOT_PROVEN"}
& python scripts/verify_embedded_legal.py --binary $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_AB_LEGAL_NOT_EMBEDDED"}
& python scripts/verify_windows_pe_brand_icon.py --exe $exe --approved assets/icon/arsonmix-256.png
if($LASTEXITCODE -ne 0){throw "IMMERSIB_AB_BRAND_ICON_NOT_APPROVED"}
$guiDir=Join-Path $BuildDir "approved_gui"
& python scripts/build_approved_native_gui.py --out $guiDir --verify
if($LASTEXITCODE -ne 0){throw "IMMERSIB_AB_GUI_PROVENANCE_FAILED"}
$gui=Get-Content -LiteralPath (Join-Path $guiDir "approved-gui-manifest.json") -Raw | ConvertFrom-Json
if($gui.authoritativePagesRoute -ne "/preview/" -or $gui.authoritativePagesOutput -ne "preview/index.html") {
 throw "IMMERSIB_AB_NOT_APPROVED_GUI"
}
# Must be real 64-bit Windows GUI, not the passing console test executable.
$stream=[System.IO.File]::OpenRead((Resolve-Path -LiteralPath $exe).Path)
try {
 $reader=[System.IO.BinaryReader]::new($stream)
 if($reader.ReadUInt16() -ne 0x5a4d){throw "IMMERSIB_AB_NO_MZ"}
 $stream.Position=0x3c
 $off=$reader.ReadInt32()
 if($off -lt 64 -or $off -gt $stream.Length-128){throw "IMMERSIB_AB_BAD_PE_OFFSET"}
 $stream.Position=$off
 if($reader.ReadUInt32() -ne 0x4550){throw "IMMERSIB_AB_NO_PE"}
 if($reader.ReadUInt16() -ne 0x8664){throw "IMMERSIB_AB_NOT_AMD64"}
 $stream.Position=$off+24
 if($reader.ReadUInt16() -ne 0x020b){throw "IMMERSIB_AB_NOT_PE32PLUS"}
 $stream.Position=$off+24+68
 if($reader.ReadUInt16() -ne 2){throw "IMMERSIB_AB_NOT_WINDOWS_GUI"}
} finally {$stream.Dispose()}
if(Test-Path -LiteralPath $OutputDir){throw "IMMERSIB_AB_STALE_PACKAGE_DIR"}
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$name="ArSonMix-ImmerSIB-FUX1-Listening-Windows-x64.exe"
$dest=Join-Path $OutputDir $name
Copy-Item -LiteralPath $exe -Destination $dest
"ARSONMIX_PORTABLE_WORKSPACE_V1" | Set-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Encoding UTF8
Copy-Item -LiteralPath "docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md" -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")
Copy-Item -LiteralPath "installer/ArSonMixStartupDiagnostics.ps1" -Destination $OutputDir
@'
# ImmerSIB stereo + vocal (FUX-1) — EXPERIMENTAL LISTENING BUILD

Portable Windows x64 test build. NOT a stable installer, release, or hardware/ASIO certification.
Do not overwrite your installed ArSonMix application or production workspace.
**Backup your .smix / Studio session before first launch.**

Changes (only for ImmerSIB): P90 preserves correlated panned left/right
instruments; P98 restores 2–4 kHz SIDE in Detail/Impact; P99 reduces masking
of center LATE hall at ~1 and ~5 kHz. Existing bass, physical KEMAR sources,
room scenes, large 104–134 degree speaker spans, .smix schema and FUX-1 GUI remain.

A/B procedure: listen on headphones at matched loudness to the same finished
L/R stereo songs as your known-good alpha.26/FUX-1 installation. Compare:
1. Center vocal body 200–300 Hz, presence ~1 kHz, articulation ~5 kHz.
2. Panned guitars/keyboard left/right vs center drift.
3. Bass punch, full 3D hall, reverb tail, cymbals and sibilance.
4. Mono fold, loud vs soft music, several approved concert presets.
Stop the test if any regression, distortion or unsafe monitoring occurs.
Record preset, song characteristics and whether old/new is preferred.

Read BUILD_PROVENANCE.json to verify exact tested source SHA, flags, EXE SHA256.
This source intentionally fails the public alpha.26 acoustic-source approval
gate. Do not publish as an official stable ArSonMix release.
'@ | Set-Content -LiteralPath (Join-Path $OutputDir "README_LISTENING_TEST.md") -Encoding UTF8
$manifest=[ordered]@{
 schema="arsonmix.immersib-unapproved-listening.v1"
 sourceCommit=$actual
 pr=855
 approvedFuxMainCheckpoint="3e7e5df2cee60b561e520a468a1f08b4d185da07"
 releaseStatus="NOT_STABLE_NOT_AUDIO_APPROVED"
 stableSourceLock="8ec5b8a3e07837c85fb6e06c952cff1621fecc51"
 sourceLockRewritten=$false
 purpose="HEADPHONE_AB_ONLY"
 executable=$name
 exeBytes=(Get-Item -LiteralPath $dest).Length
 exeSha256=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
 pe="AMD64_PE32+_WINDOWS_GUI"
 flags=[ordered]@{
   ARMIX_IMMERSIB_RELEASE_CANDIDATE="ON"
   ARMIX_IMMERSIB_CONCERT_CANDIDATE="ON"
   ARMIX_SONKU_APPROVED_P99_MAIN="ON"
 }
 embeddedSceneAndProductVerifier="PASS"
 legalAndBrandVerifier="PASS"
 approvedGuiRoute=$gui.authoritativePagesRoute
 approvedGuiSha256=$gui.canonicalSourceSha256
 actualHeadphoneAB="NOT_TESTED"
 hardwareASIO="NOT_CERTIFIED"
}
$manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDir "BUILD_PROVENANCE.json") -Encoding UTF8
Write-Host "IMMERSIB_EXPERIMENTAL_PORTABLE_PASS source=$actual exeSHA256=$($manifest.exeSha256)"
Write-Host "NOT_A_PUBLIC_RELEASE_APPROVAL"
