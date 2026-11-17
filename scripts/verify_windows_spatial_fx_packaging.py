#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Fail closed unless the DISTRIBUTED Windows GUI contains both approved spatial FX.

A successful CTest or a source/header check is insufficient: ImmerSIB is
conditionally omitted from NativeBuiltInFx when CMake's default option is OFF.
Read the exact configured cache AND the built PE: require native catalog-only
diagnostic literals and every original scene PNG embedded by JUCE BinaryData.

This is an offline distribution verifier, never an audio-thread component.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

FLAGS = ("ARMIX_IMMERSIB_RELEASE_CANDIDATE", "ARMIX_SONKU_APPROVED_P99_MAIN")
# Markers exclusively owned by the C++ native descriptor, not by JS preview.
NATIVE_CATALOG_MARKERS = (
    b"NATIVE_IMERSIP_PREPARE_EXCEPTION",
    b"Saved ImmerSIB IMRS state rejected; graph unchanged",
    b"NATIVE_SONKU_PREPARE_EXCEPTION",
)
SCENES = (
    "scene_live_concert",
    "scene_royal_concert_hall",
    "scene_arena_thunder",
    "scene_front_row_power",
    "scene_symphonic_palace",
    "scene_velvet_cinema",
)
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def require_spatial_products(cache: bytes, executable: bytes, scenes: dict[str, bytes]) -> dict[str, str]:
    """Validate real build products; raise ValueError on any missing evidence."""
    cache_lines = cache.decode("utf-8-sig", errors="strict").splitlines()
    for flag in FLAGS:
        matches = [line for line in cache_lines if line.startswith(flag + ":")]
        if matches != [f"{flag}:BOOL=ON"]:
            raise ValueError(f"SPATIAL_FEATURE_DISABLED_OR_UNVERIFIED:{flag}")
    if len(executable) < 4 * 1024 * 1024 or not executable.startswith(b"MZ"):
        raise ValueError("SPATIAL_NOT_ACTUAL_WIN64_GUI_EXE")
    for marker in NATIVE_CATALOG_MARKERS:
        if marker not in executable:
            raise ValueError(f"SPATIAL_NATIVE_CATALOG_NOT_EMBEDDED:{marker.decode()}")
    for name in SCENES:
        data = scenes.get(name)
        if not data or not data.startswith(PNG_MAGIC) or len(data) < 100:
            raise ValueError(f"SPATIAL_SCENE_INVALID_OR_MISSING:{name}")
        if executable.find(data) < 0:
            raise ValueError(f"SPATIAL_SCENE_NOT_IN_COMPILED_GUI:{name}")
    return {
        "source": "CONFIGURED_CACHE_AND_COMPILED_PE",
        "catalogId": "armix.builtin.imersip.v1",
        "sceneCount": str(len(SCENES)),
        "exeSha256": hashlib.sha256(executable).hexdigest(),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--scene-dir", type=Path, default=Path("assets/imersip/scenes"))
    args = parser.parse_args(argv)
    try:
        scenes = {name: (args.scene_dir / (name + ".png")).read_bytes() for name in SCENES}
        result = require_spatial_products(
            (args.build_dir / "CMakeCache.txt").read_bytes(),
            args.exe.read_bytes(),
            scenes,
        )
    except (OSError, UnicodeDecodeError, ValueError) as error:
        print(f"WINDOWS_SPATIAL_PRODUCT_FAT_REJECTED: {error}", file=sys.stderr)
        return 1
    print(
        "WINDOWS_SPATIAL_PRODUCT_FAT_PASS "
        "SonKu=P9.9 ImmerSIB=armix.builtin.imersip.v1 "
        f"embeddedScenes={result['sceneCount']} EXE_SHA256={result['exeSha256']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
