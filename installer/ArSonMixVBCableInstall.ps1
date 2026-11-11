# SPDX-License-Identifier: AGPL-3.0-only
# Explicitly opted-in driver install; no certificate trust-store mutations,
# endpoint default changes, silent UAC bypass or driver uninstall.
[CmdletBinding()]param(
  [Parameter(Mandatory=$true)][string]$Archive,
  [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedSha256
)
Set-StrictMode -Version Latest
$ErrorActionPreference="Stop"
$state=Join-Path $env:APPDATA "AR-MIX-Studio"
New-Item -ItemType Directory -Force -Path $state | Out-Null
$statusPath=Join-Path $state "VB-CABLE-STATUS.json"
function Record([string]$stateName,[string]$reason) {
  $temp=$statusPath+".tmp"
  [ordered]@{schema="arsonmix.vendor-driver.v1";state=$stateName;
    reason=$reason;utc=[DateTime]::UtcNow.ToString('o');
    rebootRequired=($stateName -eq 'PENDING_REBOOT')} |
      ConvertTo-Json | Set-Content -LiteralPath $temp -Encoding UTF8
  Move-Item -LiteralPath $temp -Destination $statusPath -Force
}
$dir=Join-Path $env:TEMP ("armix-cable-"+[guid]::NewGuid().ToString("N"))
try {
  # Running the vendor installer on an existing driver can REMOVE it.
  # Any suspected existing VB-Audio endpoint, healthy or damaged, blocks
  # automatic invocation; unhealthy states require manual repair.
  $present=@(Get-PnpDevice -PresentOnly -ErrorAction Stop |
    Where-Object {$_.FriendlyName -match '(?i)VB-Audio Virtual Cable|CABLE (Output|Input).*VB-Audio'})
  if($present.Count -gt 0) {
    if(@($present | Where-Object {$_.Status -ne 'OK'}).Count -gt 0){
      Record "REPAIR_REQUIRED" "VB-CABLE device is unhealthy; inspect Windows Device Manager"
      exit 3
    }
    Record "ALREADY_PRESENT" "VB-CABLE device already installed; vendor installer skipped"
    exit 0
  }
  if(-not (Test-Path -LiteralPath $Archive -PathType Leaf)){throw "Bundled driver ZIP absent"}
  $hash=(Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
  if($hash -cne $ExpectedSha256.ToLowerInvariant()){throw "Pinned driver ZIP hash mismatch"}
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
  try {
    $entries=@($zip.Entries)
    if($entries.Count -lt 5 -or $entries.Count -gt 100){throw "Unexpected driver ZIP contents"}
    foreach($e in $entries){
      $n=$e.FullName
      if($n.StartsWith("/") -or $n.StartsWith("\") -or
        $n -match '(^|[/\\])\.\.([/\\]|$)' -or
        $n -match '(?i)VBCABLE_A_B_|VBCABLE_CD_'){
        throw "Invalid archive path or secondary cable"}
    }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive,$dir)
  } finally {$zip.Dispose()}
  $setups=@(Get-ChildItem -LiteralPath $dir -Filter "VBCABLE_Setup_x64.exe" -File -Recurse)
  if($setups.Count -ne 1){throw "Vendor setup missing or ambiguous"}
  $sig=Get-AuthenticodeSignature -LiteralPath $setups[0].FullName
  if($sig.Status -ne "Valid" -or $null -eq $sig.SignerCertificate -or
     $sig.SignerCertificate.Subject -notmatch '(?i)VB-Audio|Vincent Burel'){
    throw "VB-Audio Authenticode signature unavailable or invalid"
  }
  Record "AWAITING_UAC" "Approving vendor driver installation is optional"
  # Official reference manual requires the vendor GUI, admin and reboot.
  # No undocumented silent switches and no TrustedPublisher modification.
  $process=Start-Process -FilePath $setups[0].FullName -WorkingDirectory $setups[0].DirectoryName -Verb RunAs -Wait -PassThru
  if($process.ExitCode -ne 0){throw ("Vendor installer returned "+$process.ExitCode)}
  Record "PENDING_REBOOT" "Reboot Windows, then verify CABLE Output and physical speaker"
  exit 0
} catch {
  Record "FAILED" $_.Exception.Message
  Write-Warning ("Optional VB-Audio driver step incomplete: "+$_.Exception.Message)
  exit 2
} finally {
  if(Test-Path -LiteralPath $dir){Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue}
}
