#!/usr/bin/env python3
"""P9.4 head-relative depth cue: compare actual native reference and candidate PCM.

Inputs: two C++ executables generated from the SAME source, baseline P9.1+P9.2
(default macro OFF), research P9.4 ON. No golden files or mock DSP.
"""
import math
import re
import subprocess
import sys

ROW = re.compile(
    r"^P94_PCM,(44100|48000),az=(0|90|180|270),"
    r"room_rms=([0-9]+\.[0-9]+),off_rms=([0-9]+\.[0-9]+)$"
)

def run(executable):
    done = subprocess.run([executable], capture_output=True,
                          text=True, check=True, timeout=90)
    result = {}
    for line in done.stdout.splitlines():
        match = ROW.fullmatch(line.strip())
        if not match:
            continue
        rate, angle, wet, dry = match.groups()
        key = int(rate), int(angle)
        if key in result:
            raise AssertionError(f"duplicate PCM metric {key}")
        result[key] = float(wet), float(dry)
    if len(result) != 8:
        raise AssertionError(f"expected eight PCM rows from {executable}, "
                             f"found {len(result)}\n{done.stdout[-2500:]}")
    return result

def db_ratio(candidate, baseline):
    if candidate <= 0 or baseline <= 0:
        raise AssertionError("silent or invalid PCM")
    return 20 * math.log10(candidate / baseline)

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: verify_sonku_p94_acoustic_levels.py "
                         "<p91_reference_exe> <p94_research_exe>")
    base = run(sys.argv[1])
    experiment = run(sys.argv[2])
    for key in sorted(base):
        rate, angle = key
        (before, before_off) = base[key]
        (after, after_off) = experiment[key]
        shift = db_ratio(after, before)
        dry_shift = db_ratio(after_off, before_off)
        # ROOM=OFF must be precisely identical: P9.4 cannot create new sound
        # or change the accepted HRTF-only signal in its absence.
        if before_off != after_off:
            raise AssertionError(f"ROOM OFF changed {key}: {before_off} -> {after_off}")
        # At ±90° it must be audio-transparent, not a disguised global boost.
        if angle in (90, 270) and before != after:
            raise AssertionError(f"lateral reference changed {key}: {shift:.4f} dB")
        # A/B normalized reference levels are an ELECTRICAL/RMS guard,
        # not music LUFS or headphone SPL. Reject >0.5dB direct+wet deviation.
        if abs(shift) > 0.5:
            raise AssertionError(f"unbalanced P9.4 {key}: {shift:.3f}dB")
        if abs(dry_shift) > 0.001:
            raise AssertionError(f"unexpected dry-level drift at {key}")
        print(f"P94_MATCH,rate={rate},angle={angle},"
              f"delta_room_db={shift:.4f},delta_dry_db={dry_shift:.4f}")
    print("PASS P94 real native A/B: 8 angles/rates, ROOM OFF exact, "
          "lateral PCM exactly unchanged, median-plane ±0.5dB level guard")

if __name__ == "__main__":
    main()
