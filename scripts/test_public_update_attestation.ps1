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
foreach($name in @('Test-AttestationBundleUri','Read-BoundedAttestationJson','Convert-AttestationResponse','Read-PublicAttestation','Invoke-IsolatedVerifier','Test-VerifiedRepositoryIdentity')){
    $matches=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $name},$false))
    if($matches.Count -ne 1){throw "Missing unique updater trust function: $name"}
    . ([scriptblock]::Create($matches[0].Extent.Text))
}
$work=Join-Path ([IO.Path]::GetTempPath()) ('armix-real-attestation-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($work)|Out-Null
try {
    $digest=(Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if($digest -cne 'd406f442ca0baa3d3e9c695e9cded1aa48e67dad3294b8947e5ab84f980b4844'){throw 'Unexpected public signature fixture'}
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
} finally {Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue}
