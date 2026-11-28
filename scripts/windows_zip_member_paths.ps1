# SPDX-License-Identifier: AGPL-3.0-only
# Exact ZIP file inventory for Windows paths; archive member names must be
# portable POSIX paths, as required by the public release trust manifest.
function Get-ArSonMixZipRelativePath([string]$Root,[string]$File) {
    return [IO.Path]::GetRelativePath($Root,$File).Replace([char]92,[char]47)
}

function Assert-ArSonMixZipFileSet($Zip,[string[]]$Expected) {
    $seen=[System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $names=@(foreach($entry in $Zip.Entries) {
        $name=$entry.FullName
        if($name.Contains([char]92)){throw "ZIP member is not a canonical POSIX path: $name"}
        if(-not $seen.Add($name.TrimEnd('/'))){throw "Duplicate ZIP member: $name"}
        if(-not $name.EndsWith('/')){$name}
    })
    $delta=@(Compare-Object -ReferenceObject $Expected -DifferenceObject $names -CaseSensitive)
    if($delta.Count -ne 0 -or $names.Count -ne $Expected.Count){
        throw ("ZIP does not contain exactly the verified distribution files: "+(($delta|ForEach-Object {"$($_.SideIndicator) $($_.InputObject)"}) -join '; '))
    }
}

function Assert-ArSonMixZipPayload($Zip,[string]$Root) {
    $files=@(Get-ChildItem -LiteralPath $Root -File -Recurse | ForEach-Object {
        Get-ArSonMixZipRelativePath $Root $_.FullName
    })
    Assert-ArSonMixZipFileSet $Zip $files
    foreach($name in $files){
        $entry=$Zip.GetEntry($name)
        if($null -eq $entry){throw "ZIP member missing: $name"}
        $stream=$entry.Open()
        $hasher=[Security.Cryptography.SHA256]::Create()
        try{$actual=[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
        finally{$stream.Dispose();$hasher.Dispose()}
        $expected=(Get-FileHash -LiteralPath (Join-Path $Root $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        if($actual -cne $expected){throw "ZIP payload SHA256 mismatch: $name"}
    }
}
