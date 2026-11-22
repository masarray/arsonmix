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
 throw "IMMERSIB_STAGE_SOURCE_SHA_MISMATCH expected=$ExpectedSha actual=$actual"
}
$lines=@(Get-Content -LiteralPath (Join-Path $BuildDir "CMakeCache.txt") -Encoding UTF8)
foreach($flag in @("ARMIX_IMMERSIB_RELEASE_CANDIDATE","ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH","ARMIX_SONKU_APPROVED_P99_MAIN")){
 if ("$($flag):BOOL=ON" -notin $lines) {throw "IMMERSIB_STAGE_REQUIRED_FLAG_NOT_ON:$flag"}
}
$exe=Join-Path $BuildDir "ARMixConsole_artefacts/Release/AR MIX Studio.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or (Get-Item -LiteralPath $exe).Length -lt 4MB) {
 throw "IMMERSIB_STAGE_NOT_REAL_MIXER_GUI_EXE"
}
& python scripts/verify_windows_spatial_fx_packaging.py --build-dir $BuildDir --exe $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_STAGE_COMPILED_SPATIAL_CATALOG_NOT_PROVEN"}
& python scripts/verify_embedded_legal.py --binary $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_STAGE_LEGAL_NOT_EMBEDDED"}
& python scripts/verify_windows_pe_brand_icon.py --exe $exe --approved assets/icon/arsonmix-256.png
if($LASTEXITCODE -ne 0){throw "IMMERSIB_STAGE_BRAND_ICON_NOT_APPROVED"}
$guiDir=Join-Path $BuildDir "approved_gui"
. (Join-Path "scripts" "resolve_approved_gui_python.ps1")
$approvedGuiPython = Resolve-ArSonMixApprovedGuiPython -BuildDir $BuildDir
& $approvedGuiPython scripts/build_approved_native_gui.py --out $guiDir --verify
if($LASTEXITCODE -ne 0){throw "IMMERSIB_STAGE_GUI_PROVENANCE_FAILED"}
$gui=Get-Content -LiteralPath (Join-Path $guiDir "approved-gui-manifest.json") -Raw | ConvertFrom-Json
if($gui.authoritativePagesRoute -ne "/preview/" -or $gui.authoritativePagesOutput -ne "preview/index.html") {
 throw "IMMERSIB_STAGE_NOT_APPROVED_GUI"
}
# Must be real 64-bit Windows GUI, not the passing console test executable.
$stream=[System.IO.File]::OpenRead((Resolve-Path -LiteralPath $exe).Path)
try {
 $reader=[System.IO.BinaryReader]::new($stream)
 if($reader.ReadUInt16() -ne 0x5a4d){throw "IMMERSIB_STAGE_NO_MZ"}
 $stream.Position=0x3c
 $off=$reader.ReadInt32()
 if($off -lt 64 -or $off -gt $stream.Length-128){throw "IMMERSIB_STAGE_BAD_PE_OFFSET"}
 $stream.Position=$off
 if($reader.ReadUInt32() -ne 0x4550){throw "IMMERSIB_STAGE_NO_PE"}
 if($reader.ReadUInt16() -ne 0x8664){throw "IMMERSIB_STAGE_NOT_AMD64"}
 $stream.Position=$off+24
 if($reader.ReadUInt16() -ne 0x020b){throw "IMMERSIB_STAGE_NOT_PE32PLUS"}
 $stream.Position=$off+24+68
 if($reader.ReadUInt16() -ne 2){throw "IMMERSIB_STAGE_NOT_WINDOWS_GUI"}
} finally {$stream.Dispose()}
if(Test-Path -LiteralPath $OutputDir){throw "IMMERSIB_STAGE_STALE_PACKAGE_DIR"}
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$name="ArSonMix-ImmerSIB-Fixed-Speaker-Stage-Research-Windows-x64.exe"
$dest=Join-Path $OutputDir $name
Copy-Item -LiteralPath $exe -Destination $dest
"ARSONMIX_PORTABLE_WORKSPACE_V1" | Set-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Encoding UTF8
Copy-Item -LiteralPath "docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md" -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")
Copy-Item -LiteralPath "installer/ArSonMixStartupDiagnostics.ps1" -Destination $OutputDir
@'
# ArSonMix ImmerSIB — fixed concert PA stage, NO spectral masking

Experimental Windows x64 headphone A/B, NOT a release or installer.
Extract in a NEW directory; back up .smix first. Do not overwrite live software.

This is a regression-repair candidate rebased on published main, not on v7:
- The original physical L/R stage speakers and measured KEMAR HRIRs,
  with ONE fixed 0.58 phantom-center interpolation: vocal stays forward,
  but MUSIC correlation cannot dynamically change direct HRIR kernels.
- Disable warm scene P98 MID 2.05k boost, SIDE 2.05k/3.45k subtraction,
  post-ear 3.45k and SIDE air reshaping, rather than stack more notches.
- Both original source channels get ~3dB 285Hz chest warmth and small
  stationary 1.1kHz vocal detail, independently. Hard-left source stays
  hard-left before the virtual stage.
- Disable ImmerSIB-only P99 attentional warm/late room processing.
- Existing KEMAR direct HRTF calibration reuses the prepared
  2.2k bell with bounded +3dB direct-only relief. Existing P98 EAR
  filter uses a shared stationary 3.45kHz de-harsh (~2dB at band center).
- Original bass, directional first reflections, late hall, 7 scene
  settings, JUCE GUI /preview/, mixer PCM, ASIO and FX state remain.
- Alpha.26/release defaults are unchanged; experimental CMake opt-in only.

A/B at matched perceived loudness: native release vs this candidate.
Listen quietly and stop if any midrange ringing or fatigue occurs.
Check centered clear vocals, panned guitars, bass weight, roomy depth,
intelligibility and transients on several mastered stereo songs.
No automated test certifies natural timbre or headphone externalization.
'@ | Set-Content -LiteralPath (Join-Path $OutputDir "README_LISTENING_TEST.md") -Encoding UTF8
$manifest=[ordered]@{
 schema="arsonmix.immersib-unapproved-fixed-stage.v1"
 sourceCommit=$actual
 parentMain="527e0216d226904288c4fe2dd8a76169dfb4417d"
 candidateAudioRevision="fixed-0.58-front-HRIR-warm285-detail1100-fixed-ear3450-no-P98-side-ducking"
 sourceLockRewritten=$false
 status="UNAPPROVED_HEADPHONE_AB_ONLY"
 executable=$name
 exeBytes=(Get-Item -LiteralPath $dest).Length
 exeSha256=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
 pe="AMD64_PE32+_WINDOWS_GUI"
 cmakeFlags=[ordered]@{
  ARMIX_IMMERSIB_RELEASE_CANDIDATE="ON"
  ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH="ON"
  ARMIX_SONKU_APPROVED_P99_MAIN="ON"
 }
 legalAndBrandVerifier="PASS"
 approvedGuiRoute=$gui.authoritativePagesRoute
 approvedGuiSha256=$gui.canonicalSourceSha256
 headphoneAB="NOT_TESTED"
 ASIOFAT="NOT_TESTED"
}
$manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDir "BUILD_PROVENANCE.json") -Encoding UTF8
Write-Host "IMMERSIB_STAGE_EXPERIMENTAL_PORTABLE_PASS source=$actual exeSHA256=$($manifest.exeSha256)"
Write-Host "NOT_A_PUBLIC_RELEASE_APPROVAL"
