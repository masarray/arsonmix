# SPDX-License-Identifier: AGPL-3.0-only
# ArSonMix automatic update supervisor. Installed next to ArSonMix.exe by Inno.
# Independent of JUCE/audio callback; starts from an installed GUI only.
# Downloads while the host is active; applies ONLY after normal host exit.
param([ValidateRange(0,2147483647)][int]$OwnerProcessId=0,
      [switch]$PolicySelfTest,
      [switch]$ApplyStaged,
      [switch]$RecoveryMode,
      [switch]$CheckNow,
      [switch]$PreparePortable,
      [switch]$LaunchPortable,
      [ValidatePattern('^$|^[a-f0-9]{32}$')][string]$ExitReceipt='',
      [ValidateSet('','--archive-mixer-session','--safe-webview-profile')][string]$PortableLaunchArgument='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if($LaunchPortable -and ($CheckNow -or $PreparePortable -or $ApplyStaged -or $RecoveryMode -or $PolicySelfTest)){throw 'LaunchPortable is launch-only'}
if($CheckNow -and ($ApplyStaged -or $RecoveryMode -or $PolicySelfTest)){throw 'CheckNow is check-only'}
if($PreparePortable -and ($CheckNow -or $ApplyStaged -or $RecoveryMode -or $PolicySelfTest)){throw 'PreparePortable is preparation-only'}
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12

$repo='masarray/arsonmix'
$api='https://api.github.com/repos/masarray/arsonmix/releases?per_page=100'
$portable=Test-Path -LiteralPath (Join-Path $PSScriptRoot 'portable_mode.txt') -PathType Leaf
$exeName=if($portable){'ArSonMix-GUI10CH-P012-Windows-x64.exe'}else{'ArSonMix.exe'}
$exe=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $exeName))
$provenance=Join-Path $PSScriptRoot 'BUILD_PROVENANCE.json'
$local=$env:LOCALAPPDATA
if([string]::IsNullOrWhiteSpace($local)){exit 0}
$portableStableRoot=$PSScriptRoot
if($portable){
    $folder=[IO.DirectoryInfo]::new($PSScriptRoot)
    if($folder.Name -cmatch '^[a-f0-9]{40}-[a-f0-9]{64}$' -and $null -ne $folder.Parent -and
       $folder.Parent.Name -ceq 'payloads' -and $null -ne $folder.Parent.Parent -and
       $folder.Parent.Parent.Name -ceq '.ArSonMixUpdates' -and $null -ne $folder.Parent.Parent.Parent){
        $stable=$folder.Parent.Parent.Parent
        $cap=Join-Path $stable.FullName 'portable_launcher_v2.txt'
        $marker=Join-Path $stable.FullName 'portable_mode.txt'
        $payloadMarker=Join-Path $PSScriptRoot 'portable_mode.txt'
        foreach($item in @($folder.FullName,$folder.Parent.FullName,$folder.Parent.Parent.FullName,$stable.FullName,$cap,$marker,$payloadMarker)){
            try{$info=Get-Item -LiteralPath $item -Force}catch{exit 0}
            if($info.Attributes -band [IO.FileAttributes]::ReparsePoint){exit 0}
        }
        if((Get-Item -LiteralPath $cap).Length -gt 128 -or (Get-Item -LiteralPath $marker).Length -gt 128 -or
           (Get-Item -LiteralPath $payloadMarker).Length -gt 128 -or
           (Get-Content -LiteralPath $cap -Raw).Trim() -cne 'ARSONMIX_PORTABLE_LAUNCHER_V2' -or
           (Get-Content -LiteralPath $marker -Raw).Trim() -cne 'ARSONMIX_PORTABLE_WORKSPACE_V1' -or
           (Get-Content -LiteralPath $payloadMarker -Raw).Trim() -cne 'ARSONMIX_PORTABLE_WORKSPACE_V1'){exit 0}
        $portableStableRoot=$stable.FullName
    }
}
$root=if($portable){Join-Path $portableStableRoot '.ArSonMixUpdates'}else{Join-Path $local 'ArSonMix\Updates'}
$statePath=Join-Path $root 'state.json'
# Consent is a usability acknowledgement, never a cryptographic trust boundary.
# The worker independently verifies both published installers before execution.
$approvalPath=Join-Path $root 'approved.json'
$exitReceiptPath=Join-Path $root 'normal-exit.json'
$logPath=Join-Path $root 'last-result.txt'
$mutex=$null
$locked=$false
$rollbackGuard=$null
$rollbackProof=$null
$portableOwner=$null
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
    $jsonStream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        if($jsonStream.Length -lt 1 -or $jsonStream.Length -gt 1048576){Fail 'JSON metadata exceeds 1MiB limit'}
        $reader=[IO.StreamReader]::new($jsonStream,[Text.UTF8Encoding]::new($false,$true),$true,4096,$true)
        try {return ($reader.ReadToEnd() | ConvertFrom-Json)} finally {$reader.Dispose()}
    } finally {$jsonStream.Dispose()}
}
function Write-State([object]$value) {
    Write-AtomicUpdateJson $statePath $value
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
# Older approved installations can fall beyond GitHub's first 100 releases.
# Traverse only bounded pages; fail closed rather than guess the baseline.
function Read-PublishedReleases([hashtable]$headers) {
    $result=[Collections.Generic.List[object]]::new()
    $tags=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for([int]$page=1; $page -le 10; $page++) {
        $uri=if($page -eq 1){$api}else{$api+'&page='+$page}
        $batch=Invoke-RestMethod -Uri $uri -Headers $headers -TimeoutSec 25
        if($null -eq $batch){Fail 'GitHub returned no release list'}
        $rows=@($batch)
        if($rows.Count -gt 100){Fail 'GitHub release page exceeds hard bound'}
        foreach($release in $rows) {
            if($null -eq $release -or
               $null -eq $release.PSObject.Properties['tag_name'] -or
               [string]::IsNullOrWhiteSpace([string]$release.tag_name) -or
               -not $tags.Add([string]$release.tag_name)){
                Fail 'Invalid or duplicated GitHub release page'
            }
            $result.Add($release)
        }
        if($rows.Count -lt 100){return ,$result.ToArray()}
    }
    Fail 'GitHub release history exceeds 1000 entries; defer rather than guess baseline'
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
# Digest-scoped partials cannot be reused across changed release assets.
function Get-PartialAssetPath([string]$destination,[string]$hash) {
    if($hash -cnotmatch '^[a-f0-9]{64}$'){throw 'Invalid partial asset identity'}
    return ($destination+'.'+$hash+'.partial')
}
function Get-ResumeResponseMode([int]$status,[string]$range,[long]$contentLength,[long]$offset,[long]$expected) {
    if($offset -lt 0 -or $expected -lt 1 -or $offset -ge $expected){return 'reject'}
    if($status -eq 200 -and ($contentLength -eq -1 -or $contentLength -eq $expected)){return 'restart'}
    if($status -eq 206 -and $offset -gt 0){
        $wanted='bytes '+$offset+'-'+($expected-1)+'/'+$expected
        if($range -ceq $wanted -and ($contentLength -eq -1 -or $contentLength -eq $expected-$offset)){return 'append'}
    }
    return 'reject'
}
function Test-RegularUpdateFile([string]$path) {
    try {
        $info=Get-Item -LiteralPath $path -Force -ErrorAction Stop
        return (-not $info.PSIsContainer -and ($info.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)
    } catch {return $false}
}
function Download-Verified([object]$asset,[string]$destination) {
    if([string]$asset.digest -cnotmatch '^sha256:[a-f0-9]{64}$'){Fail 'Invalid asset digest'}
    $expectedHash=([string]$asset.digest).Substring(7)
    $expectedBytes=[int64]$asset.size
    if($expectedBytes -lt 1 -or $expectedBytes -gt 209715200){Fail 'Asset exceeds 200MiB hard limit'}
    if(Test-Path -LiteralPath $destination) {
        if(-not (Test-RegularUpdateFile $destination)){Fail 'Unsafe cached asset path'}
        $existing=Get-Item -LiteralPath $destination
        if($existing.Length -eq $expectedBytes -and
           (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $expectedHash) {
            return
        }
        Remove-Item -LiteralPath $destination -Force
    }
    $parent=[IO.Path]::GetDirectoryName($destination)
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    if(((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){Fail 'Unsafe asset cache directory'}
    $part=Get-PartialAssetPath $destination $expectedHash
    # Only one partial per exact asset filename; discard superseded digests.
    $prefix=[IO.Path]::GetFileName($destination)+'.'
    foreach($stale in @(Get-ChildItem -LiteralPath $parent -File -Force)){
        if($stale.Name.StartsWith($prefix,[StringComparison]::Ordinal) -and
           $stale.Name.Substring($prefix.Length) -cmatch '^[a-f0-9]{64}\.partial$' -and
           $stale.FullName -cne $part -and (Test-RegularUpdateFile $stale.FullName)){
            Remove-Item -LiteralPath $stale.FullName -Force
        }
    }
    [long]$offset=0
    if(Test-Path -LiteralPath $part){
        if(-not (Test-RegularUpdateFile $part)){Fail 'Unsafe partial asset path'}
        $offset=(Get-Item -LiteralPath $part).Length
        if($offset -eq $expectedBytes -and
           (Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $expectedHash){
            [IO.File]::Move($part,$destination);return
        }
        if($offset -ge $expectedBytes){Remove-Item -LiteralPath $part -Force;$offset=0}
    }
    $uri=[Uri]$asset.browser_download_url
    $response=$null;$streamIn=$null;$streamOut=$null
    $discard=$false
    try {
        for($hop=0; $hop -le 5; $hop++){
            if($uri.Scheme -cne 'https' -or $uri.Port -ne 443 -or $uri.UserInfo -ne '' -or $uri.Fragment -ne '' -or
               @('github.com','release-assets.githubusercontent.com','objects.githubusercontent.com') -cnotcontains $uri.DnsSafeHost){
                $discard=$true;Fail 'Unexpected download redirect destination'
            }
            $request=[Net.HttpWebRequest]::Create($uri)
            $request.Timeout=30000;$request.ReadWriteTimeout=30000
            $request.UserAgent='ArSonMix-SmartUpdater/2'
            $request.AllowAutoRedirect=$false
            if($offset -gt 0){$request.AddRange($offset)}
            try {$response=$request.GetResponse()}
            catch [Net.WebException]{
                $errorResponse=$_.Exception.Response
                if($null -ne $errorResponse){
                    try {if([int]$errorResponse.StatusCode -eq 416){$discard=$true}} finally {$errorResponse.Dispose()}
                }
                throw
            }
            if([int]$response.StatusCode -notin @(301,302,303,307,308)){break}
            $location=$response.Headers['Location']
            $response.Dispose();$response=$null
            if($hop -eq 5 -or [string]::IsNullOrWhiteSpace($location)){$discard=$true;Fail 'Invalid release redirect chain'}
            $uri=[Uri]::new($uri,$location)
        }
        if($null -eq $response){Fail 'Missing release response'}
        $uri=$response.ResponseUri
        if($uri.Scheme -cne 'https' -or $uri.Port -ne 443 -or $uri.UserInfo -ne '' -or
           @('github.com','release-assets.githubusercontent.com','objects.githubusercontent.com') -cnotcontains $uri.DnsSafeHost){
            $discard=$true;Fail 'Unexpected download redirect destination'
        }
        $mode=Get-ResumeResponseMode ([int]$response.StatusCode) ([string]$response.Headers['Content-Range']) $response.ContentLength $offset $expectedBytes
        if($mode -ceq 'reject'){$discard=$true;Fail 'Invalid release range response'}
        if($mode -ceq 'restart'){
            # Servers may ignore Range. Replace, never append a full body.
            $offset=0
            $streamOut=[IO.File]::Open($part,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
        } else {
            $streamOut=[IO.File]::Open($part,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::None)
            if($streamOut.Length -ne $offset){$discard=$true;Fail 'Partial asset changed before resume'}
            [void]$streamOut.Seek($offset,[IO.SeekOrigin]::Begin)
            Log-Result ('RESUMING_VERIFIED_DOWNLOAD '+$offset+'/'+$expectedBytes)
        }
        $streamIn=$response.GetResponseStream()
        $buffer=New-Object byte[] 65536
        [long]$total=$offset
        while(($read=$streamIn.Read($buffer,0,$buffer.Length)) -gt 0) {
            $total+=$read
            if($total -gt $expectedBytes){$discard=$true;Fail 'Response larger than release metadata'}
            $streamOut.Write($buffer,0,$read)
        }
        $streamOut.Flush($true)
        $streamOut.Dispose();$streamOut=$null
        if($total -ne $expectedBytes){Fail 'Incomplete release download'}
        $hash=(Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash.ToLowerInvariant()
        if($hash -cne $expectedHash){$discard=$true;Fail 'Release SHA256 mismatch'}
        [IO.File]::Move($part,$destination)
    } finally {
        if($null -ne $streamOut){$streamOut.Dispose()}
        if($null -ne $streamIn){$streamIn.Dispose()}
        if($null -ne $response){$response.Dispose()}
        # Network interruption retains incomplete bytes only; protocol or
        # digest mismatch invalidates them. Nothing partial is executable.
        if($discard -and (Test-RegularUpdateFile $part)){Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue}
    }
}
# Independent signature trust. The installer ships a pinned upstream verifier;
# runtime never searches PATH, installs tools, uses user credentials or accepts
# a hash in place of an attestation. Same-user executable tampering is outside
# the per-user installation trust boundary.
function Get-AttestationVerifier([string]$installRoot=$PSScriptRoot) {
    $folder=Join-Path $installRoot 'UpdateVerifier'
    $path=Join-Path $folder 'gh.exe'
    $manifest=Join-Path $folder 'verifier.json'
    try {
        foreach($item in @($installRoot,$folder,$path,$manifest)){
            $info=Get-Item -LiteralPath $item -Force -ErrorAction Stop
            if(($info.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){return $null}
        }
        if(-not (Test-Path -LiteralPath $path -PathType Leaf)){return $null}
        $pin=Read-Json $manifest
        if($pin.version -cne '2.106.0' -or
           $pin.archiveSha256 -cne 'd406f442ca0baa3d3e9c695e9cded1aa48e67dad3294b8947e5ab84f980b4844' -or
           $pin.exeSha256 -cne '478be94eef02a4e71d2858b886f6f224494be0cc728668b7071a8e0a05fcaf7a' -or
           [long]$pin.exeBytes -lt 1MB -or [long]$pin.exeBytes -gt 100MB -or
           (Get-Item -LiteralPath $path).Length -ne [long]$pin.exeBytes -or
           (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $pin.exeSha256){return $null}
        return $path
    } catch {return $null}
}
function Get-AttestationArguments([string]$path,[string]$tag,[string]$sha,[switch]$PortableZip) {
    $suffix=if($PortableZip){'Portable.zip'}else{'Setup.exe'}
    if($tag -cnotmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-(alpha|beta)\.(0|[1-9][0-9]*)$' -or
       $null -eq (Version-Key $tag) -or $sha -cnotmatch '^[a-f0-9]{40}$' -or
       -not [IO.Path]::IsPathRooted($path) -or $path.Contains('"') -or
       [IO.Path]::GetFileName($path) -cne "ArSonMix-$tag-Windows-x64-$suffix"){return $null}
    # Derive the allowed branch solely from the already validated tag.
    $stage=if($tag -cmatch '-beta\.'){'beta'}else{'alpha'}
    return ('attestation verify "'+$path+'" --repo masarray/arsonmix'+
        ' --signer-workflow masarray/arsonmix/.github/workflows/alpha-preview-publish.yml'+
        ' --source-ref refs/heads/releases/'+$stage+'/'+$tag+
        ' --source-digest '+$sha+' --signer-digest '+$sha+' --deny-self-hosted-runners')
}
function Test-AttestationBundleUri([Uri]$uri,[string]$repositoryId) {
    # GitHub's newer API may return a signed Azure Blob URL instead of an
    # inline bundle. Only the documented attestation storage path is allowed.
    return ($null -ne $uri -and $uri.IsAbsoluteUri -and
        $uri.Scheme -ceq 'https' -and $uri.Port -eq 443 -and
        $uri.UserInfo -eq '' -and $uri.Fragment -eq '' -and
        $uri.DnsSafeHost -cmatch '^[a-z0-9]{3,24}\.blob\.core\.windows\.net$' -and
        $uri.AbsolutePath.StartsWith(('/attestations/'+$repositoryId+'/'),[StringComparison]::Ordinal) -and
        $uri.AbsolutePath -notmatch '%|\\|\.\.')
}
# Raw Snappy block transport used by GitHub attestation storage.
# https://github.com/google/snappy/blob/main/format_description.txt
# Bounded transport decoder only; the verifier owns cryptography.
function Expand-BoundedSnappy([byte[]]$inputBytes,[int]$maxBytes) {
    if($null -eq $inputBytes -or $inputBytes.Length -lt 1 -or $maxBytes -lt 1 -or $maxBytes -gt 262144){
        throw 'Invalid Snappy bounds'
    }
    $pos=0;[long]$length=0;$terminated=$false
    for($i=0;$i -lt 5;$i++){
        if($pos -ge $inputBytes.Length){throw 'Truncated Snappy length'}
        $b=[long]$inputBytes[$pos];$pos++
        $length=$length -bor (($b -band 127) -shl (7*$i))
        if($length -gt $maxBytes){throw 'Snappy expansion exceeds limit'}
        if(($b -band 128) -eq 0){$terminated=$true;break}
    }
    if(-not $terminated -or $length -lt 1){throw 'Invalid Snappy length'}
    $out=New-Object byte[] ([int]$length)
    $written=0
    while($pos -lt $inputBytes.Length -and $written -lt $length){
        $tag=[int]$inputBytes[$pos];$pos++
        $kind=$tag -band 3
        if($kind -eq 0){
            [long]$count=$tag -shr 2
            if($count -ge 60){
                $extra=[int]($count-59);$count=0
                if($pos+$extra -gt $inputBytes.Length){throw 'Truncated Snappy literal length'}
                for($j=0;$j -lt $extra;$j++){
                    $count=$count -bor ([long]$inputBytes[$pos] -shl (8*$j));$pos++
                }
            }
            $count++
            if($count -gt $length-$written -or $count -gt $inputBytes.Length-$pos){throw 'Invalid Snappy literal'}
            [Array]::Copy($inputBytes,$pos,$out,$written,[int]$count)
            $pos+=[int]$count;$written+=[int]$count
        } else {
            [long]$offset=0
            if($kind -eq 1){
                if($pos -ge $inputBytes.Length){throw 'Truncated Snappy copy'}
                $count=4+(($tag -shr 2) -band 7)
                $offset=(($tag -band 224) -shl 3) -bor [int]$inputBytes[$pos];$pos++
            } else {
                $count=1+($tag -shr 2)
                $extra=if($kind -eq 2){2}else{4}
                if($pos+$extra -gt $inputBytes.Length){throw 'Truncated Snappy copy offset'}
                for($j=0;$j -lt $extra;$j++){
                    $offset=$offset -bor ([long]$inputBytes[$pos] -shl (8*$j));$pos++
                }
            }
            if($offset -le 0 -or $offset -gt $written -or $count -gt $length-$written){throw 'Invalid Snappy backreference'}
            while($count -gt 0){
                $take=[int][Math]::Min([long]$count,$offset)
                [Array]::Copy($out,$written-[int]$offset,$out,$written,$take)
                $written+=$take;$count-=$take
            }
        }
    }
    if($written -ne $length -or $pos -ne $inputBytes.Length){throw 'Incomplete or trailing Snappy bytes'}
    return ,$out
}
function Read-BoundedAttestationJson([Uri]$uri,[int]$maxBytes,[switch]$Snappy) {
    # Both callers validate the exact host/path before reaching here. No
    # authorization headers, redirect following or logging signed URL queries.
    $request=[Net.HttpWebRequest]::Create($uri)
    $request.AllowAutoRedirect=$false
    $request.Timeout=25000
    $request.ReadWriteTimeout=25000
    $request.UserAgent='ArSonMix-SmartUpdater/2'
    $request.Accept='application/json, application/vnd.github+json'
    if($uri.DnsSafeHost -ceq 'api.github.com'){
        $request.Headers['X-GitHub-Api-Version']='2022-11-28'
    }
    $wireLimit=if($Snappy){$maxBytes+65536}else{$maxBytes}
    try {$response=$request.GetResponse()} catch {throw 'Public attestation download unavailable'}
    try {
        if([int]$response.StatusCode -ne 200 -or $response.ContentLength -gt $wireLimit){throw 'Unexpected public attestation response'}
        $stream=$response.GetResponseStream()
        $memory=[IO.MemoryStream]::new()
        try {
            $buffer=New-Object byte[] 16384
            while(($count=$stream.Read($buffer,0,$buffer.Length)) -gt 0){
                if($memory.Length+$count -gt $wireLimit){throw 'Public attestation exceeds size limit'}
                $memory.Write($buffer,0,$count)
            }
            $bytes=$memory.ToArray()
            if($Snappy){$bytes=Expand-BoundedSnappy $bytes $maxBytes}
            $json=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
            return ($json | ConvertFrom-Json)
        } finally {$memory.Dispose();$stream.Dispose()}
    } finally {$response.Dispose()}
}
function Convert-AttestationResponse([object]$response,[string]$destination,[string]$repositoryId='1432648251') {
    $rows=@($response.attestations)
    if($rows.Count -lt 1 -or $rows.Count -gt 30){throw 'No bounded public attestation bundle'}
    $lines=[Collections.Generic.List[string]]::new()
    $total=0
    foreach($row in $rows){
        if([string]$row.repository_id -cne $repositoryId){continue}
        $bundle=$null
        if($null -ne $row.PSObject.Properties['bundle'] -and $null -ne $row.bundle){
            $bundle=$row.bundle
        } elseif($null -ne $row.PSObject.Properties['bundle_url']){
            $uri=$null
            if(-not [Uri]::TryCreate([string]$row.bundle_url,[UriKind]::Absolute,[ref]$uri) -or
               -not (Test-AttestationBundleUri $uri $repositoryId)){throw 'Unexpected attestation bundle URL'}
            if(-not $uri.AbsolutePath.EndsWith('.json.sn',[StringComparison]::Ordinal)){throw 'Unexpected attestation encoding'}
            $bundle=Read-BoundedAttestationJson $uri 262144 -Snappy
        }
        if($null -eq $bundle){continue}
        $line=ConvertTo-Json -InputObject $bundle -Depth 60 -Compress
        $bytes=[Text.Encoding]::UTF8.GetByteCount($line)
        $total+=$bytes
        if($bytes -gt 262144 -or $total -gt 2MB){throw 'Oversized attestation bundle'}
        $lines.Add($line)
    }
    if($lines.Count -eq 0){throw 'No attestation from the expected repository'}
    [IO.File]::WriteAllLines($destination,$lines.ToArray(),[Text.UTF8Encoding]::new($false))
}
function Read-PublicAttestation([string]$digest,[string]$destination,
        [string]$repository='masarray/arsonmix',[string]$repositoryId='1432648251') {
    # cli/cli is only supplied by the build-side genuine signature smoke.
    # Production always uses the literal ArSonMix defaults below.
    if($digest -cnotmatch '^[a-f0-9]{64}$' -or
       $repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or
       $repositoryId -cnotmatch '^[0-9]+$'){throw 'Invalid attestation subject identity'}
    $uri=[Uri]::new("https://api.github.com/repos/$repository/attestations/sha256:$($digest)?per_page=30")
    $response=Read-BoundedAttestationJson $uri 2097152
    Convert-AttestationResponse $response $destination $repositoryId
}
function Invoke-IsolatedVerifier([string]$verifier,[string]$arguments,[string]$privateConfig) {
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$verifier
    $info.Arguments=$arguments
    $info.WorkingDirectory=$privateConfig
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true
    $info.RedirectStandardError=$true
    foreach($key in @('GH_TOKEN','GITHUB_TOKEN','GH_ENTERPRISE_TOKEN','GITHUB_ENTERPRISE_TOKEN','GH_DEBUG','GH_REPO','GH_BROWSER','GH_PAGER','PAGER')){
        $info.EnvironmentVariables.Remove($key)
    }
    $info.EnvironmentVariables['GH_CONFIG_DIR']=$privateConfig
    $info.EnvironmentVariables['GH_HOST']='github.com'
    $info.EnvironmentVariables['GH_PROMPT_DISABLED']='1'
    $info.EnvironmentVariables['GH_NO_UPDATE_NOTIFIER']='1'
    $info.EnvironmentVariables['GH_NO_EXTENSION_UPDATE_NOTIFIER']='1'
    $child=[Diagnostics.Process]::Start($info)
    if($null -eq $child){throw 'Cannot start bundled attestation verifier'}
    try {
        try {$child.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal} catch {}
        $out=$child.StandardOutput.ReadToEndAsync()
        $err=$child.StandardError.ReadToEndAsync()
        if(-not $child.WaitForExit(60000)){
            try {$child.Kill()} catch {}
            throw 'Attestation verifier timed out; update deferred'
        }
        if($child.ExitCode -ne 0){throw 'Installer cryptographic provenance not verified; update deferred'}
        $result=$out.GetAwaiter().GetResult()
        if($result.Length -gt 2MB){throw 'Oversized verifier output'}
        return $result
    } finally {$child.Dispose()}
}
function Test-VerifiedRepositoryIdentity([object]$results,[string]$repositoryId='1432648251') {
    $rows=@($results)
    if($rows.Count -lt 1 -or $rows.Count -gt 30){return $false}
    foreach($row in $rows){
        try {
            if([string]$row.verificationResult.signature.certificate.sourceRepositoryIdentifier -ceq $repositoryId){return $true}
        } catch {}
    }
    return $false
}
function Assert-InstallerAttestation([string]$path,[string]$tag,[string]$sha,[switch]$PortableZip,[string]$SaveProof='',[string]$VerifierRoot=$PSScriptRoot) {
    $arguments=Get-AttestationArguments $path $tag $sha -PortableZip:$PortableZip
    if($null -eq $arguments){Fail 'Invalid attestation subject identity'}
    $verifier=Get-AttestationVerifier $VerifierRoot
    if($null -eq $verifier){Fail 'Bundled update verifier missing or damaged; repair required'}
    # Private working directory prevents use of an existing gh config or token.
    # TUF refreshes the trusted roots through the pinned verifier, not through
    # the candidate release. Recheck the verifier while holding its file lease.
    $private=Join-Path $root ('verify-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($private) | Out-Null
    $lease=[IO.File]::Open($verifier,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
        if((Get-AttestationVerifier $VerifierRoot) -cne $verifier){Fail 'Bundled verifier changed before execution'}
        $bundle=Join-Path $private 'bundle.jsonl'
        $trusted=Join-Path $private 'trusted_root.jsonl'
        $digest=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        Read-PublicAttestation $digest $bundle
        $roots=Invoke-IsolatedVerifier $verifier 'attestation trusted-root --hostname github.com' $private
        if([string]::IsNullOrWhiteSpace($roots)){Fail 'No verified Sigstore trust roots'}
        [IO.File]::WriteAllText($trusted,$roots,[Text.UTF8Encoding]::new($false))
        $json=Invoke-IsolatedVerifier $verifier ($arguments+' --hostname github.com --bundle "'+$bundle+'" --custom-trusted-root "'+$trusted+'" --format json') $private
        if(-not (Test-VerifiedRepositoryIdentity ($json | ConvertFrom-Json))){Fail 'Attestation repository identity mismatch'}
        if($SaveProof){
            # The trust root is obtained by the pinned local verifier, never
            # from the ZIP. Only publish proof after actual signature success.
            if(Test-Path -LiteralPath $SaveProof){throw 'Portable proof already exists'}
            [IO.Directory]::CreateDirectory($SaveProof)|Out-Null
            foreach($name in @('bundle.jsonl','trusted_root.jsonl')){
                $from=Join-Path $private $name
                $to=Join-Path $SaveProof $name
                [IO.File]::Copy($from,$to,$false)
                $stream=[IO.File]::Open($to,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
                try{$stream.Flush($true)}finally{$stream.Dispose()}
            }
        }
    } finally {
        $lease.Dispose()
        Remove-Item -LiteralPath $private -Recurse -Force -ErrorAction SilentlyContinue
    }
}
function Test-ReleaseTransition([string]$operation,[object]$previous,[object]$candidate) {
    try {
        $a=Version-Key ([string]$previous.tag_name)
        $b=Version-Key ([string]$candidate.tag_name)
        if($null -eq $a -or $null -eq $b){return $false}
        if($operation -ceq 'repair'){
            return ($previous.tag_name -ceq $candidate.tag_name -and
                $previous.target_commitish -ceq $candidate.target_commitish)
        }
        return ($operation -ceq 'upgrade' -and (Compare-Version $b $a) -gt 0 -and
                ($previous.prerelease -or -not $candidate.prerelease))
    } catch {return $false}
}

function Download-Release-Verified([object]$release,[object]$installer,[string]$destination) {
    $tag=[string]$release.tag_name
    $prefix=[IO.Path]::GetDirectoryName($destination)
    $sum=Find-Asset $release 'SHA256SUMS.txt' 1
    $provenanceAsset=Find-Asset $release 'INSTALLER_PROVENANCE.json' 1
    if($null -eq $sum -or $null -eq $provenanceAsset -or
       [long]$sum.size -gt 1048576 -or [long]$provenanceAsset.size -gt 1048576){Fail 'Missing or oversized release metadata'}
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
    Assert-InstallerAttestation $destination $tag ([string]$release.target_commitish)
}
# Portable handoff: download signed ZIP only; never extract over a workspace.
function Approved-Portable([object]$release) {
    if($null -eq $release -or $release.draft -or
       [string]$release.target_commitish -cnotmatch '^[a-f0-9]{40}$' -or
       $null -eq (Version-Key ([string]$release.tag_name)) -or
       [string]::IsNullOrWhiteSpace([string]$release.published_at)){return $null}
    if($null -eq (Find-Asset $release 'RELEASE_TRUST-windows.json' 1) -or
       $null -eq (Find-Asset $release 'SHA256SUMS.txt' 1)){return $null}
    return (Find-Asset $release "ArSonMix-$($release.tag_name)-Windows-x64-Portable.zip" 2097152)
}
function Select-PortableRelease([object[]]$releases,[string]$installedSha) {
    $baseline=$null;$new=$null
    foreach($release in $releases){
        if($release.target_commitish -ceq $installedSha -and $null -ne (Approved-Portable $release)){
            if($null -eq $baseline -or (Compare-Version (Version-Key $release.tag_name) (Version-Key $baseline.tag_name)) -gt 0){$baseline=$release}
        }
    }
    if($null -eq $baseline){return [pscustomobject]@{baseline=$null;candidate=$null}}
    foreach($release in $releases){
        if($null -eq (Approved-Portable $release) -or (-not $baseline.prerelease -and $release.prerelease)){continue}
        if((Compare-Version (Version-Key $release.tag_name) (Version-Key $baseline.tag_name)) -le 0){continue}
        if($null -eq $new -or (Compare-Version (Version-Key $release.tag_name) (Version-Key $new.tag_name)) -gt 0){$new=$release}
    }
    return [pscustomobject]@{baseline=$baseline;candidate=$new}
}
function Download-PortableVerified([object]$release,[object]$asset,[string]$destination) {
    $tag=[string]$release.tag_name;$sha=[string]$release.target_commitish
    $parent=[IO.Path]::GetDirectoryName($destination)
    $sum=Find-Asset $release 'SHA256SUMS.txt' 1
    $manifest=Find-Asset $release 'RELEASE_TRUST-windows.json' 1
    if($null -eq $sum -or $null -eq $manifest -or $sum.size -gt 1048576 -or $manifest.size -gt 1048576){Fail 'Portable metadata missing or oversized'}
    $sumPath=Join-Path $parent ("SHA256SUMS-$tag.txt")
    $manifestPath=Join-Path $parent ("RELEASE_TRUST-$tag.json")
    Download-Verified $sum $sumPath
    Download-Verified $manifest $manifestPath
    $proof=Read-Json $manifestPath
    $hash=([string]$asset.digest).Substring(7)
    $rows=@($proof.assets | Where-Object {$_.name -ceq $asset.name -and $_.sha256 -ceq $hash -and [long]$_.bytes -eq [long]$asset.size})
    if($proof.schema -cne 'arsonmix.release-trust.v1' -or $proof.repository -cne 'masarray/arsonmix' -or
       $proof.platform -cne 'windows' -or $proof.tag -cne $tag -or $proof.sourceCommit -cne $sha -or $rows.Count -ne 1){Fail 'Portable release manifest identity mismatch'}
    $line=$hash+'  '+[string]$asset.name
    if(@(Get-Content -LiteralPath $sumPath -Encoding ASCII | Where-Object {$_ -ceq $line}).Count -ne 1){Fail 'Portable checksum mismatch'}
    Download-Verified $asset $destination
    Assert-InstallerAttestation $destination $tag $sha -PortableZip
}
# A ZIP is exposed to the operator only after the cryptographic check.
# The private stage keeps its exact release filename for signer policy.
function Publish-VerifiedPortableDownload([string]$stageFile,[string]$destination,[object]$asset) {
    if(-not (Test-RegularUpdateFile $stageFile) -or -not (Test-StagedAsset $stageFile $asset)){Fail 'Portable staged bytes changed'}
    $parent=[IO.Path]::GetDirectoryName($destination)
    [IO.Directory]::CreateDirectory($parent)|Out-Null
    if(((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){Fail 'Unsafe portable download folder'}
    if(Test-Path -LiteralPath $destination){
        if(-not (Test-RegularUpdateFile $destination)){Fail 'Unsafe portable download destination'}
        if(Test-StagedAsset $destination $asset){return}
        # A previous ZIP could contain user changes. Never overwrite it.
        Fail 'Portable download destination already exists with different bytes'
    }
    [IO.File]::Move($stageFile,$destination)
}
function Test-PortableCacheLayout {
    foreach($folder in @($PSScriptRoot,$portableStableRoot,$root,(Join-Path $root 'stage'),(Join-Path $root 'downloads'),(Join-Path $root 'payloads'),(Join-Path $root 'proofs'),(Join-Path $root 'selectors'))){
        if(Test-Path -LiteralPath $folder){
            $info=Get-Item -LiteralPath $folder -Force
            if(-not $info.PSIsContainer -or ($info.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){return $false}
        }
    }
    foreach($path in @($statePath,($statePath+'.new'),$logPath,(Join-Path $root 'prepared.json'),$approvalPath,$exitReceiptPath)){
        if((Test-Path -LiteralPath $path) -and -not (Test-RegularUpdateFile $path)){return $false}
    }
    return $true
}
function Invoke-PortableUpdateCheck {
    if($ApplyStaged -or $RecoveryMode -or -not (Owns-Exe $OwnerProcessId)){return}
    $marker=Join-Path $PSScriptRoot 'portable_mode.txt'
    if(-not (Test-RegularUpdateFile $marker) -or
       (Get-Content -LiteralPath $marker -Raw).Trim() -cne 'ARSONMIX_PORTABLE_WORKSPACE_V1'){return}
    if(-not (Test-PortableCacheLayout)){return}
    [IO.Directory]::CreateDirectory($root)|Out-Null
    $file=Read-Json $provenance
    if([string]$file.sourceCommit -cnotmatch '^[a-f0-9]{40}$' -or
       $file.executable -cne 'ArSonMix-GUI10CH-P012-Windows-x64.exe'){return}
    $installedSha=[string]$file.sourceCommit
    $state=[pscustomobject]@{phase='idle';lastCheckUtc='';lastAttemptUtc='';candidateTag='';candidateSha='';previousSha=$installedSha;newAsset=$null}
    if(Test-RegularUpdateFile $statePath){
        try {
            $saved=Read-Json $statePath
            if($saved.previousSha -ceq $installedSha){
                foreach($key in @('phase','lastCheckUtc','lastAttemptUtc','candidateTag','candidateSha','newAsset')){
                    if($saved.PSObject.Properties.Name -contains $key){$state.$key=$saved.$key}
                }
            }
        } catch {}
    }
    $now=[DateTime]::UtcNow
    $attempt=[DateTime]::MinValue;$checked=[DateTime]::MinValue
    $cooldown=if($CheckNow){1}else{20}
    if($state.lastAttemptUtc -and [DateTime]::TryParse([string]$state.lastAttemptUtc,[ref]$attempt) -and
       $now -ge $attempt.ToUniversalTime() -and $now.Subtract($attempt.ToUniversalTime()).TotalMinutes -lt $cooldown){return}
    if(-not $CheckNow -and $state.lastCheckUtc -and [DateTime]::TryParse([string]$state.lastCheckUtc,[ref]$checked) -and
       $now -ge $checked.ToUniversalTime() -and $now.Subtract($checked.ToUniversalTime()).TotalHours -lt 6){return}
    $state.phase='checking';$state.lastAttemptUtc=$now.ToString('o');Write-State $state
    Log-Result 'CHECKING_FOR_UPDATES'
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/2';'X-GitHub-Api-Version'='2022-11-28'}
    $selection=Select-PortableRelease (Read-PublishedReleases $headers) $installedSha
    if($null -eq $selection.baseline){$state.phase='idle';Write-State $state;Log-Result 'NO_MATCHING_PUBLISHED_BASELINE';return}
    if($null -eq $selection.candidate){
        $state.phase='idle';$state.lastCheckUtc=$now.ToString('o');$state.candidateTag='';Write-State $state;Log-Result 'UP_TO_DATE';return
    }
    $candidate=$selection.candidate;$asset=Approved-Portable $candidate
    if(Test-PortableLauncherRoot){
        $selected=Get-PortableSelection (Get-PortableBootstrap)
        # Only a different authenticated release may become ready after rollback.
        if($selected.active.sourceCommit -cne $installedSha){return}
        if($selected.blockedDigest -ceq $asset.digest){
            $state.phase='portable-blocked';$state.lastCheckUtc=$now.ToString('o')
            $state.candidateTag=[string]$candidate.tag_name;$state.candidateSha=[string]$candidate.target_commitish
            $state.newAsset=$null;Write-State $state
            Log-Result ('PREVIOUS_FAILURE_BLOCKED '+$candidate.tag_name)
            return
        }
    }
    $state.phase='downloading';$state.candidateTag=[string]$candidate.tag_name;$state.candidateSha=[string]$candidate.target_commitish;Write-State $state
    Log-Result 'DOWNLOADING_VERIFIED_UPDATE'
    $destination=Join-Path (Join-Path $root 'downloads') ([string]$asset.name)
    $stageFile=Join-Path (Join-Path $root 'stage') ([string]$asset.name)
    # Reuse already downloaded exact ZIP, but recheck current publisher
    # identity before marking it ready. Never overwrite a user-modified ZIP.
    if((Test-RegularUpdateFile $destination) -and (Test-StagedAsset $destination $asset)){
        Assert-InstallerAttestation $destination ([string]$candidate.tag_name) ([string]$candidate.target_commitish) -PortableZip
    } else {
        Download-PortableVerified $candidate $asset $stageFile
        Publish-VerifiedPortableDownload $stageFile $destination $asset
    }
    $state.phase='portable-ready';$state.lastCheckUtc=[DateTime]::UtcNow.ToString('o')
    $state.newAsset=[pscustomobject]@{name=$asset.name;digest=$asset.digest;size=$asset.size}
    Write-State $state
    Log-Result ('PORTABLE_DOWNLOAD_READY '+$candidate.tag_name)
}


# Extract only a publisher-authenticated archive under a live read-only lease.
# This is preparation, NOT activation; no private workspace is copied or moved.
function Expand-PortablePayload([IO.Stream]$lease,[string]$target,[string]$sourceSha,[switch]$VerifyOnly) {
    if($sourceSha -cnotmatch '^[a-f0-9]{40}$'){throw 'Unsafe portable payload identity'}
    if($VerifyOnly){
        if(-not (Test-Path -LiteralPath $target -PathType Container) -or
           ((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unsafe cached portable payload'}
    }elseif(Test-Path -LiteralPath $target){throw 'Unsafe portable payload target'}
    Add-Type -AssemblyName System.IO.Compression
    $lease.Position=0
    $zip=[IO.Compression.ZipArchive]::new($lease,[IO.Compression.ZipArchiveMode]::Read,$true)
    $binaryHash=$null;$binaryLease=$null
    try {
        if($zip.Entries.Count -lt 1 -or $zip.Entries.Count -gt 4096){throw 'Portable member count out of bounds'}
        $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $fileNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $members=[Collections.Generic.List[object]]::new()
        [long]$total=0
        foreach($entry in $zip.Entries){
            $name=$entry.FullName
            $dir=$name.EndsWith('/')
            $rel=if($dir){$name.Substring(0,$name.Length-1)}else{$name}
            if($rel.Length -lt 1 -or $rel.Length -gt 2048 -or $rel.Contains('\')){throw 'Unsafe portable member path'}
            $parts=$rel.Split('/')
            if($parts[0] -in @('Studio','.ArSonMixUpdates')){throw 'Portable archive contains private workspace'}
            foreach($part in $parts){
                if($part -eq '' -or $part -in @('.','..') -or $part -match '[<>:"|?*\x00-\x1f]' -or
                   $part.EndsWith('.') -or $part.EndsWith(' ') -or
                   $part.Split('.')[0].TrimEnd() -match '^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])$'){
                    throw 'Unsafe portable member component'
                }
            }
            $type=($entry.ExternalAttributes -shr 16) -band 0xf000
            if(($type -ne 0 -and $type -ne 0x8000 -and $type -ne 0x4000) -or
               ($entry.ExternalAttributes -band 0x400) -ne 0 -or
               ($dir -and ($entry.Length -ne 0 -or $type -eq 0x8000)) -or
               (-not $dir -and $type -eq 0x4000)){throw 'Unsupported portable member type'}
            if(-not $seen.Add($rel)){throw 'Duplicate portable member'}
            if(-not $dir){
                [void]$fileNames.Add($rel)
                $total+=$entry.Length
                if($entry.Length -lt 0 -or $total -gt 2GB){throw 'Portable expansion exceeds 2GiB'}
            }
            $members.Add([pscustomobject]@{entry=$entry;relative=$rel;directory=$dir})
        }
        foreach($member in $members){
            $parts=$member.relative.Split('/')
            for($i=1;$i -lt $parts.Length;$i++){
                if($fileNames.Contains(($parts[0..($i-1)] -join '/'))){throw 'Portable file/directory collision'}
            }
        }
        foreach($required in @('ArSonMix-GUI10CH-P012-Windows-x64.exe','BUILD_PROVENANCE.json','portable_mode.txt',
                               'ArSonMixAutoUpdate.ps1','UpdateVerifier/gh.exe','UpdateVerifier/verifier.json')){
            if(-not $fileNames.Contains($required)){throw 'Portable payload missing required file'}
        }
        # Verification must also reject unexpected DLLs, plugins and directories;
        # walk without following a reparse point and with the same count bound.
        if($VerifyOnly){
            $expectedDirs=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            foreach($member in $members){
                if($member.directory){[void]$expectedDirs.Add($member.relative)}
                $parts=$member.relative.Split('/')
                for($i=1;$i -lt $parts.Length;$i++){[void]$expectedDirs.Add(($parts[0..($i-1)] -join '/'))}
            }
            $pending=[Collections.Generic.Queue[object]]::new()
            $pending.Enqueue([pscustomobject]@{path=$target;relative=''})
            $count=0;$actualFiles=0
            while($pending.Count -gt 0){
                $node=$pending.Dequeue()
                foreach($item in Get-ChildItem -LiteralPath $node.path -Force){
                    $count++
                    if($count -gt 8192 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unsafe cached portable tree'}
                    $relative=if($node.relative){$node.relative+'/'+$item.Name}else{$item.Name}
                    if($item.PSIsContainer){
                        if(-not $expectedDirs.Contains($relative)){throw 'Unexpected portable directory'}
                        $pending.Enqueue([pscustomobject]@{path=$item.FullName;relative=$relative})
                    }else{
                        if(-not $fileNames.Contains($relative)){throw 'Unexpected portable file'}
                        $actualFiles++
                    }
                }
            }
            if($actualFiles -ne $fileNames.Count){throw 'Cached portable file count mismatch'}
        }else{
            # Unique scratch directory is supplied by the caller, never the active app.
            [IO.Directory]::CreateDirectory($target)|Out-Null
        }
        $prefix=[IO.Path]::GetFullPath($target).TrimEnd('\')+'\'
        $buffer=New-Object byte[] 65536
        foreach($member in $members){
            $dest=[IO.Path]::GetFullPath((Join-Path $target $member.relative))
            if(-not $dest.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Portable member escapes payload'}
            if($member.directory){
                if($VerifyOnly){
                    if(-not (Test-Path -LiteralPath $dest -PathType Container)){throw 'Cached portable directory missing'}
                }else{[IO.Directory]::CreateDirectory($dest)|Out-Null}
                continue
            }
            if($VerifyOnly){
                if(-not (Test-RegularUpdateFile $dest) -or (Get-Item -LiteralPath $dest).Length -ne $member.entry.Length){throw 'Cached portable member size/type mismatch'}
                $algorithm=[Security.Cryptography.SHA256]::Create()
                $entryStream=$member.entry.Open()
                try{
                    $expectedHash=[BitConverter]::ToString($algorithm.ComputeHash($entryStream)).Replace('-','').ToLowerInvariant()
                }finally{$entryStream.Dispose();$algorithm.Dispose()}
                $isBinary=$member.relative -ieq 'ArSonMix-GUI10CH-P012-Windows-x64.exe'
                if($isBinary){
                    # Retain a read lease until the provenance check. Reusing
                    # the exact checked hash cannot hide a subsequent write.
                    $binaryLease=[IO.File]::Open($dest,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
                }
                $actualHash=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
                if($actualHash -cne $expectedHash){throw 'Cached portable member hash mismatch'}
                if($isBinary){$binaryHash=$actualHash}
                continue
            }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))|Out-Null
            $inputStream=$member.entry.Open()
            try{
                $outputStream=[IO.File]::Open($dest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
                try{
                    [long]$written=0
                    while(($n=$inputStream.Read($buffer,0,$buffer.Length)) -gt 0){
                        $written+=$n
                        if($written -gt $member.entry.Length){throw 'Portable member length overflow'}
                        $outputStream.Write($buffer,0,$n)
                    }
                    if($written -ne $member.entry.Length){throw 'Portable member length mismatch'}
                    $outputStream.Flush($true)
                }finally{$outputStream.Dispose()}
            }finally{$inputStream.Dispose()}
        }
        $build=Read-Json (Join-Path $target 'BUILD_PROVENANCE.json')
        $binary=Join-Path $target 'ArSonMix-GUI10CH-P012-Windows-x64.exe'
        if(-not $VerifyOnly){$binaryHash=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()}
        if($build.sourceCommit -cne $sourceSha -or $build.executable -cne [IO.Path]::GetFileName($binary) -or
           $build.exeSha256 -cnotmatch '^[a-f0-9]{64}$' -or
           [long]$build.guiExeBytes -ne (Get-Item -LiteralPath $binary).Length -or
           $binaryHash -cne $build.exeSha256 -or
           (Get-Content -LiteralPath (Join-Path $target 'portable_mode.txt') -Raw).Trim() -cne 'ARSONMIX_PORTABLE_WORKSPACE_V1'){
            throw 'Portable payload provenance mismatch'
        }
    }finally{
        if($null -ne $binaryLease){$binaryLease.Dispose()}
        $zip.Dispose()
    }
}
# Write an independently replaceable record; an interrupted write leaves the
# old record or no record, never a partially written JSON selected for launch.
function Write-AtomicUpdateJson([string]$path,[object]$receipt){
    $parent=[IO.Path]::GetDirectoryName($path)
    if(-not (Test-Path -LiteralPath $parent -PathType Container) -or
       ((Get-Item -LiteralPath $parent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unsafe portable receipt parent'}
    if((Test-Path -LiteralPath $path) -and -not (Test-RegularUpdateFile $path)){throw 'Unsafe portable receipt'}
    $temp=Join-Path $parent ('.receipt-'+[guid]::NewGuid().ToString('N'))
    $backup=$temp+'.bak'
    $committed=$false
    try{
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $receipt -Depth 8 -Compress))
        $stream=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
        # Windows PowerShell 5.1 marshals $null to an invalid empty path for
        # this .NET overload. A unique same-volume backup works on both 5.1
        # and newer runtimes and is retained if replacement reports failure.
        if(Test-Path -LiteralPath $path){[IO.File]::Replace($temp,$path,$backup)}
        else{[IO.File]::Move($temp,$path)}
        $committed=$true
    }finally{
        if(Test-RegularUpdateFile $temp){Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue}
        if($committed -and (Test-RegularUpdateFile $backup)){Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue}
    }
}


# Stable portable supervisor: locally trusted bootstrap, immutable version
# directories, signed archives and append-only selection commits. No pointer
# is an authorization to execute a downloaded file.
function Test-PortableLauncherRoot {
    $cap=Join-Path $portableStableRoot 'portable_launcher_v2.txt'
    $marker=Join-Path $portableStableRoot 'portable_mode.txt'
    return ((Test-RegularUpdateFile $cap) -and (Test-RegularUpdateFile $marker) -and
        (Get-Item -LiteralPath $cap).Length -le 128 -and (Get-Item -LiteralPath $marker).Length -le 128 -and
        (Get-Content -LiteralPath $cap -Raw).Trim() -ceq 'ARSONMIX_PORTABLE_LAUNCHER_V2' -and
        (Get-Content -LiteralPath $marker -Raw).Trim() -ceq 'ARSONMIX_PORTABLE_WORKSPACE_V1')
}
function Test-PortableMixerAbsent {
    foreach($name in @('ArSonMix','ArSonMix-GUI10CH-P012-Windows-x64')){
        $processes=[Diagnostics.Process]::GetProcessesByName($name)
        try{if($processes.Count -gt 0){return $false}}
        finally{foreach($item in $processes){$item.Dispose()}}
    }
    return $true
}
function Get-PortableBootstrap {
    $build=Read-Json (Join-Path $portableStableRoot 'BUILD_PROVENANCE.json')
    if($build.sourceCommit -cnotmatch '^[a-f0-9]{40}$'){throw 'Invalid portable bootstrap source'}
    return [pscustomobject]@{sourceCommit=[string]$build.sourceCommit;tag='';digest='';bytes=0;proofId='';bundleHash='';rootHash=''}
}
function Test-PortableDescriptor([object]$value,[object]$bootstrap) {
    try{
        if($value.sourceCommit -cnotmatch '^[a-f0-9]{40}$'){return $false}
        if($value.digest -ceq ''){
            return ($value.sourceCommit -ceq $bootstrap.sourceCommit -and $value.tag -ceq '' -and
                [long]$value.bytes -eq 0 -and $value.proofId -ceq '' -and $value.bundleHash -ceq '' -and $value.rootHash -ceq '')
        }
        return ($value.digest -cmatch '^sha256:[a-f0-9]{64}$' -and
            $null -ne (Version-Key ([string]$value.tag)) -and $value.tag -cmatch '^v[0-9]+\.[0-9]+\.[0-9]+-(alpha|beta)\.[0-9]+$' -and
            [long]$value.bytes -ge 2097152 -and [long]$value.bytes -le 209715200 -and
            $value.proofId -cmatch '^[a-f0-9]{32}$' -and $value.bundleHash -cmatch '^[a-f0-9]{64}$' -and $value.rootHash -cmatch '^[a-f0-9]{64}$')
    }catch{return $false}
}
function Get-PortableSelection([object]$bootstrap) {
    $folder=Join-Path $root 'selectors'
    if(-not (Test-Path -LiteralPath $folder)){
        return [pscustomobject]@{schema='arsonmix.portable-selection.v1';generation=0;active=$bootstrap;previous=$null;blockedDigest=''}
    }
    if(-not (Test-PortableCacheLayout)){throw 'Unsafe portable selection folder'}
    $records=@(Get-ChildItem -LiteralPath $folder -Force -File | Where-Object {$_.Name -cmatch '^selection-[0-9]{12}\.json$'} | Sort-Object Name -Descending)
    if($records.Count -gt 4096){throw 'Portable selector history exceeds bound'}
    if($records.Count -eq 0){return [pscustomobject]@{schema='arsonmix.portable-selection.v1';generation=0;active=$bootstrap;previous=$null;blockedDigest=''}}
    $record=$records[0]
    if(-not (Test-RegularUpdateFile $record.FullName)){throw 'Unsafe portable selector record'}
    $state=Read-Json $record.FullName
    if($state.schema -cne 'arsonmix.portable-selection.v1' -or
        [long]$state.generation -ne [long]$record.Name.Substring(10,12) -or
        -not (Test-PortableDescriptor $state.active $bootstrap) -or
        ($null -ne $state.previous -and -not (Test-PortableDescriptor $state.previous $bootstrap)) -or
        ($state.blockedDigest -cne '' -and $state.blockedDigest -cnotmatch '^sha256:[a-f0-9]{64}$')){
        throw 'Invalid portable selection record'
    }
    return $state
}
function Write-PortableSelection([object]$old,[object]$active,[object]$previous,[string]$blockedDigest) {
    $folder=Join-Path $root 'selectors'
    [IO.Directory]::CreateDirectory($folder)|Out-Null
    if(-not (Test-PortableCacheLayout)){throw 'Unsafe portable selection directory'}
    $generation=[long]$old.generation+1
    if($generation -gt 4096){throw 'Portable selection history limit'}
    $path=Join-Path $folder ('selection-{0:D12}.json' -f $generation)
    if(Test-Path -LiteralPath $path){throw 'Portable selector generation already committed'}
    # Atomic create, never replace the preceding commit. Abandoned temporary
    # bytes and uncommitted proof directories cannot select a version.
    Write-AtomicUpdateJson $path ([pscustomobject]@{schema='arsonmix.portable-selection.v1';generation=$generation;
        active=$active;previous=$previous;blockedDigest=$blockedDigest})
}
function Test-PortableApproval([object]$approval,[object]$state) {
    try{return ($null -ne $approval -and $approval.candidateTag -ceq $state.candidateTag -and
        $approval.candidateSha -ceq $state.candidateSha -and $approval.previousSha -ceq $state.previousSha -and
        $approval.digest -ceq $state.newAsset.digest -and $approval.digest -cmatch '^sha256:[a-f0-9]{64}$')}
    catch{return $false}
}
function Assert-PortableOfflineProof([string]$zipPath,[object]$version){
    $verifier=Get-AttestationVerifier $portableStableRoot
    if($null -eq $verifier){throw 'Stable portable verifier missing or damaged'}
    $dir=Join-Path (Join-Path $root 'proofs') $version.proofId
    if(-not (Test-Path -LiteralPath $dir -PathType Container) -or
       ((Get-Item -LiteralPath $dir -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Invalid portable proof folder'}
    $bundle=Join-Path $dir 'bundle.jsonl';$trusted=Join-Path $dir 'trusted_root.jsonl'
    foreach($file in @($bundle,$trusted)){
        if(-not (Test-RegularUpdateFile $file) -or (Get-Item -LiteralPath $file).Length -gt 2MB){throw 'Unsafe offline attestation evidence'}
    }
    $bundleLease=[IO.File]::Open($bundle,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $rootLease=$null;$verifierLease=$null;$private=$null
    try{
        $rootLease=[IO.File]::Open($trusted,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        $verifierLease=[IO.File]::Open($verifier,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        if((Get-AttestationVerifier $portableStableRoot) -cne $verifier -or
           (Get-FileHash -LiteralPath $bundle -Algorithm SHA256).Hash.ToLowerInvariant() -cne $version.bundleHash -or
           (Get-FileHash -LiteralPath $trusted -Algorithm SHA256).Hash.ToLowerInvariant() -cne $version.rootHash){throw 'Portable trust evidence changed'}
        $arguments=Get-AttestationArguments $zipPath $version.tag $version.sourceCommit -PortableZip
        if($null -eq $arguments){throw 'Invalid offline subject'}
        $private=Join-Path $root ('verify-'+[guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($private)|Out-Null
        $json=Invoke-IsolatedVerifier $verifier ($arguments+' --hostname github.com --bundle "'+$bundle+'" --custom-trusted-root "'+$trusted+'" --format json') $private
        if(-not (Test-VerifiedRepositoryIdentity ($json|ConvertFrom-Json))){throw 'Offline portable repository identity mismatch'}
    }finally{
        if($null -ne $verifierLease){$verifierLease.Dispose()}
        if($null -ne $rootLease){$rootLease.Dispose()}
        $bundleLease.Dispose()
        if($private){Remove-Item -LiteralPath $private -Recurse -Force -ErrorAction SilentlyContinue}
    }
}
function Get-VerifiedPortableImage([object]$version,[object]$bootstrap){
    if(-not (Test-PortableDescriptor $version $bootstrap)){throw 'Invalid portable version descriptor'}
    if($version.digest -ceq ''){
        # Root is the user-trusted initial extraction, not a downloaded
        # candidate. Never execute a hash-only remote payload as bootstrap.
        $dir=$portableStableRoot
        $build=Read-Json (Join-Path $dir 'BUILD_PROVENANCE.json')
        $binary=Join-Path $dir 'ArSonMix-GUI10CH-P012-Windows-x64.exe'
        if(-not (Test-RegularUpdateFile $binary) -or $build.sourceCommit -cne $version.sourceCommit -or
            $build.exeSha256 -cnotmatch '^[a-f0-9]{64}$' -or (Get-Item -LiteralPath $binary).Length -ne [long]$build.guiExeBytes -or
            (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant() -cne $build.exeSha256){throw 'Portable bootstrap damaged'}
        return $binary
    }
    $zipPath=Join-Path (Join-Path $root 'downloads') ("ArSonMix-"+$version.tag+"-Windows-x64-Portable.zip")
    if(-not (Test-RegularUpdateFile $zipPath)){throw 'Portable archive unavailable'}
    $dir=Join-Path (Join-Path $root 'payloads') ($version.sourceCommit+'-'+$version.digest.Substring(7))
    $lease=[IO.File]::Open($zipPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try{
        if(-not (Test-StagedAsset $zipPath ([pscustomobject]@{digest=$version.digest;size=$version.bytes}))){throw 'Portable archive digest mismatch'}
        Assert-PortableOfflineProof $zipPath $version
        Expand-PortablePayload $lease $dir $version.sourceCommit -VerifyOnly
        return (Join-Path $dir 'ArSonMix-GUI10CH-P012-Windows-x64.exe')
    }finally{$lease.Dispose()}
}
function Start-PortableImage([string]$binary,[string]$argument='', [string]$probe=''){
    if(-not (Test-PortableMixerAbsent)){throw 'Another mixer is active; portable launch deferred'}
    $info=[Diagnostics.ProcessStartInfo]::new()
    $info.FileName=$binary;$info.WorkingDirectory=[IO.Path]::GetDirectoryName($binary)
    $info.Arguments=$argument;$info.UseShellExecute=$false
    $info.EnvironmentVariables['ARSONMIX_PORTABLE_SUPERVISED']='1'
    $info.EnvironmentVariables.Remove('ARSONMIX_UPDATE_PROBE')
    if($probe){$info.EnvironmentVariables['ARSONMIX_UPDATE_PROBE']=$probe}
    $child=[Diagnostics.Process]::Start($info)
    if($null -eq $child){throw 'Could not start portable image'}
    return $child
}
function Wait-PortableProbe([Diagnostics.Process]$child,[string]$marker,[int]$timeoutSeconds=75){
    $deadline=[DateTime]::UtcNow.AddSeconds($timeoutSeconds)
    $ready=$false
    do{
        if((Test-RegularUpdateFile $marker) -and (Get-Item -LiteralPath $marker).Length -le 128 -and
            (Get-Content -LiteralPath $marker -Raw).Trim() -ceq 'GUI_READY_NO_AUDIO'){$ready=$true}
        if($child.HasExited){return ($ready -and $child.ExitCode -eq 0)}
        Start-Sleep -Milliseconds 200
    }while([DateTime]::UtcNow -lt $deadline)
    # Never terminate a probe or another mixer to force a switch.
    return $false
}
function Test-PortableCandidateHealth([string]$binary){
    $token=[guid]::NewGuid().ToString('D')
    $healthDir=Join-Path $local 'ArSonMix\Updates\health'
    [IO.Directory]::CreateDirectory($healthDir)|Out-Null
    $marker=Join-Path $healthDir ($token+'.ready')
    $child=Start-PortableImage $binary '' $token
    try{return (Wait-PortableProbe $child $marker)}
    finally{
        $child.Dispose()
        if(Test-RegularUpdateFile $marker){Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue}
    }
}
# A normal-exit receipt is consent/lifecycle evidence, NOT publisher trust.
function Test-PortableExitReceipt([object]$receipt,[object]$state,[string]$token,[int]$pidValue,[long]$nowMs){
    try{
        return ($null -ne $receipt -and $receipt.token -cmatch '^[a-f0-9]{32}$' -and
            ($token -ceq '' -or $receipt.token -ceq $token) -and
            ($pidValue -eq 0 -or [int]$receipt.ownerPid -eq $pidValue) -and [int]$receipt.ownerPid -gt 0 -and
            [long]$receipt.createdMs -le $nowMs -and $nowMs-[long]$receipt.createdMs -le 900000 -and
            $receipt.previousSha -ceq $state.previousSha -and $receipt.candidateSha -ceq $state.candidateSha -and
            $receipt.candidateTag -ceq $state.candidateTag -and $receipt.digest -ceq $state.newAsset.digest -and
            $receipt.digest -cmatch '^sha256:[a-f0-9]{64}$')
    }catch{return $false}
}
function Read-PortableExitReceipt([string]$token,[int]$pidValue){
    if(-not (Test-RegularUpdateFile $exitReceiptPath) -or -not (Test-RegularUpdateFile $statePath)){return $null}
    try{
        $receipt=Read-Json $exitReceiptPath
        $state=Read-Json $statePath
        if($state.phase -ceq 'portable-ready' -and
           (Test-PortableExitReceipt $receipt $state $token $pidValue ([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))){return $receipt}
    }catch{}
    return $null
}
function Commit-PortableCandidate([object]$state,[object]$asset,[string]$zipPath,[string]$target,[object]$exitIntent){
    if(-not (Test-PortableLauncherRoot) -or -not (Test-PortableMixerAbsent) -or
       -not (Test-RegularUpdateFile $approvalPath)){return}
    $approval=Read-Json $approvalPath
    if(-not (Test-PortableApproval $approval $state) -or
       -not (Test-PortableExitReceipt $exitIntent $state '' 0 ([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))){return}
    $bootstrap=Get-PortableBootstrap
    $selected=Get-PortableSelection $bootstrap
    if($selected.active.sourceCommit -cne $state.previousSha -or $selected.blockedDigest -ceq $asset.digest){return}
    # Do not activate packages without the matching launcher/workspace ABI.
    $cap=Join-Path $target 'portable_launcher_v2.txt'
    if(-not (Test-RegularUpdateFile $cap) -or (Get-Item -LiteralPath $cap).Length -gt 128 -or
        (Get-Content -LiteralPath $cap -Raw).Trim() -cne 'ARSONMIX_PORTABLE_LAUNCHER_V2'){return}
    if($selected.active.tag -and (Compare-Version (Version-Key $state.candidateTag) (Version-Key $selected.active.tag)) -le 0){return}
    # Verify rollback image before committing, while the previous version is
    # still selected. A bad previous version cannot authorize a new switch.
    [void](Get-VerifiedPortableImage $selected.active $bootstrap)
    $proofId=[guid]::NewGuid().ToString('N')
    $proofParent=Join-Path $root 'proofs'
    [IO.Directory]::CreateDirectory($proofParent)|Out-Null
    if(-not (Test-PortableCacheLayout)){throw 'Unsafe portable proof cache'}
    $proof=Join-Path $proofParent $proofId
    Assert-InstallerAttestation $zipPath $state.candidateTag $state.candidateSha -PortableZip -SaveProof $proof -VerifierRoot $portableStableRoot
    $version=[pscustomobject]@{sourceCommit=[string]$state.candidateSha;tag=[string]$state.candidateTag;digest=[string]$asset.digest;
        bytes=[long]$asset.size;proofId=$proofId;
        bundleHash=(Get-FileHash -LiteralPath (Join-Path $proof 'bundle.jsonl') -Algorithm SHA256).Hash.ToLowerInvariant();
        rootHash=(Get-FileHash -LiteralPath (Join-Path $proof 'trusted_root.jsonl') -Algorithm SHA256).Hash.ToLowerInvariant()}
    $binary=Get-VerifiedPortableImage $version $bootstrap
    if(-not (Test-PortableCandidateHealth $binary)){
        Write-PortableSelection $selected $selected.active $selected.previous $version.digest
        $state.phase='portable-blocked';Write-State $state
        Log-Result ('ROLLED_BACK_PORTABLE_PROBE '+$version.tag)
        return
    }
    if(-not (Test-PortableMixerAbsent)){return}
    Write-PortableSelection $selected $version $selected.active ''
    $state.phase='portable-activated';Write-State $state
    Log-Result ('HEALTHY_PORTABLE_ACTIVATED '+$version.tag)
}
function Invoke-PortableLaunch {
    if(-not (Test-PortableLauncherRoot) -or -not (Test-PortableCacheLayout) -or
        -not (Test-PortableMixerAbsent)){return}
    $bootstrap=Get-PortableBootstrap
    $selected=Get-PortableSelection $bootstrap
    # Recover a fresh exact normal-exit intent after an interrupted worker.
    if($PortableLaunchArgument -ceq ''){
        $intent=Read-PortableExitReceipt '' 0
        if($null -ne $intent -and $intent.previousSha -ceq $selected.active.sourceCommit){
            try{
                $currentDir=if($selected.active.digest -ceq ''){$portableStableRoot}else{
                    Join-Path (Join-Path $root 'payloads') ($selected.active.sourceCommit+'-'+$selected.active.digest.Substring(7))
                }
                [void](Get-VerifiedPortableImage $selected.active $bootstrap)
                Invoke-PortablePreparation $null $intent (Join-Path $currentDir 'BUILD_PROVENANCE.json')
            }catch{Log-Result ('PORTABLE_ACTIVATION_DEFERRED '+$_.Exception.Message)}
            if(-not (Test-PortableMixerAbsent)){return}
            $selected=Get-PortableSelection $bootstrap
        }
    }
    try{
        $binary=Get-VerifiedPortableImage $selected.active $bootstrap
        $child=Start-PortableImage $binary $PortableLaunchArgument
    }catch{
        if($null -eq $selected.previous -or -not (Test-PortableMixerAbsent)){throw}
        $binary=Get-VerifiedPortableImage $selected.previous $bootstrap
        Write-PortableSelection $selected $selected.previous $null $selected.active.digest
        if(Test-RegularUpdateFile $statePath){
            try{$state=Read-Json $statePath;$state.phase='portable-blocked';Write-State $state}catch{}
        }
        Log-Result ('ROLLED_BACK_PORTABLE_IMAGE '+$selected.active.tag)
        $child=Start-PortableImage $binary $PortableLaunchArgument
    }
    $child.Dispose()
}

function Invoke-PortablePreparation([Diagnostics.Process]$owner,[object]$exitIntent=$null,[string]$currentProvenance=$provenance) {
    if($null -ne $owner){if(-not $owner.WaitForExit(120000)){return}}
    elseif($null -eq $exitIntent){return}
    if(-not (Test-PortableMixerAbsent)){return}
    # A replacement portable session may have started while the owner exited.
    # Defer all extraction in that case; never close a process.
    foreach($process in [Diagnostics.Process]::GetProcessesByName([IO.Path]::GetFileNameWithoutExtension($exe))){
        try {return} finally {$process.Dispose()}
    }
    if(-not (Test-PortableCacheLayout) -or -not (Test-RegularUpdateFile $statePath)){return}
    $state=Read-Json $statePath
    $build=Read-Json $currentProvenance
    if($state.phase -cne 'portable-ready' -or $state.previousSha -cne $build.sourceCommit){return}
    $tag=[string]$state.candidateTag;$sha=[string]$state.candidateSha
    if($null -eq (Version-Key $tag) -or $sha -cnotmatch '^[a-f0-9]{40}$'){return}
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/2';'X-GitHub-Api-Version'='2022-11-28'}
    $selection=Select-PortableRelease (Read-PublishedReleases $headers) ([string]$build.sourceCommit)
    if($null -eq $selection.candidate -or $selection.candidate.tag_name -cne $tag -or $selection.candidate.target_commitish -cne $sha){return}
    $asset=Approved-Portable $selection.candidate
    if($asset.digest -cne $state.newAsset.digest -or [long]$asset.size -ne [long]$state.newAsset.size -or $asset.name -cne $state.newAsset.name){return}
    $zipPath=Join-Path (Join-Path $root 'downloads') ([string]$asset.name)
    if(-not (Test-RegularUpdateFile $zipPath)){return}
    $parent=Join-Path $root 'payloads'
    $target=Join-Path $parent ($sha+'-'+([string]$asset.digest).Substring(7))
    # A previous interrupted attempt may have published the directory but not
    # its receipt. Re-authenticate and compare every byte; never overwrite it.
    $lease=[IO.File]::Open($zipPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $scratch=Join-Path $parent ('.prepare-'+[guid]::NewGuid().ToString('N'))
    try{
        if(-not (Test-StagedAsset $zipPath $asset)){throw 'Portable ZIP changed before preparation'}
        Assert-InstallerAttestation $zipPath $tag $sha -PortableZip
        [IO.Directory]::CreateDirectory($parent)|Out-Null
        if(-not (Test-PortableCacheLayout)){throw 'Unsafe portable payload cache'}
        if(Test-Path -LiteralPath $target){
            Expand-PortablePayload $lease $target $sha -VerifyOnly
        }else{
            Expand-PortablePayload $lease $scratch $sha
            # Same-volume directory rename is the only publication boundary.
            [IO.Directory]::Move($scratch,$target)
        }
        # Receipt is observability, not permission to execute. It only records
        # that the exact ZIP and payload were checked within this worker.
        $receipt=[pscustomobject]@{schema='arsonmix.portable-prepared.v1';tag=$tag;sourceCommit=$sha;
            digest=[string]$asset.digest;bytes=[long]$asset.size;previousSha=[string]$build.sourceCommit}
        Write-AtomicUpdateJson (Join-Path $root 'prepared.json') $receipt
        Log-Result ('PORTABLE_PAYLOAD_PREPARED '+$tag)
        Commit-PortableCandidate $state $asset $zipPath $target $exitIntent
    }finally{
        $lease.Dispose()
        # Scratch is a new updater-owned directory, never the active workspace.
        if(Test-Path -LiteralPath $scratch -PathType Container){
            Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
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
function Test-UserApproval([object]$approval,[object]$transaction) {
    if($null -eq $transaction -or
       $transaction.operation -cnotin @('repair','upgrade')) {return $false}
    if($transaction.operation -ceq 'repair'){return $true}
    # Approval is only a usability gate. It does NOT authenticate publisher.
    return ($null -ne $approval -and
        [string]$approval.candidateSha -ceq [string]$transaction.candidateSha -and
        [string]$approval.previousSha -ceq [string]$transaction.previousSha -and
        [string]$approval.candidateTag -ceq [string]$transaction.candidateTag)
}

function Test-InstallableAssetPair([string]$newPath,[object]$newAsset,
                                   [string]$oldPath,[object]$oldAsset) {
    return ((Test-StagedAsset $newPath $newAsset) -and
            (Test-StagedAsset $oldPath $oldAsset))
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
function Test-InstalledRelease([string]$expectedSha) {
    try {
        if($expectedSha -cnotmatch '^[a-f0-9]{40}$'){return $false}
        $manifest=Read-Json $provenance
        return ([string]$manifest.sourceCommit -ceq $expectedSha -and
                (Get-InstalledImageStatus $manifest) -ceq 'healthy')
    } catch {return $false}
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
    $releases=Read-PublishedReleases $headers
    $sourceRelease=$null
    foreach($release in $releases) {
        if([string]$release.target_commitish -ceq $sourceSha -and
           $null -ne (Approved-Installer $release)) {
            if($null -eq $sourceRelease) {
                $sourceRelease=$release
            } else {
                $candidateVersion=Version-Key ([string]$release.tag_name)
                $chosenVersion=Version-Key ([string]$sourceRelease.tag_name)
                if((Compare-Version $candidateVersion $chosenVersion) -gt 0) {
                    $sourceRelease=$release
                }
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
    if(-not (Run-Installer $destination $asset ([string]$sourceRelease.tag_name) $sourceSha -AllowMissingExeRecovery)){
        throw 'Verified recovery installer was not accepted by Windows'
    }
    if(-not (Test-InstalledRelease $sourceSha)){
        throw 'Repair did not restore exact authenticated EXE and source manifest'
    }
    if(-not (Launch-WithHealth $sourceSha)){
        throw 'Repaired application did not pass the GUI startup check'
    }
    Log-Result ('RECOVERY_HEALTHY '+$sourceRelease.tag_name)
    # Keep verified installer cached for next recovery; no Studio cleanup.
}

# This proof exists ONLY in the current supervisor's memory. It is issued
# after cryptographic attestation under a read-only file lease, before any
# installer replaces the verifier. Persisted state can never issue this proof.
function Test-RollbackProof([object]$proof,[IO.FileStream]$lease,[string]$file,[object]$asset,[string]$tag,[string]$sha) {
    try {
        return ($null -ne $proof -and $null -ne $lease -and $lease.CanRead -and
            -not $lease.SafeFileHandle.IsClosed -and
            [string]::Equals($lease.Name,[IO.Path]::GetFullPath($file),[StringComparison]::OrdinalIgnoreCase) -and
            $proof.path -ceq [IO.Path]::GetFullPath($file) -and $proof.tag -ceq $tag -and
            $proof.sha -ceq $sha -and $proof.digest -ceq $asset.digest -and
            [long]$proof.bytes -eq [long]$asset.size -and
            (Test-StagedAsset $file $asset))
    } catch {return $false}
}
function Run-Installer([string]$file,[object]$asset,[string]$tag,[string]$sha,[switch]$AllowMissingExeRecovery,[switch]$UseVerifiedRollback) {
    # Inno performs exact-path process shutdown and EXE replacement preflight.
    # Its silent [Run] is disabled: only this supervisor relaunches once.
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)){return $false}
    # Hold the verified executable read-only until installer exits.
    $guard=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    try {
    if(-not (Test-StagedAsset $file $asset)){Fail 'Installer bytes changed before execution'}
    if($UseVerifiedRollback){
        if(-not (Test-RollbackProof $rollbackProof $rollbackGuard $file $asset $tag $sha)){Fail 'In-memory rollback proof invalid'}
    } else {
        Assert-InstallerAttestation $file $tag $sha
    }
    if(-not (Test-NoActiveMixer)){Fail 'Mixer is active; update deferred'}
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
    } finally {$guard.Dispose()}
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
                if((Get-Content -LiteralPath $marker -Raw).Trim() -ceq 'GUI_READY_NO_AUDIO' -and
                   (Test-InstalledRelease $expectedSha)) {
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
    $beta=Version-Key 'v0.5.0-beta.1'
    $stable=Version-Key 'v0.5.0'
    if($null -eq $alpha -or $null -ne (Version-Key 'v0.5.0-alpha.5/evil') -or
       (Compare-Version $next $alpha) -le 0 -or
       (Compare-Version $beta $next) -le 0 -or
       (Compare-Version $stable $beta) -le 0 -or
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
    $fixtureInstaller=Join-Path ([IO.Path]::GetTempPath()) ("ArSonMix-$tag-Windows-x64-Setup.exe")
    $verifyArgs=Get-AttestationArguments $fixtureInstaller $tag ('f'*40)
    if($null -eq $verifyArgs -or $verifyArgs -cnotmatch '--signer-workflow masarray/arsonmix/\.github/workflows/alpha-preview-publish\.yml' -or
       $verifyArgs -cnotmatch '--deny-self-hosted-runners'){throw 'Attestation identity policy failed'}
    $betaTag='v0.5.0-beta.1'
    foreach($suffix in @('Setup.exe','Portable.zip')){
        $betaPath=Join-Path ([IO.Path]::GetTempPath()) ("ArSonMix-$betaTag-Windows-x64-$suffix")
        $betaArgs=Get-AttestationArguments $betaPath $betaTag ('f'*40) -PortableZip:($suffix -eq 'Portable.zip')
        if($null -eq $betaArgs -or -not $betaArgs.Contains('--source-ref refs/heads/releases/beta/'+$betaTag+' ') -or
           -not $verifyArgs.Contains('--source-ref refs/heads/releases/alpha/'+$tag+' ')){throw 'Alpha/Beta attestation ref policy failed'}
    }
    if($null -ne (Get-AttestationArguments $fixtureInstaller '../bad' ('f'*40)) -or
       $null -ne (Get-AttestationArguments $fixtureInstaller $tag 'main') -or
       $null -ne (Get-AttestationArguments (Join-Path ([IO.Path]::GetTempPath()) 'other.exe') $tag ('f'*40))){
        throw 'Unsafe attestation arguments accepted'
    }
    $previous=[pscustomobject]@{tag_name='v0.5.0-alpha.5';target_commitish=('a'*40);prerelease=$true}
    $candidate=[pscustomobject]@{tag_name=$tag;target_commitish=('f'*40);prerelease=$true}
    if(-not (Test-ReleaseTransition 'upgrade' $previous $candidate) -or
       (Test-ReleaseTransition 'upgrade' $candidate $previous) -or
       (Test-ReleaseTransition 'repair' $previous $candidate) -or
       -not (Test-ReleaseTransition 'repair' $previous $previous)){throw 'Unsafe release transition accepted'}
    if(-not (Test-AttestationBundleUri ([Uri]'https://tmaproduction.blob.core.windows.net/attestations/1432648251/2026/11/27/1.json.sn?sig=fixture') '1432648251') -or
       (Test-AttestationBundleUri ([Uri]'https://attacker.example/attestations/1432648251/1.json') '1432648251') -or
       (Test-AttestationBundleUri ([Uri]'https://tmaproduction.blob.core.windows.net/attestations/1/1.json') '1432648251') -or
       (Test-AttestationBundleUri ([Uri]'http://tmaproduction.blob.core.windows.net/attestations/1432648251/1.json') '1432648251')){
        throw 'Unsafe public bundle URL policy'
    }
    $ascii=[Text.Encoding]::ASCII
    if($ascii.GetString((Expand-BoundedSnappy ([byte[]](3,8,65,66,67)) 16)) -cne 'ABC' -or
       $ascii.GetString((Expand-BoundedSnappy ([byte[]](5,0,65,1,1)) 16)) -cne 'AAAAA' -or
       $ascii.GetString((Expand-BoundedSnappy ([byte[]](5,0,65,14,1,0)) 16)) -cne 'AAAAA' -or
       $ascii.GetString((Expand-BoundedSnappy ([byte[]](5,0,65,15,1,0,0,0)) 16)) -cne 'AAAAA'){
        throw 'Snappy literal/copy decode policy failed'
    }
    $literal=[byte[]](@(61,240,60)+@(65)*61)
    if($ascii.GetString((Expand-BoundedSnappy $literal 64)) -cne ('A'*61)){throw 'Snappy extended literal failed'}
    $badSnappy=@(
        [pscustomobject]@{bytes=[byte[]](128);max=16},
        [pscustomobject]@{bytes=[byte[]](17,0,65);max=16},
        [pscustomobject]@{bytes=[byte[]](3,8,65);max=16},
        [pscustomobject]@{bytes=[byte[]](4,1,0);max=16},
        [pscustomobject]@{bytes=[byte[]](1,0,65,0);max=16},
        [pscustomobject]@{bytes=[byte[]](5,0,65,14,2,0);max=16})
    foreach($fixture in $badSnappy){
        $rejected=$false
        try {[void](Expand-BoundedSnappy $fixture.bytes $fixture.max)} catch {$rejected=$true}
        if(-not $rejected){throw 'Malformed Snappy block accepted'}
    }
    Write-Output 'ARSONMIX_UPDATER_SNAPPY_POLICY_PASS'
    $cert=[pscustomobject]@{verificationResult=[pscustomobject]@{signature=[pscustomobject]@{certificate=[pscustomobject]@{sourceRepositoryIdentifier='1432648251'}}}}
    if(-not (Test-VerifiedRepositoryIdentity @($cert)) -or
       (Test-VerifiedRepositoryIdentity @()) -or
       (Test-VerifiedRepositoryIdentity @([pscustomobject]@{verificationResult=$null}))){throw 'Repository ID policy failed'}
    $cert.verificationResult.signature.certificate.sourceRepositoryIdentifier='1'
    if(Test-VerifiedRepositoryIdentity @($cert)){throw 'Foreign repository identity accepted'}
    $fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('armix-verifier-policy-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($fixtureRoot)|Out-Null
    try {
        if($null -ne (Get-AttestationVerifier $fixtureRoot)){throw 'Missing verifier accepted'}
        $bundlePath=Join-Path $fixtureRoot 'bundle.jsonl'
        $bundle=[pscustomobject]@{mediaType='test-only';verificationMaterial=[pscustomobject]@{}}
        $response=[pscustomobject]@{attestations=@([pscustomobject]@{repository_id=1432648251;bundle=$bundle})}
        Convert-AttestationResponse $response $bundlePath
        if((Get-Content -LiteralPath $bundlePath -Raw | ConvertFrom-Json).mediaType -cne 'test-only'){throw 'Bundle conversion failed'}
        $response.attestations[0].repository_id=1
        $rejected=$false
        try {Convert-AttestationResponse $response $bundlePath} catch {$rejected=$true}
        if(-not $rejected){throw 'Foreign API bundle accepted'}
    } finally {Remove-Item -LiteralPath $fixtureRoot -Recurse -Force}
    Write-Output 'ARSONMIX_UPDATER_AUTONOMOUS_POLICY_PASS'
    Write-Output 'ARSONMIX_UPDATER_ATTESTATION_POLICY_PASS'
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
            $savedProvenance=$provenance
            try {
                $provenance=Join-Path $tmp 'BUILD_PROVENANCE.json'
                $sampleSha='1'*40
                [IO.File]::WriteAllText($provenance,(
                    @{sourceCommit=$sampleSha;exeSha256=$goodHash;guiExeBytes=2097152} |
                    ConvertTo-Json -Compress),[Text.Encoding]::UTF8)
                if(-not (Test-InstalledRelease $sampleSha)){throw 'Authentic installed image rejected'}
                if(Test-InstalledRelease ('2'*40)){throw 'Incorrect source accepted'}
                [IO.File]::WriteAllText($provenance,(
                    @{sourceCommit=$sampleSha;exeSha256=('0'*64);guiExeBytes=2097152} |
                    ConvertTo-Json -Compress),[Text.Encoding]::UTF8)
                if(Test-InstalledRelease $sampleSha){throw 'Same source with changed EXE accepted'}
            } finally {$provenance=$savedProvenance}

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
    # Last-mile policy: tamper with EITHER installer, not just the candidate.
    $pairFolder=Join-Path ([IO.Path]::GetTempPath()) ('ArSonMix-VerifiedPair-'+[guid]::NewGuid().ToString('N'))
    try {
        [IO.Directory]::CreateDirectory($pairFolder) | Out-Null
        $oldSample=Join-Path $pairFolder 'previous.exe'
        $newSample=Join-Path $pairFolder 'candidate.exe'
        $known=New-Object byte[] 2097152
        [IO.File]::WriteAllBytes($oldSample,$known)
        [IO.File]::WriteAllBytes($newSample,$known)
        $fingerprint=(Get-FileHash -LiteralPath $newSample -Algorithm SHA256).Hash.ToLowerInvariant()
        $identity=[pscustomobject]@{size=2097152;digest='sha256:'+$fingerprint}
        if(-not (Test-InstallableAssetPair $newSample $identity $oldSample $identity)){
            throw 'Valid pair rejected'
        }
        $known[10]=123
        [IO.File]::WriteAllBytes($oldSample,$known)
        if(Test-InstallableAssetPair $newSample $identity $oldSample $identity){
            throw 'Tampered rollback accepted'
        }
        [IO.File]::WriteAllBytes($oldSample,(New-Object byte[] 2097152))
        $known[11]=17
        [IO.File]::WriteAllBytes($newSample,$known)
        if(Test-InstallableAssetPair $newSample $identity $oldSample $identity){
            throw 'Tampered candidate accepted'
        }
        Write-Output 'ARSONMIX_UPDATER_LAST_MILE_PAIR_PASS'
    } finally {
        Remove-Item -LiteralPath $pairFolder -Recurse -Force -ErrorAction SilentlyContinue
    }
    if(-not (Should-RetryInstaller 1 $false $true)){throw 'First transient retry rejected'}
    if(Should-RetryInstaller 2 $false $true){throw 'Repeated installer retry accepted'}
    if(Should-RetryInstaller 1 $false $false){throw 'Missing old version retried'}
    if(Should-RetryInstaller 1 $true $true){throw 'Successful install retried'}
    # Test same-version repair and exact candidate acknowledgement offline.
    $transaction=[pscustomobject]@{
        operation='upgrade';candidateSha=('a'*40);previousSha=('b'*40);
        candidateTag='v0.5.0-alpha.6'}
    $goodAck=[pscustomobject]@{
        candidateSha=('a'*40);previousSha=('b'*40);candidateTag='v0.5.0-alpha.6'}
    if(-not (Test-UserApproval $goodAck $transaction)) {throw 'Exact update acknowledgement rejected'}
    if(Test-UserApproval $null $transaction) {throw 'Missing acknowledgement accepted'}
    $goodAck.candidateSha='c'*40
    if(Test-UserApproval $goodAck $transaction) {throw 'Different release acknowledgement accepted'}
    $goodAck.candidateSha='a'*40
    $goodAck.previousSha='c'*40
    if(Test-UserApproval $goodAck $transaction) {throw 'Different baseline acknowledgement accepted'}
    $goodAck.previousSha='b'*40
    $goodAck.candidateTag='v0.5.0-alpha.7'
    if(Test-UserApproval $goodAck $transaction) {throw 'Different version acknowledgement accepted'}
    $transaction.operation='repair'
    if(-not (Test-UserApproval $null $transaction)) {throw 'Same-version repair incorrectly gated'}
    Write-Output 'ARSONMIX_UPDATER_USER_ACK_POLICY_PASS'
    Write-Output 'ARSONMIX_UPDATER_EXACT_IMAGE_POLICY_PASS'
    Write-Output 'ARSONMIX_UPDATER_RETRY_POLICY_PASS'
    Write-Output 'ARSONMIX_UPDATER_CACHE_AND_RECOVERY_PASS'

    Write-Output 'ARSONMIX_UPDATER_SELF_REPAIR_POLICY_PASS'
    Write-Output 'ARSONMIX_UPDATER_STAGED_INTEGRITY_PASS'
    # Deterministic protocol and offline rollback lease fixtures.
    if((Get-ResumeResponseMode 206 'bytes 100-999/1000' 900 100 1000) -cne 'append' -or
       (Get-ResumeResponseMode 200 '' 1000 100 1000) -cne 'restart' -or
       (Get-ResumeResponseMode 206 'bytes 99-999/1000' 901 100 1000) -cne 'reject' -or
       (Get-ResumeResponseMode 206 'bytes 100-999/1001' 900 100 1000) -cne 'reject' -or
       (Get-ResumeResponseMode 206 'bytes 100-999/1000' 899 100 1000) -cne 'reject' -or
       (Get-ResumeResponseMode 416 '' 0 100 1000) -cne 'reject' -or
       (Get-ResumeResponseMode 200 '' 999 100 1000) -cne 'reject'){throw 'Resume response policy failed'}
    if((Get-PartialAssetPath 'asset.exe' ('a'*64)) -ceq (Get-PartialAssetPath 'asset.exe' ('b'*64))){throw 'Partial identity collision'}
    $ptag='v0.5.0-alpha.9'
    $pzip=Join-Path ([IO.Path]::GetTempPath()) "ArSonMix-$ptag-Windows-x64-Portable.zip"
    if($null -eq (Get-AttestationArguments $pzip $ptag ('a'*40) -PortableZip) -or
       $null -ne (Get-AttestationArguments $pzip $ptag ('a'*40))){throw 'Portable attestation filename policy failed'}
    $makePortable={
        param([string]$v,[string]$commit)
        $names=@("ArSonMix-$v-Windows-x64-Portable.zip",'SHA256SUMS.txt','RELEASE_TRUST-windows.json')
        $assets=@(foreach($name in $names){[pscustomobject]@{name=$name;state='uploaded';size=2097152;digest=('sha256:'+('a'*64));browser_download_url="https://github.com/masarray/arsonmix/releases/download/$v/$name"}})
        [pscustomobject]@{tag_name=$v;target_commitish=$commit;draft=$false;prerelease=$true;published_at='2026-11-27T00:00:00Z';assets=$assets}
    }
    $baseline=& $makePortable 'v0.5.0-alpha.8' ('a'*40)
    $candidate=& $makePortable 'v0.5.0-alpha.9' ('b'*40)
    $selection=Select-PortableRelease @($baseline,$candidate) ('a'*40)
    if($selection.candidate.tag_name -cne $candidate.tag_name){throw 'Portable release selection failed'}
    $candidate.assets=@($candidate.assets | Where-Object {$_.name -cne 'RELEASE_TRUST-windows.json'})
    if($null -ne (Select-PortableRelease @($baseline,$candidate) ('a'*40)).candidate){throw 'Portable release without trust manifest accepted'}
    if($null -ne (Select-PortableRelease @($baseline) ('c'*40)).baseline){throw 'Unknown portable baseline accepted'}
    $priorLogPath=$logPath
    $priorRoot=$root
    $portableFixture=Join-Path ([IO.Path]::GetTempPath()) ('armix-portable-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($portableFixture)|Out-Null
    $root=$portableFixture;$logPath=Join-Path $portableFixture 'test.log'
    try {
        $source=Join-Path $portableFixture 'stage.zip'
        $destination=Join-Path $portableFixture 'downloads\ready.zip'
        [IO.File]::WriteAllBytes($source,(New-Object byte[] 2097152))
        $asset=[pscustomobject]@{size=2097152;digest='sha256:'+(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()}
        Publish-VerifiedPortableDownload $source $destination $asset
        if(-not (Test-StagedAsset $destination $asset) -or (Test-Path -LiteralPath $source)){throw 'Portable promotion failed'}
        [IO.File]::WriteAllBytes($source,(New-Object byte[] 2097152))
        [IO.File]::WriteAllText($destination,'private modified file')
        $rejected=$false
        try {Publish-VerifiedPortableDownload $source $destination $asset} catch {$rejected=$true}
        if(-not $rejected -or [IO.File]::ReadAllText($destination) -cne 'private modified file'){throw 'Portable promotion overwrote existing data'}
    } finally {$root=$priorRoot;$logPath=$priorLogPath;Remove-Item -LiteralPath $portableFixture -Recurse -Force}
    Write-Output 'ARSONMIX_PORTABLE_DOWNLOAD_POLICY_PASS'

    # Exercise actual ZIP extraction (not a mock) without executing payloads.
    $fixture=Join-Path ([IO.Path]::GetTempPath()) ('armix-payload-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($fixture)|Out-Null
    Add-Type -AssemblyName System.IO.Compression
    try{
        $binaryBytes=[byte[]](1,2,3,4)
        $hashSource=Join-Path $fixture 'binary'
        [IO.File]::WriteAllBytes($hashSource,$binaryBytes)
        $expected=(Get-FileHash -LiteralPath $hashSource -Algorithm SHA256).Hash.ToLowerInvariant()
        $sha='a'*40
        $manifest=ConvertTo-Json -Compress @{sourceCommit=$sha;executable='ArSonMix-GUI10CH-P012-Windows-x64.exe';guiExeBytes=4;exeSha256=$expected}
        $makeZip={
            param([string]$path,[string]$extra)
            $out=[IO.File]::Open($path,[IO.FileMode]::CreateNew)
            try{
                $archive=[IO.Compression.ZipArchive]::new($out,[IO.Compression.ZipArchiveMode]::Create,$true)
                try{
                    $entries=[ordered]@{'ArSonMix-GUI10CH-P012-Windows-x64.exe'=$binaryBytes;
                        'BUILD_PROVENANCE.json'=[Text.Encoding]::UTF8.GetBytes($manifest);
                        'portable_mode.txt'=[Text.Encoding]::UTF8.GetBytes('ARSONMIX_PORTABLE_WORKSPACE_V1');
                        'ArSonMixAutoUpdate.ps1'=[byte[]](1);'UpdateVerifier/gh.exe'=[byte[]](2);
                        'UpdateVerifier/verifier.json'=[byte[]](3)}
                    foreach($name in $entries.Keys){
                        $stream=$archive.CreateEntry($name).Open()
                        try{$bytes=$entries[$name];$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
                    }
                    if($extra){$stream=$archive.CreateEntry($extra).Open();$stream.Dispose()}
                }finally{$archive.Dispose()}
            }finally{$out.Dispose()}
        }
        foreach($case in @('valid','traversal','studio','duplicate','reserved','collision','wrong-source')){
            $extra=switch($case){
                traversal {'../escape.txt'} studio {'Studio/private.smix'}
                duplicate {'updateverifier/GH.EXE'} reserved {'CON.txt'}
                collision {'UpdateVerifier'} default {''}
            }
            $zipFile=Join-Path $fixture ($case+'.zip')
            & $makeZip $zipFile $extra
            $target=Join-Path $fixture $case
            $stream=[IO.File]::OpenRead($zipFile)
            $rejected=$false
            try{Expand-PortablePayload $stream $target $(if($case -eq 'wrong-source'){'b'*40}else{$sha})}
            catch{$rejected=$true}finally{$stream.Dispose()}
            if($case -eq 'valid'){
                if($rejected -or -not (Test-Path -LiteralPath (Join-Path $target 'UpdateVerifier/gh.exe'))){throw 'Valid portable payload rejected'}
            }elseif(-not $rejected){throw "Unsafe portable payload accepted: $case"}
        }
        if(Test-Path -LiteralPath (Join-Path $fixture 'escape.txt')){throw 'ZIP traversal escaped staging'}
        $valid=Join-Path $fixture 'valid'
        $zipFile=Join-Path $fixture 'valid.zip'
        $candidateFile=Join-Path $valid 'UpdateVerifier/gh.exe'
        foreach($change in @('clean','same-size-tamper','extra-file','missing-file','extra-directory')){
            switch($change){
                'same-size-tamper' {[IO.File]::WriteAllBytes($candidateFile,[byte[]](9))}
                'extra-file' {[IO.File]::WriteAllBytes($candidateFile,[byte[]](2));[IO.File]::WriteAllBytes((Join-Path $valid 'foreign.dll'),[byte[]](7))}
                'missing-file' {Remove-Item -LiteralPath (Join-Path $valid 'foreign.dll');Remove-Item -LiteralPath $candidateFile}
                'extra-directory' {[IO.File]::WriteAllBytes($candidateFile,[byte[]](2));[IO.Directory]::CreateDirectory((Join-Path $valid 'foreign'))|Out-Null}
            }
            $stream=[IO.File]::OpenRead($zipFile);$rejected=$false
            try{Expand-PortablePayload $stream $valid $sha -VerifyOnly}catch{$rejected=$true}finally{$stream.Dispose()}
            if(($change -eq 'clean' -and $rejected) -or ($change -ne 'clean' -and -not $rejected)){throw "Portable cached payload validation failed: $change"}
        }
        $receiptPath=Join-Path $fixture 'prepared.json'
        Write-AtomicUpdateJson $receiptPath ([pscustomobject]@{sourceCommit=$sha;tag='v0.5.0-alpha.8'})
        # Simulate an abandoned temp from a crash; only canonical name is read.
        [IO.File]::WriteAllText((Join-Path $fixture '.receipt-abandoned'),'partial')
        if((Read-Json $receiptPath).sourceCommit -cne $sha){throw 'Initial portable receipt mismatch'}
        Write-AtomicUpdateJson $receiptPath ([pscustomobject]@{sourceCommit=('b'*40);tag='v0.5.0-alpha.9'})
        if((Read-Json $receiptPath).sourceCommit -cne ('b'*40)){throw 'Portable receipt replacement mismatch'}
        # Exercise the real installed/portable transaction state writer too:
        # initial write, repeated replace and abandoned temporary bytes.
        $priorStatePath=$statePath
        $statePath=Join-Path $fixture 'transaction-state.json'
        try{
            Write-State ([pscustomobject]@{phase='checking';previousSha=$sha})
            Write-State ([pscustomobject]@{phase='downloading';previousSha=$sha})
            [IO.File]::WriteAllText(($statePath+'.new'),'interrupted legacy temp')
            Write-State ([pscustomobject]@{phase='staged';previousSha=$sha})
            $actual=Read-Json $statePath
            if($actual.phase -cne 'staged' -or $actual.previousSha -cne $sha){throw 'Updater state atomic replacement failed'}
        }finally{$statePath=$priorStatePath}
        Write-Output 'ARSONMIX_PORTABLE_CACHE_REVALIDATION_PASS'
    }finally{Remove-Item -LiteralPath $fixture -Recurse -Force}
    Write-Output 'ARSONMIX_PORTABLE_PAYLOAD_PREPARATION_PASS'

    $fixture=Join-Path ([IO.Path]::GetTempPath()) ('armix-selector-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($fixture)|Out-Null
    $oldRoot=$root;$root=$fixture
    try{
        $base=[pscustomobject]@{sourceCommit=('a'*40);tag='';digest='';bytes=0;proofId='';bundleHash='';rootHash=''}
        $next=[pscustomobject]@{sourceCommit=('b'*40);tag='v0.5.0-alpha.9';digest=('sha256:'+('c'*64));bytes=2097152;
            proofId=('d'*32);bundleHash=('e'*64);rootHash=('f'*64)}
        $zero=Get-PortableSelection $base
        if($zero.generation -ne 0 -or $zero.active.sourceCommit -cne $base.sourceCommit){throw 'Portable bootstrap selector mismatch'}
        Write-PortableSelection $zero $next $base ''
        $one=Get-PortableSelection $base
        if($one.generation -ne 1 -or $one.active.sourceCommit -cne $next.sourceCommit -or $one.previous.sourceCommit -cne $base.sourceCommit){throw 'Portable activation commit mismatch'}
        [IO.File]::WriteAllText((Join-Path $root 'selectors\.receipt-crashed'),'incomplete')
        if((Get-PortableSelection $base).generation -ne 1){throw 'Uncommitted selector accepted'}
        Write-PortableSelection $one $base $null $next.digest
        $two=Get-PortableSelection $base
        if($two.generation -ne 2 -or $two.active.sourceCommit -cne $base.sourceCommit -or $two.blockedDigest -cne $next.digest){throw 'Portable rollback commit mismatch'}
        $state=[pscustomobject]@{candidateSha=$next.sourceCommit;previousSha=$base.sourceCommit;candidateTag=$next.tag;newAsset=[pscustomobject]@{digest=$next.digest}}
        $approval=[pscustomobject]@{candidateSha=$next.sourceCommit;previousSha=$base.sourceCommit;candidateTag=$next.tag;digest=$next.digest}
        if(-not (Test-PortableApproval $approval $state)){throw 'Exact portable consent rejected'}
        $approval.digest='sha256:'+('0'*64)
        if(Test-PortableApproval $approval $state){throw 'Wrong digest consent accepted'}
        $now=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        $intent=[pscustomobject]@{token=('1'*32);ownerPid=123;createdMs=$now;previousSha=$base.sourceCommit;
            candidateSha=$next.sourceCommit;candidateTag=$next.tag;digest=$next.digest}
        if(-not (Test-PortableExitReceipt $intent $state ('1'*32) 123 $now)){throw 'Valid normal exit receipt rejected'}
        if((Test-PortableExitReceipt $intent $state ('2'*32) 123 $now) -or
           (Test-PortableExitReceipt $intent $state ('1'*32) 124 $now) -or
           (Test-PortableExitReceipt $intent $state ('1'*32) 123 ($now+900001))){throw 'Stale or wrong normal exit receipt accepted'}
        foreach($case in @('healthy','no-marker','nonzero')){
            $marker=Join-Path $fixture ($case+'.ready')
            $script=Join-Path $fixture ($case+'.ps1')
            $body=if($case -eq 'no-marker'){'exit 0'}else{'[IO.File]::WriteAllText($args[0],''GUI_READY_NO_AUDIO''); exit '+$(if($case -eq 'healthy'){0}else{1})}
            [IO.File]::WriteAllText($script,$body)
            $info=[Diagnostics.ProcessStartInfo]::new()
            $info.FileName=Join-Path $PSHOME 'powershell.exe';$info.UseShellExecute=$false;$info.CreateNoWindow=$true
            $info.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$script+'" "'+$marker+'"'
            $child=[Diagnostics.Process]::Start($info)
            try{$ok=Wait-PortableProbe $child $marker 15}finally{$child.Dispose()}
            if($ok -ne ($case -eq 'healthy')){throw "Portable health process fixture failed: $case"}
        }
    }finally{$root=$oldRoot;Remove-Item -LiteralPath $fixture -Recurse -Force}
    Write-Output 'ARSONMIX_PORTABLE_ACTIVATION_RECOVERY_PASS'

    Write-Output 'ARSONMIX_UPDATER_RESUME_POLICY_PASS'
    $leaseDir=Join-Path ([IO.Path]::GetTempPath()) ('armix-rollback-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($leaseDir)|Out-Null
    $lease=$null
    try {
        $previous=Join-Path $leaseDir 'previous.exe'
        [IO.File]::WriteAllBytes($previous,(New-Object byte[] 2097152))
        $asset=[pscustomobject]@{digest='sha256:'+(Get-FileHash -LiteralPath $previous -Algorithm SHA256).Hash.ToLowerInvariant();size=2097152}
        $proof=[pscustomobject]@{path=$previous;tag='v0.5.0-alpha.1';sha=('a'*40);digest=$asset.digest;bytes=$asset.size}
        $lease=[IO.File]::Open($previous,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        if(-not (Test-RollbackProof $proof $lease $previous $asset $proof.tag $proof.sha)){throw 'Valid rollback lease rejected'}
        if((Test-RollbackProof $null $lease $previous $asset $proof.tag $proof.sha) -or
           (Test-RollbackProof $proof $lease $previous $asset $proof.tag ('b'*40))){throw 'Unissued or wrong identity rollback proof accepted'}
        $rejected=$false
        try {[IO.File]::WriteAllBytes($previous,[byte[]](1,2,3))} catch {$rejected=$true}
        if(-not $rejected){throw 'Rollback lease allowed replacement'}
        $lease.Dispose();$lease=$null
        if(Test-RollbackProof $proof $lease $previous $asset $proof.tag $proof.sha){throw 'Expired rollback proof accepted'}
    } finally {
        if($null -ne $lease){$lease.Dispose()}
        Remove-Item -LiteralPath $leaseDir -Recurse -Force
    }
    Write-Output 'ARSONMIX_UPDATER_OFFLINE_ROLLBACK_LEASE_PASS'
    Write-Output 'ARSONMIX_UPDATER_POLICY_PASS'
    exit 0
}

try {
    if($LaunchPortable){
        if(-not $portable -or $portableStableRoot -cne $PSScriptRoot -or -not (Test-PortableLauncherRoot)){exit 0}
        # Native root launcher exits before any child takes JUCE's instance
        # lock. If the original PID is already gone, no wait is necessary.
        if($OwnerProcessId -gt 0 -and (Owns-Exe $OwnerProcessId)){
            $launcher=[Diagnostics.Process]::GetProcessById($OwnerProcessId)
            try{if(-not $launcher.WaitForExit(15000)){exit 0}}finally{$launcher.Dispose()}
        }
    }
    $exitIntent=$null
    if($PreparePortable){
        if(-not $portable){exit 0}
        if(Owns-Exe $OwnerProcessId){
            try{
                $portableOwner=[Diagnostics.Process]::GetProcessById($OwnerProcessId)
                if(-not [string]::Equals([IO.Path]::GetFullPath($portableOwner.MainModule.FileName),$exe,[StringComparison]::OrdinalIgnoreCase)){exit 0}
                [void]$portableOwner.Handle
            }catch{if($null -ne $portableOwner){$portableOwner.Dispose();$portableOwner=$null}}
        }
        if($ExitReceipt){$exitIntent=Read-PortableExitReceipt $ExitReceipt $OwnerProcessId}
        if($null -eq $portableOwner -and $null -eq $exitIntent){exit 0}
    }
    if($portable -and (-not (Test-PortableCacheLayout) -or $ApplyStaged -or $RecoveryMode)){exit 0}
    if($RecoveryMode -and -not (Test-Path -LiteralPath $provenance -PathType Leaf)) {
        Show-RecoveryFailure 'Installation version metadata is missing'
        exit 0
    }
    if(-not (Test-Path -LiteralPath $provenance -PathType Leaf) -or
       (-not $RecoveryMode -and -not (Test-Path -LiteralPath $exe -PathType Leaf)) -or
       (-not $LaunchPortable -and -not $PreparePortable -and -not $ApplyStaged -and -not $RecoveryMode -and -not (Owns-Exe $OwnerProcessId))) {exit 0}
    # One update worker per user, for installed and portable packages,
    # arbitrary scripts outside {app}, or second ArSonMix updater instance.
    if($portable -and ($ApplyStaged -or $RecoveryMode)){exit 0}
    $mutex=[Threading.Mutex]::new($false,'Local\ArSonMix.AutoUpdater.v1')
    try {
        $locked=$mutex.WaitOne($(if($LaunchPortable -or $PreparePortable -or $ApplyStaged -or $RecoveryMode){120000}else{0}))
    } catch [Threading.AbandonedMutexException] {
        # Previous worker was killed. WaitOne owns the mutex despite throwing.
        $locked=$true
        Log-Result 'ABANDONED_MUTEX_RECLAIMED'
    }
    if(-not $locked){exit 0}
    if($LaunchPortable){Invoke-PortableLaunch;exit 0}
    if($PreparePortable){Invoke-PortablePreparation $portableOwner $exitIntent;exit 0}
    if($portable){Invoke-PortableUpdateCheck;exit 0}
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $file=Read-Json $provenance
    $installedSha=[string]$file.sourceCommit
    if($installedSha -cnotmatch '^[a-f0-9]{40}$'){
        if($RecoveryMode){Show-RecoveryFailure 'Installed source identity is invalid'}
        exit 0
    }
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
    $attemptCooldown=if($CheckNow){1}else{20}
    if($state.lastAttemptUtc -and [DateTime]::TryParse([string]$state.lastAttemptUtc,[ref]$attempt) -and
       $now -ge $attempt.ToUniversalTime() -and
       $now.Subtract($attempt.ToUniversalTime()).TotalMinutes -lt $attemptCooldown){exit 0}
    $checked=[DateTime]::MinValue
    if(-not $CheckNow -and $imageStatus -cne 'damaged' -and $state.lastCheckUtc -and
       [DateTime]::TryParse([string]$state.lastCheckUtc,[ref]$checked) -and
       $now -ge $checked.ToUniversalTime() -and
       $now.Subtract($checked.ToUniversalTime()).TotalHours -lt 6){exit 0}
    $state.lastAttemptUtc=$now.ToString('o')
    Write-State $state
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/1';
               'X-GitHub-Api-Version'='2022-11-28'}
    Log-Result 'CHECKING_FOR_UPDATES'
    $releases=Read-PublishedReleases $headers
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
    # Release-list retrieval succeeded, but the transaction is NOT checked
    # until both release packages/checksums have passed verification.
    # On network/metadata failure here, retry after lastAttemptUtc's
    # 20-minute backoff, not the six-hour successful-check cooldown.
    $state.phase='staging-retry'
    $state.lastCheckUtc=''
    Write-State $state
    Log-Result 'DOWNLOADING_VERIFIED_UPDATE'
    Download-Release-Verified $new $newAsset $newPath
    # Rollback must be possible offline. Do not begin update without a
    # byte-verified previous installer.
    Download-Release-Verified $current $oldAsset $oldPath
    $state.phase='staged'
    $state.lastCheckUtc=[DateTime]::UtcNow.ToString('o')
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
    # Validate both independently. Do not build nested arrays here:
    # PowerShell 5.1 pipeline unrolling can flatten the pair boundaries.
    if(-not (Test-InstallableAssetPair $newPath $state.newAsset $oldPath $state.previousAsset)) {
        Fail 'Offline staged install/rollback bytes not verified'
    }
    # A published upgrade must be acknowledged once by the user in the
    # lightweight GUI prompt. A missing/stale/corrupt token defers installation,
    # without breaking mixer startup or forcing an active audio session closed.
    # Same-release binary repair is separate and does not require this consent.
    if($state.operation -ceq 'upgrade') {
        $approval=$null
        if(Test-Path -LiteralPath $approvalPath -PathType Leaf) {
            try {$approval=Read-Json $approvalPath} catch { }
        }
        if(-not (Test-UserApproval $approval $state)) {
            Log-Result 'UPDATE_WAITING_FOR_USER_OK'
            exit 0
        }
    }
    # Re-query public release to reject a revoked or replaced candidate.
    # No network is needed *during* live audio; if offline on close, defer.
    $headers=@{Accept='application/vnd.github+json';'User-Agent'='ArSonMix-SmartUpdater/1';
               'X-GitHub-Api-Version'='2022-11-28'}
    $liveReleases=Read-PublishedReleases $headers
    $approved=$null
    $approvedOld=$null
    $candidateRelease=$null
    $baselineRelease=$null
    foreach($release in $liveReleases) {
        if($release.tag_name -ceq $state.candidateTag -and
           $release.target_commitish -ceq $state.candidateSha) {
            $approved=Approved-Installer $release
            $candidateRelease=$release
        }
        if($release.tag_name -ceq $state.previousTag -and
           $release.target_commitish -ceq $state.previousSha) {
            $approvedOld=Approved-Installer $release
            $baselineRelease=$release
        }
    }
    if($null -eq $approvedOld -or
       $approvedOld.name -cne $state.previousAsset.name -or
       $approvedOld.digest -cne $state.previousAsset.digest -or
       [long]$approvedOld.size -ne [long]$state.previousAsset.size) {
        # The rollback package is mandatory. Fail closed if its published
        # source/asset identity was withdrawn or the local cache was altered.
        # Do not install a newer version without an authentic way back.
        Log-Result 'ROLLBACK_RELEASE_UNAVAILABLE_UPDATE_DEFERRED'
        exit 0
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
    if(-not (Test-ReleaseTransition $state.operation $baselineRelease $candidateRelease)){Fail 'Unsafe staged version transition'}
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
    # Reverify after GitHub re-query and process-exit waiting, immediately
    # before Inno touches installed files. Old rollback bytes are included.
    if(-not (Test-InstallableAssetPair $newPath $state.newAsset $oldPath $state.previousAsset)) {
        Log-Result 'INSTALLER_BYTES_CHANGED_UPDATE_DEFERRED'
        exit 0
    }
    # Keep the previously authenticated installer immutable until this
    # transaction finishes. Rollback does not depend on the NEW verifier or
    # network access after installation. No proof is persisted across restart.
    $rollbackGuard=[IO.File]::Open($oldPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    if(-not (Test-StagedAsset $oldPath $oldAsset)){Fail 'Rollback bytes changed before attestation'}
    Assert-InstallerAttestation $oldPath ([string]$state.previousTag) ([string]$state.previousSha)
    $rollbackProof=[pscustomobject]@{path=[IO.Path]::GetFullPath($oldPath);tag=[string]$state.previousTag;
        sha=[string]$state.previousSha;digest=[string]$oldAsset.digest;bytes=[long]$oldAsset.size}
    $state.phase='installing'
    $state.installAttempts=[int]$state.installAttempts+1
    Write-State $state
    $upgradeOK=Run-Installer $newPath $newAsset ([string]$state.candidateTag) ([string]$state.candidateSha)
    $actualSha=''
    try {$actualSha=[string](Read-Json $provenance).sourceCommit} catch {}
    if($upgradeOK -and (Test-InstalledRelease ([string]$new.target_commitish)) -and
       (Launch-WithHealth ([string]$new.target_commitish))) {
        $state.lastInstalledTag=$new.tag_name
        $state.phase='idle'
        $state.retryAfterUtc=''
        $state.installAttempts=0
        $state.blockedTag=''
        Write-State $state
        Log-Result ($(if($state.operation -ceq 'repair'){'REPAIRED '}else{'HEALTHY '})+$new.tag_name)
        Remove-Item -LiteralPath $approvalPath -Force -ErrorAction SilentlyContinue
        if($null -ne $rollbackGuard){$rollbackGuard.Dispose();$rollbackGuard=$null;$rollbackProof=$null}
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
        # Do not surprise the operator by restarting audible audio after Quit.
        # The previous verified EXE remains available on next deliberate launch.
        Log-Result 'PREVIOUS_EXE_RETAINED_FOR_NEXT_MANUAL_LAUNCH'
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
        # A failed post-exit update must not auto-start an audible mixer.
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
    if(-not (Test-StagedAsset $oldPath $oldAsset)){
        Fail 'Rollback installer modified after verified stage'
    }
    if(Run-Installer $oldPath $oldAsset ([string]$state.previousTag) ([string]$state.previousSha) -UseVerifiedRollback) {
        if(Test-InstalledRelease $installedSha) {
            $state.phase='rolled-back'
            Write-State $state
            Log-Result ('ROLLED_BACK '+$new.tag_name)
            # Restored version is ready for the next deliberate user launch.
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
    if(-not $portable -or (Test-PortableCacheLayout)){Log-Result ('FAIL_CLOSED '+$_.Exception.Message)}
    if($LaunchPortable){
        try{
            Add-Type -AssemblyName System.Windows.Forms
            [Windows.Forms.MessageBox]::Show("ArSonMix could not verify or launch the portable version. Studio was not removed. Restore the official verified package; see .ArSonMixUpdates\last-result.txt.",'ArSonMix - Portable Recovery')|Out-Null
        }catch{}
    }
} finally {
    if($null -ne $portableOwner){$portableOwner.Dispose()}
    if($null -ne $rollbackGuard){$rollbackGuard.Dispose()}
    if($locked -and $null -ne $mutex){$mutex.ReleaseMutex()}
    if($null -ne $mutex){$mutex.Dispose()}
}
