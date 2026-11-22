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
 throw "IMMERSIB_LYRIC_SOURCE_SHA_MISMATCH expected=$ExpectedSha actual=$actual"
}
$lines=@(Get-Content -LiteralPath (Join-Path $BuildDir "CMakeCache.txt") -Encoding UTF8)
foreach($flag in @("ARMIX_IMMERSIB_RELEASE_CANDIDATE","ARMIX_IMMERSIB_LYRIC_MONITOR_RESEARCH","ARMIX_SONKU_APPROVED_P99_MAIN")){
 if ("$($flag):BOOL=ON" -notin $lines) {throw "IMMERSIB_LYRIC_REQUIRED_FLAG_NOT_ON:$flag"}
}
$exe=Join-Path $BuildDir "ARMixConsole_artefacts/Release/AR MIX Studio.exe"
if (!(Test-Path -LiteralPath $exe -PathType Leaf) -or (Get-Item -LiteralPath $exe).Length -lt 4MB) {
 throw "IMMERSIB_LYRIC_NOT_REAL_MIXER_GUI_EXE"
}
& python scripts/verify_windows_spatial_fx_packaging.py --build-dir $BuildDir --exe $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_LYRIC_COMPILED_SPATIAL_CATALOG_NOT_PROVEN"}
& python scripts/verify_embedded_legal.py --binary $exe
if($LASTEXITCODE -ne 0){throw "IMMERSIB_LYRIC_LEGAL_NOT_EMBEDDED"}
& python scripts/verify_windows_pe_brand_icon.py --exe $exe --approved assets/icon/arsonmix-256.png
if($LASTEXITCODE -ne 0){throw "IMMERSIB_LYRIC_BRAND_ICON_NOT_APPROVED"}
$guiDir=Join-Path $BuildDir "approved_gui"
. (Join-Path "scripts" "resolve_approved_gui_python.ps1")
$approvedGuiPython = Resolve-ArSonMixApprovedGuiPython -BuildDir $BuildDir
& $approvedGuiPython scripts/build_approved_native_gui.py --out $guiDir --verify
if($LASTEXITCODE -ne 0){throw "IMMERSIB_LYRIC_GUI_PROVENANCE_FAILED"}
$gui=Get-Content -LiteralPath (Join-Path $guiDir "approved-gui-manifest.json") -Raw | ConvertFrom-Json
if($gui.authoritativePagesRoute -ne "/preview/" -or $gui.authoritativePagesOutput -ne "preview/index.html") {
 throw "IMMERSIB_LYRIC_NOT_APPROVED_GUI"
}
# Must be real 64-bit Windows GUI, not the passing console test executable.
$stream=[System.IO.File]::OpenRead((Resolve-Path -LiteralPath $exe).Path)
try {
 $reader=[System.IO.BinaryReader]::new($stream)
 if($reader.ReadUInt16() -ne 0x5a4d){throw "IMMERSIB_LYRIC_NO_MZ"}
 $stream.Position=0x3c
 $off=$reader.ReadInt32()
 if($off -lt 64 -or $off -gt $stream.Length-128){throw "IMMERSIB_LYRIC_BAD_PE_OFFSET"}
 $stream.Position=$off
 if($reader.ReadUInt32() -ne 0x4550){throw "IMMERSIB_LYRIC_NO_PE"}
 if($reader.ReadUInt16() -ne 0x8664){throw "IMMERSIB_LYRIC_NOT_AMD64"}
 $stream.Position=$off+24
 if($reader.ReadUInt16() -ne 0x020b){throw "IMMERSIB_LYRIC_NOT_PE32PLUS"}
 $stream.Position=$off+24+68
 if($reader.ReadUInt16() -ne 2){throw "IMMERSIB_LYRIC_NOT_WINDOWS_GUI"}
} finally {$stream.Dispose()}
if(Test-Path -LiteralPath $OutputDir){throw "IMMERSIB_LYRIC_STALE_PACKAGE_DIR"}
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$name="ArSonMix-ImmerSIB-Lyric-Front-Pocket-Research-Windows-x64.exe"
$dest=Join-Path $OutputDir $name
Copy-Item -LiteralPath $exe -Destination $dest
"ARSONMIX_PORTABLE_WORKSPACE_V1" | Set-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Encoding UTF8
Copy-Item -LiteralPath "docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md" -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")
Copy-Item -LiteralPath "installer/ArSonMixStartupDiagnostics.ps1" -Destination $OutputDir
@'
# ArSonMix ImmerSIB — Lyric Front Pocket P100

Experimental Windows x64 A/B, not a release or installer.
Extract to a NEW directory, back up .smix, never overwrite live installation.
Baseline: public alpha.31 identical DSP to parent main 527e0216.
Only replace P99's adaptive late-center relief with fixed 1800Hz Q=.65 -2dB
center-field bell. P99 direct/early, HRIR, stage geometry, bass, presets,
original FDN feedback, limiter, FX state and approved GUI remain unchanged.
The bell acts on late wet MID only; not vocals/stems, not overall EQ.
Six warm scenes affected, Velvet Cinema unchanged.
Compare matched perceived loudness, same room settings and existing FX chain.
Disable compensating post-EQ identically for both before comparison.
Listen for lyric intelligibility, comfort, warmth, stereo, ringing.
No source test certifies listening or physical ASIO.
'@ | Set-Content -LiteralPath (Join-Path $OutputDir "README_LISTENING_TEST.md") -Encoding UTF8
$manifest=[ordered]@{
 schema="arsonmix.immersib-unapproved-lyric-pocket.v1"
 sourceCommit=$actual
 parentMain="527e0216d226904288c4fe2dd8a76169dfb4417d"
 candidateAudioRevision="p100-replace-P99-adaptive-late-mid-with-fixed-1800hz-2db"
 sourceLockRewritten=$false
 status="UNAPPROVED_HEADPHONE_AB_ONLY"
 executable=$name
 exeBytes=(Get-Item -LiteralPath $dest).Length
 exeSha256=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
 pe="AMD64_PE32+_WINDOWS_GUI"
 cmakeFlags=[ordered]@{
  ARMIX_IMMERSIB_RELEASE_CANDIDATE="ON"
  ARMIX_IMMERSIB_LYRIC_MONITOR_RESEARCH="ON"
  ARMIX_SONKU_APPROVED_P99_MAIN="ON"
 }
 legalAndBrandVerifier="PASS"
 approvedGuiRoute=$gui.authoritativePagesRoute
 approvedGuiSha256=$gui.canonicalSourceSha256
 headphoneAB="NOT_TESTED"
 ASIOFAT="NOT_TESTED"
}
$manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDir "BUILD_PROVENANCE.json") -Encoding UTF8
Write-Host "IMMERSIB_LYRIC_EXPERIMENTAL_PORTABLE_PASS source=$actual exeSHA256=$($manifest.exeSha256)"
Write-Host "NOT_A_PUBLIC_RELEASE_APPROVAL"
