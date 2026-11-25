# SPDX-License-Identifier: AGPL-3.0-only
# Interactive installer dependency preflight, never a realtime audio callback.
[CmdletBinding()]param([switch]$PolicySelfTest)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$official='https://go.microsoft.com/fwlink/p/?LinkId=2124703'
$guid='{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
function Valid-Version([string]$value) {
  if($value -notmatch '^\d+\.\d+\.\d+\.\d+$'){return $false}
  try {return ([version]$value) -ge [version]'109.0.0.0'}catch{return $false}
}
function Microsoft-Uri([uri]$uri) {
  if($null -eq $uri -or $uri.Scheme -cne 'https'){return $false}
  $hostName=$uri.DnsSafeHost.ToLowerInvariant()
  return $hostName -eq 'microsoft.com' -or $hostName.EndsWith('.microsoft.com')
}
function Microsoft-Signer($signature) {
  if($null -eq $signature -or [string]$signature.Status -cne 'Valid' -or
    $null -eq $signature.SignerCertificate){return $false}
  return [string]$signature.SignerCertificate.Subject -match '(?i)(^|,\s*)(CN|O)=Microsoft Corporation(,|$)'
}
if($PolicySelfTest) {
  $good=[pscustomobject]@{Status='Valid';SignerCertificate=[pscustomobject]@{Subject='CN=Microsoft Corporation, O=Microsoft Corporation'}}
  $bad=[pscustomobject]@{Status='NotTrusted';SignerCertificate=[pscustomobject]@{Subject='CN=Microsoft Corporation'}}
  if(-not (Valid-Version '120.0.0.1') -or (Valid-Version '0.0.0.0') -or
    (Valid-Version 'bogus') -or (Valid-Version '108.0.0.1') -or
    -not (Microsoft-Uri ([uri]$official)) -or
    (Microsoft-Uri ([uri]'http://go.microsoft.com/setup.exe')) -or
    (Microsoft-Uri ([uri]'https://malicious-microsoft.com/setup.exe')) -or
    -not (Microsoft-Signer $good) -or (Microsoft-Signer $bad)){
    throw 'ARSONMIX_WEBVIEW2_PREREQUISITE_POLICY_FAIL'
  }
  Write-Output 'ARSONMIX_WEBVIEW2_PREREQUISITE_POLICY_PASS'
  exit 0
}
$folder=Join-Path $env:LOCALAPPDATA 'ArSonMix\diagnostics'
$report=Join-Path $folder 'webview2-prerequisite.json'
function Record([string]$state,[string]$detail) {
  try {
    [IO.Directory]::CreateDirectory($folder)|Out-Null
    $temp=$report+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    [ordered]@{schema='arsonmix.webview2-prerequisite.v1';state=$state;detail=$detail;utc=[DateTime]::UtcNow.ToString('o')} |
      ConvertTo-Json -Compress | Set-Content -LiteralPath $temp -Encoding UTF8
    Move-Item -LiteralPath $temp -Destination $report -Force
  }catch{Write-Warning ('Unable to save WebView2 diagnostic: '+$_.Exception.Message)}
}
function Find-EvergreenRuntime {
  $keys=@(
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$guid",
    "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$guid",
    "HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$guid",
    "HKCU:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$guid"
  )
  $roots=@(
    (Join-Path $env:LOCALAPPDATA 'Microsoft\EdgeWebView\Application'),
    (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft\EdgeWebView\Application'),
    (Join-Path $env:ProgramFiles 'Microsoft\EdgeWebView\Application')
  ) | Select-Object -Unique
  foreach($key in $keys){
    try {
      $pv=[string](Get-ItemProperty -LiteralPath $key -Name pv -ErrorAction Stop).pv
      if(-not (Valid-Version $pv)){continue}
      foreach($root in $roots){
        if(Test-Path -LiteralPath (Join-Path (Join-Path $root $pv) 'msedgewebview2.exe') -PathType Leaf){return $pv}
      }
    }catch{Write-Verbose ('WebView2 registration query: '+$_.Exception.Message)}
  }
  return ''
}
$work=$null
$bootstrapperStillRunning=$false
try {
  $os=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
  if(-not [Environment]::Is64BitOperatingSystem -or [int]$os.CurrentBuildNumber -lt 17763){
    Record 'UNSUPPORTED_OS' 'ArSonMix requires Windows 10 build 17763+ x64 or Windows 11 x64'
    exit 3
  }
  $installed=Find-EvergreenRuntime
  if($installed){
    Record 'ALREADY_PRESENT' ('WebView2 '+$installed+' is installed; no changes needed')
    Write-Output 'ARSONMIX_WEBVIEW2_ALREADY_PRESENT'
    exit 0
  }
  # Download only when missing. Final HTTPS host and Microsoft Authenticode
  # must both validate. No package from third-party mirrors is ever executed.
  $work=Join-Path $env:TEMP ('arsonmix-webview2-'+[guid]::NewGuid().ToString('N'))
  [IO.Directory]::CreateDirectory($work)|Out-Null
  $file=Join-Path $work 'MicrosoftEdgeWebView2Setup.exe'
  [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
  $request=[Net.HttpWebRequest]::Create($official)
  $request.AllowAutoRedirect=$true
  $request.MaximumAutomaticRedirections=8
  $request.Timeout=20000
  $request.ReadWriteTimeout=20000
  $response=$request.GetResponse()
  try {
    if(-not (Microsoft-Uri $response.ResponseUri)){throw 'Unapproved Microsoft bootstrapper redirect host'}
    $stream=$response.GetResponseStream()
    $output=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
    try {
      $bytes=New-Object byte[] 65536
      $total=0
      while(($n=$stream.Read($bytes,0,$bytes.Length)) -gt 0){
        $total+=$n
        if($total -gt 16777216){throw 'Oversized WebView2 bootstrapper'}
        $output.Write($bytes,0,$n)
      }
      if($total -lt 32768){throw 'Incomplete WebView2 download'}
    }finally{$output.Dispose();$stream.Dispose()}
  }finally{$response.Dispose()}
  $signature=Get-AuthenticodeSignature -LiteralPath $file -ErrorAction Stop
  if(-not (Microsoft-Signer $signature)){throw 'Untrusted WebView2 bootstrapper (not executed)'}
  # Official supported Evergreen installer switches; respects Windows policy.
  # Bound only our wait, NEVER kill or interrupt the trusted Microsoft setup.
  # A slow machine or enterprise policy must not hold the ArSonMix installer
  # indefinitely. A still-running setup keeps its temporary executable.
  $process=Start-Process -FilePath $file -ArgumentList @('/silent','/install') -PassThru -ErrorAction Stop
  if(-not $process.WaitForExit(120000)){
    $bootstrapperStillRunning=$true
    throw 'WebView2 setup is still running; ArSonMix installation will continue. Check Startup Diagnostics after setup completes'
  }
  if($process.ExitCode -ne 0){throw ('Microsoft WebView2 setup failed: '+$process.ExitCode)}
  $installed=''
  for($i=0;$i -lt 5;$i++){
    $installed=Find-EvergreenRuntime
    if($installed){break}
    Start-Sleep -Seconds 2
  }
  if(-not $installed){throw 'WebView2 setup finished but runtime was not detected'}
  Record 'INSTALLED' ('WebView2 '+$installed+' verified')
  Write-Output 'ARSONMIX_WEBVIEW2_INSTALLED'
  exit 0
}catch{
  # Offline, group-policy, restricted permissions and failed downloads are
  # recoverable. Do not loop, claim success, or delete user session data.
  Record 'NEEDS_ATTENTION' $_.Exception.Message
  Write-Warning ('WebView2 installation incomplete: '+$_.Exception.Message)
  exit 2
}finally{
  if(-not $bootstrapperStillRunning -and $work -and (Test-Path -LiteralPath $work)){
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
  }
}
