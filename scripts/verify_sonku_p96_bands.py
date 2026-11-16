#!/usr/bin/env python3
"""Compare real native P9.5 versus P9.6 mode-9 PCM without hiding a mismatch."""
import sys
from pathlib import Path

def read(path):
    rows = {}
    for line in Path(path).read_text().splitlines():
        if not line.startswith("P96_FULL,"):
            continue
        p = line.split(",")
        assert len(p) == 9, (path, line)
        key = (int(p[1]), int(p[2]), int(p[3]), float(p[4]))
        assert key not in rows
        rows[key] = (tuple(float(x) for x in p[5:8]), p[8])
    assert len(rows) == 12, (path, len(rows))
    return rows

def main():
    assert len(sys.argv) == 3
    baseline, candidate = read(sys.argv[1]), read(sys.argv[2])
    assert baseline.keys() == candidate.keys()
    for key, (base, hash_old) in baseline.items():
        other, hash_new = candidate[key]
        rate, az, room, meters = key
        if room == 0 or meters <= 2.0:
            assert hash_old == hash_new, f"P9.5 exact PCM regression: {key}"
            continue
        relative = [new / old for old, new in zip(base, other)]
        print(f"P96_FULL_BAND_CHANGE rate={rate} az={az} d={meters}: bass={relative[0]:.3f} mid={relative[1]:.3f} treble={relative[2]:.3f}")
        assert relative[0] > 1.25, (key, relative)
        assert relative[2] > 1.15, (key, relative)
        assert relative[0] / relative[1] > 1.16, (key, relative)
        assert relative[2] / relative[1] > 1.10, (key, relative)
    print("SONKU_P96_INTEGRATED_SPECTRAL_AND_EXACT_LEGACY_PCM_PASSED")

if __name__ == "__main__":
    main()
