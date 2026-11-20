# SPDX-License-Identifier: AGPL-3.0-only
[CmdletBinding()]
param([switch]$SafeWebViewProfile, [switch]$ShowReport)

$ErrorActionPreference = 'Stop'
$folder = Join-Path $env:LOCALAPPDATA 'ArSonMix\diagnostics'
[IO.Directory]::CreateDirectory($folder) | Out-Null
$report = Join-Path $folder 'windows-startup-report.txt'
$os = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$build = [int]$os.CurrentBuildNumber
$exe = @('ArSonMix.exe','ArSonMix-GUI10CH-P012-Windows-x64.exe') |
    ForEach-Object { Join-Path $PSScriptRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
$lines = @(
    "Observed: $([DateTimeOffset]::Now.ToString('o'))"
    "Windows build: $build.$($os.UBR)"
    "64-bit OS: $([Environment]::Is64BitOperatingSystem)"
    "Minimum Windows x64 build: 17763"
)
if ($exe) {
    $lines += "Executable SHA256: $((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)"
} else {
    $lines += 'Executable missing. Try Repair ArSonMix from the Start menu.'
}
# Evergreen WebView2 Runtime registration is per-machine or per-user.
$webViewKey = '{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
$registrations = @(
    "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$webViewKey"
    "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$webViewKey"
    "HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$webViewKey"
) | ForEach-Object {
    (Get-ItemProperty -LiteralPath $_ -Name pv -ErrorAction SilentlyContinue).pv
} | Where-Object { $_ -and $_ -ne '0.0.0.0' } | Sort-Object -Unique
if ($registrations) {
    $lines += "WebView2 Evergreen registration: $($registrations -join ', ')"
} else {
    $lines += 'WebView2 Evergreen registration absent. Download or repair the Microsoft WebView2 Runtime.'
}
$preflight = Join-Path $folder 'webview2-prerequisite.json'
if (Test-Path -LiteralPath $preflight -PathType Leaf) {
    try {
        $prereqStatus = Get-Content -LiteralPath $preflight -Raw | ConvertFrom-Json -ErrorAction Stop
        $lines += "Installer WebView2 prerequisite: $($prereqStatus.state)"
        if ($prereqStatus.state -eq 'NEEDS_ATTENTION') {
            $lines += "Installer dependency detail: $($prereqStatus.detail)"
        }
    } catch {
        $lines += 'Installer WebView2 prerequisite status unreadable.'
    }
}
$startupLog = Join-Path $folder 'startup-failure.txt'
if (Test-Path -LiteralPath $startupLog -PathType Leaf) {
    $lines += ''
    $lines += 'Latest native startup trace (match timestamp and SHA before using):'
    $lines += Get-Content -LiteralPath $startupLog -TotalCount 12
}
$lines += ''
$lines += 'Windows Application log (Application Error, WER, Application Hang and SideBySide):'
try {
    $events = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'; Id = @(33,59,1000,1001,1002)
        StartTime = (Get-Date).AddDays(-7)
    } -MaxEvents 240 -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match '(?i)ArSonMix[-\w]*\.exe|AR[- ]?Mix.*\.exe' } |
        Select-Object -First 12)
    foreach ($event in $events) {
        $lines += "$($event.TimeCreated) | $($event.ProviderName) | event $($event.Id)"
        $lines += ($event.Message -split '\r?\n' |
            Where-Object { $_ -match '(?i)fault|exception|\.dll|side-by-side' } |
            Select-Object -First 5)
    }
    if (!$events.Count) { $lines += 'No matching event in the recent query.' }
} catch {
    $lines += "Application log could not be read: $($_.Exception.Message)"
}
if ($SafeWebViewProfile -and $exe -and $build -ge 17763 -and [Environment]::Is64BitOperatingSystem) {
    try {
        Start-Process -FilePath $exe -ArgumentList '--safe-webview-profile' -WorkingDirectory $PSScriptRoot
        $lines += 'Recovery requested with a separate WebView2 profile. Check the native trace for GUI_READY.'
    } catch {
        $lines += "Windows could not launch the executable: $($_.Exception.Message)"
    }
}
$lines | Set-Content -LiteralPath $report -Encoding UTF8
Write-Output $report
if ($ShowReport) { Start-Process -FilePath (Join-Path $env:WINDIR 'notepad.exe') -ArgumentList ('"' + $report + '"') }
