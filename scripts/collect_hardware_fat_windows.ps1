# SPDX-License-Identifier: AGPL-3.0-only
# Offline Windows field evidence recorder. NEVER controls audio devices, DSP, ARM or plugins.
param(
  [ValidateSet('Verify','Start','Stop','Summary')][string]$Action='Verify',
  [ValidateSet('OUTPUT_ONLY_TONE','CAPTURE_ONLY','DRY_THROUGH_0FX','LIVE_THROUGH_1FX')][string]$Mode='DRY_THROUGH_0FX',
  [string]$Backend='',
  [int]$SampleRate=0,
  [int]$BufferFrames=0,
  [string]$InputPins='',
  [string]$OutputPins='',
  [string]$CaptureEndpoint='',
  [string]$RenderEndpoint='',
  # MANUALLY read from the native VST3 rack; not a synthetic software probe.
  # A 1FX run must identify exactly one active, non-bypassed vendor insert.
  [ValidateRange(0,8)][int]$FxCount=0,
  [string]$Vst3Name='',
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
  if (-not $Backend.Trim() -or $SampleRate -notin @(44100,48000) -or
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
  if ($Mode -in @('DRY_THROUGH_0FX','LIVE_THROUGH_1FX') -and
      ($inputs.Count -lt 1 -or $outputs.Count -lt 1)) {
    throw 'Live-through FAT requires actual capture and render pins'
  }
  if (($inputs.Count -gt 0 -and [string]::IsNullOrWhiteSpace($CaptureEndpoint)) -or
      ($outputs.Count -gt 0 -and [string]::IsNullOrWhiteSpace($RenderEndpoint))) {
    throw 'Real physical capture/render endpoint identity required before timer starts'
  }
  # Prevent confusing a 0FX soak with a real active vendor effect. The
  # operator enters verified-in-GUI details; collector cannot observe DSP.
  if ($Mode -in @('DRY_THROUGH_0FX','LIVE_THROUGH_1FX') -and -not $Wired) {
    throw 'Both matched 0FX/1FX tests require actual -Wired hardware routing'
  }
  if ($Mode -eq 'LIVE_THROUGH_1FX') {
    if ($FxCount -ne 1 -or [string]::IsNullOrWhiteSpace($Vst3Name) -or
        $Vst3Name.Length -gt 120) {
      throw '1FX FAT requires -FxCount 1 and -Vst3Name copied from the active native VST3 rack'
    }
    # A user should not spend another 30 minutes on an incomparable setup.
    # Compare against the most recent completed, witnessed 0FX baseline
    # BEFORE the 1FX timer starts; never accept a guess about the device.
    $baseline=@($j.sessions | Where-Object { $_.mode -eq 'DRY_THROUGH_0FX' -and [int]$_.sampleRate -eq $SampleRate } | Select-Object -Last 1)
    if ($baseline.Count -ne 1 -or $baseline[0].elapsedSeconds -lt 1800 -or
        -not $baseline[0].wired -or -not $baseline[0].reportSha256 -or
        -not $baseline[0].witness -or
        $baseline[0].operatorObservation -eq 'INCONCLUSIVE') {
      throw 'Record a completed 30-minute wired 0FX baseline with witness and full report before 1FX'
    }
    $base=$baseline[0]
    if ($base.backend -ne $Backend -or
        $base.sampleRate -ne $SampleRate -or
        $base.bufferFrames -ne $BufferFrames -or
        ($base.capturePins -join ',') -ne ($inputs -join ',') -or
        ($base.renderPins -join ',') -ne ($outputs -join ',') -or
        $base.captureEndpoint -ne $CaptureEndpoint -or
        $base.renderEndpoint -ne $RenderEndpoint) {
      throw '1FX must use the identical ASIO/WASAPI device, clock, buffer and physical ports as the 0FX baseline'
    }
  } elseif ($FxCount -ne 0 -or -not [string]::IsNullOrWhiteSpace($Vst3Name)) {
    throw 'Only LIVE_THROUGH_1FX accepts an active VST3 rack; other FAT modes require zero FX'
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
    operatorReportedFxCount=$FxCount
    operatorReportedVst3Name=$Vst3Name.Trim()
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
    operatorReportedFxCount=$j.active.operatorReportedFxCount
    operatorReportedVst3Name=$j.active.operatorReportedVst3Name
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
  if ($session.mode -in @('DRY_THROUGH_0FX','LIVE_THROUGH_1FX') -and $elapsed -lt 1800) {
    Write-Warning 'Under 30 minutes; this does NOT satisfy the matched 0FX/1FX soak comparison.'
  }
  exit 0
}
if ($Action -eq 'Summary') {
  $missing=@()
  $selected=@()
  foreach ($rate in @(44100,48000)) {
    $byMode=@{}
    foreach ($required in @('OUTPUT_ONLY_TONE','CAPTURE_ONLY','DRY_THROUGH_0FX','LIVE_THROUGH_1FX')) {
      $matches=@($j.sessions | Where-Object {
        [int]$_.sampleRate -eq $rate -and $_.mode -eq $required
      } | Sort-Object endedUtc)
      $tag=([string]$rate+' '+$required)
      if ($matches.Count -lt 1) { $missing+=($tag+': not recorded'); continue }
      $s=$matches[-1]
      $byMode[$required]=$s
      $selected+= $s
      # Independent 30-minute evidence cannot trust a typed elapsedSeconds.
      # Use UTC-aware instants: a forged 1800s counter with 1s timestamps is
      # INCOMPLETE even if all other user-entered fields claim CLEAR.
      try {
        $startUtc=[DateTimeOffset]::Parse([string]$s.startedUtc,[Globalization.CultureInfo]::InvariantCulture)
        $endUtc=[DateTimeOffset]::Parse([string]$s.endedUtc,[Globalization.CultureInfo]::InvariantCulture)
        $wallSeconds=($endUtc-$startUtc).TotalSeconds
        $claimedSeconds=[double]$s.elapsedSeconds
        if ($wallSeconds -lt 0 -or $claimedSeconds -lt 0 -or
            [double]::IsNaN($claimedSeconds) -or [double]::IsInfinity($claimedSeconds) -or
            [Math]::Abs($wallSeconds-$claimedSeconds) -gt 5) {
          $missing+=($tag+': recorded duration inconsistent with UTC start/end')
        }
      } catch {
        $missing+=($tag+': invalid UTC start/end or elapsed duration')
      }
      if ($s.operatorObservation -ne 'CLEAR' -or
          [string]::IsNullOrWhiteSpace([string]$s.witness)) {
        $missing+=($tag+': verified CLEAR observation and human witness required')
      }
      if ([string]::IsNullOrWhiteSpace([string]$s.reportFile) -or
          [string]$s.reportSha256 -notmatch '^[0-9a-f]{64}$') {
        $missing+=($tag+': native COPY FULL REPORT and digest required')
      } else {
        $reportName=[string]$s.reportFile
        if ([IO.Path]::GetFileName($reportName) -ne $reportName) {
          $missing+=($tag+': unsafe report filename')
        } else {
          $reportInKit=Join-Path $root $reportName
          if (-not (Test-Path -LiteralPath $reportInKit -PathType Leaf)) {
            $missing+=($tag+': native report file missing')
          } elseif ((Get-FileHash -LiteralPath $reportInKit -Algorithm SHA256).Hash.ToLowerInvariant() -ne
                    [string]$s.reportSha256) {
            $missing+=($tag+': native report SHA256 mismatch')
          }
        }
      }
      if ([string]::IsNullOrWhiteSpace([string]$s.backend) -or
          [int]$s.bufferFrames -lt 16 -or [int]$s.bufferFrames -gt 16384) {
        $missing+=($tag+': actual backend and buffer required')
      }
      if ($required -ne 'OUTPUT_ONLY_TONE' -and
          [string]::IsNullOrWhiteSpace([string]$s.captureEndpoint)) {
        $missing+=($tag+': capture endpoint identity missing')
      }
      if ($required -ne 'CAPTURE_ONLY' -and
          [string]::IsNullOrWhiteSpace([string]$s.renderEndpoint)) {
        $missing+=($tag+': render endpoint identity missing')
      }
      if ($required -in @('DRY_THROUGH_0FX','LIVE_THROUGH_1FX')) {
        if ($s.elapsedSeconds -lt 1800 -or -not $s.wired) {
          $missing+=($tag+': 1800-second real wired route required')
        }
      }
      if ($required -eq 'LIVE_THROUGH_1FX') {
        if ($s.operatorReportedFxCount -ne 1 -or
            [string]::IsNullOrWhiteSpace([string]$s.operatorReportedVst3Name)) {
          $missing+=($tag+': one real native VST3 insert identity required')
        }
      } elseif ($s.operatorReportedFxCount -ne 0 -or
                -not [string]::IsNullOrWhiteSpace([string]$s.operatorReportedVst3Name)) {
        $missing+=($tag+': 0FX/source isolation must have zero active inserts')
      }
    }
    if ($byMode.ContainsKey('DRY_THROUGH_0FX') -and
        $byMode.ContainsKey('LIVE_THROUGH_1FX')) {
      $zero=$byMode['DRY_THROUGH_0FX']
      $one=$byMode['LIVE_THROUGH_1FX']
      if ($zero.backend -ne $one.backend -or
          $zero.sampleRate -ne $one.sampleRate -or
          $zero.bufferFrames -ne $one.bufferFrames -or
          ($zero.capturePins -join ',') -ne ($one.capturePins -join ',') -or
          ($zero.renderPins -join ',') -ne ($one.renderPins -join ',') -or
          $zero.captureEndpoint -ne $one.captureEndpoint -or
          $zero.renderEndpoint -ne $one.renderEndpoint) {
        $missing+=([string]$rate+': 0FX/1FX native device/clock/buffer/pins mismatch')
      }
      if ([DateTime]::Parse($one.startedUtc).ToUniversalTime() -lt
          [DateTime]::Parse($zero.endedUtc).ToUniversalTime()) {
        $missing+=([string]$rate+': 1FX started before its 0FX baseline ended')
      }
    }
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
    fxComparisonSource='OPERATOR_REPORTED_ONLY'
    unresolvedIssue=15
    sessions=$selected
    blockers=$missing
    caution='Manual observations are NOT actual callback/DAC proof. Review reports, real 0FX/1FX audio and clock/pins. Capture discontinuity flags are NOT render xruns.'
  }
  $outPath=Join-Path $root 'FAT_SUMMARY.json'
  $resultJson | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $outPath -Encoding UTF8
  Write-Host ($state+'; wrote '+$outPath+'; certified=false')
  Write-Warning 'Inspect report/log files for sensitive device info before sharing. Issue #15 remains open.'
  exit 0
}
