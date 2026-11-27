# SPDX-License-Identifier: AGPL-3.0-only
# Build-side intake only: never fetch executable dependencies on a user's PC.
[CmdletBinding()]param([Parameter(Mandatory=$true)][string]$StageDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$pin=Get-Content -LiteralPath 'config/update-verifier-package.json' -Raw | ConvertFrom-Json
if($pin.version -cne '2.106.0' -or
   $pin.url -cne 'https://github.com/cli/cli/releases/download/v2.106.0/gh_2.106.0_windows_amd64.zip' -or
   $pin.sha256 -cne 'd406f442ca0baa3d3e9c695e9cded1aa48e67dad3294b8947e5ab84f980b4844' -or $pin.bytes -ne 15265298){throw 'Unapproved updater verifier pin'}
$work=Join-Path ([IO.Path]::GetTempPath()) ('armix-gh-intake-'+[guid]::NewGuid().ToString('N'))
$folder=Join-Path $StageDirectory 'UpdateVerifier'
[IO.Directory]::CreateDirectory($work)|Out-Null
try {
    $archive=Join-Path $work 'upstream.zip'
    Invoke-WebRequest -UseBasicParsing -Uri $pin.url -OutFile $archive -MaximumRedirection 3 -TimeoutSec 90
    if((Get-Item -LiteralPath $archive).Length -ne $pin.bytes -or
       (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -cne $pin.sha256){throw 'Official verifier archive changed'}
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entries=@($zip.Entries | Where-Object {$_.Name -ceq 'gh.exe'})
        if($entries.Count -ne 1 -or $entries[0].Length -lt 1MB -or $entries[0].Length -gt 100MB){throw 'Unexpected pinned verifier archive'}
        $binary=Join-Path $work 'gh.exe'
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0],$binary)
    } finally {$zip.Dispose()}
    $versionOutput=& $binary --version
    if($LASTEXITCODE -ne 0 -or ($versionOutput -join ' ') -notmatch '^gh version 2\.106\.0(?:\s|$)'){throw 'Unexpected verifier version'}
    $env:GH_PAGER=''
    $licenses=& $binary licenses
    if($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace(($licenses -join "\n"))){throw 'Missing dependency license inventory'}
    [IO.Directory]::CreateDirectory($folder)|Out-Null
    Copy-Item -LiteralPath $binary -Destination (Join-Path $folder 'gh.exe')
    Copy-Item -LiteralPath 'installer/UPDATE-VERIFIER-LICENSE.txt' -Destination (Join-Path $folder 'LICENSE.txt')
    [IO.File]::WriteAllLines((Join-Path $folder 'THIRD-PARTY-LICENSES.txt'),[string[]]$licenses,[Text.UTF8Encoding]::new($false))
    $receipt=[ordered]@{version=$pin.version;archiveSha256=$pin.sha256;
        exeSha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant();
        exeBytes=(Get-Item -LiteralPath $binary).Length}
    [IO.File]::WriteAllText((Join-Path $folder 'verifier.json'),(ConvertTo-Json $receipt -Compress),[Text.UTF8Encoding]::new($false))
    Write-Host ('ARSONMIX_UPDATE_VERIFIER_STAGED '+$receipt.exeSha256)
} finally {Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue}
