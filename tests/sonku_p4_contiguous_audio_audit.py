#!/usr/bin/env python3
"""Bit-exact 140-tap source PCM P4 vs P2, 60/120 spread, independent stereo.

This validates data invariance, not claimed improved subjective externalization.
"""
import array
import json
import math
import sys
import wave
from pathlib import Path

RATE = 48000
FRAMES = RATE * 2
SCENES = ("front-spread60", "rear-spread60",
          "front-spread120", "rear-spread120")
HARMONICS = (165, 940, 2200, 4700, 5100)

def read(path):
    with wave.open(str(path), "rb") as f:
        assert (f.getnchannels(), f.getsampwidth(), f.getframerate(),
                f.getnframes()) == (2, 2, RATE, FRAMES), path
        data = f.readframes(FRAMES)
    result = array.array("h")
    result.frombytes(data)
    if sys.byteorder != "little":
        result.byteswap()
    return data, result

def amplitude(signal, ear, hz):
    start, count = RATE//2, RATE
    re = im = 0.
    for i in range(start, start+count):
        phase = 2.*math.pi*hz*i/RATE
        value = signal[2*i+ear]/32768.
        re += value*math.cos(phase)
        im -= value*math.sin(phase)
    return 2.*math.hypot(re, im)/count

def run(folder):
    report = {"method": "identical original independent-stereo source P2 vs P4",
              "note": "no human hearing / ASIO / dearVR comparison", "scenes": {}}
    for scene in SCENES:
        full, a = read(folder/f"sonku-p4-full-{scene}.wav")
        contig, b = read(folder/f"sonku-p4-contiguous-{scene}.wav")
        assert full == contig, ("P4 audio must be BYTE EXACT to P2", scene)
        for ear in (0, 1):
            band = {str(hz): amplitude(b, ear, hz) for hz in HARMONICS}
            assert any(v > .001 for v in band.values()), (scene, ear, band)
        report["scenes"][scene] = {
            "byteExact": True, "samplePeakLsb": max(map(abs, a)),
            "frames": FRAMES, "interleavedSamples": len(a)}
    (folder/"SONKU-P4-EXACT-PCM.json").write_text(
        json.dumps(report, indent=2, sort_keys=True)+"\n",
        encoding="utf-8")
    print("PASS P4 native independent stereo byte-exact 60/120 FRONT/REAR")

if __name__ == "__main__":
    run(Path(sys.argv[1]))
