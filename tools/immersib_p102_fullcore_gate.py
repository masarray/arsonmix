#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Full branded ImmerSIB P102.3 PCM byte-exact and same-runner CPU A/B.

Build TWO independent *actual* SonKuSpatialCore binaries, not a fake FIR:
1) unchanged 30 frozen acoustic sources (P102.3 approved commit baseline);
2) one explicitly pinned optimized EARLY header, other 29 identical sources.
Compare every stereo FLOAT BYTE across seven scenes × both sample rates,
ROOM off/on, yaw, stereo/antiphase/silence and buffer partitions. Then compare
the real P102 core callback p50/p99 on the SAME Linux runner.
"""
from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

from immersib_p102_geometry_cache_probe import MACROS, ROOT
from immersib_p102_fir_shipping_gate import (
    APPROVED_BLOB, APPROVED_SOURCE_SHA, CANDIDATE_BLOB, PATH, blob_hash,
)

SOURCE = ROOT / "tools/immersib_p102_fullcore_ab.cpp"
SCENE = ROOT / "engine/builtin_fx/ImerSipP92Scene.h"
SONKU = ROOT / "engine/builtin_fx/sonku"
FLOAT_SIZE = 4
METRIC = re.compile(
    r"IMMERSIB_P102_FULL_CORE_CPU rate=(\d+) scene=(\d+) frames=(\d+) "
    r"blocks=(\d+) p50_us=([\d.]+) p95_us=([\d.]+) p99_us=([\d.]+)"
)


def approved_header() -> bytes:
    old = subprocess.run(
        ["git", "show", f"{APPROVED_SOURCE_SHA}:{PATH}"],
        cwd=ROOT, check=True, capture_output=True, timeout=30,
    ).stdout.replace(b"\r\n", b"\n")
    if blob_hash(old) != APPROVED_BLOB:
        raise ValueError("IMMERSIB_P102_ORIGINAL_SOURCE_DRIFT")
    return old


def build_and_measure(root: pathlib.Path, label: str, original: bytes,
                      msvc: bool, override_bytes: bytes | None = None) -> tuple[bytes, dict]:
    target = root / label
    audio = target / "engine/builtin_fx"
    audio.mkdir(parents=True)
    shutil.copytree(SONKU, audio / "sonku")
    shutil.copyfile(SCENE, audio / SCENE.name)
    tools = target / "tools"
    tools.mkdir()
    shutil.copyfile(SOURCE, tools / SOURCE.name)
    header = target / PATH
    if label == "original":
        header.write_bytes(original)
    elif label == "specialized" and override_bytes is not None:
        header.write_bytes(override_bytes)
    elif blob_hash(header.read_bytes()) != CANDIDATE_BLOB:
        raise ValueError("IMMERSIB_P102_OPTIMIZED_SOURCE_DRIFT")

    executable = target / ("p102_fullcore.exe" if msvc else "p102_fullcore")
    if msvc:
        if shutil.which("cl.exe") is None:
            raise RuntimeError("IMMERSIB_FULL_CORE_MSVC_ENV_NOT_INITIALIZED")
        command = [
            "cl.exe", "/nologo", "/std:c++20", "/O2", "/EHsc",
            "/fp:precise", "/W3", "/utf-8",
            *["/D" + macro + "=1" for macro in MACROS],
            str(tools / SOURCE.name), "/Fe:" + str(executable),
        ]
    else:
        command = [
            "g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
            "-pedantic", "-ffp-contract=off",
            *["-D" + macro + "=1" for macro in MACROS],
            str(tools / SOURCE.name), "-o", str(executable),
        ]
    subprocess.run(command, check=True, cwd=target, timeout=180)
    output_file = target / "all_scenes_2rates_exact_pcm.f32"
    p = subprocess.run(
        [str(executable), "--pcm", str(output_file)],
        check=True, cwd=target, text=True, capture_output=True, timeout=180,
    )
    print(f"IMMERSIB_FULLCORE_VARIANT={label}", flush=True)
    print(p.stdout, end="", flush=True)
    samples = output_file.read_bytes()
    if len(samples)<500_000 or len(samples)%FLOAT_SIZE:
        raise ValueError("IMMERSIB_FULL_CORE_AB_INVALID_AUDIO_BYTE_COUNT")
    metrics = {}
    for match in METRIC.finditer(p.stdout):
        rate, scene, frames, blocks = map(int, match.group(1,2,3,4))
        if frames != 256 or blocks != 192:
            raise ValueError("IMMERSIB_FULL_CORE_AB_INVALID_BENCH")
        metrics[(rate, scene)] = tuple(map(float, match.group(5,6,7)))
    if len(metrics)!=4:
        raise ValueError("IMMERSIB_FULL_CORE_AB_MISSING_CASES")
    return samples,metrics


def main() -> int:
    msvc=len(sys.argv)==2 and sys.argv[1]=="--msvc"
    if len(sys.argv)>1 and not msvc:
        raise ValueError("IMMERSIB_FULL_CORE_INVALID_COMPILER_OPTION")
    subprocess.run(
        [sys.executable, "scripts/verify_immersib_approved_pcm_sources.py"],
        cwd=ROOT, check=True, timeout=30,
    )
    original = approved_header()
    if blob_hash((ROOT / PATH).read_bytes()) != CANDIDATE_BLOB:
        raise ValueError("IMMERSIB_P102_UNEXPECTED_CANDIDATE_BLOB")
    with tempfile.TemporaryDirectory(prefix="imersib-p102-fullcore-ab-") as td:
        root = pathlib.Path(td)
        approved_bytes, base = build_and_measure(root,"original",original,msvc)
        candidate_bytes, opt = build_and_measure(root,"optimized",original,msvc)
        if approved_bytes != candidate_bytes:
            first = next(i for i,(a,b) in enumerate(zip(approved_bytes,candidate_bytes))
                         if a!=b)
            raise ValueError(
                "IMMERSIB_P102_FULL_CORE_PCM_NOT_BIT_EXACT "
                f"offset={first} stereoSample={first//8} "
                f"originalByte={approved_bytes[first]} optimizedByte={candidate_bytes[first]}"
            )
        print(
            "IMMERSIB_P102_FULL_CORE_PCM_BITEXACT_PASS "
            f"bytes={len(approved_bytes)} samples={len(approved_bytes)//4} "
            "scenes=7 rates=44100_48000 approved_reference=HISTORICAL_SHA "
            "room_off_on=YES yaw_edit=YES inputs=STEREO_ANTIPHASE_SILENCE "
            "engine=P102_APPROVED_FULL_SONKU compiler=" + ("MSVC" if msvc else "GCC")
        )
        for key in sorted(base):
            a=base[key];b=opt[key]
            print(
                "IMMERSIB_P102_FULL_CORE_AB "
                f"rate={key[0]} scene={key[1]} "
                f"reference_p50_us={a[0]:.2f} optimized_p50_us={b[0]:.2f} "
                f"p50_ratio={b[0]/a[0]:.4f} "
                f"reference_p99_us={a[2]:.2f} optimized_p99_us={b[2]:.2f} "
                "scope=SAME_LINUX_RUNNER_NOT_ASIO"
            )
    return 0


if __name__=="__main__":
    sys.exit(main())
