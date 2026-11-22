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
foreach($flag in @("ARMIX_IMMERSIB_RELEASE_CANDIDATE","ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH","ARMIX_SONKU_APPROVED_P99_MAIN","ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH")){
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
$name="ArSonMix-ImmerSIB-P102-Refined-Clarity-Windows-x64.exe"
$dest=Join-Path $OutputDir $name
Copy-Item -LiteralPath $exe -Destination $dest
"ARSONMIX_PORTABLE_WORKSPACE_V1" | Set-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Encoding UTF8
Copy-Item -LiteralPath "docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md" -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")
Copy-Item -LiteralPath "installer/ArSonMixStartupDiagnostics.ps1" -Destination $OutputDir
@'
# Fixed PA + Musical EQ (P102.3 Refined Clarity)
Experimental A/B only. Exact source baseline 6399c0f5, no P100/P101 algorithm.
Original spatial output and limiter unchanged. Afterward a common PEQ:
250Hz +5.5 Q .85 (warm-body refinement); 1120Hz +2.3 Q1.2 (unchanged);
3240Hz high shelf +3.3; 7400Hz -1.8 Q1.1 (unchanged).
Added 310Hz -1.25 Q1.8; 5000Hz +.75 Q1.1; 10500Hz +1.35 Q1.5.
Conservative musical refinement, not a copy of the owner's stronger PEQ.
Screenshot-derived musical voicing, not inverse flattening. Extra linked
peak protection only when the post-EQ sample exceeds .98. No automatic gain pad.
Extract to separate folder. Compare with original Fixed PA at matched loudness,
same scene and EQ bypassed on both. Compare prior P102.1 9fc749df.
Avoid double EQ. Match perceived loudness, allow headroom (up to approximately +4.9dB steady-state EQ; actual PCM peaks depend on the music).
Not production ready.
'@ | Set-Content -LiteralPath (Join-Path $OutputDir "README_LISTENING_TEST.md") -Encoding UTF8
$manifest=[ordered]@{
 schema="arsonmix.immersib-unapproved-fixed-stage.v1"
 sourceCommit=$actual
 parentMain="527e0216d226904288c4fe2dd8a76169dfb4417d"
 candidateAudioRevision="6399c0f5-exact-fixed-pa-then-musical-eq-v102.3-refined-clarity"
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
  ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH="ON"
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
