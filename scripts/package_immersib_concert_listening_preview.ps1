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
. (Join-Path "scripts" "resolve_approved_gui_python.ps1")
$approvedGuiPython = Resolve-ArSonMixApprovedGuiPython -BuildDir $BuildDir
& $approvedGuiPython scripts/build_approved_native_gui.py --out $guiDir --verify
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
$name="ArSonMix-ImmerSIB-V6-Direct-Comfort-Windows-x64.exe"
$dest=Join-Path $OutputDir $name
Copy-Item -LiteralPath $exe -Destination $dest
"ARSONMIX_PORTABLE_WORKSPACE_V1" | Set-Content -LiteralPath (Join-Path $OutputDir "portable_mode.txt") -Encoding UTF8
Copy-Item -LiteralPath "docs/FIRST_SOUND_FAT_QUICKSTART_20261101.md" -Destination (Join-Path $OutputDir "HARDWARE_FAT_GUIDE.md")
Copy-Item -LiteralPath "installer/ArSonMixStartupDiagnostics.ps1" -Destination $OutputDir
@'
# ArSonMix ImmerSIB v6 — direct-HRTF 2k comfort, EXPERIMENTAL A/B

Portable Windows x64 KHUSUS uji listening, BUKAN installer/rilis stabil.
Ekstrak dalam folder baru. Backup .smix dan sesi Studio lebih dahulu.
Jangan menimpa instalasi ArSonMix produksi.

Perubahan v6 dibanding v5 (feedback: mid 1–3k masih menusuk):
- Pengukuran FULL P9 original v5 menghasilkan gain mono-center 2.05kHz
  sekitar +11.7dB pada scene Live Concert/44.1k sementara 285Hz +4.5dB.
  Lonjakan mid tetap terjadi ROOM OFF sehingga P94 bukan akar utama.
- Existing SonKu P80 sudah mempunyai correction bell di 2.2kHz; v6
  menambah hingga 4dB reduksi pada bell YANG SAMA di DIRECT HRTF saja.
  Tidak menambah EQ baru, frequency pump/detektor, atau mengolah late/ER.
- Koreksi P80 diberikan sama ke kedua telinga sebelum room sum sehingga
  arah stereo/ILD tidak dibuat palsu; LF/bass dan efek ruang tetap sama.
- Tes P9 44.1/48k mengukur apakah mid benar-benar turun di FULL PCM.
Yang tetap sama dari v5:
- P90: pertahankan phantom-center v4 yang telah terdengar lebih hadir di depan.
- P94: untuk dua virtual loudspeaker stereo L/R pada sudut panggung
  104–134 derajat, hitung DIRECT-vs-EARLY dari arah KEDUA speaker,
  bukan menganggap semua materi berasal dari satu speaker di sudut 0.
- Pada Live Concert 112 derajat (L=-56, R=+56), pengali P94 EARLY
  kira-kira 1.027 vs 1.280 pada v4, dengan direct lebih mendekati unity.
  Ini bukan EQ 1–3 kHz dan BUKAN pengurangan global semua reverb.
- P98 dan P99: kembali ke source tonal main/alpha.31 yang disetujui.
  Tidak ada dynamic high-mid EQ/350Hz boost tambahan dari v2/v3.
- Sudut speaker, bass, 6-surface floor/ceiling, FDN late hall, GUI FUX-1,
  IMRS/.smix, ASIO primary dan satu render PCM tetap sama.

Cara membandingkan (headphone, kerasnya disamakan):
1. Gunakan lagu stereo L/R jadi yang sama dan preset konser yang sama.
2. Bandingkan dengan v4 artifact/alpha.31 yang sebelumnya Anda dengar.
3. Uji vokal hangat 200–300Hz dan presence 1–3kHz: jelas/tidak menusuk?
4. Uji gitar akustik body dan instrumen terpan kiri/kanan: tetap berisi?
5. Uji 3D: vokal depan; reverb menyelimuti, tidak menggantung di atas?
6. Bass, ambience akhir, mono/antiphase dan transien tidak boleh regresi.
Jika timbul rasa menusuk/clicking, hentikan dan kembali ke versi nyaman.

Eksperimen P94/P80 v6 diuji C++/PCM native, tetapi belum disetujui
secara musikal oleh pendengar/hardware. Lihat BUILD_PROVENANCE.json
untuk commit exact, SHA256 executable dan flag kandidat. PR #857 DRAFT.
Tidak boleh dianggap versi stable atau diumumkan sebagai installer resmi.
'@ | Set-Content -LiteralPath (Join-Path $OutputDir "README_LISTENING_TEST.md") -Encoding UTF8
$manifest=[ordered]@{
 schema="arsonmix.immersib-unapproved-listening.v2"
 sourceCommit=$actual
 pr=857
 approvedFuxMainCheckpoint="527e0216d226904288c4fe2dd8a76169dfb4417d"
 candidateAudioRevision="v6-direct-HRTF-P80-measured-2k-comfort-plus-v5-P94-and-v4-front"
 previouslyAuditionedReference="v4-pr857-3fd52a96f6a1e8c7716325342481704fd156f033"
 p98P99TonalSource="IDENTICAL_TO_APPROVED_MAIN"
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
