#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Fail-closed, real-code A/B: owner P102.3 frozen EARLY vs optimized EARLY.

Reads the original class from the immutable approved Git commit (NOT the
manifest's mutable contents). Builds historical and current *actual* source
in one binary, asserts PCM float bit identity over seven presets, both rates,
realtime geometry and ROOM edits, warm-up, all stereo input modes, ring wrap.
Reports matched isolated p50/p99. Does not change or certify product audio.
"""
from __future__ import annotations

import hashlib
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile

from immersib_p102_geometry_cache_probe import MACROS, ROOT, TEST

APPROVED_SOURCE_SHA = "3095af08b817d59a3e14874dab404bf6e243f38a"
PATH = "engine/builtin_fx/sonku/SonKuEarlyReflections.h"
APPROVED_BLOB = "5a6a9b48cfdc60729f130c61110c8d8590b20f35"
CANDIDATE_BLOB = "94b90db3d699d9f26601448a38b2f489f51b5fb7"
CLASS_MARKER = "class SonKuEarlyReflections final {"
TAIL_MARKER = "} // namespace armix::sonku"


def blob_hash(data: bytes) -> str:
    data = data.replace(b"\r\n", b"\n")
    return hashlib.sha1(
        f"blob {len(data)}\0".encode("ascii") + data
    ).hexdigest()


def historical_reference() -> str:
    output = subprocess.run(
        ["git", "show", f"{APPROVED_SOURCE_SHA}:{PATH}"],
        cwd=ROOT, capture_output=True, check=True, timeout=30,
    ).stdout
    if blob_hash(output) != APPROVED_BLOB:
        raise ValueError("IMMERSIB_P102_HISTORICAL_SOURCE_BLOB_CHANGED")
    source = output.decode("utf-8").replace("\r\n", "\n")
    if source.count(CLASS_MARKER) != 1 or source.count(TAIL_MARKER) != 1:
        raise ValueError("IMMERSIB_P102_HISTORICAL_CLASS_LAYOUT_CHANGED")
    body = source[source.index(CLASS_MARKER):source.rindex(TAIL_MARKER)]
    body = body.replace(
        CLASS_MARKER, "class SonKuEarlyReflectionsApprovedPCM final {", 1
    )
    return (
        "// GENERATED FROM EXACT OWNER APPROVED GIT SHA, TEST ONLY\n"
        "#pragma once\n"
        '#include "engine/builtin_fx/sonku/SonKuEarlyReflections.h"\n'
        "namespace armix::sonku {\n" + body + "\n} // namespace armix::sonku\n"
    )


def main() -> int:
    msvc = len(sys.argv)==2 and sys.argv[1]=="--msvc"
    if len(sys.argv)>1 and not msvc:
        raise ValueError("IMMERSIB_P102_INVALID_COMPILER_OPTION")
    current = (ROOT / PATH).read_bytes()
    actual = blob_hash(current)
    if actual != CANDIDATE_BLOB:
        raise ValueError(
            f"IMMERSIB_P102_CANDIDATE_SOURCE_DRIFT expected={CANDIDATE_BLOB} "
            f"actual={actual}"
        )
    baseline = historical_reference()
    with tempfile.TemporaryDirectory(prefix="imersib-p102-actual-source-ab-") as d:
        target = Path(d)
        (target / "SonKuEarlyReflectionsApprovedPCM.h").write_text(
            baseline, encoding="utf-8"
        )
        if msvc:
            if shutil.which("cl.exe") is None:
                raise RuntimeError("IMMERSIB_P102_MSVC_ENV_NOT_INITIALIZED")
            compiler = [
                "cl.exe", "/nologo", "/std:c++20", "/O2", "/EHsc",
                "/fp:precise", "/W3", "/utf-8",
                "/I" + str(ROOT), "/I" + str(target),
                "/DARMIX_IMMERSIB_SHIPPING_AB=1",
                *["/D" + macro + "=1" for macro in MACROS],
                str(TEST), "/Fe:" + str(target / "shipping_bitexact.exe"),
            ]
        else:
            compiler = [
                "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                "-pedantic", "-ffp-contract=off", "-I", str(ROOT), "-I", str(target),
                "-DARMIX_IMMERSIB_SHIPPING_AB=1",
                *["-D" + macro + "=1" for macro in MACROS],
                str(TEST), "-o", str(target / "shipping_bitexact"),
            ]
        subprocess.run(compiler, check=True, cwd=target, timeout=180)
        subprocess.run(
            [str(target / ("shipping_bitexact.exe" if msvc else "shipping_bitexact"))],
            check=True, cwd=ROOT, timeout=180,
        )
    print(
        "IMMERSIB_P102_PRODUCTION_SOURCE_AB_PASS "
        f"original={APPROVED_BLOB} candidate={CANDIDATE_BLOB} "
        "algorithm=GUARDED_KERNEL_STASIS source_lock=REFERENCE_SEPARATELY_PINNED "
        "hardware=NOT_TESTED compiler=" + ("MSVC" if msvc else "GCC")
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
