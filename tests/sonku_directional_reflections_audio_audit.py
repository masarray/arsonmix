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

def tone_amplitude(samples, channel, hz):
    # Integer cycles in a fixed, 1-second steady window. These are
    # synthetic component amplitudes, NOT LUFS or a perceptual metric.
    first, count = RATE // 2, RATE
    real = imag = 0.0
    for i in range(first, first + count):
        value = samples[2*i+channel] / 32768.
        phase = 2. * math.pi * hz * i / RATE
        real += value * math.cos(phase)
        imag -= value * math.sin(phase)
    return 2. * math.hypot(real, imag) / count

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
        # Report actual synthetic harmonic spectral changes rather than
        # claiming externalization or music neutrality from RMS alone.
        # 165/660/2200/4700Hz map to the exact original source motif.
        band_delta = {}
        for hz in (165, 660, 2200, 4700):
            before = [tone_amplitude(base,ch,hz) for ch in (0,1)]
            after = [tone_amplitude(next_,ch,hz) for ch in (0,1)]
            assert min(before) > 1e-6 and min(after) > 1e-6, (
                "invalid harmonic measurement",pos,hz,before,after)
            differences = [20*math.log10(a/b) for a,b in zip(after,before)]
            assert all(math.isfinite(db) for db in differences)
            band_delta[str(hz)] = differences
        report["scenes"][pos]["harmonicChangeDb"] = band_delta
        report["scenes"][pos]["rmsChangeDb"] = [
            20*math.log10(v/b) for v,b in zip(values,original)]
        # 1.5dB is an engineering review flag, not a subjective PASS gate.
        report["scenes"][pos]["tonalityReviewFlag"] = any(
            abs(db)>1.5 for pair in band_delta.values() for db in pair)
    (folder/"SONKU-P2-DIRECTIONAL-OBJECTIVE.json").write_text(
        json.dumps(report, indent=2, sort_keys=True)+"\n",encoding="utf-8")
    return report

if __name__ == "__main__":
    result=run(Path(sys.argv[1]))
    for name, metric in result["scenes"].items():
        print("SONKU_P2",name,"differenceRms",metric["pcmRmsDifference"],
              "light",metric["lightEarRms"],
              "directional",metric["directionalEarRms"],
              "harmonicDeltaDb",metric["harmonicChangeDb"],
              "tonalityReviewFlag",metric["tonalityReviewFlag"])
    print("PASS SonKu P2 directional native PCM A/B measurements")
