#!/usr/bin/env python3
"""P9.9 stage proof against actual P9.8 native PCM, not invented stem labels.

Both CSVs come from the SAME complete SonKu KEMAR+room+P95-P98 graph with
the same factory parameters at 44.1/48 kHz and front/rear; only P99 macro
differs. Venue, ROOM OFF and MIX=0 must be bit-for-bit preserved.
"""
from pathlib import Path
import sys


def load(path):
    rows = {}
    for line in Path(path).read_text().splitlines():
        if not line.startswith("P98_SCENE,"):
            continue
        fields = line.split(",")
        assert len(fields) == 10, line
        key = (int(fields[1]), int(fields[2]), int(fields[3]), fields[4])
        assert key not in rows, key
        rows[key] = (tuple(map(float, fields[5:9])), fields[9])
    assert len(rows) == 24, (path, len(rows))
    return rows


def main():
    assert len(sys.argv) == 3
    reference = load(sys.argv[1])
    candidate = load(sys.argv[2])
    assert reference.keys() == candidate.keys()
    seen = []
    for key in sorted(reference):
        old, old_hash = reference[key]
        new, new_hash = candidate[key]
        rate, azimuth, index, scene = key
        ratios = tuple(new[i] / old[i] for i in range(3))
        assert all(.89 < r < 1.27 for r in ratios), (key, ratios)
        if index in (0, 4, 5):
            assert new_hash == old_hash, f"P99_APPROVED_VENUE_PCM_CHANGED:{key}"
            print("P99_VENUE_ROOMOFF_MIX0_BIT_EXACT", key)
            continue
        # Music warmth 86Hz must not be deleted to make vocals seem louder.
        assert .96 < ratios[0] < 1.09, (key, ratios)
        # 2.05k center articulation should rise without a harsh upper shelf.
        assert 1.005 < ratios[1] < 1.17, (key, ratios)
        assert .96 < ratios[2] < 1.07, (key, ratios)
        assert new[3] <= .9805, (key, "overshoot")
        seen.append((index, ratios[1]))
        print("P99_COMPLETE_SPATIAL_GRAPH", key, *[round(x, 5) for x in ratios])
    assert len(seen) == 12
    print("P99_LOCKED_VENUE_AND_MUSICAL_VOCAL_DEPTH_FULL_PCM_PASSED")


if __name__ == "__main__":
    main()
