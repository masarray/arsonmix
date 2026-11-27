# SPDX-License-Identifier: AGPL-3.0-only
# Offline A/B of exact cached-payload verifier from canonical bc195591.
# Baseline function is retained solely in this test fixture; it cannot launch.
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'installer/ArSonMixAutoUpdate.ps1').Path,[ref]$null,[ref]$null)
foreach($name in @('Read-Json','Test-RegularUpdateFile','Expand-PortablePayload')){
    $matches=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$false))
    if($matches.Count -ne 1){throw "Missing unique verifier function: $name"}
    . ([scriptblock]::Create($matches[0].Extent.Text))
}
function Fail([string]$reason){throw $reason}
function Expand-PortablePayloadBaseline([IO.Stream]$lease,[string]$target,[string]$sourceSha,[switch]$VerifyOnly) {
    if($sourceSha -cnotmatch '^[a-f0-9]{40}$'){throw 'Unsafe portable payload identity'}
    if($VerifyOnly){
        if(-not (Test-Path -LiteralPath $target -PathType Container) -or
           ((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unsafe cached portable payload'}
    }elseif(Test-Path -LiteralPath $target){throw 'Unsafe portable payload target'}
    Add-Type -AssemblyName System.IO.Compression
    $lease.Position=0
    $zip=[IO.Compression.ZipArchive]::new($lease,[IO.Compression.ZipArchiveMode]::Read,$true)
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
                if((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expectedHash){throw 'Cached portable member hash mismatch'}
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
        if($build.sourceCommit -cne $sourceSha -or $build.executable -cne [IO.Path]::GetFileName($binary) -or
           $build.exeSha256 -cnotmatch '^[a-f0-9]{64}$' -or
           [long]$build.guiExeBytes -ne (Get-Item -LiteralPath $binary).Length -or
           (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant() -cne $build.exeSha256 -or
           (Get-Content -LiteralPath (Join-Path $target 'portable_mode.txt') -Raw).Trim() -cne 'ARSONMIX_PORTABLE_WORKSPACE_V1'){
            throw 'Portable payload provenance mismatch'
        }
    }finally{$zip.Dispose()}
}

# Count actual successful disk hash invocations and exact bytes; time the
# complete production function (archive decompression, tree walk, hashes).
$script:hashCalls=0;$script:hashBytes=[long]0;$script:binaryCalls=0
$script:checkLease=$false;$script:leaseDenied=$false
function Get-FileHash([string]$LiteralPath,[string]$Algorithm='SHA256'){
    $length=(Get-Item -LiteralPath $LiteralPath).Length
    $script:hashCalls++;$script:hashBytes+=$length
    if([IO.Path]::GetFileName($LiteralPath) -ceq 'ArSonMix-GUI10CH-P012-Windows-x64.exe'){
        $script:binaryCalls++
        if($script:checkLease){
            try{$write=[IO.File]::Open($LiteralPath,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite);$write.Dispose()}
            catch [IO.IOException]{$script:leaseDenied=$true}
        }
    }
    Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
}
function Measure-Variant([string]$variant,[string]$archive,[string]$payload,[string]$sha){
    $script:hashCalls=0;$script:hashBytes=[long]0;$script:binaryCalls=0
    $script:checkLease=$variant -ceq 'after';$script:leaseDenied=$false
    $stream=[IO.File]::OpenRead($archive)
    $sw=[Diagnostics.Stopwatch]::StartNew()
    try{
        if($variant -ceq 'before'){Expand-PortablePayloadBaseline $stream $payload $sha -VerifyOnly}
        else{Expand-PortablePayload $stream $payload $sha -VerifyOnly}
    }finally{$sw.Stop();$stream.Dispose()}
    if($script:checkLease -and -not $script:leaseDenied){throw 'Optimized verifier did not hold executable read lease'}
    [pscustomobject]@{variant=$variant;ms=$sw.Elapsed.TotalMilliseconds;calls=$script:hashCalls;bytes=$script:hashBytes;binaryCalls=$script:binaryCalls}
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$work=Join-Path ([IO.Path]::GetTempPath()) ('armix-verify-profile-'+[guid]::NewGuid().ToString('N'))
$payload=Join-Path $work 'payload';$archive=Join-Path $work 'profile.zip'
[IO.Directory]::CreateDirectory((Join-Path $payload 'UpdateVerifier'))|Out-Null
try{
    $sha='a'*40
    $binary=Join-Path $payload 'ArSonMix-GUI10CH-P012-Windows-x64.exe'
    $block=New-Object byte[] 1048576
    [Random]::new(1701).NextBytes($block)
    $stream=[IO.File]::Create($binary)
    try{for($i=0;$i -lt 64;$i++){$stream.Write($block,0,$block.Length)}}finally{$stream.Dispose()}
    $hash=(Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    @{sourceCommit=$sha;executable=[IO.Path]::GetFileName($binary);exeSha256=$hash;guiExeBytes=64MB}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $payload 'BUILD_PROVENANCE.json')
    [IO.File]::WriteAllText((Join-Path $payload 'portable_mode.txt'),'ARSONMIX_PORTABLE_WORKSPACE_V1')
    [IO.File]::WriteAllText((Join-Path $payload 'ArSonMixAutoUpdate.ps1'),'fixture')
    [IO.File]::WriteAllText((Join-Path $payload 'UpdateVerifier/gh.exe'),'fixture')
    [IO.File]::WriteAllText((Join-Path $payload 'UpdateVerifier/verifier.json'),'{}')
    # .NET Framework's CreateFromDirectory uses backslashes under Windows
    # PowerShell 5.1. Explicit portable ZIP names are forward-slash only.
    $zipStream=[IO.File]::Open($archive,[IO.FileMode]::CreateNew)
    try{
        $zip=[IO.Compression.ZipArchive]::new($zipStream,[IO.Compression.ZipArchiveMode]::Create,$true)
        try{
            foreach($file in Get-ChildItem -LiteralPath $payload -File -Recurse){
                $relative=$file.FullName.Substring($payload.Length+1).Replace('\','/')
                $entry=$zip.CreateEntry($relative,[IO.Compression.CompressionLevel]::NoCompression)
                $source=[IO.File]::OpenRead($file.FullName)
                try{$dest=$entry.Open();try{$source.CopyTo($dest)}finally{$dest.Dispose()}}finally{$source.Dispose()}
            }
        }finally{$zip.Dispose()}
    }finally{$zipStream.Dispose()}
    # Warm both paths, then alternate order to reduce file-cache/order bias.
    [void](Measure-Variant 'before' $archive $payload $sha)
    [void](Measure-Variant 'after' $archive $payload $sha)
    $before=@();$after=@()
    for($i=0;$i -lt 7;$i++){
        $order=if($i%2 -eq 0){@('before','after')}else{@('after','before')}
        foreach($variant in $order){
            $row=Measure-Variant $variant $archive $payload $sha
            if($variant -ceq 'before'){$before+= $row}else{$after+= $row}
        }
    }
    foreach($i in 0..6){
        if($before[$i].binaryCalls -ne 2 -or $after[$i].binaryCalls -ne 1 -or
            $before[$i].calls-$after[$i].calls -ne 1 -or $before[$i].bytes-$after[$i].bytes -ne 64MB){
            throw 'Expected exact redundant executable read was not eliminated'
        }
    }
    # Hash and provenance must both remain authoritative. A different source
    # is rejected even when the actual ZIP and all disk files are identical.
    $rejected=$false;$stream=[IO.File]::OpenRead($archive)
    try{Expand-PortablePayload $stream $payload ('b'*40) -VerifyOnly}catch{$rejected=$true}finally{$stream.Dispose()}
    if(-not $rejected){throw 'Optimized path accepted incorrect source provenance'}
    $stream=[IO.File]::Open($binary,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.WriteByte([byte]($block[0] -bxor 255))}finally{$stream.Dispose()}
    $rejected=$false;$stream=[IO.File]::OpenRead($archive)
    try{Expand-PortablePayload $stream $payload $sha -VerifyOnly}catch{$rejected=$true}finally{$stream.Dispose()}
    if(-not $rejected){throw 'Optimized path accepted same-size binary modification'}
    $b=($before.ms|Sort-Object)[3];$a=($after.ms|Sort-Object)[3]
    $result=[pscustomobject]@{fixtureBytes=64MB;beforeMedianMs=$b;afterMedianMs=$a;beforeHashCalls=$before[0].calls;afterHashCalls=$after[0].calls;beforeHashBytes=$before[0].bytes;afterHashBytes=$after[0].bytes;beforeMs=@($before.ms);afterMs=@($after.ms)}
    Write-Output ('ARSONMIX_PORTABLE_PROFILE '+($result|ConvertTo-Json -Compress))
    Write-Output 'ARSONMIX_PORTABLE_PROFILE_PASS'
}finally{Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue}
