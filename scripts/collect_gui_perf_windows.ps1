# Stage A: optional Windows-side process evidence for the affected ArSonMix PC.
# Run alongside the Ctrl+Alt+Shift+F9 JavaScript/JUCE capture.
# No installation, admin access, ETW provider or continuous background service.
[CmdletBinding()]
param(
    [ValidateRange(5, 120)][int]$Seconds = 20,
    [ValidateRange(250, 2000)][int]$IntervalMs = 500,
    [string]$ExePath = "",
    [string]$OutputDirectory = ""
)
$ErrorActionPreference = "Stop"
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path (Get-Location) ("gui-perf-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$start = Get-Date
$gpu = @()
try {
    $gpu = @(Get-CimInstance Win32_VideoController | Select-Object Name, DriverVersion)
} catch {
    Write-Warning "GPU model/driver unavailable without CIM access; continue with process metrics."
}
# The JUCE target is ARMixConsole, but PRODUCT_NAME makes the installed
# process "AR MIX Studio". The portable EXE is packaged under a separate name.
# Use process names from the actual build and, when supplied, the EXE name.
$hostNames = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($name in @("AR MIX Studio", "ArSonMix-GUI10CH-P012-Windows-x64", "ARMixConsole")) {
    [void]$hostNames.Add($name)
}
$sha = $null
if ($ExePath) {
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        throw "EXE path was supplied but does not exist."
    }
    $resolvedExe = (Resolve-Path -LiteralPath $ExePath).Path
    $sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedExe).Hash
    [void]$hostNames.Add([System.IO.Path]::GetFileNameWithoutExtension($resolvedExe))
}
$environment = [ordered]@{
    schema = "arsonmix.windows-gui-process-sampling.v1"
    recordedAt = $start.ToString("o")
    seconds = $Seconds
    intervalMs = $IntervalMs
    exeSHA256 = $sha
    # No username, hostname or private folder paths are exported.
    osVersion = [System.Environment]::OSVersion.VersionString
    processors = [System.Environment]::ProcessorCount
    gpu = $gpu
    limits = "Process CPU/memory only; no GPU raster, pointer input-to-photon, or ETW scheduling evidence"
}
$environment | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $OutputDirectory "environment.json") -Encoding UTF8
$previous = @{}
$rows = [System.Collections.Generic.List[object]]::new()
$hostSampleCount = 0
$steps = [math]::Ceiling($Seconds * 1000.0 / $IntervalMs)
for ($i = 0; $i -lt $steps; $i++) {
    $now = Get-Date
    $processes = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $hostNames.Contains($_.ProcessName) -or
                       $_.ProcessName -eq "msedgewebview2" })
    foreach ($p in $processes) {
        if ($hostNames.Contains($p.ProcessName)) { $hostSampleCount++ }
        $id = [string]$p.Id
        $cpu = [double]$p.CPU
        $delta = $null
        if ($previous.ContainsKey($id)) {
            $old = $previous[$id]
            $elapsed = ($now - $old.time).TotalSeconds
            if ($elapsed -gt 0 -and $cpu -ge $old.cpu) {
                # CPU relative to ONE logical core; values above 100% possible.
                $delta = [math]::Round(100.0 * ($cpu - $old.cpu) / $elapsed, 2)
            }
        }
        $previous[$id] = @{cpu = $cpu; time = $now}
        $rows.Add([pscustomobject]@{
            timestamp = $now.ToString("o")
            elapsedMs = [math]::Round(($now - $start).TotalMilliseconds)
            processName = $p.ProcessName
            pid = $p.Id
            cpuPctOneCore = $delta
            workingSetMB = [math]::Round($p.WorkingSet64 / 1MB, 2)
            privateMB = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
            handles = $p.HandleCount
        })
    }
    Start-Sleep -Milliseconds $IntervalMs
}
$out = Join-Path $OutputDirectory "process-timeline.csv"
$rows | Export-Csv -Path $out -NoTypeInformation -Encoding UTF8
Write-Host "ArSonMix P0 process capture: $($rows.Count) samples -> $out"
if ($hostSampleCount -eq 0) {
    Write-Warning "NO_NATIVE_GUI_HOST_SAMPLES: launch AR MIX Studio / packaged EXE before profiling; WebView2-only data cannot measure JUCE host CPU."
} else {
    Write-Host "Native JUCE host samples: $hostSampleCount (AR MIX Studio or packaged EXE)."
}
Write-Host "Pair with GUI_PERF_JS + GUI_PERF_NATIVE logs and WebView2 GPU trace for Issue #56."
