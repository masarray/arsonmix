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
$sha = $null
if ($ExePath) {
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        throw "EXE path was supplied but does not exist."
    }
    $sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $ExePath).Hash
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
$steps = [math]::Ceiling($Seconds * 1000.0 / $IntervalMs)
for ($i = 0; $i -lt $steps; $i++) {
    $now = Get-Date
    $processes = @(Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -match "^(ARMixConsole|msedgewebview2)$" })
    foreach ($p in $processes) {
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
Write-Host "Pair with GUI_PERF_JS + GUI_PERF_NATIVE logs and WebView2 GPU trace for Issue #56."
