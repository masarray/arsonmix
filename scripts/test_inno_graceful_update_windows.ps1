# SPDX-License-Identifier: AGPL-3.0-only
# Fast Windows Inno/Restart Manager smoke, independent of JUCE.
# The staged .exe is a test-only WinForms window, NOT a product release.
Set-StrictMode -Version Latest
# Offline functional policy gate for the actual shipped updater script.
# This mode never reads an install, touches a live process or calls the network.
$updaterPolicy=Join-Path (Resolve-Path .).Path "installer/ArSonMixAutoUpdate.ps1"
$policyOutput=& "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -File $updaterPolicy -OwnerProcessId 1 -PolicySelfTest
if($LASTEXITCODE -ne 0 -or $policyOutput -notcontains 'ARSONMIX_UPDATER_POLICY_PASS'){
    throw "Smart updater offline version/origin/duplicate-asset policy failed: $policyOutput"
}
Write-Host "ARSONMIX_UPDATER_POLICY_PASS"

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
    $setup=Join-Path $out "ArSonMix-Windows-x64-Setup-Preview.exe"
    Assert-Condition (Test-Path -LiteralPath $setup -PathType Leaf) "Compiled Inno test installer missing"
    $setupArgs=@("/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/CURRENTUSER","/NOCLOSEAPPLICATIONS","/DIR=$install")
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
    # Upgrade from a PRE-HANDSHAKE ArSonMix.exe, exactly the case the old
    # test omitted and which caused DeleteFile failed; code 5 in the field.
    # Its FormClosing marker proves an old GUI was asked to quit normally.
    Remove-Item -LiteralPath $ready,$closed -Force -ErrorAction SilentlyContinue
    $fixtureProcess=Start-Process -FilePath $installedExe -ArgumentList @(
        ('"' + $ready + '"'),('"' + $closed + '"'),"legacy") -PassThru
    for($i=0;$i -lt 75 -and -not (Test-Path -LiteralPath $ready);$i++){
        $fixtureProcess.Refresh()
        if($fixtureProcess.HasExited){break}
        Start-Sleep -Milliseconds 200
    }
    Assert-Condition (Test-Path -LiteralPath $ready) "Legacy pre-handshake fixture never became ready"
    Assert-Condition ((Get-Content -LiteralPath $ready -Raw).Trim() -eq "WINDOW_READY_LEGACY") "Legacy fixture did not receive mode argument"
    $legacy=Start-Process -FilePath $setup -ArgumentList ($setupArgs + @('/LOG="' + (Join-Path $work 'legacy-upgrade.log') + '"')) -Wait -PassThru
    $fixtureProcess.Refresh()
    $legacyMarker=if(Test-Path -LiteralPath $closed){(Get-Content -LiteralPath $closed -Raw).Trim()}else{"MISSING"}
    Write-Host "INNO_LEGACY_DIAG exit=$($legacy.ExitCode) exited=$($fixtureProcess.HasExited) marker=$legacyMarker"
    Assert-Condition ($legacy.ExitCode -eq 0 -and $fixtureProcess.HasExited) "Legacy running GUI did not close before EXE replacement"
    Assert-Condition ($legacyMarker -eq "WM_CLOSE_SAVED") "Legacy GUI did not receive WM_CLOSE and save"
    Assert-Condition ((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash -eq $fixtureHash) "Legacy upgrade changed verified EXE hash"
    Assert-Condition ((Test-Path -LiteralPath $userFile) -and (Test-Path -LiteralPath $privateSentinel)) "Legacy upgrade damaged Studio/user files"
    Write-Host "INNO_LEGACY_NO_HANDSHAKE_UPGRADE_PASS: exact exe process requested WM_CLOSE before replacement"
    $fixtureProcess.Dispose();$fixtureProcess=$null

    # Busy/hung GUI: it REFUSES WM_CLOSE and has no newer handshake.
    # Upgrade must fail before [Files], leave old EXE and Studio untouched.
    Remove-Item -LiteralPath $ready,$closed -Force -ErrorAction SilentlyContinue
    $fixtureProcess=Start-Process -FilePath $installedExe -ArgumentList @(
        ('"' + $ready + '"'),('"' + $closed + '"'),"blocked") -PassThru
    for($i=0;$i -lt 75 -and -not (Test-Path -LiteralPath $ready);$i++){
        $fixtureProcess.Refresh()
        if($fixtureProcess.HasExited){break}
        Start-Sleep -Milliseconds 200
    }
    Assert-Condition (Test-Path -LiteralPath $ready) "Unresponsive fixture never became ready"
    Assert-Condition ((Get-Content -LiteralPath $ready -Raw).Trim() -eq "WINDOW_READY_BLOCKED") "Unresponsive fixture did not receive block mode argument"
    $blocked=Start-Process -FilePath $setup -ArgumentList ($setupArgs + @('/LOG="' + (Join-Path $work 'blocked-upgrade.log') + '"')) -Wait -PassThru
    $fixtureProcess.Refresh()
    Write-Host "INNO_BLOCKED_DIAG exit=$($blocked.ExitCode) stillRunning=$(-not $fixtureProcess.HasExited)"
    Show-UpgradeDiagnostics (Join-Path $work 'blocked-upgrade.log')
    Assert-Condition ($blocked.ExitCode -ne 0 -and -not $fixtureProcess.HasExited) "Installer improperly replaced a live GUI that refused to close"
    Assert-Condition ((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash -eq $fixtureHash) "Blocked upgrade unexpectedly modified installed EXE"
    Assert-Condition ((Test-Path -LiteralPath $userFile) -and (Test-Path -LiteralPath $privateSentinel)) "Blocked upgrade removed personal files"
    Assert-Condition (-not (Test-Path -LiteralPath $closed)) "Blocked fixture unexpectedly wrote a saved-close marker"
    Write-Host "INNO_BLOCKED_FAIL_CLOSED_PASS: live process stays running, no overwrite, Studio intact"
    Stop-Process -Id $fixtureProcess.Id -Force -ErrorAction Stop # TEST-ONLY synthetic fixture teardown
    $fixtureProcess.Dispose();$fixtureProcess=$null

    $uninstaller=Join-Path $install "unins000.exe"
    Assert-Condition (Test-Path -LiteralPath $uninstaller) "Uninstaller missing"
    $uninstall=Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART") -Wait -PassThru
    Assert-Condition ($uninstall.ExitCode -eq 0) "Uninstall failed"
    Assert-Condition (Test-Path -LiteralPath $privateSentinel) "Uninstall erased Studio"
    Write-Host "INNO_GRACEFUL_SMOKE_PASS: handshake + legacy GUI close, hung GUI fail-closed, same-path SHA, Studio retention, uninstall"
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
