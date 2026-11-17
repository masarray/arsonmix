#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Fail closed unless exact-source legal texts are embedded in a native binary.

JUCE's juce_add_binary_data generates byte-identical constant resource arrays.
This checks the *real compiled executable*, not just a CMake or source marker.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import tempfile

SOURCES = (
    "LICENSE",
    "LICENSE-NOTICE.txt",
    "docs/THIRD_PARTY_8D_SONKU_HRTF.md",
    "third_party/signalsmith/LICENSE-STRETCH.txt",
    "third_party/signalsmith/LICENSE-LINEAR.txt",
    "web/assets/fonts/OFL-LICENSE-Inter.txt",
    "installer/VB-CABLE-NOTICE.txt",
)


def verify(binary: bytes, root: Path) -> None:
    for path in SOURCES:
        source = (root / path).read_bytes()
        if len(source) < 50:
            raise ValueError(f"Legal notice unexpectedly short: {path}")
        if source not in binary:
            raise ValueError(f"Native executable missing exact embedded notice: {path}")
    if b"GNU AFFERO GENERAL PUBLIC LICENSE" not in binary:
        raise ValueError("AGPLv3 marker absent")
    if b"END OF TERMS AND CONDITIONS" not in binary:
        raise ValueError("Truncated AGPLv3")


def self_test(root: Path) -> None:
    combined = b"\0".join((root / item).read_bytes() for item in SOURCES)
    verify(combined, root)
    for missing in SOURCES:
        if missing == "LICENSE":
            removed = combined.replace((root / missing).read_bytes(), b"bad")
        else:
            removed = combined.replace((root / missing).read_bytes(), b"bad")
        try:
            verify(removed, root)
        except ValueError:
            continue
        raise AssertionError(f"Negative embedded-notice check passed: {missing}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root = args.source_root.resolve()
    if args.self_test:
        self_test(root)
        print("EMBEDDED_LEGAL_SELF_TEST_PASS")
        return
    if args.binary is None:
        parser.error("--binary is required without --self-test")
    verify(args.binary.read_bytes(), root)
    print("ARSONMIX_EMBEDDED_LEGAL_BINARY_PASS")


if __name__ == "__main__":
    main()
