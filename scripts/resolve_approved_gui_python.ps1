# SPDX-License-Identifier: AGPL-3.0-only
# Exact byte-for-byte /preview/ verifier must run under the interpreter
# selected by CMake for its generated favicon/PNG resources. A different
# Python/zlib from PATH may emit different PNG/ICO compressed bytes.
function Resolve-ArSonMixApprovedGuiPython {
    param([Parameter(Mandatory=$true)][string] $BuildDir)
    $note = Join-Path $BuildDir "approved-gui-python.txt"
    if (-not (Test-Path -LiteralPath $note -PathType Leaf)) {
        throw "CMake Python provenance missing: $note"
    }
    $selected = (Get-Content -LiteralPath $note -Raw -Encoding UTF8).Trim()
    if ([string]::IsNullOrWhiteSpace($selected) -or
        -not (Test-Path -LiteralPath $selected -PathType Leaf)) {
        throw "CMake-selected Python unavailable; refusing unverified GUI regeneration"
    }
    return (Resolve-Path -LiteralPath $selected).Path
}
