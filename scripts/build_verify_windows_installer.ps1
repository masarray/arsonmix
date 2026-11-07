# SPDX-License-Identifier: AGPL-3.0-only
# Independent FAT-only GUI installer build AND real per-user install/uninstall.
param([string]$OutputDir="dist",[string]$InstallerOutput="installer-output")
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$dist=(Resolve-Path -LiteralPath $OutputDir).Path
$sha=(& git rev-parse HEAD).Trim()
$manifest=Get-Content -LiteralPath (Join-Path $dist "BUILD_PROVENANCE.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$exe=Join-Path $dist "ArSonMix-GUI10CH-P012-Windows-x64.exe"
$appHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
if($LASTEXITCODE -ne 0 -or $manifest.sourceCommit -ne $sha -or
   $manifest.exeSha256 -ne $appHash -or $manifest.fatStatus -ne "EXPERIMENTAL_NOT_HARDWARE_QUALIFIED" -or
   (Get-Item -LiteralPath $exe).Length -ne $manifest.guiExeBytes -or
   (Test-Path -LiteralPath (Join-Path $dist "Studio"))) {
    throw "Installer requires the exact already verified and hardware-unqualified FAT PE"
}
$which=Get-Command ISCC.exe -ErrorAction SilentlyContinue
$iscc=if($which){$which.Source}else{"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"}
if(-not (Test-Path -LiteralPath $iscc -PathType Leaf)){throw "Inno Setup 6 compiler missing"}
$root=(Resolve-Path .).Path
$out=Join-Path $root $InstallerOutput
New-Item -ItemType Directory -Force -Path $out | Out-Null
$stage=Join-Path $env:RUNNER_TEMP ("arsonmix-install-stage-"+[guid]::NewGuid().ToString("N"))
$install=Join-Path $env:RUNNER_TEMP ("arsonmix-install-test-"+[guid]::NewGuid().ToString("N"))
$studio=Join-Path $env:APPDATA "AR-MIX-Studio"
New-Item -ItemType Directory -Force -Path $studio | Out-Null
$sentinel=Join-Path $studio ("installer-test-"+[guid]::NewGuid().ToString("N")+".keep")
"persistent-private-data" | Set-Content -LiteralPath $sentinel
try {
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item -LiteralPath $exe -Destination (Join-Path $stage "verified-gui.exe")
    foreach($item in @("BUILD_PROVENANCE.json","README.md")){
        Copy-Item -LiteralPath (Join-Path $dist $item) -Destination (Join-Path $stage $item)
    }
    Copy-Item -LiteralPath "assets/icon/arsonmix-app.ico" -Destination (Join-Path $stage "arsonmix-app.ico")
    $factory=Join-Path $dist "FactoryFX"
    if(Test-Path -LiteralPath $factory -PathType Container){
        Copy-Item -LiteralPath $factory -Destination (Join-Path $stage "FactoryFX") -Recurse
    }
    & $iscc "/Qp" "/DSourceRoot=$stage" "/DOutputRoot=$out" "installer/ArSonMix.iss"
    $setup=Join-Path $out "ArSonMix-Windows-x64-Setup-UNQUALIFIED.exe"
    if($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $setup -PathType Leaf) -or
       (Get-Item -LiteralPath $setup).Length -lt 2MB){throw "ISCC failed to build installer RC"}
    $start=Start-Process -FilePath $setup -ArgumentList @(
        "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/CURRENTUSER","/DIR=$install"
    ) -Wait -PassThru
    if($start.ExitCode -ne 0){throw "Actual Windows installation failed"}
    $installedExe=Join-Path $install "ArSonMix.exe"
    if(-not (Test-Path -LiteralPath $installedExe -PathType Leaf) -or
       (Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $appHash -or
       (Test-Path -LiteralPath (Join-Path $install "portable_mode.txt"))){
        throw "Installed EXE hash or storage mode differs from source"
    }
    $installedManifest=Get-Content -LiteralPath (Join-Path $install "BUILD_PROVENANCE.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    if($installedManifest.sourceCommit -ne $sha){throw "Wrong installed provenance"}
    # Exercise the real Inno UPDATE path, not only a clean installation.
    # Silent CI must never launch the live audio application. Restart Manager
    # closes a running installation gracefully when needed; real open-process
    # behavior is separately qualified on hardware.
    $upgrade=Start-Process -FilePath $setup -ArgumentList @(
        "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/CURRENTUSER",
        "/CLOSEAPPLICATIONS","/NORESTARTAPPLICATIONS","/DIR=$install"
    ) -Wait -PassThru
    if($upgrade.ExitCode -ne 0){throw "Inno in-place update failed: $($upgrade.ExitCode)"}
    if(-not (Test-Path -LiteralPath $installedExe -PathType Leaf) -or
       (Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $appHash -or
       -not (Test-Path -LiteralPath $sentinel) -or
       (Test-Path -LiteralPath (Join-Path $install "portable_mode.txt"))){
        throw "In-place update changed the verified GUI, erased Studio or switched to portable mode"
    }
    $upgradeManifest=Get-Content -LiteralPath (Join-Path $install "BUILD_PROVENANCE.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    if($upgradeManifest.sourceCommit -ne $sha){throw "In-place update lost exact source attestation"}
    $uninstall=Join-Path $install "unins000.exe"
    if(-not (Test-Path -LiteralPath $uninstall -PathType Leaf)){throw "Uninstaller missing"}
    $stop=Start-Process -FilePath $uninstall -ArgumentList @(
        "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART"
    ) -Wait -PassThru
    if($stop.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $sentinel)){
        throw "Uninstaller failed or erased user Studio data"
    }
    [ordered]@{
        sourceCommit=$sha;guiSha256=$appHash
        installerSha256=(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
        certification="UNSIGNED_HARDWARE_FAT_PENDING";mode="PER_USER_NO_AUTOARM"
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out "INSTALLER_PROVENANCE.json") -Encoding UTF8
    Write-Host "INSTALLER_RC_VERIFIED: SHA-matched PE, fresh install, in-place update, uninstall, private Studio intact"
    Write-Host "PHYSICAL_FAT_PENDING"
}
finally {
    if(Test-Path -LiteralPath $sentinel){Remove-Item -LiteralPath $sentinel -Force}
    if(Test-Path -LiteralPath $stage){Remove-Item -LiteralPath $stage -Recurse -Force}
    if(Test-Path -LiteralPath $install){Remove-Item -LiteralPath $install -Recurse -Force -ErrorAction SilentlyContinue}
}
