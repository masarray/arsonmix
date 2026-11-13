#!/usr/bin/env python3
"""Measured directional image-source A/B. Objective PCM, NOT perception."""
import array
import json
import math
import sys
import wave
from pathlib import Path

RATE = 48000
FRAMES = RATE * 2

def read(path):
    with wave.open(str(path), "rb") as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate(),
                w.getnframes()) == (2, 2, RATE, FRAMES), path
        raw = w.readframes(FRAMES)
    samples = array.array("h")
    samples.frombytes(raw)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples

def rms(samples, ch, first, last):
    return math.sqrt(sum((samples[2*i+ch]/32768.)**2
                         for i in range(first,last))/(last-first))

def run(folder):
    report = {"note": "Measured PCM change, not proven improved externalization",
              "rate": RATE, "frames": FRAMES, "scenes": {}}
    for pos in ("front", "rear"):
        base = read(folder/f"sonku-p2-light-{pos}.wav")
        next_ = read(folder/f"sonku-p2-directional-{pos}.wav")
        assert base[:300] == next_[:300], "direct pre-reflection PCM changed"
        assert max(map(abs, next_)) < 30000, "clipped P2"
        delta = math.sqrt(sum(((a-b)/32768.)**2
                              for a,b in zip(base[48000:144000], next_[48000:144000]))
                          / 96000)
        assert delta > .000005, (pos, "directional room did not alter reflection", delta)
        values = [rms(next_, ch, RATE//2, RATE*3//2) for ch in (0,1)]
        original = [rms(base, ch, RATE//2, RATE*3//2) for ch in (0,1)]
        assert min(values) > .001 and max(values) < .5
        report["scenes"][pos] = {
            "pcmRmsDifference": delta,
            "lightEarRms": original,
            "directionalEarRms": values,
            "directionalSamplePeakDbFS":
                20*math.log10(max(map(abs,next_))/32768.),
        }
    (folder/"SONKU-P2-DIRECTIONAL-OBJECTIVE.json").write_text(
        json.dumps(report, indent=2, sort_keys=True)+"\n",encoding="utf-8")
    return report

if __name__ == "__main__":
    result=run(Path(sys.argv[1]))
    for name, metric in result["scenes"].items():
        print("SONKU_P2",name,"differenceRms",metric["pcmRmsDifference"],
              "light",metric["lightEarRms"],
              "directional",metric["directionalEarRms"])
    print("PASS SonKu P2 directional native PCM A/B measurements")
