#!/usr/bin/env python3
"""Exact cross-product PCM isolation gate for P9.9 (private).

The SAME SonKuSpatialCore and full KEMAR+6-wall+late-room graph are built
twice. The reference has P9.8 enabled and P9.9 absent. The candidate has
P9.9 compiled into the binary but does NOT opt its instance in. Every
output case MUST retain its full PCM hash and band meters bit-for-bit.
No JS capability surrogate, disabled native test, approximate tolerance or
claimed vocal stem extraction.
"""
import sys
from pathlib import Path


def rows(path):
    entries = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.startswith("P98_SCENE,"):
            continue
        fields = line.split(",")
        assert len(fields) == 10, (path, line)
        key = tuple(fields[1:5])
        assert key not in entries, (path, key)
        entries[key] = line
    assert len(entries) == 24, (path, len(entries))
    return entries


def main():
    assert len(sys.argv) == 3, "usage: reference.csv unopted.csv"
    reference, unopted = map(rows, sys.argv[1:3])
    assert reference.keys() == unopted.keys(), "scene inventory changed"
    for key, expected in reference.items():
        got = unopted[key]
        assert got == expected, (
            f"SHARED_CORE_P99_UNREQUESTED_PCM_MUTATION {key}\n"
            f"P98 reference: {expected}\nP99 unopted:  {got}"
        )
        print("SHARED_CORE_P99_UNOPTED_PCM_BIT_EXACT", ",".join(key))
    print("P99_CROSS_PRODUCT_SHARED_CORE_ISOLATION_24_CASES_PASSED")


if __name__ == "__main__":
    main()
