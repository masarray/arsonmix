#!/usr/bin/env python3
"""Exact-source SonKu headphone A/B comparison against pinned public alpha.23.

No perceptual/dearVR claim: measures native PCM rescue of deterministic
anti-phase stereo and coherent 2kHz rear comb-filter coloration.
All inputs match in length, phase, SPL/gain settings and native audio path.
"""
from __future__ import annotations

import array
import json
import math
from pathlib import Path
import sys
import wave

RATE = 48_000
FRAMES = RATE * 4
FILES = (
    "sonku-music-positive-front.wav",
    "sonku-music-anti-phase-front.wav",
    "sonku-music-anti-phase-rear.wav",
    "sonku-music-coherent-front-spread60.wav",
    "sonku-music-coherent-rear-spread60.wav",
    "sonku-music-splitband-bass-coherent-treble-inverted.wav",
)


def read_audio(folder: Path, name: str) -> tuple[bytes, array.array]:
    path = folder / name
    assert path.is_file() and path.stat().st_size == FRAMES * 4 + 44, path
    with wave.open(str(path), "rb") as f:
        assert (f.getnchannels(), f.getsampwidth(), f.getframerate(),
                f.getnframes(), f.getcomptype()) == (2, 2, RATE, FRAMES, "NONE"), name
        raw = f.readframes(FRAMES)
    assert len(raw) == FRAMES * 4
    audio = array.array("h")
    audio.frombytes(raw)
    if sys.byteorder != "little":
        audio.byteswap()
    assert max(map(abs, audio)) < 30_000, (name, "clipping", max(map(abs, audio)))
    return raw, audio


def rms(audio: array.array, ear: int) -> float:
    # 1.5-3.5s: exclude onset/correlation attack and 20ms last fade.
    start = int(RATE * 1.5)
    end = int(RATE * 3.5)
    values = (audio[2 * i + ear] / 32768. for i in range(start, end))
    energy = sum(sample * sample for sample in values)
    return math.sqrt(energy / (end - start))


def tone_amplitude(audio: array.array, ear: int, frequency: int = 2000) -> float:
    # Exactly 48k steady frames = integer cycles at all fixture frequencies.
    # Coherent sine/cosine demod measures *2kHz tonal content*, not overall
    # level which could hide comb filtering through other musical harmonics.
    start, count = RATE * 2, RATE
    re = im = 0.
    for i in range(count):
        sample = audio[(start + i) * 2 + ear] / 32768.
        phase = 2. * math.pi * frequency * i / RATE
        re += sample * math.cos(phase)
        im += sample * math.sin(phase)
    return (2. / count) * math.hypot(re, im)


def run(baseline_dir: Path, candidate_dir: Path) -> dict:
    base, improved = {}, {}
    for name in FILES:
        base[name] = read_audio(baseline_dir, name)
        improved[name] = read_audio(candidate_dir, name)
    # Legacy 0° SPREAD mono/coherent signal must be bit-exact.
    center = FILES[0]
    assert base[center][0] == improved[center][0], (
        "regression: previously approved center KEMAR PCM has changed")
    report = {"baseline": "v0.5.0-alpha.23 native SonKu core",
              "note": "Measured numerical transfer only; headphone externalization unproven",
              "sampleRate": RATE, "frames": FRAMES,
              "exactCenteredPcm": True, "antiPhase": {}, "coherentMid2kHz": {},
              "mixedPolarityTreble": {}}
    # Old filters are identical for L=-R / 0 spread and cancel completely.
    # New guard must preserve actual music energy in *both ears*, not just
    # front/rear labels, despite the native 0.5 wet mix.
    for name in FILES[1:3]:
        old = [rms(base[name][1], ear) for ear in (0, 1)]
        new = [rms(improved[name][1], ear) for ear in (0, 1)]
        assert max(old) < 0.001, (name, "old should null", old)
        assert min(new) > 0.025, (name, "anti-phase PCM not rescued", old, new)
        assert max(new) < 0.5, (name, "excess output", new)
        report["antiPhase"][name] = {"baselineRms": old,
                                     "candidateRms": new}
    # Coherent 60° two-source FIR is a direct comb-filter mechanism. Verify
    # native kernel-center compensation, not an overall boost or new reverb.
    for name, threshold in ((FILES[3], 1.30), (FILES[4], 2.0)):
        old = [tone_amplitude(base[name][1], ear) for ear in (0, 1)]
        new = [tone_amplitude(improved[name][1], ear) for ear in (0, 1)]
        factors = [a / max(1.e-10, b) for a, b in zip(new, old)]
        assert min(factors) > threshold, (
            name, "2kHz cancellation not repaired", factors)
        assert max(new) < 0.25, (name, "excessive compensated gain", new)
        report["coherentMid2kHz"][name] = {
            "baselineAmplitude": old, "candidateAmplitude": new,
            "candidateOverBaseline": factors,
            "increaseDb": [20. * math.log10(x) for x in factors],
        }
    # Original coherent bass dominates full-band L/R correlation and
    # should be numerically unchanged by high-frequency protection.
    name = FILES[5]
    old_high = [tone_amplitude(base[name][1], ear, 3200) for ear in (0, 1)]
    new_high = [tone_amplitude(improved[name][1], ear, 3200) for ear in (0, 1)]
    assert max(old_high) < 0.0002, ("baseline treble should cancel", old_high)
    assert min(new_high) > 0.006, ("split-band treble not rescued", new_high)
    assert max(new_high) < 0.1, ("split-band rescue unexpectedly loud", new_high)
    old_low = [tone_amplitude(base[name][1], ear, 220) for ear in (0, 1)]
    new_low = [tone_amplitude(improved[name][1], ear, 220) for ear in (0, 1)]
    low_delta = [20. * math.log10(max(n, 1e-12) / max(b, 1e-12))
                 for b, n in zip(old_low, new_low)]
    assert max(map(abs, low_delta)) < .25, ("coherent bass changed", low_delta)
    report["mixedPolarityTreble"][name] = {
        "baselineTrebleAmplitude": old_high,
        "candidateTrebleAmplitude": new_high,
        "bassDifferenceDb": low_delta,
    }
    # A relative binaural phase measurement is necessary for a signal
    # that must remain artistically inverted, not silently become mono.
    def phasor(ear: int) -> complex:
        start, count, hz = RATE * 2, RATE, 3200
        re = im = 0.
        for j in range(count):
            sample = improved[name][1][2 * (start + j) + ear] / 32768.
            phase = 2. * math.pi * hz * j / RATE
            re += sample * math.cos(phase)
            im -= sample * math.sin(phase)
        return complex(re, im) * (2. / count)

    cross_phase = phasor(0) * phasor(1).conjugate()
    interaural_deg = math.degrees(math.atan2(cross_phase.imag, cross_phase.real))
    assert abs(abs(interaural_deg) - 180.) < 5., (
        "recovered artistic treble polarity was not retained", interaural_deg)
    report["mixedPolarityTreble"][name]["interauralPhaseDifferenceDeg"] = interaural_deg
    # RMS dBFS is measured PCM level, NOT K-weighted LUFS or true-peak.
    report["mixedPolarityTreble"][name]["candidateRmsDbFS"] = [
        20. * math.log10(max(rms(improved[name][1], ear), 1e-12))
        for ear in (0, 1)
    ]
    peak = max(abs(v) for v in improved[name][1]) / 32768.
    report["mixedPolarityTreble"][name]["samplePeakDbFS"] = (
        20. * math.log10(max(peak, 1e-12))
    )
    (candidate_dir / "MUSIC-INTEGRITY-OBJECTIVE.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("Usage: sonku_music_integrity_comparison.py BASELINE_DIR CANDIDATE_DIR")
    result = run(Path(sys.argv[1]), Path(sys.argv[2]))
    for name, case in result["coherentMid2kHz"].items():
        print("SONKU_COHERENT_HRIR_2KHZ", name, "perEarGainDb",
              [round(v, 2) for v in case["increaseDb"]])
    for name, case in result["antiPhase"].items():
        print("SONKU_ANTI_PHASE_RECOVERY", name, "perEarRms",
              [round(v, 5) for v in case["candidateRms"]])
    for name, case in result["mixedPolarityTreble"].items():
        print("SONKU_SPLITBAND_TREBLE_3200", name,
              "baseline", [round(v, 6) for v in case["baselineTrebleAmplitude"]],
              "candidate", [round(v, 6) for v in case["candidateTrebleAmplitude"]],
              "bassDeltaDb", [round(v, 4) for v in case["bassDifferenceDb"]],
              "phaseDeg", round(case["interauralPhaseDifferenceDeg"], 2),
              "samplePeakDbFS", round(case["samplePeakDbFS"], 2))
    print("PASS SONKU MUSIC INTEGRITY exact baseline-vs-new PCM, 6 WAV, 48k")
