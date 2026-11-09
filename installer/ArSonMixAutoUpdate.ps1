# SPDX-License-Identifier: AGPL-3.0-only
# ArSonMix automatic update supervisor. Installed next to ArSonMix.exe by Inno.
# Independent of JUCE/audio callback; starts from an installed GUI only.
# Downloads while the host is active; applies ONLY after normal host exit.
param([ValidateRange(0,2147483647)][int]$OwnerProcessId=0,
      [switch]$PolicySelfTest,
      [switch]$ApplyStaged,
      [switch]$RecoveryMode)
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
# Network, hashing and installer orchestration must yield CPU to live ASIO.
try { [Diagnostics.Process]::GetCurrentProcess().PriorityClass =
    [Diagnostics.ProcessPriorityClass]::BelowNormal } catch { }

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
function Test-StagedAsset([string]$path,[object]$asset) {
    try {
        if($null -eq $asset -or $asset.digest -cnotmatch '^sha256:[a-f0-9]{64}$' -or
           [long]$asset.size -lt 2097152 -or [long]$asset.size -gt 209715200 -or
           -not (Test-Path -LiteralPath $path -PathType Leaf)) {return $false}
        $file=Get-Item -LiteralPath $path
        if($file.Length -ne [long]$asset.size){return $false}
        return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ceq
            ([string]$asset.digest).Substring(7)
    } catch {return $false}
}

function Should-RetryInstaller([int]$attempts,[bool]$success,[bool]$oldRemains) {
    # Only a completed, nonzero-exit installer is eligible. A timeout
    # throws and must not spawn a second installer while the first runs.
    return (-not $success -and $oldRemains -and $attempts -ge 1 -and $attempts -lt 2)
}
function Get-InstalledImageStatus([object]$build) {
    try {
        $hash=[string]$build.exeSha256
        $bytes=[int64]$build.guiExeBytes
        if($hash -cnotmatch '^[a-f0-9]{64}$' -or $bytes -lt 1048576 -or $bytes -gt 209715200){return 'unknown'}
        if((Get-Item -LiteralPath $exe -ErrorAction Stop).Length -ne $bytes){return 'damaged'}
        if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -cne $hash){return 'damaged'}
        return 'healthy'
    } catch {return 'unknown'}
}
function Classify-InterruptedTransaction([string]$phase,[string]$actual,[string]$previous,[string]$candidate) {
    if(@('installing','installer-failed','health-failed') -cnotcontains $phase){return ''}
    if($previous -cmatch '^[a-f0-9]{40}$' -and $actual -ceq $previous){return 'previous-retained'}
    if($candidate -cmatch '^[a-f0-9]{40}$' -and $actual -ceq $candidate){return 'candidate-unverified'}
    return 'unknown-installation'
}

function Test-CompleteStagedTransaction([object]$transaction) {
    try {
        if($transaction.phase -cne 'staged' -or
           $null -eq (Version-Key ([string]$transaction.candidateTag)) -or
           $null -eq (Version-Key ([string]$transaction.previousTag))){return $false}
        $newName="ArSonMix-$($transaction.candidateTag)-Windows-x64-Setup.exe"
        $oldName="ArSonMix-$($transaction.previousTag)-Windows-x64-Setup.exe"
        if($transaction.newAsset.name -cne $newName -or
           $transaction.previousAsset.name -cne $oldName){return $false}
        $folder=Join-Path $root 'stage'
        return ((Test-StagedAsset (Join-Path $folder $newName) $transaction.newAsset) -and
                (Test-StagedAsset (Join-Path $folder $oldName) $transaction.previousAsset))
    } catch {return $false}
}

function Clear-StagedVendorFiles([object]$transaction) {
    # Never recursively delete a directory or touch private Studio/.smix.
    $folder=Join-Path $root 'stage'
    foreach($tag in @([string]$transaction.previousTag,[string]$transaction.candidateTag)) {
        if($null -eq (Version-Key $tag)){continue}
        foreach($name in @("ArSonMix-$tag-Windows-x64-Setup.exe","SHA256SUMS-$tag.txt",
                           "INSTALLER_PROVENANCE-$tag.json")) {
            Remove-Item -LiteralPath (Join-Path $folder $name) -Force -ErrorAction SilentlyContinue
        }
    }
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

function Test-NoActiveMixer {
    foreach($process in [Diagnostics.Process]::GetProcessesByName('ArSonMix')) {
        try {
            if([string]::Equals([IO.Path]::GetFullPath($process.MainModule.FileName),
                $exe,[StringComparison]::OrdinalIgnoreCase)){return $false}
        } catch {return $false} # Unreadable image identity cannot authorize file replacement.
        finally {$process.Dispose()}
    }
    return $true
}
function Show-RecoveryFailure([string]$reason) {
    Log-Result ('RECOVERY_FAILED '+$reason)
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        $nl=[Environment]::NewLine
        [Windows.Forms.MessageBox]::Show(
            "ArSonMix could not repair itself safely."+$nl+$nl+
            "Your Studio sessions and plugins were not deleted."+$nl+$nl+
            "Use the official ArSonMix installer if this continues."+$nl+$nl+
            "Reason: "+$reason,
            'ArSonMix - Safe Recovery',
            [Windows.Forms.MessageBoxButtons]::OK,
            [Windows.Forms.MessageBoxIcon]::Information) | Out-Null
    } catch { }
}
function Restore-InstalledRelease([string]$sourceSha) {
    if(-not (Test-NoActiveMixer)){throw 'Another ArSonMix session is still active'}
    # A separately invoked recovery shortcut works when EXE is missing.
    # Match current SHA against GitHub's actual published release rather
    # than trusting local update state as installation authority.
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/1';
               'X-GitHub-Api-Version'='2022-11-28'}
    $releases=@(Invoke-RestMethod -Uri $api -Headers $headers -TimeoutSec 25)
    $sourceRelease=$null
    foreach($release in $releases) {
        if([string]$release.target_commitish -ceq $sourceSha -and
           $null -ne (Approved-Installer $release)) {
            if($null -eq $sourceRelease -or
               (Compare-Version (Version-Key $release.tag_name)
                                (Version-Key $sourceRelease.tag_name)) -gt 0){
                $sourceRelease=$release
            }
        }
    }
    if($null -eq $sourceRelease){throw 'No published, verified release matches this installation'}
    $asset=Approved-Installer $sourceRelease
    $destination=Join-Path (Join-Path $root 'stage') ([string]$asset.name)
    Download-Release-Verified $sourceRelease $asset $destination
    if(-not (Test-StagedAsset $destination $asset)){throw 'Recovery installer SHA check failed'}
    if(-not (Test-NoActiveMixer)){throw 'An active mixer appeared during repair'}
    # Missing-EXE mode is NOT normal auto-update and never closes a mixer.
    if(-not (Run-Installer $destination -AllowMissingExeRecovery)){
        throw 'Verified recovery installer was not accepted by Windows'
    }
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf) -or
       [string](Read-Json $provenance).sourceCommit -cne $sourceSha){
        throw 'Repair did not restore the expected release'
    }
    if(-not (Launch-WithHealth $sourceSha)){
        throw 'Repaired application did not pass the GUI startup check'
    }
    Log-Result ('RECOVERY_HEALTHY '+$sourceRelease.tag_name)
    # Keep verified installer cached for next recovery; no Studio cleanup.
}

function Run-Installer([string]$file,[switch]$AllowMissingExeRecovery) {
    # Inno performs exact-path process shutdown and EXE replacement preflight.
    # Its silent [Run] is disabled: only this supervisor relaunches once.
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)){return $false}
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$file
    $info.Arguments='/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /NOCLOSEAPPLICATIONS /ARSONMIXAUTOUPDATE /DIR="'+$PSScriptRoot+'"'
    if($AllowMissingExeRecovery){$info.Arguments+=' /ARSONMIXRECOVERY'}
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $process=[Diagnostics.Process]::Start($info)
    if($null -eq $process){return $false}
    try {
        if(-not $process.WaitForExit(120000)){
            # This Inno process is still able to replace program files.
            # Never start another GUI or a rollback installer concurrently.
            Fail 'Installer is still running after timeout; no relaunch or rollback'
        }
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
    try {$process=[Diagnostics.Process]::Start($info)}
    catch {Log-Result 'UPDATED_EXE_FAILED_TO_START';return $false}
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
    # Positive and negative staged binary integrity tests. These execute
    # under synthetic TEMP; never use or modify the installed program.
    $tmp=Join-Path ([IO.Path]::GetTempPath()) ('ArSonMix-Updater-Policy-'+[guid]::NewGuid().ToString('N'))
    try {
        [IO.Directory]::CreateDirectory($tmp) | Out-Null
        $fixturePath=Join-Path $tmp 'test-installer.exe'
        [IO.File]::WriteAllBytes($fixturePath,(New-Object byte[] 2097152))
        $goodHash=(Get-FileHash -LiteralPath $fixturePath -Algorithm SHA256).Hash.ToLowerInvariant()
        $fixtureAsset=[pscustomobject]@{digest=('sha256:'+$goodHash);size=2097152}
        if(-not (Test-StagedAsset $fixturePath $fixtureAsset)) {throw 'Valid staged EXE rejected'}
        $fixtureAsset.digest='sha256:'+('0'*64)
        if(Test-StagedAsset $fixturePath $fixtureAsset){throw 'Tampered staged EXE accepted'}
        $fixtureAsset.digest='sha256:'+$goodHash
        $fixtureAsset.size=2097153
        if(Test-StagedAsset $fixturePath $fixtureAsset){throw 'Short staged EXE accepted'}
        $fixtureAsset.size=2097152
        $priorExe=$exe
        try {
            $exe=$fixturePath
            $validBuild=[pscustomobject]@{exeSha256=$goodHash;guiExeBytes=2097152}
            if((Get-InstalledImageStatus $validBuild) -cne 'healthy'){throw 'Healthy image rejected'}
            $validBuild.exeSha256='0'*64
            if((Get-InstalledImageStatus $validBuild) -cne 'damaged'){throw 'Tampered image not detected'}
            $validBuild.exeSha256=$goodHash
            $validBuild.guiExeBytes=2097153
            if((Get-InstalledImageStatus $validBuild) -cne 'damaged'){throw 'Truncated image not detected'}
            $validBuild.guiExeBytes=0
            if((Get-InstalledImageStatus $validBuild) -cne 'unknown'){throw 'Unknown manifest not fenced'}
            if((Classify-InterruptedTransaction 'installing' ('1'*40) ('1'*40) ('2'*40)) -cne 'previous-retained'){
                throw 'Old EXE recovery not classified'
            }
            if((Classify-InterruptedTransaction 'installing' ('2'*40) ('1'*40) ('2'*40)) -cne 'candidate-unverified'){
                throw 'Candidate recovery not classified'
            }
        } finally {$exe=$priorExe}
        [IO.File]::Delete($fixturePath)
        if(Test-StagedAsset $fixturePath $fixtureAsset){throw 'Missing staged EXE accepted'}
    } finally {
        if(Test-Path -LiteralPath $tmp){Remove-Item -LiteralPath $tmp -Recurse -Force}
    }
    # Offline stage reuse must require BOTH full-size byte-verified EXEs.
    $previousRoot=$root
    try {
        $root=$tmp
        $stageDir=Join-Path $root 'stage'
        [IO.Directory]::CreateDirectory($stageDir) | Out-Null
        $tag='v0.5.0-alpha.6'
        $stageFile=Join-Path $stageDir "ArSonMix-$tag-Windows-x64-Setup.exe"
        [IO.File]::WriteAllBytes($stageFile,(New-Object byte[] 2097152))
        $hash=(Get-FileHash -LiteralPath $stageFile -Algorithm SHA256).Hash.ToLowerInvariant()
        $stored=[pscustomobject]@{name="ArSonMix-$tag-Windows-x64-Setup.exe";size=2097152;digest="sha256:$hash"}
        $transaction=[pscustomobject]@{phase='staged';candidateTag=$tag;previousTag=$tag;
            newAsset=$stored;previousAsset=$stored}
        if(-not (Test-CompleteStagedTransaction $transaction)){throw 'Verified stage not reused'}
        $stored.digest='sha256:'+('0'*64)
        if(Test-CompleteStagedTransaction $transaction){throw 'Corrupted staged transaction reused'}
    } finally {$root=$previousRoot}
    if(-not (Should-RetryInstaller 1 $false $true)){throw 'First transient retry rejected'}
    if(Should-RetryInstaller 2 $false $true){throw 'Repeated installer retry accepted'}
    if(Should-RetryInstaller 1 $false $false){throw 'Missing old version retried'}
    if(Should-RetryInstaller 1 $true $true){throw 'Successful install retried'}
    Write-Output 'ARSONMIX_UPDATER_RETRY_POLICY_PASS'
    Write-Output 'ARSONMIX_UPDATER_CACHE_AND_RECOVERY_PASS'

    Write-Output 'ARSONMIX_UPDATER_SELF_REPAIR_POLICY_PASS'
    Write-Output 'ARSONMIX_UPDATER_STAGED_INTEGRITY_PASS'
    Write-Output 'ARSONMIX_UPDATER_POLICY_PASS'
    exit 0
}

try {
    if($RecoveryMode -and -not (Test-Path -LiteralPath $provenance -PathType Leaf)) {
        Show-RecoveryFailure 'Installation version metadata is missing'
        exit 0
    }
    if(-not (Test-Path -LiteralPath $provenance -PathType Leaf) -or
       (-not $RecoveryMode -and -not (Test-Path -LiteralPath $exe -PathType Leaf)) -or
       (-not $ApplyStaged -and -not $RecoveryMode -and -not (Owns-Exe $OwnerProcessId))) {exit 0}
    # No update worker from portable builds, different installation roots,
    # arbitrary scripts outside {app}, or second ArSonMix updater instance.
    if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'portable_mode.txt')){exit 0}
    $mutex=[Threading.Mutex]::new($false,'Local\ArSonMix.AutoUpdater.v1')
    try {
        $locked=$mutex.WaitOne($(if($ApplyStaged -or $RecoveryMode){60000}else{0}))
    } catch [Threading.AbandonedMutexException] {
        # Previous worker was killed. WaitOne owns the mutex despite throwing.
        $locked=$true
        Log-Result 'ABANDONED_MUTEX_RECLAIMED'
    }
    if(-not $locked){exit 0}
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $file=Read-Json $provenance
    $installedSha=[string]$file.sourceCommit
    if($installedSha -cnotmatch '^[a-f0-9]{40}
    $state=[pscustomobject]@{lastCheckUtc='';lastAttemptUtc='';blockedTag='';lastInstalledTag='';phase='idle';operation='upgrade';installAttempts=0;retryAfterUtc='';
       candidateTag='';candidateSha='';previousSha='';previousTag='';
       newAsset=$null;previousAsset=$null}
    if(Test-Path -LiteralPath $statePath -PathType Leaf) {
        try {
            $read=Read-Json $statePath
            if($null -eq $read -or $null -eq $read.PSObject.Properties['phase']){throw 'Invalid updater state'}
        } catch {
            # Quarantine only the updater's own metadata, never user files.
            $quarantine=$statePath+'.invalid.'+[guid]::NewGuid().ToString('N')
            [IO.File]::Move($statePath,$quarantine)
            Log-Result 'INVALID_UPDATER_STATE_QUARANTINED'
            $read=$null
        }
        foreach($key in @('lastCheckUtc','lastAttemptUtc','blockedTag','lastInstalledTag','phase','operation','installAttempts','retryAfterUtc','candidateTag','candidateSha','previousSha','previousTag','newAsset','previousAsset')) {
            if($null -ne $read -and $read.PSObject.Properties.Name -contains $key){$state.$key=$read.$key}
        }
    }
    if($RecoveryMode) {
        # Explicit Repair always verifies and reapplies the published
        # application files. EXE may be intact while FactoryFX or packaged
        # WebView resources are missing. No uninstall or user-data deletion.
        try {Restore-InstalledRelease $installedSha}
        catch {Show-RecoveryFailure $_.Exception.Message}
        exit 0
    }
    $recovery=Classify-InterruptedTransaction ([string]$state.phase) $installedSha ([string]$state.previousSha) ([string]$state.candidateSha)
    if($recovery) {
        if($state.candidateTag){$state.blockedTag=$state.candidateTag}
        $state.phase=$recovery
        Write-State $state
        Log-Result ('RECOVERED_TRANSACTION_STATUS '+$recovery)
        if($recovery -ceq 'unknown-installation'){exit 0}
    }
    if(-not $ApplyStaged) {
    # A staged transaction stays staged across app opens. Installation
    # always rechecks publication/revocation before touching any files.
    if($state.phase -ceq 'staged' -and $state.previousSha -ceq $installedSha){
        if(Test-CompleteStagedTransaction $state){
            Log-Result 'STAGED_READY_FOR_NORMAL_EXIT'
            exit 0
        }
        # Incomplete/altered cached installers must be downloaded afresh.
        # Never attempt an installer solely because metadata says "staged".
        $state.phase='staging-retry'
        $state.lastCheckUtc=''
        $state.lastAttemptUtc=''
        Write-State $state
        Log-Result 'STAGED_CACHE_INVALID_REDOWNLOAD'
    }
    if($state.phase -ceq 'idle' -and $state.candidateTag -and
       $state.lastInstalledTag -ceq $state.candidateTag) {
        Clear-StagedVendorFiles $state
    }
    $imageStatus=Get-InstalledImageStatus $file
    $now=[DateTime]::UtcNow
    $attempt=[DateTime]::MinValue
    if($state.lastAttemptUtc -and [DateTime]::TryParse([string]$state.lastAttemptUtc,[ref]$attempt) -and
       $now -ge $attempt.ToUniversalTime() -and
       $now.Subtract($attempt.ToUniversalTime()).TotalMinutes -lt 20){exit 0}
    $checked=[DateTime]::MinValue
    if($imageStatus -cne 'damaged' -and $state.lastCheckUtc -and
       [DateTime]::TryParse([string]$state.lastCheckUtc,[ref]$checked) -and
       $now -ge $checked.ToUniversalTime() -and
       $now.Subtract($checked.ToUniversalTime()).TotalHours -lt 6){exit 0}
    $state.lastAttemptUtc=$now.ToString('o')
    Write-State $state
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/1';
               'X-GitHub-Api-Version'='2022-11-28'}
    $releases=@(Invoke-RestMethod -Uri $api -Headers $headers -TimeoutSec 25)
    $state.lastCheckUtc=[DateTime]::UtcNow.ToString('o')
    Write-State $state
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
    $operation='upgrade'
    if($null -eq $new) {
        if($imageStatus -cne 'damaged'){Log-Result 'UP_TO_DATE';exit 0}
        # Repair only a cryptographically proven EXE mismatch. Use the
        # same verified Inno release in place; never uninstall user data.
        $new=$current
        $operation='repair'
        Log-Result 'STAGING_VERIFIED_SAME_VERSION_REPAIR'
    }
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
    $state.operation=$operation
    $state.installAttempts=0
    $state.retryAfterUtc=''
    $state.candidateTag=[string]$new.tag_name
    $state.candidateSha=[string]$new.target_commitish
    $state.previousTag=[string]$current.tag_name
    $state.previousSha=$installedSha
    $state.newAsset=[pscustomobject]@{name=$newAsset.name;digest=$newAsset.digest;size=$newAsset.size}
    $state.previousAsset=[pscustomobject]@{name=$oldAsset.name;digest=$oldAsset.digest;size=$oldAsset.size}
    Write-State $state
    Log-Result ('STAGED '+$new.tag_name+' WAITING_FOR_NORMAL_EXIT')
    exit 0 # Checker ends here. Never keep PowerShell resident during mixing.
    }
    # Post-close supervisor: no resident updater during live audio.
    if($state.phase -cne 'staged' -or $state.previousSha -cne $installedSha){exit 0}
    if($state.operation -cnotin @('upgrade','repair')){Fail 'Unknown update operation'}
    if($state.operation -ceq 'repair' -and $state.candidateSha -cne $installedSha){
        Fail 'Repair must use the installed release'
    }
    $retryAt=[DateTime]::MinValue
    if($state.retryAfterUtc -and
       [DateTime]::TryParse([string]$state.retryAfterUtc,[ref]$retryAt) -and
       [DateTime]::UtcNow -lt $retryAt.ToUniversalTime()){
        Log-Result 'INSTALLER_RETRY_DEFERRED_COOLDOWN'
        exit 0
    }
    if($state.candidateSha -cnotmatch '^[a-f0-9]{40}$' -or
       $null -eq (Version-Key ([string]$state.candidateTag)) -or
       $null -eq (Version-Key ([string]$state.previousTag)) -or
       $state.blockedTag -ceq $state.candidateTag) {Fail 'Invalid persisted update transaction'}
    $newName='ArSonMix-'+$state.candidateTag+'-Windows-x64-Setup.exe'
    $oldName='ArSonMix-'+$state.previousTag+'-Windows-x64-Setup.exe'
    if($state.newAsset.name -cne $newName -or
       $state.previousAsset.name -cne $oldName) {Fail 'Unsafe staged installer identity'}
    $stage=Join-Path $root 'stage'
    $newPath=Join-Path $stage $newName
    $oldPath=Join-Path $stage $oldName
    foreach($pair in @(@($newPath,$state.newAsset),@($oldPath,$state.previousAsset))) {
        $path=[string]$pair[0]
        $asset=$pair[1]
        if(-not (Test-StagedAsset $path $asset)) {
            Fail 'Offline staged install/rollback bytes not verified'
        }
    }
    # Re-query public release to reject a revoked or replaced candidate.
    # No network is needed *during* live audio; if offline on close, defer.
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/1';
               'X-GitHub-Api-Version'='2022-11-28'}
    $liveReleases=@(Invoke-RestMethod -Uri $api -Headers $headers -TimeoutSec 25)
    $approved=$null
    foreach($release in $liveReleases) {
        if($release.tag_name -ceq $state.candidateTag -and
           $release.target_commitish -ceq $state.candidateSha) {
            $approved=Approved-Installer $release
            break
        }
    }
    if($null -eq $approved -or $approved.digest -cne $state.newAsset.digest -or
       [long]$approved.size -ne [long]$state.newAsset.size){
        # A withdrawn release must not be retried on every shutdown.
        # Permit another newly published, attested release on next startup.
        $state.blockedTag=$state.candidateTag
        $state.phase='revoked'
        $state.lastCheckUtc=''
        $state.lastAttemptUtc=''
        Write-State $state
        Log-Result 'RELEASE_REVOKED_UPDATE_DEFERRED'
        exit 0
    }
    $new=[pscustomobject]@{tag_name=$state.candidateTag;target_commitish=$state.candidateSha}
    $oldAsset=$state.previousAsset
    $newAsset=$state.newAsset
    # Never send shutdown event or WM_CLOSE from the updater itself.
    # A live singer/session cannot be interrupted by release publication.
    try {
        $hostProcess=[Diagnostics.Process]::GetProcessById($OwnerProcessId)
        try {
            if(-not $hostProcess.WaitForExit(120000)) {exit 0} # bounded normal exit
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
    $state.installAttempts=[int]$state.installAttempts+1
    Write-State $state
    $upgradeOK=Run-Installer $newPath
    $actualSha=''
    try {$actualSha=[string](Read-Json $provenance).sourceCommit} catch {}
    if($upgradeOK -and $actualSha -ceq $new.target_commitish -and
       (Launch-WithHealth ([string]$new.target_commitish))) {
        $state.lastInstalledTag=$new.tag_name
        $state.phase='idle'
        $state.retryAfterUtc=''
        $state.installAttempts=0
        $state.blockedTag=''
        Write-State $state
        Log-Result ($(if($state.operation -ceq 'repair'){'REPAIRED '}else{'HEALTHY '})+$new.tag_name)
        Clear-StagedVendorFiles $state
        exit 0
    }
    # A transient, completed Inno rejection can retry ONCE on a later
    # normal exit. Timeout/exception never qualifies for a second installer.
    $oldRemains=($actualSha -ceq $installedSha) -and (Test-Path -LiteralPath $exe -PathType Leaf)
    if(Should-RetryInstaller ([int]$state.installAttempts) ([bool]$upgradeOK) ([bool]$oldRemains)) {
        $state.phase='staged'
        $state.retryAfterUtc=[DateTime]::UtcNow.AddMinutes(30).ToString('o')
        Write-State $state
        Log-Result ('INSTALLER_TRANSIENT_FAILURE_RETRY_NEXT_EXIT '+$new.tag_name)
        try {[Diagnostics.Process]::Start($exe) | Out-Null} catch {
            Log-Result 'PREVIOUS_EXE_RELAUNCH_FAILED'
        }
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
    if($state.operation -ceq 'repair') {
        # Another copy of the same installer cannot act as rollback.
        Log-Result ('REPAIR_HEALTH_FAILED_BLOCKED '+$new.tag_name)
        exit 0
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
