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
$fixtureReady=Join-Path $env:RUNNER_TEMP ("arsonmix-fixture-ready-"+[guid]::NewGuid().ToString("N"))
$fixtureClosed=Join-Path $env:RUNNER_TEMP ("arsonmix-fixture-closed-"+[guid]::NewGuid().ToString("N"))
$fixtureProcess=$null
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
    # REAL Windows in-place UPDATE test: replace the *test-installed* PE
    # with a harmless WinForms window. Installer must ask it to WM_CLOSE,
    # observe the graceful close marker, replace its running .exe with the
    # verified FAT PE and preserve private Studio plus unknown user files.
    # This never replaces the source FAT executable or touches a user install.
    $csc=Join-Path $env:WINDIR "Microsoft.NET\Framework\v4.0.30319\csc.exe"
    $fixtureSource=Join-Path $root "tests\fixtures\installer_graceful_close.cs"
    if(-not (Test-Path -LiteralPath $csc -PathType Leaf) -or
       -not (Test-Path -LiteralPath $fixtureSource -PathType Leaf)){
        throw "Windows Framework C# compiler or WM_CLOSE fixture missing"
    }
    & $csc "/nologo" "/target:winexe" "/reference:System.Windows.Forms.dll" "/out:$installedExe" $fixtureSource
    if($LASTEXITCODE -ne 0){throw "Cannot prepare graceful-close test window"}
    $fixtureProcess=Start-Process -FilePath $installedExe -ArgumentList @(
        ('"' + $fixtureReady + '"'), ('"' + $fixtureClosed + '"')
    ) -PassThru
    for($i=0;$i -lt 75 -and -not (Test-Path -LiteralPath $fixtureReady);$i++){
        $fixtureProcess.Refresh()
        if($fixtureProcess.HasExited){break}
        Start-Sleep -Milliseconds 200
    }
    if(-not (Test-Path -LiteralPath $fixtureReady)){
        throw "Installed upgrade fixture did not show its Windows message-loop window"
    }
    $installSentinel=Join-Path $install "arsonmix-user-file.keep"
    "preserve-user-files-across-upgrade" | Set-Content -LiteralPath $installSentinel
    $upgradeLog=Join-Path $env:RUNNER_TEMP ("arsonmix-upgrade-"+[guid]::NewGuid().ToString("N")+".log")
    $upgrade=Start-Process -FilePath $setup -ArgumentList @(
        "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/CURRENTUSER",
        "/CLOSEAPPLICATIONS","/NOFORCECLOSEAPPLICATIONS",
        "/DIR=$install",('/LOG="' + $upgradeLog + '"')
    ) -Wait -PassThru
    $fixtureProcess.Refresh()
    if($upgrade.ExitCode -ne 0 -or -not $fixtureProcess.HasExited -or
       -not (Test-Path -LiteralPath $fixtureClosed) -or
       (Get-Content -LiteralPath $fixtureClosed -Raw).Trim() -ne "WM_CLOSE_SAVED"){
        throw "In-place update did not gracefully close the running ArSonMix fixture (see $upgradeLog)"
    }
    if((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $appHash -or
       -not (Test-Path -LiteralPath $installSentinel) -or
       -not (Test-Path -LiteralPath $sentinel) -or
       (Test-Path -LiteralPath (Join-Path $install "portable_mode.txt"))){
        throw "In-place update failed verified EXE replacement or data preservation"
    }
    Write-Host "INNO_GRACEFUL_UPDATE_PASS: running app WM_CLOSE, verified PE replacement, user files preserved"
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
        updaterTest="WM_CLOSE_IN_PLACE_SHA256_AND_USER_DATA_PRESERVED"
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out "INSTALLER_PROVENANCE.json") -Encoding UTF8
    Write-Host "INSTALLER_RC_VERIFIED: first install, graceful running-app update, SHA-matched PE, uninstall, private Studio intact"
    Write-Host "PHYSICAL_FAT_PENDING"
}
finally {
    # Only a failing *test fixture* may be terminated in CI teardown.
    # Production Inno Setup never force-kills the user's audio application.
    if($null -ne $fixtureProcess){
        try{
            $fixtureProcess.Refresh()
            if(-not $fixtureProcess.HasExited){
                Stop-Process -Id $fixtureProcess.Id -Force -ErrorAction SilentlyContinue
            }
        }catch{}
        $fixtureProcess.Dispose()
    }
    foreach($marker in @($fixtureReady,$fixtureClosed)){
        if(Test-Path -LiteralPath $marker){Remove-Item -LiteralPath $marker -Force}
    }
    if(Test-Path -LiteralPath $sentinel){Remove-Item -LiteralPath $sentinel -Force}
    if(Test-Path -LiteralPath $stage){Remove-Item -LiteralPath $stage -Recurse -Force}
    if(Test-Path -LiteralPath $install){Remove-Item -LiteralPath $install -Recurse -Force -ErrorAction SilentlyContinue}
}
