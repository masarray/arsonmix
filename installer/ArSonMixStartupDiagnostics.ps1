# SPDX-License-Identifier: AGPL-3.0-only
[CmdletBinding()]
param([switch]$SafeWebViewProfile, [switch]$ShowReport, [switch]$PolicySelfTest)

$ErrorActionPreference = 'Stop'

# Pure failure classification: this is NOT a dependency installer, a crash fix
# or a declaration that a third-party VST is defective. It labels clues only.
function Classify-StartupEvidence([string]$evidence, [string]$osSupport,
    [bool]$exePresent, [bool]$webViewHealthy, [string]$installerState) {
    if ($osSupport -eq 'UNSUPPORTED') {
        return [pscustomobject]@{
            Code='UNSUPPORTED_WINDOWS'
            Action='Use Windows 10 build 17763 or newer (x64), or Windows 11 x64. Windows 7/x86 are unsupported.'
        }
    }
    if (-not $exePresent) {
        return [pscustomobject]@{
            Code='HOST_EXE_MISSING'
            Action='Use Start Menu > Repair ArSonMix or reinstall the verified x64 setup. Keep Studio/.smix data.'
        }
    }
    # An error mentioning VCRUNTIME is not proof that the statically linked
    # ArSonMix EXE needs VC++ redistributables. Record the exact fault module.
    if ($evidence -match '(?i)(?:vcruntime\d*[a-z0-9_-]*|msvcp\d+[a-z0-9_-]*|ucrtbased)\.dll|Microsoft\.VC\d+\.CRT') {
        return [pscustomobject]@{
            Code='VC_RUNTIME_RELATED_ERROR'
            Action='Inspect the exact Windows faulting module and VST3 dependencies. The verified ArSonMix host uses static CRT; do not install a global VC++ package blindly.'
        }
    }
    if ($evidence -match '(?i)\.vst3(?:[\\/\s]|$)|waveshell(?:[^\r\n]*?)\.(?:dll|vst3)') {
        return [pscustomobject]@{
            Code='THIRD_PARTY_PLUGIN_CRASH_SUSPECTED'
            Action='Record vendor/plugin name, faulting module and exception code. Reopen ArSonMix with saved FX safe recovery; do not delete .smix or VST licences.'
        }
    }
    if ($evidence -match '(?i)0xc0000135|0xc000007b|the specified module could not be found|side-by-side configuration is incorrect') {
        return [pscustomobject]@{
            Code='EARLY_DLL_LOADER_FAILURE_SUSPECTED'
            Action='Record missing DLL name, architecture and exception code. Use verified Repair ArSonMix; a third-party VST3 may require its own vendor runtime.'
        }
    }
    if ($evidence -match '(?i)WEBVIEW_STARTUP_FAILED|WEBVIEW_GUI_READY_TIMEOUT|WEBVIEW_AWAITING_GUI_READY') {
        return [pscustomobject]@{
            Code='WEBVIEW2_OR_PROFILE_STARTUP_FAILURE'
            Action='Check WebView2 Evergreen status and Windows profile folder permissions. Try Start ArSonMix with Recovery Profile; keep .smix.'
        }
    }
    if (-not $webViewHealthy -or $installerState -eq 'NEEDS_ATTENTION') {
        return [pscustomobject]@{
            Code='WEBVIEW2_RUNTIME_NOT_VERIFIED'
            Action='Use the official Microsoft WebView2 Evergreen Runtime installer/repair and retry. If offline or blocked by group policy, show this local report to support.'
        }
    }
    if ($evidence -match '(?i)Faulting application name:|Exception code:|application hang|Application Error') {
        return [pscustomobject]@{
            Code='WINDOWS_APP_FAILURE_NEEDS_FAULT_MODULE'
            Action='Capture latest Reliability Monitor (perfmon /rel) faulting module and exception code; a general crash is not proof of a specific root cause.'
        }
    }
    if ($osSupport -eq 'UNKNOWN') {
        return [pscustomobject]@{
            Code='WINDOWS_VERSION_UNREADABLE'
            Action='Windows build could not be inspected. Ask support to verify operating system compatibility.'
        }
    }
    return [pscustomobject]@{
        Code='NO_FATAL_STARTUP_CAUSE_IDENTIFIED'
        Action='No definitive cause in this local report. Retry normal launch, record exact step and error, and attach this report without private preset/licence data.'
    }
}
function Assert-DiagnosticCategory([string]$inputEvidence, [string]$os,
    [bool]$exePresent, [bool]$webview, [string]$installer, [string]$expected) {
    $actual=(Classify-StartupEvidence $inputEvidence $os $exePresent $webview $installer).Code
    if ($actual -cne $expected) {
        throw "STARTUP_DIAGNOSTIC_TEST_FAIL expected=$expected actual=$actual"
    }
}
if ($PolicySelfTest) {
    # Deterministic, hermetic. No Windows registry, event log, app, device,
    # network, VST, installer or user documents are accessed here.
    Assert-DiagnosticCategory '' 'UNSUPPORTED' $true $true '' 'UNSUPPORTED_WINDOWS'
    Assert-DiagnosticCategory '' 'SUPPORTED' $false $true '' 'HOST_EXE_MISSING'
    Assert-DiagnosticCategory 'Faulting module name: VCRUNTIME140_1.dll' 'SUPPORTED' $true $true '' 'VC_RUNTIME_RELATED_ERROR'
    Assert-DiagnosticCategory 'Dependent assembly Microsoft.VC140.CRT missing' 'SUPPORTED' $true $true '' 'VC_RUNTIME_RELATED_ERROR'
    Assert-DiagnosticCategory 'Faulting module: C:\VST3\Vendor.vst3\Contents\x86_64-win\Vendor.dll' 'SUPPORTED' $true $true '' 'THIRD_PARTY_PLUGIN_CRASH_SUSPECTED'
    Assert-DiagnosticCategory 'The application was unable to start: 0xc0000135' 'SUPPORTED' $true $true '' 'EARLY_DLL_LOADER_FAILURE_SUSPECTED'
    Assert-DiagnosticCategory 'WEBVIEW_GUI_READY_TIMEOUT' 'SUPPORTED' $true $true '' 'WEBVIEW2_OR_PROFILE_STARTUP_FAILURE'
    Assert-DiagnosticCategory '' 'SUPPORTED' $true $false '' 'WEBVIEW2_RUNTIME_NOT_VERIFIED'
    Assert-DiagnosticCategory '' 'SUPPORTED' $true $true 'NEEDS_ATTENTION' 'WEBVIEW2_RUNTIME_NOT_VERIFIED'
    Assert-DiagnosticCategory 'Faulting application name: ArSonMix.exe, Exception code: 0xc0000005' 'SUPPORTED' $true $true '' 'WINDOWS_APP_FAILURE_NEEDS_FAULT_MODULE'
    Assert-DiagnosticCategory '' 'SUPPORTED' $true $true '' 'NO_FATAL_STARTUP_CAUSE_IDENTIFIED'
    Write-Output 'ARSONMIX_STARTUP_DIAGNOSTIC_POLICY_PASS'
    exit 0
}

$folder = Join-Path $env:LOCALAPPDATA 'ArSonMix\diagnostics'
[IO.Directory]::CreateDirectory($folder) | Out-Null
$report = Join-Path $folder 'windows-startup-report.txt'
$os = $null
$build = 0
$osSupport = 'UNKNOWN'
try {
    $os = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
    $build = [int]$os.CurrentBuildNumber
    $osSupport = if ([Environment]::Is64BitOperatingSystem -and $build -ge 17763) { 'SUPPORTED' } else { 'UNSUPPORTED' }
} catch {
    # If registry access itself is restricted, still generate the local
    # diagnostic report rather than crashing our only troubleshooting path.
    $osSupport = 'UNKNOWN'
}
$exe = @('ArSonMix.exe','ArSonMix-GUI10CH-P012-Windows-x64.exe') |
    ForEach-Object { Join-Path $PSScriptRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
$lines = @(
    "Observed: $([DateTimeOffset]::Now.ToString('o'))"
    "Windows build: $(if($os){$build.ToString()+'.'+[string]$os.UBR}else{'UNAVAILABLE'})"
    "64-bit OS: $([Environment]::Is64BitOperatingSystem)"
    "Minimum Windows x64 build: 17763"
    "OS support: $osSupport"
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
$webViewHealthy = $false
if ($registrations) {
    $lines += "WebView2 Evergreen registration: $($registrations -join ', ')"
    $roots = @(
        (Join-Path $env:LOCALAPPDATA 'Microsoft\EdgeWebView\Application'),
        (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft\EdgeWebView\Application'),
        (Join-Path $env:ProgramFiles 'Microsoft\EdgeWebView\Application')
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    foreach ($pv in $registrations) {
        try {
            if ([version]$pv -lt [version]'109.0.0.0') { continue }
            foreach ($root in $roots) {
                if (Test-Path -LiteralPath (Join-Path (Join-Path $root $pv) 'msedgewebview2.exe') -PathType Leaf) {
                    $webViewHealthy = $true
                    break
                }
            }
            if ($webViewHealthy) { break }
        } catch { } # Invalid pv / restricted filesystem is not proof of runtime health.
    }
    if (-not $webViewHealthy) {
        $lines += 'WebView2 registration exists but the runtime executable was not verified. It may be stale or inaccessible.'
    }
} else {
    $lines += 'WebView2 Evergreen registration absent. Download or repair the Microsoft WebView2 Runtime.'
}
$lines += "WebView2 physical runtime verified: $webViewHealthy"
$installerState = ''
$preflight = Join-Path $folder 'webview2-prerequisite.json'
if (Test-Path -LiteralPath $preflight -PathType Leaf) {
    try {
        $prereqStatus = Get-Content -LiteralPath $preflight -Raw | ConvertFrom-Json -ErrorAction Stop
        $installerState = [string]$prereqStatus.state
        $lines += "Installer WebView2 prerequisite: $installerState"
        if ($prereqStatus.state -eq 'NEEDS_ATTENTION') {
            $lines += "Installer dependency detail: $($prereqStatus.detail)"
        }
    } catch {
        $lines += 'Installer WebView2 prerequisite status unreadable.'
    }
}
$startupEvidence = ''
$windowsEventEvidence = ''
$startupLog = Join-Path $folder 'startup-failure.txt'
if (Test-Path -LiteralPath $startupLog -PathType Leaf) {
    $lines += ''
    $lines += 'Latest native startup trace (match timestamp and SHA before using):'
    $recentNativeTrace = @(Get-Content -LiteralPath $startupLog -TotalCount 12)
    $lines += $recentNativeTrace
    $startupEvidence = ($recentNativeTrace -join [Environment]::NewLine)
}
$lines += ''
$lines += 'Windows Application log (Application Error, WER, Application Hang and SideBySide):'
try {
    $events = @(Get-WinEvent -FilterHashtable @{
        LogName = 'Application'; Id = @(33,59,1000,1001,1002)
        StartTime = (Get-Date).AddDays(-7)
    } -MaxEvents 1200 -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match '(?i)ArSonMix[-\w]*\.exe|AR[- ]?Mix.*\.exe' } |
        Select-Object -First 12)
    foreach ($event in $events) {
        $windowsEventEvidence += [Environment]::NewLine + [string]$event.Message
        $lines += "$($event.TimeCreated) | $($event.ProviderName) | event $($event.Id)"
        # Keep actionable faulting module/exception lines, but never place
        # the Windows profile's account name in a shareable field report.
        $safeMessage = [regex]::Replace([string]$event.Message,
            '(?i)([a-z]:\\Users\\)[^\\\r\n]+', '${1}<USER>')
        $lines += ($safeMessage -split '\r?\n' |
            Where-Object { $_ -match '(?i)fault|exception|\.dll|\.vst3|side-by-side' } |
            Select-Object -First 5)
    }
    if (!$events.Count) { $lines += 'No matching event in the recent query.' }
} catch {
    $lines += "Application log could not be read: $($_.Exception.Message)"
}
$category = Classify-StartupEvidence ($windowsEventEvidence + [Environment]::NewLine + $startupEvidence) $osSupport ([bool]$exe) $webViewHealthy $installerState
$lines += ''
$lines += "STARTUP_DIAGNOSIS: $($category.Code)"
$lines += "RECOMMENDED_ACTION: $($category.Action)"
$lines += 'This is a local clue, not a proven root cause. No diagnostics are uploaded.'
$lines += 'Do not send licences, plugin serials, saved .smix content or personal Windows paths.'
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
