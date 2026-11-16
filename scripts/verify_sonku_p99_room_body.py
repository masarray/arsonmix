#!/usr/bin/env python3
"""Proof of P9.9 coherent vocal upper-body exciting real ER + late room.

The fixture uses the complete SonKu source->KEMAR->room->limiter graph with
320Hz chest, 2050Hz clarity and 7500Hz sibilance components. Compare P9.8
with P9.9, not a bypass-only or stand-alone surrogate. Exactly preserve
Venue/ROOM OFF/MIX0; no false claim of extracted singer stems.
"""
from pathlib import Path
import sys


def load(path):
    rows = {}
    for line in Path(path).read_text().splitlines():
        if not line.startswith("P98_SCENE,"):
            continue
        p = line.split(",")
        assert len(p) == 10, line
        key = (int(p[1]), int(p[2]), int(p[3]), p[4])
        assert key not in rows
        rows[key] = (tuple(map(float, p[5:9])), p[9])
    assert len(rows) == 24, (path, len(rows))
    return rows


def main():
    assert len(sys.argv) == 3
    base, candidate = load(sys.argv[1]), load(sys.argv[2])
    assert base.keys() == candidate.keys()
    count = 0
    for key in sorted(base):
        old, old_hash = base[key]
        new, new_hash = candidate[key]
        rate, az, index, scene = key
        gains = tuple(new[k] / old[k] for k in range(3))
        assert new[3] <= .9805, ("linked guard", key, new[3])
        if index in (0, 4, 5):
            assert old_hash == new_hash, ("Venue/off/bypass changed", key)
            print("P99_ROOM_BODY_LOCKED_PCM", key)
            continue
        # Near 320Hz warm room-body must be an actual change to the
        # COMPLETE spatial engine, not merely an altered unit-test meter.
        # No LF loss to make vocals appear loud; no huge boom.
        lower = {1: 1.055, 2: 1.008, 3: 1.035}[index]
        assert lower < gains[0] < 1.29, ("room-body insufficient or boomy", key, gains)
        assert 1.005 < gains[1] < 1.18, ("vocal articulation", key, gains)
        assert .95 < gains[2] < 1.07, ("sibilant/cymbal", key, gains)
        count += 1
        print("P99_TRUE_ROOM_COHERENT_BODY", key, *[round(g, 5) for g in gains])
    assert count == 12
    print("P99_ROOM_RESONANCE_320HZ_FULL_GRAPH_AND_VENUE_LOCK_PASSED")


if __name__ == "__main__":
    main()
