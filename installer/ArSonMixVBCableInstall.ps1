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
$script:bootUtc=""
function Record([string]$stateName,[string]$reason) {
  $temp=$statusPath+".tmp"
  [ordered]@{schema="arsonmix.vendor-driver.v1";state=$stateName;
    reason=$reason;utc=[DateTime]::UtcNow.ToString('o');
    bootUtc=$script:bootUtc;
    rebootRequired=($stateName -eq 'PENDING_REBOOT')} |
      ConvertTo-Json | Set-Content -LiteralPath $temp -Encoding UTF8
  Move-Item -LiteralPath $temp -Destination $statusPath -Force
}
$dir=Join-Path $env:TEMP ("armix-cable-"+[guid]::NewGuid().ToString("N"))
try {
  # A driver already queued for reboot must NOT be run again: the VB-Audio
  # setup utility can switch into REMOVE mode on a second invocation.
  # Record the Windows boot epoch and only clear pending after *another* boot
  # AND a healthy PnP readback. Never infer installation from a process exit.
  $os=Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
  $script:bootUtc=$os.LastBootUpTime.ToUniversalTime().ToString('o')
  $previous=$null
  if(Test-Path -LiteralPath $statusPath -PathType Leaf){
    $previous=Get-Content -LiteralPath $statusPath -Raw -ErrorAction Stop | ConvertFrom-Json
  }
  $isCable={ $_.FriendlyName -match '(?i)VB-Audio Virtual Cable|CABLE (Output|Input).*VB-Audio' }
  # Include non-present devices. Only examining PresentOnly can accidentally
  # launch the vendor setup in REMOVE mode when a driver awaits reboot.
  $all=@(Get-PnpDevice -ErrorAction Stop | Where-Object $isCable)
  $present=@(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object $isCable)
  if($null -ne $previous -and $previous.state -eq 'PENDING_REBOOT'){
    if([string]::IsNullOrWhiteSpace($previous.bootUtc) -or
       $previous.bootUtc -eq $script:bootUtc){
      Record "PENDING_REBOOT" "Restart Windows to complete the already-launched VB-CABLE installation"
      exit 0
    }
    if($present.Count -eq 0 -or @($present | Where-Object {$_.Status -ne 'OK'}).Count -gt 0){
      Record "REPAIR_REQUIRED" "After reboot, VB-CABLE did not expose healthy PnP endpoints; do not rerun vendor setup automatically"
      exit 3
    }
    Record "ALREADY_PRESENT" "Reboot complete; verified VB-CABLE PnP presence"
    exit 0
  }
  if($all.Count -gt 0){
    if($present.Count -eq 0 -or @($all | Where-Object {$_.Status -ne 'OK'}).Count -gt 0){
      Record "REPAIR_REQUIRED" "VB-CABLE installed but missing or unhealthy; inspect Windows Device Manager"
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
     $sig.SignerCertificate.Subject -notmatch '(?i)VB[- ]Audio|Vincent\s+Burel|Burel\s+Vincent'){
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
