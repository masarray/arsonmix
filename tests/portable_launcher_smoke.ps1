# SPDX-License-Identifier: AGPL-3.0-only
# Actual stable-supervisor smoke with synthetic initial extraction; no signed
# candidate substitution and no native product claim.
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('ArSonMix launcher fixture '+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($root)|Out-Null
try{
    $exe=Join-Path $root 'ArSonMix-GUI10CH-P012-Windows-x64.exe'
    $code=@'
using System;
using System.IO;
public static class Fixture {
    public static int Main(string[] args) {
        if (Environment.GetEnvironmentVariable("ARSONMIX_PORTABLE_SUPERVISED")!="1") return 2;
        if (!String.IsNullOrEmpty(Environment.GetEnvironmentVariable("ARSONMIX_UPDATE_PROBE"))) return 3;
        File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"launch.receipt"),
            Environment.CurrentDirectory + "|" + String.Join(" ",args));
        return 0;
    }
}
'@
    Add-Type -TypeDefinition $code -OutputAssembly $exe -OutputType ConsoleApplication
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\installer\ArSonMixAutoUpdate.ps1') -Destination (Join-Path $root 'ArSonMixAutoUpdate.ps1')
    'ARSONMIX_PORTABLE_WORKSPACE_V1'|Set-Content -LiteralPath (Join-Path $root 'portable_mode.txt')
    'ARSONMIX_PORTABLE_LAUNCHER_V2'|Set-Content -LiteralPath (Join-Path $root 'portable_launcher_v2.txt')
    @{sourceCommit=('a'*40);executable=[IO.Path]::GetFileName($exe);
       guiExeBytes=(Get-Item -LiteralPath $exe).Length;exeSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()} |
       ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'BUILD_PROVENANCE.json')
    $studio=Join-Path $root 'Studio'
    [IO.Directory]::CreateDirectory($studio)|Out-Null
    $private=Join-Path $studio 'private.smix'
    [IO.File]::WriteAllText($private,'session sentinel')
    & (Join-Path $PSHOME 'powershell.exe') -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $root 'ArSonMixAutoUpdate.ps1') -LaunchPortable -PortableLaunchArgument '--safe-webview-profile'
    if($LASTEXITCODE -ne 0){throw 'Portable supervisor process failed'}
    $receipt=Join-Path $root 'launch.receipt'
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    while(-not (Test-Path -LiteralPath $receipt) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
    if(-not (Test-Path -LiteralPath $receipt) -or [IO.File]::ReadAllText($receipt) -cne ($root+'|--safe-webview-profile')){
        throw 'Stable supervisor failed to execute the synthetic bootstrap'
    }
    if([IO.File]::ReadAllText($private) -cne 'session sentinel'){throw 'Private Studio changed'}
    Write-Output 'ARSONMIX_PORTABLE_BOOTSTRAP_LAUNCH_PASS'
    # A committed candidate whose archive disappeared must roll back through
    # the production launcher, not a mocked signature or executable path.
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    do{
        $processes=[Diagnostics.Process]::GetProcessesByName('ArSonMix-GUI10CH-P012-Windows-x64')
        $running=$processes.Count -gt 0
        foreach($process in $processes){$process.Dispose()}
        if($running){Start-Sleep -Milliseconds 100}
    }while($running -and [DateTime]::UtcNow -lt $deadline)
    if($running){throw 'Synthetic bootstrap did not exit'}
    Remove-Item -LiteralPath $receipt
    $base=[pscustomobject]@{sourceCommit=('a'*40);tag='';digest='';bytes=0;proofId='';bundleHash='';rootHash=''}
    $bad=[pscustomobject]@{sourceCommit=('b'*40);tag='v0.5.0-alpha.999';digest=('sha256:'+('c'*64));bytes=2097152;
        proofId=('d'*32);bundleHash=('e'*64);rootHash=('f'*64)}
    $selectors=Join-Path $root '.ArSonMixUpdates\selectors'
    [IO.Directory]::CreateDirectory($selectors)|Out-Null
    @{schema='arsonmix.portable-selection.v1';generation=1;active=$bad;previous=$base;blockedDigest=''} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $selectors 'selection-000000000001.json')
    & (Join-Path $PSHOME 'powershell.exe') -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $root 'ArSonMixAutoUpdate.ps1') -LaunchPortable
    if($LASTEXITCODE -ne 0){throw 'Portable rollback supervisor process failed'}
    $deadline=[DateTime]::UtcNow.AddSeconds(10)
    while(-not (Test-Path -LiteralPath $receipt) -and [DateTime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 100}
    if(-not (Test-Path -LiteralPath $receipt) -or [IO.File]::ReadAllText($receipt) -cne ($root+'|')){
        throw 'Previous synthetic image was not launched after rollback'
    }
    $rollback=Get-Content -LiteralPath (Join-Path $selectors 'selection-000000000002.json') -Raw | ConvertFrom-Json
    if($rollback.generation -ne 2 -or $rollback.active.sourceCommit -cne $base.sourceCommit -or
        $rollback.blockedDigest -cne $bad.digest -or [IO.File]::ReadAllText($private) -cne 'session sentinel'){
        throw 'Portable rollback state or Studio invariant failed'
    }
    Write-Output 'ARSONMIX_PORTABLE_ROLLBACK_LAUNCH_PASS'
}finally{Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue}
