# SPDX-License-Identifier: AGPL-3.0-only
# Opt-in FAT ONLY: two genuine historical ArSonMix releases, update,
# rollback, user-data retention. Not used by the low-cost PR smoke path.
# This requires a disposable GitHub Windows runner, NEVER a user PC.
param([string]$CandidateInstaller='', [string]$CandidateProvenance='', [string]$CandidateTag='', [string]$CandidateSha='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -cne 'true' -or
   $env:RUNNER_OS -cne 'Windows' -or
   [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    throw 'REAL_RELEASE_FAT_REQUIRES_DISPOSABLE_GITHUB_WINDOWS_RUNNER'
}
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
# Release metadata is an API request, not a public asset download. Anonymous
# GitHub Windows runner IPs can exhaust the unauthenticated 60/hour quota
# even when every CTest, EXE, ZIP and Inno installer gate has passed.
# Use ONLY the scoped, masked, read-only Actions token for metadata.
# Absence of credentials is a hard failure, not permission to skip FAT.
if([string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
    throw 'GENUINE_RELEASE_METADATA_AUTH_REQUIRED'
}
$releaseHeaders=@{
    'User-Agent'='ArSonMix-GenuineFAT/1'
    Accept='application/vnd.github+json'
    'X-GitHub-Api-Version'='2022-11-28'
    Authorization="Bearer $env:GITHUB_TOKEN"
}
$repo='masarray/arsonmix'
$matrix=@(
    @{tag='v0.5.0-alpha.4';sha='7447d82e84a52e583c7e4c30b9a7bbcded1abd11'},
    @{tag='v0.5.0-alpha.5';sha='d7cf109ad1d45f0666431ce748d72572dd49facc'}
)
$candidateMode= -not [string]::IsNullOrWhiteSpace($CandidateInstaller)
if($candidateMode){
    if($CandidateTag -cnotmatch '^v[0-9]+\.[0-9]+\.[0-9]+-beta\.[0-9]+$' -or
       $CandidateSha -cnotmatch '^[a-f0-9]{40}$' -or $CandidateSha -cne $env:GITHUB_SHA -or
       -not (Test-Path -LiteralPath $CandidateInstaller -PathType Leaf) -or
       -not (Test-Path -LiteralPath $CandidateProvenance -PathType Leaf)){
        throw 'BETA_MIGRATION_REQUIRES_EXACT_LOCAL_CANDIDATE'
    }
    $matrix=@(@{tag='v0.5.0-alpha.33';sha='213d2a4b861cd592c41c3ce6a4b318bf347b9e7b'})
}
$root=Join-Path $env:RUNNER_TEMP ('arsonmix-genuine-upgrade-'+[guid]::NewGuid().ToString('N'))
$packages=Join-Path $root 'packages'
$install=Join-Path $root 'installed'
$studio=Join-Path $root 'private-Studio'
[IO.Directory]::CreateDirectory($packages) | Out-Null
[IO.Directory]::CreateDirectory($install) | Out-Null
[IO.Directory]::CreateDirectory($studio) | Out-Null
$sentinel=Join-Path $studio 'proof-user-session.smix'
$installSentinel=Join-Path $install 'USER_PRESERVE_ME.txt'
[IO.File]::WriteAllText($sentinel,'real-release-upgrade-and-rollback')
[IO.File]::WriteAllText($installSentinel,'first-party-unrecognized-user-file')
$profileSentinel=$null
if($candidateMode){
    $profile=Join-Path $env:APPDATA 'AR-MIX-Studio'
    [IO.Directory]::CreateDirectory($profile) | Out-Null
    $profileSentinel=Join-Path $profile ('beta-migration-'+[guid]::NewGuid().ToString('N')+'.smix')
    [IO.File]::WriteAllText($profileSentinel,'preserve-real-studio-file')
}
function Assert-True([bool]$ok,[string]$message) {
    if(-not $ok){throw $message}
}
function Release-Asset([object]$release,[string]$name) {
    $found=@($release.assets | Where-Object {
        $_.name -ceq $name -and $_.state -ceq 'uploaded' -and
        $_.digest -cmatch '^sha256:[a-f0-9]{64}$' -and
        $_.browser_download_url -ceq ("https://github.com/$repo/releases/download/$($release.tag_name)/$name")
    })
    Assert-True ($found.Count -eq 1) "Published release lacks a unique verified $name"
    return $found[0]
}
function Fetch-Verified([object]$asset,[string]$destination) {
    $size=[int64]$asset.size
    Assert-True ($size -ge 1 -and $size -le 209715200) "Unsafe published artifact size"
    $url=[uri]$asset.browser_download_url
    Assert-True ($url.Scheme -ceq 'https' -and $url.Host -ceq 'github.com') "Unexpected source hostname"
    Invoke-WebRequest -Uri $url -OutFile $destination -TimeoutSec 180 -MaximumRedirection 5
    Assert-True ((Get-Item -LiteralPath $destination).Length -eq $size) "Downloaded asset truncated"
    $hash=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-True ($hash -ceq ([string]$asset.digest).Substring(7)) "Downloaded asset SHA256 mismatch"
}
function Run-Silent([string]$installer,[string]$label) {
    Assert-True (Test-Path -LiteralPath $installer) "Missing $label installer"
    $arguments=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER',
                 '/NOCLOSEAPPLICATIONS',('/DIR="'+$install+'"'))
    $p=Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru
    try {
        Assert-True ($p.ExitCode -eq 0) "$label installation failed with $($p.ExitCode)"
    } finally {$p.Dispose()}
}
function Verify-Installed([string]$sha,[string]$label,[string]$expectedGui='') {
    $exe=Join-Path $install 'ArSonMix.exe'
    $manifest=Join-Path $install 'BUILD_PROVENANCE.json'
    Assert-True ((Test-Path -LiteralPath $exe) -and (Test-Path -LiteralPath $manifest)) "$label missing native PE/manifest"
    $proof=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    Assert-True ($proof.sourceCommit -ceq $sha) "$label wrong installed source commit"
    Assert-True ([string]$proof.exeSha256 -cmatch '^[a-f0-9]{64}$') "$label invalid EXE digest"
    Assert-True ((Get-Item -LiteralPath $exe).Length -eq [int64]$proof.guiExeBytes) "$label EXE size mismatch"
    Assert-True ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $proof.exeSha256) "$label EXE hash mismatch"
    Assert-True ((Get-Content -LiteralPath $sentinel -Raw) -ceq 'real-release-upgrade-and-rollback') "$label private .smix changed"
    Assert-True ((Get-Content -LiteralPath $installSentinel -Raw) -ceq 'first-party-unrecognized-user-file') "$label install-local user file deleted"
    if($expectedGui){Assert-True ($proof.exeSha256 -ceq $expectedGui) "$label unexpected installed executable"}
    if($candidateMode){Assert-True ((Get-Content -LiteralPath $profileSentinel -Raw) -ceq 'preserve-real-studio-file') "$label real Studio data changed"}
}
$downloaded=@()
try {
    foreach($item in $matrix) {
        $tag=$item.tag
        $uri="https://api.github.com/repos/$repo/releases/tags/$tag"
        $release=Invoke-RestMethod -Uri $uri -Headers $releaseHeaders -TimeoutSec 45
        Assert-True (-not $release.draft -and $release.tag_name -ceq $tag -and
                     $release.target_commitish -ceq $item.sha) "Unexpected historical published release identity"
        $name="ArSonMix-$tag-Windows-x64-Setup.exe"
        $installerAsset=Release-Asset $release $name
        $proofAsset=Release-Asset $release 'INSTALLER_PROVENANCE.json'
        $sumAsset=Release-Asset $release 'SHA256SUMS.txt'
        $dir=Join-Path $packages $tag
        [IO.Directory]::CreateDirectory($dir) | Out-Null
        $installer=Join-Path $dir $name
        $proofFile=Join-Path $dir 'INSTALLER_PROVENANCE.json'
        $sums=Join-Path $dir 'SHA256SUMS.txt'
        Fetch-Verified $proofAsset $proofFile
        Fetch-Verified $sumAsset $sums
        $proof=Get-Content -LiteralPath $proofFile -Raw | ConvertFrom-Json
        $digest=([string]$installerAsset.digest).Substring(7)
        Assert-True ($proof.sourceCommit -ceq $item.sha -and
                     $proof.installerSha256 -ceq $digest) "Release installer provenance disagrees with official SHA"
        $line=$digest+'  '+$name
        Assert-True (@(Get-Content -LiteralPath $sums | Where-Object {$_ -ceq $line}).Count -eq 1) "Published release SHA256SUMS mismatch"
        Fetch-Verified $installerAsset $installer
        $gui=''
        if($candidateMode){
            Assert-True ($digest -ceq '7e8d2849fb14ddd6d17cf9a3ec7543e85a88e22296f5995814578ac0c195937b') 'Pinned Alpha.33 rollback digest changed'
            & gh attestation verify $installer --repo masarray/arsonmix --signer-workflow masarray/arsonmix/.github/workflows/alpha-preview-publish.yml --source-ref "refs/heads/releases/alpha/$tag" --source-digest $item.sha --signer-digest $item.sha --deny-self-hosted-runners
            if($LASTEXITCODE -ne 0){throw 'ALPHA33_ROLLBACK_ATTESTATION_FAILED'}
            $gui=$proof.guiSha256
        }
        $downloaded+=@{sha=$item.sha;tag=$tag;installer=$installer;gui=$gui}
    }
    if($candidateMode){
        $local=Get-Content -LiteralPath $CandidateProvenance -Raw | ConvertFrom-Json
        $localHash=(Get-FileHash -LiteralPath $CandidateInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-True ($local.sourceCommit -ceq $CandidateSha -and $local.installerSha256 -ceq $localHash -and
                     $local.appVersion -ceq $CandidateTag.Substring(1) -and $local.guiSha256 -cmatch '^[a-f0-9]{64}$') 'Local Beta candidate provenance mismatch'
        $downloaded+=@{sha=$CandidateSha;tag=$CandidateTag;installer=(Resolve-Path -LiteralPath $CandidateInstaller).Path;gui=$local.guiSha256}
    }
    Run-Silent $downloaded[0].installer ($downloaded[0].tag+' initial')
    Verify-Installed $downloaded[0].sha ($downloaded[0].tag+' initial') $downloaded[0].gui
    Write-Host 'GENUINE_RELEASE_BASELINE_PASS'
    Run-Silent $downloaded[1].installer ($downloaded[1].tag+' upgrade')
    Verify-Installed $downloaded[1].sha ($downloaded[1].tag+' upgrade') $downloaded[1].gui
    if($candidateMode){
        $arp=Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\ArSonMix.LiveStudio_is1' -ErrorAction Stop
        Assert-True ($arp.DisplayVersion -ceq $CandidateTag.Substring(1)) 'Installed Beta display version mismatch'
        $installedUpdater=Join-Path $install 'ArSonMixAutoUpdate.ps1'
        Assert-True ((Get-FileHash -LiteralPath $installedUpdater -Algorithm SHA256).Hash -ceq
                     (Get-FileHash -LiteralPath 'installer/ArSonMixAutoUpdate.ps1' -Algorithm SHA256).Hash) 'Installed Beta updater differs from reviewed source'
    }
    Write-Host 'GENUINE_RELEASE_UPGRADE_PASS'
    Run-Silent $downloaded[0].installer ($downloaded[0].tag+' rollback')
    Verify-Installed $downloaded[0].sha ($downloaded[0].tag+' rollback') $downloaded[0].gui
    if($candidateMode){Write-Host 'BETA_ALPHA33_MIGRATION_AND_ROLLBACK_PASS'}
    Write-Host 'GENUINE_RELEASE_ROLLBACK_PASS'
} finally {
    $uninstall=Join-Path $install 'unins000.exe'
    if(Test-Path -LiteralPath $uninstall) {
        try {
            $p=Start-Process -FilePath $uninstall -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -PassThru -Wait
            if($p){$p.Dispose()}
        } catch {Write-Warning 'FAT fixture uninstall failed; GitHub runner remains disposable'}
    }
    # This is a unique RUNNER_TEMP directory created by this script,
    # never the real installation or any ArSonMix user profile.
    if($profileSentinel -and (Test-Path -LiteralPath $profileSentinel)){Remove-Item -LiteralPath $profileSentinel -Force}
    if(Test-Path -LiteralPath $root){Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue}
}
