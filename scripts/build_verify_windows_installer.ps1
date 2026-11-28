# SPDX-License-Identifier: AGPL-3.0-only
# Independent FAT-only GUI installer build AND real per-user install/uninstall.
param([string]$OutputDir="dist",[string]$InstallerOutput="installer-output",[switch]$IncludePortableUpdater)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$dist=(Resolve-Path -LiteralPath $OutputDir).Path
$sha=(& git rev-parse HEAD).Trim()
$manifest=Get-Content -LiteralPath (Join-Path $dist "BUILD_PROVENANCE.json") -Raw -Encoding UTF8 | ConvertFrom-Json
$exe=Join-Path $dist "ArSonMix-GUI10CH-P012-Windows-x64.exe"
$appHash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
# Binary-level fresh-Win10 prerequisite gate: GitHub runners have the VC++
# redist preinstalled, so a successful CI launch alone cannot catch /MD.
# Inspect real PE import and delay-import tables BEFORE shipping/installation.
& python scripts/verify_win10_pe_runtime.py --exe $exe
if($LASTEXITCODE -ne 0){throw "Packaged PE still requires dynamic MSVC redistributable"}
if($LASTEXITCODE -ne 0 -or $manifest.sourceCommit -ne $sha -or
   $manifest.exeSha256 -ne $appHash -or $manifest.fatStatus -ne "SOFTWARE_VERIFIED_PREVIEW_CANDIDATE" -or
   (Get-Item -LiteralPath $exe).Length -ne $manifest.guiExeBytes -or
   (Test-Path -LiteralPath (Join-Path $dist "Studio"))) {
    throw "Installer requires an exact source-SHA software-verified Preview GUI PE"
}
$which=Get-Command ISCC.exe -ErrorAction SilentlyContinue
$iscc=if($which){$which.Source}else{"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"}
if(-not (Test-Path -LiteralPath $iscc -PathType Leaf)){throw "Inno Setup 6 compiler missing"}
$root=(Resolve-Path .).Path
# One authoritative product version: CMake native app and Inno Add/Remove Programs.
$cmake=(Get-Content -LiteralPath (Join-Path $root "CMakeLists.txt") -Raw -Encoding UTF8)
if($cmake -notmatch '(?m)^project\(ARMixConsole VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES'){
    throw "Installer version does not match the native CMake project"
}
$appVersion=$Matches[1]
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
# Compare actual installed FactoryFX resources against the same verified
# lock, not merely the mixer EXE hash. Use the portable receipt as the
# expected inventory; the installed tree must independently validate.
function Assert-InstalledFactoryFx([string]$Phase) {
    $receipt = Join-Path $install "FACTORYFX_STAGE_REPORT.json"
    $portableManifest = Join-Path $dist "FactoryFX/factoryfx-manifest.json"
    $installedFxManifest = Join-Path $install "FactoryFX/factoryfx-manifest.json"
    if (Test-Path -LiteralPath $portableManifest -PathType Leaf) {
        if (-not (Test-Path -LiteralPath $installedFxManifest -PathType Leaf) -or
            (Get-FileHash -LiteralPath $portableManifest -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $installedFxManifest -Algorithm SHA256).Hash) {
            throw "Installed FactoryFX manifest differs from portable: $Phase"
        }
    }
    try {
        Copy-Item -LiteralPath (Join-Path $dist "FACTORYFX_STAGE_REPORT.json") -Destination $receipt
        & python scripts/factoryfx_release_bundle.py verify --lock config/factoryfx-approved-releases.json --output $install --best-effort
        if ($LASTEXITCODE -ne 0) { throw "Installed FactoryFX verification failed: $Phase" }
        Write-Host "FACTORYFX_INSTALLED_TREE_VERIFIED: $Phase"
    } finally {
        Remove-Item -LiteralPath $receipt -Force -ErrorAction SilentlyContinue
    }
}

# GUI-only updater health deliberately suppresses vendor discovery. Exercise
# the actual installed ArSonMix.exe's existing isolated VST3 scanner after
# the independent installed-tree hash gate, never an external fake host.
function Test-InstalledFactoryFxScanner([string]$Application) {
    $manifestPath=Join-Path $install "FactoryFX/factoryfx-manifest.json"
    if(-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)){return 0}
    $fxManifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $count=0
    foreach($row in @($fxManifest.plugins)){
        $module=(Resolve-Path -LiteralPath (Join-Path $install $row.bundleRelativePath)).Path
        $nonce=[guid]::NewGuid().ToString('N')
        $receiptDir=Join-Path ([IO.Path]::GetTempPath()) ("arsonmix-vst3-probe-"+$nonce)
        if(Test-Path -LiteralPath $receiptDir){throw "FactoryFX scan receipt collision"}
        $child=$null
        try {
            New-Item -ItemType Directory -Path $receiptDir | Out-Null
            $info=[Diagnostics.ProcessStartInfo]::new()
            $info.FileName=$Application
            $info.WorkingDirectory=$install
            $info.UseShellExecute=$false
            $info.Arguments='--arsonmix-vst3-scan-worker "'+$module+'" '+$nonce
            $child=[Diagnostics.Process]::Start($info)
            if($null -eq $child -or !$child.WaitForExit(40000) -or $child.ExitCode -ne 0){
                throw "Installed FactoryFX scanner failed: $($row.productId)"
            }
            $receipt=Join-Path $receiptDir "result.xml"
            if(-not (Test-Path -LiteralPath $receipt -PathType Leaf) -or
               (Get-Item -LiteralPath $receipt).Length -gt 1MB){
                throw "Missing or oversized FactoryFX scanner receipt: $($row.productId)"
            }
            $settings=[System.Xml.XmlReaderSettings]::new()
            $settings.DtdProcessing=[System.Xml.DtdProcessing]::Prohibit
            $settings.XmlResolver=$null
            $settings.MaxCharactersInDocument=1048576
            $xml=[System.Xml.XmlDocument]::new()
            $xml.XmlResolver=$null
            $reader=[System.Xml.XmlReader]::Create($receipt,$settings)
            try { $xml.Load($reader) } finally { $reader.Dispose() }
            $node=$xml.DocumentElement
            if($null -eq $node -or $node.Name -cne 'ARSONMIX_VST3_SCAN_V1' -or
               $node.GetAttribute('nonce') -cne $nonce -or $node.GetAttribute('path') -cne $module){
                throw "Invalid FactoryFX scanner receipt binding: $($row.productId)"
            }
            $matching=@($node.SelectNodes('PLUGIN') | Where-Object {
                $uid=$_.GetAttribute('uniqueId')
                $uid -match '^[0-9a-fA-F]{1,8}$' -and
                [Convert]::ToInt32($uid,16) -eq [int]$row.hostUniqueId -and
                $_.GetAttribute('format') -ceq 'VST3' -and
                $_.GetAttribute('file') -ceq $module -and
                $_.GetAttribute('version') -ceq [string]$row.pluginVersion -and
                $_.GetAttribute('isInstrument') -in @('0','false')
            })
            if($matching.Count -ne 1){throw "Installed FactoryFX UID/version mismatch: $($row.productId)"}
            ++$count
            Write-Host "FACTORYFX_INSTALLED_NATIVE_SCAN_PASS product=$($row.productId) uid=$($row.hostUniqueId) version=$($row.pluginVersion)"
        } finally {
            if($null -ne $child){
                if(!$child.HasExited){$child.Kill();[void]$child.WaitForExit(5000)}
                $child.Dispose()
            }
            Remove-Item -LiteralPath $receiptDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    if($fxManifest.distribution -ceq 'production-three' -and $count -ne 3){
        throw "Alpha.33 FactoryFX installed scanner did not find all three modules"
    }
    if($fxManifest.distribution -ceq 'production-four' -and $count -ne 4){
        throw "Standard FactoryFX installed scanner did not find all four modules"
    }
    return $count
}

$fixtureProcess=$null
$probeProcess=$null
$probeToken=[guid]::NewGuid().ToString('D')
$probeMarker=Join-Path $env:LOCALAPPDATA "ArSonMix\Updates\health\$probeToken.ready"
try {
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item -LiteralPath $exe -Destination (Join-Path $stage "verified-gui.exe")
    foreach($item in @("BUILD_PROVENANCE.json")){
        Copy-Item -LiteralPath (Join-Path $dist $item) -Destination (Join-Path $stage $item)
    }
    Copy-Item -LiteralPath "assets/icon/arsonmix-app.ico" -Destination (Join-Path $stage "arsonmix-app.ico")
    $factory=Join-Path $dist "FactoryFX"
    if(Test-Path -LiteralPath $factory -PathType Container){
        Copy-Item -LiteralPath $factory -Destination (Join-Path $stage "FactoryFX") -Recurse
    }
    # Official standard VB-CABLE archive must be downloaded, exact-SHA
    # verified and staged BEFORE compiling Setup. Fatal if it changed.
    & ./scripts/prepare_vbcable_package.ps1 -StageDirectory $stage
    & ./scripts/prepare_update_verifier.ps1 -StageDirectory $stage
    $vendor=Get-Content -LiteralPath "config/vbcable-official-package.json" -Raw -Encoding UTF8 | ConvertFrom-Json
    & $iscc "/Qp" "/DSourceRoot=$stage" "/DOutputRoot=$out" "/DSourceAppVersion=$appVersion" "/DVBCableSha256=$($vendor.sha256)" "installer/ArSonMix.iss"
    $setup=Join-Path $out "ArSonMix-Windows-x64-Setup-Preview.exe"
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
    Assert-InstalledFactoryFx "fresh-install"
    $factoryFxScanCount=Test-InstalledFactoryFxScanner $installedExe
    $installedManifest=Get-Content -LiteralPath (Join-Path $install "BUILD_PROVENANCE.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    if($installedManifest.sourceCommit -ne $sha){throw "Wrong installed provenance"}
    foreach($item in @("gh.exe","verifier.json","LICENSE.txt","THIRD-PARTY-LICENSES.txt")){
        $staged=Join-Path (Join-Path $stage "UpdateVerifier") $item
        $actual=Join-Path (Join-Path $install "UpdateVerifier") $item
        if((Get-FileHash -LiteralPath $staged).Hash -cne (Get-FileHash -LiteralPath $actual).Hash){throw "Installed update verifier payload differs: $item"}
    }
    $diagnosticHelper=Join-Path $install "ArSonMixStartupDiagnostics.ps1"
    if ((Get-FileHash -LiteralPath $diagnosticHelper -Algorithm SHA256).Hash.ToLowerInvariant() -ne
        $manifest.startupDiagnosticsSha256) {
        throw "Installed diagnostics helper differs from the verified portable source"
    }
    # Verify the genuine installed GUI enters the approved native WebView.
    # Unique probe GUID inhibits device discovery, plugin restore and output.
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$installedExe
    $info.WorkingDirectory=$install
    $info.UseShellExecute=$false
    $info.EnvironmentVariables['ARSONMIX_UPDATE_PROBE']=$probeToken
    $probeProcess=[Diagnostics.Process]::Start($info)
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    while (!(Test-Path -LiteralPath $probeMarker -PathType Leaf) -and
           !$probeProcess.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
    }
    if (!(Test-Path -LiteralPath $probeMarker -PathType Leaf) -or
        (Get-Content -LiteralPath $probeMarker -Raw).Trim() -cne 'GUI_READY_NO_AUDIO' -or
        !$probeProcess.WaitForExit(12000) -or $probeProcess.ExitCode -ne 0) {
        try {
            & $diagnosticHelper | Write-Host
            $diagnosticReport=Join-Path $env:LOCALAPPDATA 'ArSonMix\diagnostics\windows-startup-report.txt'
            Get-Content -LiteralPath $diagnosticReport -TotalCount 80 | Write-Host
        } catch { Write-Warning $_ }
        throw "Installed ArSonMix did not complete its native GUI-only startup probe"
    }
    Write-Host "INSTALLER_NATIVE_GUI_READY_NO_AUDIO: runner build $([Environment]::OSVersion.Version.Build)"
    # Full AGPL and third-party terms live in ArSonMix.exe and Setup itself.
    # The installed directory must have NO redundant top-level legal files.
    & python scripts/verify_embedded_legal.py --binary $installedExe
    if($LASTEXITCODE -ne 0){throw "Installed EXE missing exact-source embedded licenses"}
    foreach($name in @("LICENSE","LICENSE-NOTICE.txt",
        "LICENSE-SIGNALSMITH-STRETCH.txt","LICENSE-SIGNALSMITH-LINEAR.txt",
        "OFL-LICENSE-Inter.txt","THIRD_PARTY_8D_SONKU_HRTF.md")){
        if(Test-Path -LiteralPath (Join-Path $install $name)){
            throw "Installer left a redundant top-level legal document: $name"
        }
    }
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
    # Upgrade fixture: migration from the old alpha.24 footprint must not
    # leave the old top-level licenses/README in the installed application.
    $legacyLooseFiles=@("LICENSE","LICENSE-NOTICE.txt",
        "LICENSE-SIGNALSMITH-STRETCH.txt","LICENSE-SIGNALSMITH-LINEAR.txt",
        "OFL-LICENSE-Inter.txt","THIRD_PARTY_8D_SONKU_HRTF.md","README.md")
    foreach($name in $legacyLooseFiles){
        "legacy-installer-document" | Set-Content -LiteralPath (Join-Path $install $name)
    }
    $installSentinel=Join-Path $install "arsonmix-user-file.keep"
    "preserve-user-files-across-upgrade" | Set-Content -LiteralPath $installSentinel
    $upgradeLog=Join-Path $env:RUNNER_TEMP ("arsonmix-upgrade-"+[guid]::NewGuid().ToString("N")+".log")
    $upgrade=Start-Process -FilePath $setup -ArgumentList @(
        "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART","/CURRENTUSER",
        "/NOCLOSEAPPLICATIONS",
        "/DIR=$install",('/LOG="' + $upgradeLog + '"')
    ) -Wait -PassThru
    $fixtureProcess.Refresh()
    $closeMarker=Test-Path -LiteralPath $fixtureClosed
    $closeText=if($closeMarker){(Get-Content -LiteralPath $fixtureClosed -Raw).Trim()}else{"MISSING"}
    $installedAfter=(Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "INNO_UPGRADE_DIAG: installerExit=$($upgrade.ExitCode) oldProcessExited=$($fixtureProcess.HasExited) gracefulMarker=$closeText restoredExeShaOk=$($installedAfter -eq $appHash)"
    $acceptedSaves=@("WM_CLOSE_SAVED","RM_QUERY_SAVED","RM_ENDSESSION_SAVED","INSTALLER_HANDSHAKE_SAVED")
    if($upgrade.ExitCode -ne 0 -or -not $fixtureProcess.HasExited -or
       -not $closeMarker -or -not ($acceptedSaves -contains $closeText)){
        if(Test-Path -LiteralPath $upgradeLog){
            Get-Content -LiteralPath $upgradeLog |
              Select-String -Pattern 'Restart Manager|close application|closing application|file in use|ArSonMix|error|installing' |
              Select-Object -Last 45 |
              ForEach-Object {Write-Host "INNO_RESTART_MANAGER_LOG: $($_.Line)"}
        }
        throw "In-place update did not gracefully close the running ArSonMix fixture (see $upgradeLog)"
    }
    if((Get-FileHash -LiteralPath $installedExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $appHash -or
       -not (Test-Path -LiteralPath $installSentinel) -or
       -not (Test-Path -LiteralPath $sentinel) -or
       (Test-Path -LiteralPath (Join-Path $install "portable_mode.txt"))){
        throw "In-place update failed verified EXE replacement or data preservation"
    }
    foreach($name in $legacyLooseFiles){
        if(Test-Path -LiteralPath (Join-Path $install $name)){
            throw "Old loose legal file survived in-place update: $name"
        }
    }
    Assert-InstalledFactoryFx "in-place-upgrade"
    Write-Host "INNO_GRACEFUL_UPDATE_PASS: running app Windows session close, verified PE replacement, user files preserved"
    $uninstall=Join-Path $install "unins000.exe"
    if(-not (Test-Path -LiteralPath $uninstall -PathType Leaf)){throw "Uninstaller missing"}
    $stop=Start-Process -FilePath $uninstall -ArgumentList @(
        "/VERYSILENT","/SUPPRESSMSGBOXES","/NORESTART"
    ) -Wait -PassThru
    if($stop.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $sentinel)){
        throw "Uninstaller failed or erased user Studio data"
    }
    [ordered]@{
        sourceCommit=$sha;appVersion=$appVersion;guiSha256=$appHash
        installerSha256=(Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
        certification="PREVIEW__INSTALLER_INTEGRITY_AND_UPDATE_TESTED";mode="PER_USER_NO_AUTOARM"
        updaterTest="WM_CLOSE_IN_PLACE_SHA256_AND_USER_DATA_PRESERVED"
        factoryFxInstall="VERIFIED_LOCK_AND_RESOURCE_HASHES_FRESH_AND_UPGRADE"
        factoryFxInstalledScanCount=$factoryFxScanCount
        factoryFxInstalledScan=if($factoryFxScanCount -gt 0){"NATIVE_INSTALLED_EXE_UID_VERSION_MATCH"}else{"NOT_APPLICABLE_CORE_ONLY"}
        guiStartup="NATIVE_WEBVIEW_READY_NO_AUDIO"
        guiStartupRunnerBuild=[Environment]::OSVersion.Version.Build
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out "INSTALLER_PROVENANCE.json") -Encoding UTF8
    if($IncludePortableUpdater){
        $portableVerifier=Join-Path $dist 'UpdateVerifier'
        if(Test-Path -LiteralPath $portableVerifier){throw 'Portable verifier destination already exists'}
        Copy-Item -LiteralPath (Join-Path $stage 'UpdateVerifier') -Destination $portableVerifier -Recurse
        Copy-Item -LiteralPath 'installer/ArSonMixAutoUpdate.ps1' -Destination $dist
        'ARSONMIX_PORTABLE_LAUNCHER_V2' | Set-Content -LiteralPath (Join-Path $dist 'portable_launcher_v2.txt') -Encoding utf8
        Write-Host 'PORTABLE_RELEASE_UPDATE_DOWNLOADER_STAGED'
    }
    Write-Host "INSTALLER_RC_VERIFIED: first install, graceful running-app graceful update, SHA-matched PE, uninstall, private Studio intact"
    Write-Host "OWNER_FIELD_AUDIO_LISTENING_REPORTED__PER_BUILD_HARDWARE_REPORT_SEPARATE"
}
finally {
    if($null -ne $probeProcess) {
        # CI-owned child only. The production updater never kills ArSonMix.
        if(!$probeProcess.HasExited) {
            $probeProcess.Kill()
            [void]$probeProcess.WaitForExit(5000)
        }
        $probeProcess.Dispose()
    }
    Remove-Item -LiteralPath $probeMarker -Force -ErrorAction SilentlyContinue
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
