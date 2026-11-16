#!/usr/bin/env python3
"""Protect retired SonKu field workflow YAML from duplicated paste errors.

On 2026-11-16, GitHub produced zero-job failures because a copied Windows
step fragment was appended beneath the artifact upload with-map. Run this
lightweight structural gate before Windows, with no extra dependencies.
GitHub's actual workflow parser remains authoritative.
"""
from pathlib import Path
import re

for flavor, path in {
    "P89": Path(".github/workflows/sonku-p89-windows-field.yml"),
    "P90": Path(".github/workflows/sonku-p90-windows-field.yml"),
}.items():
    source = path.read_text(encoding="utf-8")
    lines = source.splitlines()
    assert len(lines) < 375, (path, "duplicate field job", len(lines))
    assert source.count("actions/upload-artifact@v4") == 1, path
    assert source.count("- name: Compile actual ARMixConsole Windows GUI") == 1, path
    assert source.count("compression-level: 0") == 1, path
    assert "workflow_dispatch:" in source
    assert f"sonku-{flavor.lower()}-field-" in source
    assert f"NativeSonKu{flavor}JuceV" in source
    assert f"ARMixSonKu{flavor}NativeTests" in source
    assert f"stateSchema = {10 if flavor == 'P89' else 11}" in source
    if flavor == "P89":
        assert "ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT=ON" not in source
        assert "P90JuceV11AppliedAckAndLegacyRestore" not in source
        assert "p90TrueExternalSpeaker = $true" not in source
    else:
        assert "ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT=ON" in source
        assert "workflow_call:" in source
    assert re.search(
        rf"ctest --test-dir build -C Release --output-on-failure -R "
        rf"'\^NativeSonKu{flavor}JuceV\d+AppliedAckAnd\w+\$'",
        source,
    ), (path, "unterminated native CTest expression")
    trailer = source.split("- uses: actions/upload-artifact@v4", 1)[1]
    assert not re.search(r"(?m)^\s+- name:", trailer), path
    assert not re.search(r"(?m)^\s+if \(\$LASTEXITCODE", trailer), path
    allowed = ("name:", "with:", "path:", "if-no-files-found:",
               "retention-days:", "compression-level:", "#")
    assert all(not line.strip() or line.lstrip().startswith(allowed)
               for line in trailer.splitlines()), (path, "invalid upload trailer")
print("SONKU_LEGACY_P89_P90_WORKFLOW_STRUCTURE_PASSED")
