# SPDX-License-Identifier: AGPL-3.0-only
# Build-side real signature regression with the pinned, publicly attested
# upstream gh release. Executes ONLY extracted trust functions, never the
# updater main, installs software, or grants Actions attestation permissions.
param([Parameter(Mandatory=$true)][string]$Archive,[Parameter(Mandatory=$true)][string]$Verifier)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'installer/ArSonMixAutoUpdate.ps1').Path,[ref]$tokens,[ref]$errors)
if($errors.Count -ne 0){throw 'Updater PowerShell parse failure'}
foreach($name in @('Get-PartialAssetPath','Get-ResumeResponseMode','Test-RegularUpdateFile','Download-Verified','Test-AttestationBundleUri','Expand-BoundedSnappy','Read-BoundedAttestationJson','Convert-AttestationResponse','Read-PublicAttestation','Invoke-IsolatedVerifier','Test-VerifiedRepositoryIdentity')){
    $matches=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$false))
    if($matches.Count -ne 1){throw "Missing unique updater trust function: $name"}
    . ([scriptblock]::Create($matches[0].Extent.Text))
}
# Use the production transfer function with an isolated test-only log sink.
$script:transferLog=[Collections.Generic.List[string]]::new()
function Log-Result([string]$message){$script:transferLog.Add($message)}
function Fail([string]$reason){throw $reason}
$work=Join-Path ([IO.Path]::GetTempPath()) ('armix-real-attestation-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work)|Out-Null
try {
    $digest=(Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if($digest -cne 'd406f442ca0baa3d3e9c695e9cded1aa48e67dad3294b8947e5ab84f980b4844'){throw 'Unexpected public signature fixture'}
    # Simulate interruption after a 1 MiB prefix of the actual official archive.
    # The production downloader must resume or safely restart if CDN ignores
    # Range. The final byte count and complete digest must still match.
    $asset=[pscustomobject]@{digest='sha256:'+$digest;size=(Get-Item -LiteralPath $Archive).Length;
        browser_download_url='https://github.com/cli/cli/releases/download/v2.106.0/gh_2.106.0_windows_amd64.zip'}
    $resumed=Join-Path $work 'resumed.zip'
    $partial=Get-PartialAssetPath $resumed $digest
    $prefix=New-Object byte[] 1048576
    $input=[IO.File]::OpenRead($Archive)
    try {
        $offset=0
        while($offset -lt $prefix.Length){
            $n=$input.Read($prefix,$offset,$prefix.Length-$offset)
            if($n -le 0){throw 'Unexpected upstream prefix EOF'}
            $offset+=$n
        }
    } finally {$input.Dispose()}
    [IO.File]::WriteAllBytes($partial,$prefix)
    Download-Verified $asset $resumed
    if((Get-FileHash -LiteralPath $resumed -Algorithm SHA256).Hash.ToLowerInvariant() -cne $digest -or
       (Test-Path -LiteralPath $partial)){throw 'Real resume did not promote exact bytes'}
    if(@($script:transferLog | Where-Object {$_ -like 'RESUMING_VERIFIED_DOWNLOAD *'}).Count -eq 1){
        Write-Host 'ARSONMIX_REAL_HTTP_RANGE_RESUME_PASS'
    } else {
        Write-Host 'ARSONMIX_REAL_HTTP_RANGE_SERVER_RESTART_PASS'
    }
    $bundle=Join-Path $work 'bundle.jsonl'
    $trusted=Join-Path $work 'trusted_root.jsonl'
    Read-PublicAttestation $digest $bundle 'cli/cli' '212613049'
    $roots=Invoke-IsolatedVerifier $Verifier 'attestation trusted-root --hostname github.com' $work
    if([string]::IsNullOrWhiteSpace($roots)){throw 'No verified Sigstore root'}
    [IO.File]::WriteAllText($trusted,$roots,[Text.UTF8Encoding]::new($false))
    $flags=' --repo cli/cli --hostname github.com --bundle "'+$bundle+'" --custom-trusted-root "'+$trusted+'" --deny-self-hosted-runners --format json'
    $json=Invoke-IsolatedVerifier $Verifier ('attestation verify "'+$Archive+'"'+$flags) $work
    if(-not (Test-VerifiedRepositoryIdentity ($json|ConvertFrom-Json) '212613049')){throw 'Real signature did not match upstream repository'}
    if(Test-VerifiedRepositoryIdentity ($json|ConvertFrom-Json) '1432648251'){throw 'Foreign upstream signature matched ArSonMix repository'}
    $tampered=Join-Path $work 'tampered.zip'
    Copy-Item -LiteralPath $Archive -Destination $tampered
    $stream=[IO.File]::Open($tampered,[IO.FileMode]::Append)
    try {$stream.WriteByte(0)} finally {$stream.Dispose()}
    $rejected=$false
    try {[void](Invoke-IsolatedVerifier $Verifier ('attestation verify "'+$tampered+'"'+$flags) $work)} catch {$rejected=$true}
    if(-not $rejected){throw 'Tampered public artifact accepted'}
    Write-Host 'ARSONMIX_ANONYMOUS_REAL_ATTESTATION_PASS: public bundle + fresh TUF + signed repository ID + tamper rejection; no user token'
    # Read-only migration observation; existing release assets predate the
    # new publisher. Never retroactively attest or publish them in CI.
    foreach($old in @(
        @{tag='v0.5.0-alpha.31';digest='ff895128e44eb47f62d674b170238dbc2d0ba0afe66e7e8a4c7bf8d8fe0d7aed'},
        @{tag='v0.5.0-alpha.32';digest='80629c19f3b11f6bcde10d5f163c1d1958b1981836f632ce2b1c1e7ea4d06297'})){
        $url=[Uri]::new("https://api.github.com/repos/masarray/arsonmix/attestations/sha256:$($old.digest)?per_page=30")
        try {
            $result=Read-BoundedAttestationJson $url 2097152
            $count=@($result.attestations).Count
            Write-Host "ARSONMIX_LEGACY_ATTESTATION_OBSERVATION $($old.tag) count=$count (presence only, not verification)"
        } catch {
            Write-Host "ARSONMIX_LEGACY_ATTESTATION_OBSERVATION $($old.tag) unavailable (not proof of absence)"
        }
    }
} finally {Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue}
