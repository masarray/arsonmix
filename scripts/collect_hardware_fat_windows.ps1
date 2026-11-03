# SPDX-License-Identifier: AGPL-3.0-only
# Offline Windows field evidence recorder. NEVER controls audio devices, DSP, ARM or plugins.
param(
  [ValidateSet('Verify','Start','Stop','Summary')][string]$Action='Verify',
  [ValidateSet('OUTPUT_ONLY_TONE','CAPTURE_ONLY','DRY_THROUGH_0FX')][string]$Mode='DRY_THROUGH_0FX',
  [string]$Backend='',
  [int]$SampleRate=0,
  [int]$BufferFrames=0,
  [string]$InputPins='',
  [string]$OutputPins='',
  [string]$CaptureEndpoint='',
  [string]$RenderEndpoint='',
  [switch]$Wired,
  [ValidateSet('CLEAR','CRACKLE','INCONCLUSIVE')][string]$Result='INCONCLUSIVE',
  [string]$ReportPath='',
  [string]$Witness='',
  [string]$Notes=''
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$manifestPath=Join-Path $root 'BUILD_PROVENANCE.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Missing BUILD_PROVENANCE.json: extract the complete verified Windows FAT ZIP first' }
$m=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([string]$m.fatStatus -ne 'EXPERIMENTAL_NOT_HARDWARE_QUALIFIED') { throw 'Unexpected hardware qualification claim' }
if ([string]$m.sourceCommit -notmatch '^[a-f0-9]{40}$') { throw 'Invalid exact source SHA' }
if ([string]$m.fatCollectorSha256 -notmatch '^[a-f0-9]{64}$') { throw 'Missing FAT collector provenance' }
$selfHash=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($selfHash -ne [string]$m.fatCollectorSha256) { throw 'FAT collector was modified after packaging' }
$app=Join-Path $root ([string]$m.executable)
if (-not (Test-Path -LiteralPath $app -PathType Leaf)) { throw 'Missing packaged native GUI EXE' }
$exeHash=(Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash.ToLowerInvariant()
if ($exeHash -ne [string]$m.exeSha256) { throw 'GUI EXE SHA-256 mismatch: do not test or label this artifact' }
$journalPath=Join-Path $root 'FAT_RUNS.json'
function Save-Journal($j) {
  $j | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $journalPath -Encoding UTF8
}
function Read-Journal {
  if (Test-Path -LiteralPath $journalPath -PathType Leaf) {
    $j=Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($j.schema -ne 'arsonmix.hardware-fat-field-journal.v1' -or
        $j.sourceSha -ne $m.sourceCommit -or $j.exeSha256 -ne $exeHash) {
      throw 'FAT_RUNS.json belongs to a different or unsupported build'
    }
    return $j
  }
  return [pscustomobject]@{
    schema='arsonmix.hardware-fat-field-journal.v1'
    sourceSha=[string]$m.sourceCommit
    exeSha256=$exeHash
    active=$null
    sessions=@()
  }
}
function Parse-Pins([string]$raw) {
  if ([string]::IsNullOrWhiteSpace($raw)) { return @() }
  $parsed=@()
  foreach ($part in $raw.Split(',')) {
    $pin=0
    if (-not [int]::TryParse($part.Trim(),[ref]$pin) -or $pin -lt 0 -or $pin -gt 255) {
      throw 'Pin values must be physical callback ordinals 0..255 separated by commas, confirmed from native readback'
    }
    if ($parsed -contains $pin) { throw 'Duplicate physical pin in evidence' }
    $parsed+= $pin
  }
  return $parsed
}
if ($Action -eq 'Verify') {
  Write-Host ('FAT PACKAGE OK; build='+$m.sourceCommit+'; EXE SHA256='+$exeHash)
  Write-Host 'UNQUALIFIED 10CH/4AUX candidate. No sound, device, latency or hardware result has been tested.'
  exit 0
}
$j=Read-Journal
if ($Action -eq 'Start') {
  if ($null -ne $j.active) { throw 'A FAT mode is already active; Stop it before starting another' }
  if (-not $Backend.Trim() -or $SampleRate -lt 8000 -or $SampleRate -gt 384000 -or
      $BufferFrames -lt 16 -or $BufferFrames -gt 16384) {
    throw 'Specify actual native readback -Backend, -SampleRate, -BufferFrames (not draft device values)'
  }
  $inputs=@(Parse-Pins $InputPins)
  $outputs=@(Parse-Pins $OutputPins)
  if ($Mode -eq 'OUTPUT_ONLY_TONE' -and ($inputs.Count -ne 0 -or $outputs.Count -lt 1)) {
    throw 'OUTPUT_ONLY_TONE must have NO capture pins and a real render pin'
  }
  if ($Mode -eq 'CAPTURE_ONLY' -and ($outputs.Count -ne 0 -or $inputs.Count -lt 1)) {
    throw 'CAPTURE_ONLY must have NO render pins and a real capture pin'
  }
  if ($Mode -eq 'DRY_THROUGH_0FX' -and ($inputs.Count -lt 1 -or $outputs.Count -lt 1)) {
    throw 'DRY_THROUGH_0FX needs verified capture and render pins'
  }
  # The operator must select the mode in the running native app FIRST.
  # Starting this timer does not change any endpoint or output ARM.
  $j.active=[pscustomobject]@{
    mode=$Mode
    startedUtc=[DateTime]::UtcNow.ToString('o')
    backend=$Backend
    sampleRate=$SampleRate
    bufferFrames=$BufferFrames
    capturePins=$inputs
    renderPins=$outputs
    captureEndpoint=$CaptureEndpoint
    renderEndpoint=$RenderEndpoint
    wired=[bool]$Wired
  }
  Save-Journal $j
  Write-Host ('FAT TIMER STARTED '+$Mode+' at '+$j.active.startedUtc)
  Write-Host 'Listen at LOW wired volume; use SIGNAL CHECK > MARK CRACKLE, COPY FULL REPORT. No audio or ARM controls were changed.'
  exit 0
}
if ($Action -eq 'Stop') {
  if ($null -eq $j.active) { throw 'No active FAT session; use -Action Start after selecting actual native mode' }
  $end=[DateTime]::UtcNow
  $elapsed=[math]::Max(0,[math]::Floor(($end-([DateTime]::Parse($j.active.startedUtc).ToUniversalTime())).TotalSeconds))
  $reportFileName=$null
  $reportHash=$null
  if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    if (-not (Test-Path -LiteralPath $ReportPath -PathType Leaf)) { throw 'COPY FULL REPORT file not found' }
    $reportFileName=('FAT_REPORT_'+$j.active.mode+'_'+$end.ToString('yyyyMMddTHHmmssZ')+'.txt')
    $destination=Join-Path $root $reportFileName
    Copy-Item -LiteralPath $ReportPath -Destination $destination -ErrorAction Stop
    $reportHash=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $session=[pscustomobject]@{
    mode=$j.active.mode
    startedUtc=$j.active.startedUtc
    endedUtc=$end.ToString('o')
    elapsedSeconds=[int]$elapsed
    backend=$j.active.backend
    sampleRate=$j.active.sampleRate
    bufferFrames=$j.active.bufferFrames
    capturePins=$j.active.capturePins
    renderPins=$j.active.renderPins
    captureEndpoint=$j.active.captureEndpoint
    renderEndpoint=$j.active.renderEndpoint
    wired=$j.active.wired
    operatorObservation=$Result
    witness=$Witness
    reportFile=$reportFileName
    reportSha256=$reportHash
    notes=$Notes
  }
  $j.sessions=@($j.sessions)+@($session)
  $j.active=$null
  Save-Journal $j
  Write-Host ('FAT '+$session.mode+' ended: '+$elapsed+' seconds; observation='+$Result)
  if ($null -eq $reportHash) { Write-Warning 'No full report supplied. Evidence will be incomplete.' }
  if ($session.mode -eq 'DRY_THROUGH_0FX' -and $elapsed -lt 1800) { Write-Warning 'Under 30 minutes; this does NOT satisfy the 0FX soak gate.' }
  exit 0
}
if ($Action -eq 'Summary') {
  $missing=@()
  $selected=@()
  foreach ($required in @('OUTPUT_ONLY_TONE','CAPTURE_ONLY','DRY_THROUGH_0FX')) {
    $s=@($j.sessions | Where-Object { $_.mode -eq $required } | Select-Object -Last 1)
    if ($s.Count -eq 0) { $missing+= $required; continue }
    $selected+= $s[0]
    if ($null -eq $s[0].reportSha256 -or $s[0].operatorObservation -eq 'INCONCLUSIVE' -or -not $s[0].witness) {
      $missing+=($required+': report/observation/witness missing')
    }
  }
  $dry=@($selected | Where-Object { $_.mode -eq 'DRY_THROUGH_0FX' })
  if ($dry.Count -eq 1 -and ($dry[0].elapsedSeconds -lt 1800 -or -not $dry[0].wired)) {
    $missing+='DRY_THROUGH_0FX: 1800 seconds on wired route required'
  }
  if ($null -ne $j.active) { $missing+='An active session must be stopped first' }
  $state=if ($missing.Count -eq 0) { 'EVIDENCE_READY_FOR_HUMAN_REVIEW' } else { 'EVIDENCE_INCOMPLETE' }
  $resultJson=[ordered]@{
    schema='arsonmix.hardware-fat-summary.v1'
    sourceSha=$m.sourceCommit
    exeSha256=$exeHash
    recordedAtUtc=[DateTime]::UtcNow.ToString('o')
    status=$state
    hardwareCertified=$false
    physicalAudibilityVerifiedBySoftware=$false
    unresolvedIssue=15
    sessions=$selected
    blockers=$missing
    caution='Observations are human-entered. Validate driver clocks, RAW/MAIN PCM, glitch windows and speaker audio manually. Capture flags are NOT render xruns.'
  }
  $outPath=Join-Path $root 'FAT_SUMMARY.json'
  $resultJson | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outPath -Encoding UTF8
  Write-Host ($state+'; wrote '+$outPath+'; certified=false')
  Write-Warning 'Inspect FAT_RUNS.json and diagnostic files for sensitive device info before sharing. Issue #15 remains open.'
  exit 0
}