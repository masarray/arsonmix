# SPDX-License-Identifier: AGPL-3.0-only
# ArSonMix automatic update supervisor. Installed next to ArSonMix.exe by Inno.
# Independent of JUCE/audio callback; starts from an installed GUI only.
# Downloads while the host is active; applies ONLY after normal host exit.
param([Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$OwnerProcessId,
      [switch]$PolicySelfTest)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12

$repo='masarray/arsonmix'
$api='https://api.github.com/repos/masarray/arsonmix/releases?per_page=100'
$exe=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'ArSonMix.exe'))
$provenance=Join-Path $PSScriptRoot 'BUILD_PROVENANCE.json'
$local=$env:LOCALAPPDATA
if([string]::IsNullOrWhiteSpace($local)){exit 0}
$root=Join-Path $local 'ArSonMix\Updates'
$statePath=Join-Path $root 'state.json'
$logPath=Join-Path $root 'last-result.txt'
$mutex=$null
$locked=$false

function Log-Result([string]$message) {
    try {
        [IO.Directory]::CreateDirectory($root) | Out-Null
        [IO.File]::WriteAllText($logPath,([DateTime]::UtcNow.ToString('o')+' '+$message),[Text.Encoding]::UTF8)
    } catch { } # Observability must never break the mixer.
}
function Fail([string]$reason) {
    Log-Result ('FAIL_CLOSED '+$reason)
    throw $reason
}
function Read-Json([string]$path) {
    return (Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json)
}
function Write-State([object]$value) {
    $temp=$statePath+'.new'
    $json=ConvertTo-Json -InputObject $value -Depth 8 -Compress
    [IO.File]::WriteAllText($temp,$json,[Text.Encoding]::UTF8)
    if(Test-Path -LiteralPath $statePath -PathType Leaf) {
        [IO.File]::Replace($temp,$statePath,$null)
    } else { [IO.File]::Move($temp,$statePath) }
}
function Version-Key([string]$tag) {
    $m=[regex]::Match($tag,'^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-(alpha|beta|rc)\.(0|[1-9][0-9]*))?$')
    if(-not $m.Success){return $null}
    try {
        $base=[version]::new([int]$m.Groups[1].Value,[int]$m.Groups[2].Value,[int]$m.Groups[3].Value)
        $rank=switch($m.Groups[4].Value) { 'alpha' {0} 'beta' {1} 'rc' {2} default {3} }
        $n=if($m.Groups[5].Success){[int]$m.Groups[5].Value}else{0}
        return [pscustomobject]@{ Tag=$tag; Base=$base; Rank=$rank; Number=$n }
    } catch {return $null}
}
function Compare-Version([object]$a,[object]$b) {
    $base=$a.Base.CompareTo($b.Base)
    if($base -ne 0){return $base}
    if($a.Rank -ne $b.Rank){return [Math]::Sign($a.Rank-$b.Rank)}
    return [Math]::Sign($a.Number-$b.Number)
}
function Find-Asset([object]$release,[string]$name,[int64]$minBytes) {
    $items=@($release.assets | Where-Object {
        $_.name -ceq $name -and $_.state -ceq 'uploaded' -and
        $_.size -is [ValueType] -and [int64]$_.size -ge $minBytes -and
        [string]$_.digest -cmatch '^sha256:[a-f0-9]{64}$' -and
        $_.browser_download_url -ceq ("https://github.com/$repo/releases/download/$($release.tag_name)/$name")
    })
    if($items.Count -ne 1){return $null}
    return $items[0]
}
function Approved-Installer([object]$release) {
    if($null -eq $release -or $release.draft -or
       [string]$release.target_commitish -cnotmatch '^[a-f0-9]{40}$' -or
       -not (Version-Key ([string]$release.tag_name)) -or
       [string]::IsNullOrWhiteSpace([string]$release.published_at)){return $null}
    $tag=[string]$release.tag_name
    if($null -eq (Find-Asset $release 'INSTALLER_PROVENANCE.json' 1) -or
       $null -eq (Find-Asset $release 'SHA256SUMS.txt' 1)){return $null}
    return (Find-Asset $release "ArSonMix-$tag-Windows-x64-Setup.exe" 2097152)
}
function Download-Verified([object]$asset,[string]$destination) {
    $expectedHash=([string]$asset.digest).Substring(7)
    $expectedBytes=[int64]$asset.size
    if($expectedBytes -gt 209715200){Fail 'Asset exceeds 200MiB hard limit'}
    if(Test-Path -LiteralPath $destination -PathType Leaf) {
        $existing=Get-Item -LiteralPath $destination
        if($existing.Length -eq $expectedBytes -and
           (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $expectedHash) {
            return
        }
        Remove-Item -LiteralPath $destination -Force
    }
    $parent=[IO.Path]::GetDirectoryName($destination)
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    $part=$destination+'.partial'
    if(Test-Path -LiteralPath $part){Remove-Item -LiteralPath $part -Force}
    $request=[Net.HttpWebRequest]::Create([string]$asset.browser_download_url)
    $request.Timeout=30000
    $request.ReadWriteTimeout=30000
    $request.UserAgent='ArSonMix-SmartUpdater/1'
    $request.AllowAutoRedirect=$true
    $request.MaximumAutomaticRedirections=5
    $response=$null
    $streamIn=$null
    $streamOut=$null
    try {
        $response=$request.GetResponse()
        $uri=$response.ResponseUri
        if($uri.Scheme -cne 'https' -or
           @('github.com','release-assets.githubusercontent.com','objects.githubusercontent.com') -cnotcontains $uri.DnsSafeHost) {
            Fail 'Unexpected download redirect destination'
        }
        if($response.ContentLength -gt $expectedBytes){Fail 'Oversize release response'}
        $streamIn=$response.GetResponseStream()
        $streamOut=[IO.File]::Open($part,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $buffer=New-Object byte[] 65536
        [int64]$total=0
        while(($read=$streamIn.Read($buffer,0,$buffer.Length)) -gt 0) {
            $total+=$read
            if($total -gt $expectedBytes){Fail 'Response larger than release metadata'}
            $streamOut.Write($buffer,0,$read)
        }
        $streamOut.Flush($true)
        $streamOut.Dispose();$streamOut=$null
        if($total -ne $expectedBytes){Fail 'Incomplete release download'}
        $hash=(Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash.ToLowerInvariant()
        if($hash -cne $expectedHash){Fail 'Release SHA256 mismatch'}
        [IO.File]::Move($part,$destination)
    } finally {
        if($null -ne $streamOut){$streamOut.Dispose()}
        if($null -ne $streamIn){$streamIn.Dispose()}
        if($null -ne $response){$response.Dispose()}
        if(Test-Path -LiteralPath $part){Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue}
    }
}
function Download-Release-Verified([object]$release,[object]$installer,[string]$destination) {
    $tag=[string]$release.tag_name
    $prefix=[IO.Path]::GetDirectoryName($destination)
    $sum=Find-Asset $release 'SHA256SUMS.txt' 1
    $provenanceAsset=Find-Asset $release 'INSTALLER_PROVENANCE.json' 1
    if($null -eq $sum -or $null -eq $provenanceAsset){Fail 'Missing attested release metadata'}
    # Use per-tag metadata paths; never allow the second release to reuse
    # the first release's hashes, even if tags share the same base version.
    $sumPath=Join-Path $prefix ('SHA256SUMS-'+$tag+'.txt')
    $proofPath=Join-Path $prefix ('INSTALLER_PROVENANCE-'+$tag+'.json')
    Download-Verified $sum $sumPath
    Download-Verified $provenanceAsset $proofPath
    $proof=Read-Json $proofPath
    $hash=([string]$installer.digest).Substring(7)
    if($proof.sourceCommit -cne $release.target_commitish -or
       $proof.installerSha256 -cne $hash) {Fail 'Installer provenance does not match published source/digest'}
    $line=$hash+'  '+[string]$installer.name
    $matches=@(Get-Content -LiteralPath $sumPath -Encoding ASCII |
        Where-Object {$_ -ceq $line})
    if($matches.Count -ne 1){Fail 'SHA256SUMS does not attest exactly one chosen installer'}
    Download-Verified $installer $destination
}
function Owns-Exe([int]$processId) {
    try {
        $process=[Diagnostics.Process]::GetProcessById($processId)
        try {
            return [string]::Equals([IO.Path]::GetFullPath($process.MainModule.FileName),
                $exe,[StringComparison]::OrdinalIgnoreCase)
        } finally {$process.Dispose()}
    } catch {return $false}
}
function Run-Installer([string]$file) {
    # Inno performs exact-path process shutdown and EXE replacement preflight.
    # Its silent [Run] is disabled: only this supervisor relaunches once.
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)){return $false}
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$file
    $info.Arguments='/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /NOCLOSEAPPLICATIONS /DIR="'+$PSScriptRoot+'"'
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $process=[Diagnostics.Process]::Start($info)
    if($null -eq $process){return $false}
    try {
        if(-not $process.WaitForExit(120000)){return $false}
        return $process.ExitCode -eq 0
    } finally {$process.Dispose()}
}
function Launch-WithHealth([string]$expectedSha) {
    $token=[guid]::NewGuid().ToString('D')
    $healthDir=Join-Path $root 'health'
    [IO.Directory]::CreateDirectory($healthDir) | Out-Null
    $marker=Join-Path $healthDir ($token+'.ready')
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$exe
    $info.WorkingDirectory=$PSScriptRoot
    $info.UseShellExecute=$false
    $info.EnvironmentVariables['ARSONMIX_UPDATE_PROBE']=$token
    $process=[Diagnostics.Process]::Start($info)
    if($null -eq $process){return $false}
    try {
        $deadline=[DateTime]::UtcNow.AddSeconds(75)
        do {
            if($process.HasExited){return $false}
            if(Test-Path -LiteralPath $marker -PathType Leaf) {
                if((Get-Content -LiteralPath $marker -Raw).Trim() -ceq 'GUI_READY' -and
                   (Read-Json $provenance).sourceCommit -ceq $expectedSha) {
                    Start-Sleep -Seconds 2
                    $process.Refresh()
                    return (-not $process.HasExited)
                }
                return $false
            }
            Start-Sleep -Milliseconds 500
        } while([DateTime]::UtcNow -lt $deadline)
        # Never terminate a hung GUI to roll back: leave it for manual recovery.
        return $false
    } finally {
        $process.Dispose()
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
    }
}

if($PolicySelfTest) {
    # Deterministic offline policy fixtures; no host process, network,
    # installer, audio device or file mutation is permitted by this mode.
    $alpha=Version-Key 'v0.5.0-alpha.5'
    $next=Version-Key 'v0.5.0-alpha.6'
    $stable=Version-Key 'v0.5.0'
    if($null -eq $alpha -or $null -ne (Version-Key 'v0.5.0-alpha.5/evil') -or
       (Compare-Version $next $alpha) -le 0 -or
       (Compare-Version $stable $next) -le 0 -or
       (Compare-Version (Version-Key 'v1.0.0') $stable) -le 0) {
        throw 'Updater SemVer policy failed'
    }
    $tag='v0.5.0-alpha.6'
    $sha='a'*64
    $asset=[pscustomobject]@{name="ArSonMix-$tag-Windows-x64-Setup.exe";
        size=3000000;state='uploaded';digest="sha256:$sha";
        browser_download_url="https://github.com/$repo/releases/download/$tag/ArSonMix-$tag-Windows-x64-Setup.exe"}
    $sum=[pscustomobject]@{name='SHA256SUMS.txt';size=250;state='uploaded';
        digest="sha256:$sha";browser_download_url="https://github.com/$repo/releases/download/$tag/SHA256SUMS.txt"}
    $proof=[pscustomobject]@{name='INSTALLER_PROVENANCE.json';size=500;
        state='uploaded';digest="sha256:$sha";
        browser_download_url="https://github.com/$repo/releases/download/$tag/INSTALLER_PROVENANCE.json"}
    $release=[pscustomobject]@{tag_name=$tag;draft=$false;
        published_at='2026-11-09T00:00:00Z';target_commitish=('f'*40);
        assets=@($asset,$sum,$proof)}
    if($null -eq (Approved-Installer $release)){throw 'Valid release rejected'}
    $asset.browser_download_url='https://attacker.example/update.exe'
    if($null -ne (Approved-Installer $release)){throw 'Untrusted asset origin accepted'}
    $asset.browser_download_url="https://github.com/$repo/releases/download/$tag/$($asset.name)"
    $release.assets=@($asset,$asset,$sum,$proof)
    if($null -ne (Approved-Installer $release)){throw 'Duplicate installer accepted'}
    Write-Output 'ARSONMIX_UPDATER_POLICY_PASS'
    exit 0
}

try {
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf) -or
       -not (Test-Path -LiteralPath $provenance -PathType Leaf) -or
       -not (Owns-Exe $OwnerProcessId)) {exit 0}
    # No update worker from portable builds, different installation roots,
    # arbitrary scripts outside {app}, or second ArSonMix updater instance.
    if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'portable_mode.txt')){exit 0}
    $mutex=[Threading.Mutex]::new($false,'Local\ArSonMix.AutoUpdater.v1')
    $locked=$mutex.WaitOne(0)
    if(-not $locked){exit 0}
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $file=Read-Json $provenance
    $installedSha=[string]$file.sourceCommit
    if($installedSha -cnotmatch '^[a-f0-9]{40}$'){exit 0}
    $state=[pscustomobject]@{lastCheckUtc='';blockedTag='';lastInstalledTag='';phase='idle'}
    if(Test-Path -LiteralPath $statePath -PathType Leaf) {
        $read=Read-Json $statePath
        foreach($key in @('lastCheckUtc','blockedTag','lastInstalledTag','phase')) {
            if($read.PSObject.Properties.Name -contains $key){$state.$key=$read.$key}
        }
    }
    if($state.lastCheckUtc) {
        $checked=[DateTime]::MinValue
        if([DateTime]::TryParse([string]$state.lastCheckUtc,[ref]$checked) -and
            [DateTime]::UtcNow.Subtract($checked.ToUniversalTime()).TotalHours -lt 6 -and
            [DateTime]::UtcNow -ge $checked.ToUniversalTime() -and
            $state.phase -cne 'staged') {exit 0}
    }
    $state.lastCheckUtc=[DateTime]::UtcNow.ToString('o')
    Write-State $state
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/1';
               'X-GitHub-Api-Version'='2022-11-28'}
    $releases=@(Invoke-RestMethod -Uri $api -Headers $headers -TimeoutSec 25)
    $current=$null
    foreach($release in $releases) {
        if([string]$release.target_commitish -ceq $installedSha -and
           $null -ne (Approved-Installer $release)) {
            if($null -eq $current -or (Compare-Version (Version-Key $release.tag_name) (Version-Key $current.tag_name)) -gt 0) {
                $current=$release
            }
        }
    }
    if($null -eq $current){Log-Result 'NO_MATCHING_PUBLISHED_BASELINE';exit 0}
    $new=$null
    foreach($release in $releases) {
        if($null -eq (Approved-Installer $release)){continue}
        if(-not $current.prerelease -and $release.prerelease){continue}
        if((Compare-Version (Version-Key $release.tag_name) (Version-Key $current.tag_name)) -le 0){continue}
        if($null -eq $new -or (Compare-Version (Version-Key $release.tag_name) (Version-Key $new.tag_name)) -gt 0) {
            $new=$release
        }
    }
    if($null -eq $new){Log-Result 'UP_TO_DATE';exit 0}
    if($state.blockedTag -ceq $new.tag_name){Log-Result 'PREVIOUS_FAILURE_BLOCKED';exit 0}
    $oldAsset=Approved-Installer $current
    $newAsset=Approved-Installer $new
    $stage=Join-Path $root 'stage'
    $oldPath=Join-Path $stage ([string]$oldAsset.name)
    $newPath=Join-Path $stage ([string]$newAsset.name)
    Download-Release-Verified $new $newAsset $newPath
    # Rollback must be possible offline. Do not begin update without a
    # byte-verified previous installer.
    Download-Release-Verified $current $oldAsset $oldPath
    $state.phase='staged'
    Write-State $state
    Log-Result ('STAGED '+$new.tag_name+' WAITING_FOR_NORMAL_EXIT')
    # Never send shutdown event or WM_CLOSE from the updater itself.
    # A live singer/session cannot be interrupted by release publication.
    try {
        $hostProcess=[Diagnostics.Process]::GetProcessById($OwnerProcessId)
        try {
            if(-not $hostProcess.WaitForExit(43200000)) {exit 0} # 12h cap
        } finally {$hostProcess.Dispose()}
    } catch { } # Host already exited.
    # Inno is the only authority allowed to close/replace its own installed EXE.
    # If a new mixer instance is running, defer; do not hijack its session.
    foreach($process in [Diagnostics.Process]::GetProcessesByName('ArSonMix')) {
        try {
            if([string]::Equals([IO.Path]::GetFullPath($process.MainModule.FileName),$exe,
                [StringComparison]::OrdinalIgnoreCase)){Log-Result 'DEFER_NEW_SESSION';exit 0}
        } catch {Log-Result 'DEFER_UNKNOWN_PROCESS';exit 0}
        finally {$process.Dispose()}
    }
    if((Read-Json $provenance).sourceCommit -cne $installedSha) {Log-Result 'DEFER_VERSION_CHANGED';exit 0}
    $state.phase='installing'
    Write-State $state
    $upgradeOK=Run-Installer $newPath
    $actualSha=''
    try {$actualSha=[string](Read-Json $provenance).sourceCommit} catch {}
    if($upgradeOK -and $actualSha -ceq $new.target_commitish -and
       (Launch-WithHealth ([string]$new.target_commitish))) {
        $state.lastInstalledTag=$new.tag_name
        $state.phase='idle'
        $state.blockedTag=''
        Write-State $state
        Log-Result ('HEALTHY '+$new.tag_name)
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        exit 0
    }
    # An installer failure leaves the previous app intact or may be partial.
    # If the known-good source is still installed, relaunch it once.
    # Never silently retry the failing release on every startup.
    # If the new GUI still runs but did not acknowledge readiness, NEVER
    # kill it or overwrite its image. Block this release pending inspection.
    $state.blockedTag=$new.tag_name
    $state.phase=if($upgradeOK){'health-failed'}else{'installer-failed'}
    Write-State $state
    if($actualSha -ceq $installedSha) {
        Log-Result ('INSTALLER_FAILED_PREVIOUS_VERSION_RETAINED '+$new.tag_name)
        [Diagnostics.Process]::Start($exe) | Out-Null
        exit 0
    }
    foreach($process in [Diagnostics.Process]::GetProcessesByName('ArSonMix')) {
        try {
            if([string]::Equals([IO.Path]::GetFullPath($process.MainModule.FileName),$exe,
                [StringComparison]::OrdinalIgnoreCase)) {Log-Result 'HEALTH_FAILED_NEW_GUI_STILL_OPEN';exit 0}
        } catch {Log-Result 'HEALTH_FAILED_PROCESS_UNKNOWN';exit 0}
        finally {$process.Dispose()}
    }
    # Roll back by running the previously authenticated Inno installer,
    # not by copying arbitrary stale binaries over an installed application.
    if(Run-Installer $oldPath) {
        if((Read-Json $provenance).sourceCommit -ceq $installedSha) {
            $state.phase='rolled-back'
            Write-State $state
            Log-Result ('ROLLED_BACK '+$new.tag_name)
            [Diagnostics.Process]::Start($exe) | Out-Null
            exit 0
        }
    }
    Fail 'Health failed and verified rollback installer did not restore old source'
 } catch {
    # An interrupted/crashed updater must never repeatedly apply the same
    # candidate. Inno itself is fail-closed; a later normal app launch remains
    # usable even if the updater encountered an unexpected exception.
    try {
        if((Get-Variable -Name state -ErrorAction SilentlyContinue) -and
           (Get-Variable -Name new -ErrorAction SilentlyContinue) -and
           $null -ne $new -and $state.phase -ceq 'installing') {
            $state.blockedTag=$new.tag_name
            $state.phase='installer-failed'
            Write-State $state
        }
    } catch { }
    Log-Result ('FAIL_CLOSED '+$_.Exception.Message)
} finally {
    if($locked -and $null -ne $mutex){$mutex.ReleaseMutex()}
    if($null -ne $mutex){$mutex.Dispose()}
}
