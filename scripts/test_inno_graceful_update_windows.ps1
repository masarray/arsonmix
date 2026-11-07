# SPDX-License-Identifier: AGPL-3.0-only
# Fast Windows Inno/Restart Manager smoke, independent of JUCE.
# The staged .exe is a test-only WinForms window, NOT a product release.
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$root=(Resolve-Path .).Path
$work=Join-Path $env:RUNNER_TEMP ("arsonmix-inno-smoke-"+[guid]::NewGuid().ToString("N"))
$stage=Join-Path $work "stage"
$out=Join-Path $work "compiled"
$install=Join-Path $work "installed"
$ready=Join-Path $work "ready.marker"
$closed=Join-Path $work "closed.marker"
$log=Join-Path $work "upgrade.log"
$studio=Join-Path $env:APPDATA "AR-MIX-Studio"
$fixtureProcess=$null
$privateSentinel=Join-Path $studio ("installer-smoke-"+[guid]::NewGuid().ToString("N")+".keep")

function Assert-Condition([bool]$condition,[string]$message) {
    if(-not $condition){throw $message}
}
function Show-UpgradeDiagnostics([string]$logFile){
    if(Test-Path -LiteralPath $logFile){
        Get-Content -LiteralPath $logFile |
          Select-String -Pattern 'Restart Manager|close application|closing application|file in use|ArSonMix|error|installing' |
          Select-Object -Last 45 |
          ForEach-Object {Write-Host "INNO_RESTART_MANAGER_LOG: $($_.Line)"}
    }else{Write-Host "INNO_RESTART_MANAGER_LOG_MISSING"}
}
try {
    New-Item -ItemType Directory -Force -Path $stage,$out,$install,$studio | Out-Null
    "private-user-data" | Set-Content -LiteralPath $privateSentinel
    $csc=Join-Path $env:WINDIR "Microsoft.NET/Framework/v4.0.30319/csc.exe"
    Assert-Condition (Test-Path -LiteralPath $csc -PathType Leaf) "Windows C# compiler missing"
    & $csc "/nologo" "/target:winexe" "/reference:System.Windows.Forms.dll" "/out:$(Join-Path $stage 'verified-gui.exe')" (Join-Path $root "tests/fixtures/installer_graceful_close.cs")
    Assert-Condition ($LASTEXITCODE -eq 0) "Test-only WinForms fixture failed to compile"
    Copy-Item -LiteralPath (Join-Path $root "assets/icon/arsonmix-app.ico") -Destination (Join-Path $stage "arsonmix-app.ico")
    "Test-only installer mechanics fixture" | Set-Content -LiteralPath (Join-Path $stage "README.md")
    '{"sourceCommit":"TEST_FIXTURE_ONLY","fatStatus":"NOT_A_PRODUCT"}' | Set-Content -LiteralPath (Join-Path $stage "BUILD_PROVENANCE.json")
    $found=Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $iscc=if($found){$found.Source}else{"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"}
    Assert-Condition (Test-Path -LiteralPath $iscc -PathType Leaf) "ISCC.exe missing"
    & $iscc "/Qp" "/DSourceRoot=$stage" "/DOutputRoot=$out" "/DSourceAppVersion=0.5.0" (Join-Path $root "installer/ArSonMix.iss")
    Assert-Condition ($LASTEXITCODE -eq 0) "Cannot compile Inno fixture setup"
    $setup=Join-Path $out "ArSonMix-Windows-x64-Setup-UNQUALIFIED.exe"
    Assert-Condition (Test-Path -LiteralPath $setup -PathType Leaf) "Compiled Inno test installer missing"
    $setupArgs=@("/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/CURRENTUSER","/CLOSEAPPLICATIONS","/NOFORCECLOSEAPPLICATIONS","/DIR=$install")
    $first=Start-Process -FilePath $setup -ArgumentList $setupArgs -Wait -PassThru
    Assert-Condition ($first.ExitCode -eq 0) ("First test install exit="+$first.ExitCode)
    $installedExe=Join-Path $install "ArSonMix.exe"
    Assert-Condition (Test-Path -LiteralPath $installedExe -PathType Leaf) "Installed fixture missing"
    $fixtureHash=(Get-FileHash -LiteralPath (Join-Path $stage "verified-gui.exe") -Algorithm SHA256).Hash
    Assert-Condition ((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash -eq $fixtureHash) "First install changed test executable bytes"
    $userFile=Join-Path $install "user-file.keep"
    "user-file-must-survive" | Set-Content -LiteralPath $userFile
    $fixtureProcess=Start-Process -FilePath $installedExe -ArgumentList @(('"' + $ready + '"'),('"' + $closed + '"')) -PassThru
    for($i=0;$i -lt 60 -and -not (Test-Path -LiteralPath $ready);$i++){
        $fixtureProcess.Refresh()
        if($fixtureProcess.HasExited){break}
        Start-Sleep -Milliseconds 200
    }
    Assert-Condition (Test-Path -LiteralPath $ready) "Fixture window did not start"
    $upgrade=Start-Process -FilePath $setup -ArgumentList ($setupArgs + @('/LOG="' + $log + '"')) -Wait -PassThru
    $fixtureProcess.Refresh()
    $exited=$fixtureProcess.HasExited
    $marker=Test-Path -LiteralPath $closed
    $markerText=if($marker){(Get-Content -LiteralPath $closed -Raw).Trim()}else{"MISSING"}
    $newHash=(Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash
    Write-Host "INNO_UPGRADE_DIAG exit=$($upgrade.ExitCode) exited=$exited marker=$markerText hashOk=$($newHash -eq $fixtureHash)"
    Show-UpgradeDiagnostics $log
    Assert-Condition ($upgrade.ExitCode -eq 0) "Silent Inno upgrade returned error"
    $acceptedSaves=@("WM_CLOSE_SAVED","RM_QUERY_SAVED","RM_ENDSESSION_SAVED","INSTALLER_HANDSHAKE_SAVED")
    Assert-Condition ($exited -and $marker -and ($acceptedSaves -contains $markerText)) "Inno did not request a saved, graceful Windows session close"
    Assert-Condition ($newHash -eq $fixtureHash) "Updated installed fixture hash mismatch"
    Assert-Condition ((Test-Path -LiteralPath $userFile) -and (Test-Path -LiteralPath $privateSentinel)) "Update removed user data"
    $uninstaller=Join-Path $install "unins000.exe"
    Assert-Condition (Test-Path -LiteralPath $uninstaller) "Uninstaller missing"
    $uninstall=Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART") -Wait -PassThru
    Assert-Condition ($uninstall.ExitCode -eq 0) "Uninstall failed"
    Assert-Condition (Test-Path -LiteralPath $privateSentinel) "Uninstall erased Studio"
    Write-Host "INNO_GRACEFUL_SMOKE_PASS: running app Windows session close, same-path upgrade, integrity, Studio retention, uninstall"
} finally {
    # Test-only cleanup; never used by production Inno Setup.
    if($null -ne $fixtureProcess){
        try{
            $fixtureProcess.Refresh()
            if(-not $fixtureProcess.HasExited){Stop-Process -Id $fixtureProcess.Id -Force -ErrorAction SilentlyContinue}
        }catch{}
        $fixtureProcess.Dispose()
    }
    if(Test-Path -LiteralPath $privateSentinel){Remove-Item -LiteralPath $privateSentinel -Force}
    if(Test-Path -LiteralPath $work){Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue}
}
