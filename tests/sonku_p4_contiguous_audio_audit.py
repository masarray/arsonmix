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
        differences = [int(x)-int(y) for x,y in zip(a,b)]
        peakError = max(map(abs,differences))
        changed = sum(d!=0 for d in differences)
        fraction = changed/len(a)
        rmsError = math.sqrt(sum(d*d for d in differences)/len(a))/32768.
        assert peakError<=1,(scene,"excessive sample delta in LSB",peakError)
        assert fraction<.02,(scene,"widespread numeric drift",fraction)
        assert rmsError<1e-5,(scene,"excessive RMS delta",rmsError)
        bands={}
        for ear in (0, 1):
            baseline = {str(hz): amplitude(a, ear, hz) for hz in HARMONICS}
            optimized = {str(hz): amplitude(b, ear, hz) for hz in HARMONICS}
            assert any(v > .001 for v in baseline.values()),(scene,ear)
            changes = {}
            for hz in HARMONICS:
                before, after = baseline[str(hz)], optimized[str(hz)]
                assert min(before,after)>1e-7
                db = 20.*math.log10(after/before)
                assert abs(db)<.01,(scene,ear,hz,db)
                changes[str(hz)] = db
            bands[str(ear)] = changes
        report["scenes"][scene] = {
            "byteExact": full == contig,
            "peakSampleDifferenceLSB": peakError,
            "changedSampleCount": changed,
            "changedSampleFraction": fraction,
            "differenceRms": rmsError,
            "harmonicDeltaDb": bands,
            "frames": FRAMES,
            "interleavedSamples": len(a)}
    (folder/"SONKU-P4-EXACT-PCM.json").write_text(
        json.dumps(report, indent=2, sort_keys=True)+"\n",
        encoding="utf-8")
    for scene, data in report["scenes"].items():
        print("SONKU_P4_PCM",scene,
              "peakLSB",data["peakSampleDifferenceLSB"],
              "changedFraction",data["changedSampleFraction"],
              "rms",data["differenceRms"],
              "harmonicDeltaDb",data["harmonicDeltaDb"])
    print("PASS P4 native independent stereo 1-LSB / 0.01dB tolerance 60/120 FRONT/REAR")

if __name__ == "__main__":
    run(Path(sys.argv[1]))
