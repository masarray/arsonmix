#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Verify accepted ImmerSIB/SonKu DSP against owner-approved P102.3 blobs.

The Windows MSVC and GNU C++ runtimes are not guaranteed to return bitwise
identical sin()/floating DSP samples. For that reason the legacy GNU FNV64
PCM literal is retained on GNU; on MSVC the *approved source identity*
(including every acoustic header) is independently pinned here, while the
native fixture still checks all real PCM behavior, limits, scene separation,
and same-toolchain bit-exact Venue identity.

This is an offline *source provenance* gate, not hardware listening proof.
Any future acoustic retune requires a new, explicitly accepted baseline.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

SCHEMA = "arsonmix.approved-spatial-pcm-source-lock.v1"
APPROVED_SHA = "3095af08b817d59a3e14874dab404bf6e243f38a"
APPROVED_FILE_COUNT = 30
REQUIRED_ROOTS = {
    "engine/builtin_fx/ImerSipP92Processor.h",
    "engine/builtin_fx/ImerSipP92Scene.h",
    "engine/builtin_fx/SonKuSpatialProcessor.h",
}
SONKU_DIRECTORY = Path("engine/builtin_fx/sonku")
# The P102.3 acoustic approval is NEVER re-sealed. One separately measured
# implementation variant may be admitted only by this hard-coded exact blob,
# the separate provenance document, and full regression/PCM parity gates.
OPTIMIZED_PATH = "engine/builtin_fx/sonku/SonKuEarlyReflections.h"
APPROVED_ORIGINAL_BLOB = "5a6a9b48cfdc60729f130c61110c8d8590b20f35"
OPTIMIZED_BLOB = "c92a1fc88cf129e606a6e7c1ba4df1e32269fc65"
OPTIMIZED_PROVENANCE_PATH = (
    "config/immersib_p102_bitexact_optimized_implementation.json"
)
OPTIMIZED_SCHEMA = "arsonmix.immersib-p102-verified-bitexact-implementation.v1"

# Explicit owner-requested SonKu-only control correction; historical acoustic
# manifest remains frozen. Exact adapter blob, not an unrestricted exception.
SONKU_CONTROL_PATH = "engine/builtin_fx/SonKuSpatialProcessor.h"
SONKU_CONTROL_ORIGINAL = "e55f83d758cb3cb5d2f01999e99e1ff1314293ff"
SONKU_CONTROL_VARIANT = "60993a16ada364c9ee5440f75432f693dc36accd"


def verify_exact_optimized_provenance(root: Path) -> None:
    path = root / OPTIMIZED_PROVENANCE_PATH
    try:
        provenance = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(
            "IMMERSIB_OPTIMIZED_IMPLEMENTATION_PROVENANCE_MISSING_OR_BAD"
        ) from exc
    required = {
        "schema": OPTIMIZED_SCHEMA,
        "status": "PROVISIONAL_NOT_RELEASED",
        "approvedSourceSha": APPROVED_SHA,
        "approvedSourceLock": "config/immersib_approved_pcm_sources.json",
        "optimizedPath": OPTIMIZED_PATH,
        "approvedOriginalBlob": APPROVED_ORIGINAL_BLOB,
        "optimizedImplementationBlob": OPTIMIZED_BLOB,
    }
    if not isinstance(provenance, dict) or any(
        provenance.get(key) != value for key, value in required.items()
    ):
        raise ValueError("IMMERSIB_OPTIMIZED_IMPLEMENTATION_PROVENANCE_TAMPERED")



def github_blob_hash(data: bytes) -> str:
    return hashlib.sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()


def validate(root: Path, manifest: dict) -> int:
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA or \
            manifest.get("approvedSourceSha") != APPROVED_SHA:
        raise ValueError("IMMERSIB_APPROVED_SOURCE_LOCK_INVALID_MANIFEST")
    files = manifest.get("files")
    if not isinstance(files, list) or len(files) != APPROVED_FILE_COUNT:
        raise ValueError("IMMERSIB_APPROVED_SOURCE_LOCK_INCOMPLETE")
    declared: set[str] = set()
    for entry in files:
        if not isinstance(entry, dict):
            raise ValueError("IMMERSIB_APPROVED_SOURCE_LOCK_BAD_ENTRY")
        path, expected = entry.get("path"), entry.get("blob")
        if not isinstance(path, str) or not isinstance(expected, str) or \
                not re.fullmatch(r"[0-9a-f]{40}", expected):
            raise ValueError("IMMERSIB_APPROVED_SOURCE_LOCK_BAD_ENTRY")
        pure = Path(path)
        if pure.is_absolute() or ".." in pure.parts or \
                path not in REQUIRED_ROOTS and not (
                    path.startswith("engine/builtin_fx/sonku/")
                    and pure.suffix == ".h" and len(pure.parts) == 4
                ):
            raise ValueError(f"IMMERSIB_APPROVED_SOURCE_LOCK_UNTRUSTED_PATH:{path}")
        if path in declared:
            raise ValueError(f"IMMERSIB_APPROVED_SOURCE_LOCK_DUPLICATE:{path}")
        declared.add(path)
        data = (root / path).read_bytes().replace(b"\r\n", b"\n")
        actual = github_blob_hash(data)
        if actual != expected:
            if (path == SONKU_CONTROL_PATH and expected == SONKU_CONTROL_ORIGINAL
                    and actual == SONKU_CONTROL_VARIANT):
                continue
            # Owner-approved acoustic baseline remains pinned to the ORIGINAL
            # thirty blobs in its historical manifest. Do not silently
            # substitute an arbitrary new hash or allow secondary DSP edits.
            if not (
                path == OPTIMIZED_PATH
                and expected == APPROVED_ORIGINAL_BLOB
                and actual == OPTIMIZED_BLOB
            ):
                raise ValueError(
                    f"IMMERSIB_APPROVED_PCM_SOURCE_CHANGED:{path}:"
                    f"expected={expected}:actual={actual}"
                )
            verify_exact_optimized_provenance(root)
    actual_files = REQUIRED_ROOTS | {
        p.relative_to(root).as_posix()
        for p in (root / SONKU_DIRECTORY).glob("*.h")
    }
    if declared != actual_files:
        raise ValueError(
            "IMMERSIB_APPROVED_SOURCE_LOCK_FILES_MISMATCH:"
            f"missing={sorted(actual_files-declared)} extra={sorted(declared-actual_files)}"
        )
    return len(files)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument(
        "--manifest", type=Path,
        default=Path("config/immersib_approved_pcm_sources.json"),
    )
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        path = args.manifest
        if not path.is_absolute():
            path = root / path
        total = validate(root, json.loads(path.read_text(encoding="utf-8")))
    except (ValueError, OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f"IMMERSIB_APPROVED_PCM_SOURCE_REJECTED: {exc}", file=sys.stderr)
        return 1
    optimized = (
        github_blob_hash(
            (root / OPTIMIZED_PATH).read_bytes().replace(b"\r\n", b"\n")
        ) == OPTIMIZED_BLOB
    )
    print(
        f"IMMERSIB_APPROVED_PCM_SOURCE_PASS source={APPROVED_SHA} "
        f"acousticBaselineFiles={total} "
        f"bitExactImplementationVariant={'PINNED_PROVISIONAL' if optimized else 'NONE'} "
        f"audioRetuning=NONE_SINCE_P102_3_OWNER_APPROVAL"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
