# SPDX-License-Identifier: AGPL-3.0-only
# FAT builder only. Download exact official standard VB-CABLE AS IS.
[CmdletBinding()]param([Parameter(Mandatory=$true)][string]$StageDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$m=Get-Content -LiteralPath "config/vbcable-official-package.json" -Raw | ConvertFrom-Json
if($m.filename -ne "VBCABLE_Driver_Pack45.zip" -or
   $m.url -ne "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip" -or
   $m.sha256 -notmatch '^[a-f0-9]{64}$' -or $m.consent -ne $true){
  throw "Unverified third-party package manifest"
}
$file=Join-Path $env:RUNNER_TEMP ("armix-vbcable-"+[guid]::NewGuid().ToString("N")+".zip")
try {
  Invoke-WebRequest -Uri $m.url -OutFile $file -MaximumRedirection 3
  $bytes=(Get-Item -LiteralPath $file).Length
  if($bytes -lt 512KB -or $bytes -gt 10MB){throw "Vendor archive size out of bounds"}
  $hash=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
  if($hash -cne $m.sha256){throw "VB-Audio ZIP hash changed; re-audit upstream before releasing"}
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip=[IO.Compression.ZipFile]::OpenRead($file)
  try {
    $names=@($zip.Entries | ForEach-Object FullName)
    if($names.Count -lt 5 -or $names.Count -gt 100){throw "Unexpected VB-Audio archive structure"}
    foreach($n in $names){
      if($n.StartsWith("/") -or $n.StartsWith("\") -or
         $n -match '(^|[/\\])\.\.([/\\]|$)' -or
         $n -match '(?i)VBCABLE_A_B_|VBCABLE_CD_'){
        throw "Unsafe vendor archive or nonredistributable secondary cable"}
    }
    if(@($names | Where-Object {$_ -match '(?i)(^|[/\\])VBCABLE_Setup_x64\.exe$'}).Count -ne 1 -or
       @($names | Where-Object {$_ -match '(?i)\.inf$'}).Count -lt 1){
      throw "VB-CABLE standard setup/INF not found"
    }
    $total=($zip.Entries | Measure-Object -Property Length -Sum).Sum
    if($total -gt 50MB){throw "Expanded vendor archive exceeds safety budget"}
  } finally {$zip.Dispose()}
  New-Item -ItemType Directory -Force -Path $StageDirectory | Out-Null
  Copy-Item -LiteralPath $file -Destination (Join-Path $StageDirectory $m.filename)
  Copy-Item -LiteralPath "installer/VB-CABLE-NOTICE.txt" -Destination (Join-Path $StageDirectory "VB-CABLE-NOTICE.txt")
  Write-Host "OFFICIAL_VBCABLE_BUNDLE_VERIFIED: $hash ($bytes bytes)"
} finally {
  if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file -Force}
}
