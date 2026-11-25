#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Opt-in recorded-audio smoke, not corpus qualification or a listening test."""
import argparse
import array
import csv
import hashlib
import math
import random
import subprocess
import sys
import urllib.request
import wave
from pathlib import Path

REV = "ed5ebcbbbc2d11f0adddc9b50b78d581c29f738c"
BASE = "https://raw.githubusercontent.com/pdx-cs-sound/wavs/" + REV + "/"
# Bart Massey 2024, CC0 unless otherwise noted; pinned README and exact WAV Git blobs.
CLIPS = (
    ("speech", "voice.wav", "1e60e7eb3cd9a324b65dd09b4de8245b360729b1", "auto-mic"),
    ("sung-note", "voice-note.wav", "20188237a28f38cddb12ab7136989509a1f22070", "auto-singing"),
    ("acoustic-guitar", "gc.wav", "e1e382b27dbdc5ec30e796ca39405a03742b86b5", "music"),
)
README_SHA = "109aede3214f6b8066a0ef68a9344ffe12c93936"


def download(name, expected, dest):
    with urllib.request.urlopen(BASE + name, timeout=25) as response:
        data = response.read(8 * 1024 * 1024 + 1)
    if len(data) > 8 * 1024 * 1024:
        raise ValueError("unexpectedly large source file")
    actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    if actual != expected:
        raise ValueError("source blob mismatch: " + name)
    dest.write_bytes(data)


def read_pcm(path):
    with wave.open(str(path), "rb") as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        if rate not in (44100, 48000, 88200, 96000) or channels not in (1, 2) or width not in (2, 3, 4):
            raise ValueError(f"unsupported source format: {rate}/{channels}/{width}")
        data = w.readframes(min(w.getnframes(), 6 * rate))
    scale = 2 ** (8 * width - 1)
    samples = [int.from_bytes(data[i:i+width], "little", signed=True) / scale
               for i in range(0, len(data), width)]
    if not samples or len(samples) % channels:
        raise ValueError("invalid source PCM")
    peak = max(map(abs, samples))
    if peak <= 1e-6:
        raise ValueError("silent source")
    return rate, channels, [v * .45 / peak for v in samples]


def write_pcm(path, rate, channels, samples):
    if any(not math.isfinite(v) or abs(v) > 1 for v in samples):
        raise ValueError("invalid or clipped PCM")
    pcm = array.array("h", (round(v * 32767) for v in samples))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def hiss(frames, channels, seed):
    rng = random.Random(seed)
    prev = [0.0] * channels
    out = []
    for _ in range(frames):
        for c in range(channels):
            v = rng.uniform(-1, 1)
            out.append(v - prev[c])
            prev[c] = v
    return out


def rms(values):
    return math.sqrt(math.fsum(v * v for v in values) / len(values))


def prepare(source, case, filename, sha):
    rate, channels, clip = read_pcm(source)
    frames = len(clip) // channels
    pad = rate // 2
    clean = [0.0] * (pad * channels) + clip + [0.0] * (pad * channels)
    noise = hiss(frames + 2 * pad, channels, 72019)
    learn = hiss(3 * rate, channels, 81021)
    # 10 dB input SNR across the recorded clip (not a per-active-window SNR).
    scale = rms(clip) / (math.sqrt(10) * rms(noise[pad*channels:(pad+frames)*channels]))
    noise = [v * scale for v in noise]
    learn = [v * scale for v in learn]
    write_pcm(case / "clean.wav", rate, channels, clean)
    write_pcm(case / "noise.wav", rate, channels, noise)
    write_pcm(case / "learn.wav", rate, channels, learn)
    # Low-energy window is an energy-selected proxy, not a phonetic/tail annotation.
    width = rate // 10
    windows = [(rms(clip[f*channels:(f+width)*channels]), f)
               for f in range(0, frames-width+1, width)]
    labels = [f"recorded_clip {pad} {pad+frames} active",
              f"idle_end {pad+frames+rate//4} {pad+frames+pad} idle"]
    if windows:
        top = max(x[0] for x in windows)
        candidates = [x for x in windows if x[0] > top * .01]
        if candidates:
            _, f = min(candidates)
            labels.append(f"low_energy_window {pad+f} {pad+f+width} active")
    (case / "segments.tsv").write_text("\n".join(labels) + "\n")
    (case / "provenance.tsv").write_text(
        f"clean\tCC0-1.0\tBart Massey 2024; {BASE+filename}; Git blob {sha}; first <=6 s, peak scaled, PCM16\n"
        "noise\tsynthetic\tseed 72019; independent first-difference uniform noise\n"
        "learn\tsynthetic\tseed 81021; independent first-difference uniform noise\n")
    print(f"source={filename} blob={sha} rate={rate} channels={channels} frames={frames}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qualifier", type=Path)
    parser.add_argument("output", type=Path, help="new local directory; do not commit or publish WAV outputs")
    args = parser.parse_args()
    binary = args.qualifier.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=False)
    download("README.md", README_SHA, args.output / "SOURCE-README.md")
    for name, filename, sha, intent in CLIPS:
        case = args.output / name
        case.mkdir()
        source = case / "source.wav"
        download(filename, sha, source)
        prepare(source, case, filename, sha)
        for mode, clean, protect in (("auto", 0, 1), ("auto", 14, 1), ("auto", 14, 0), (intent, 14, 1)):
            tag = f"{mode}-{clean}-{protect}"
            cmd = [str(binary), *[str(case / f) for f in
                   ("clean.wav", "noise.wav", "learn.wav", "segments.tsv", "provenance.tsv")],
                   str(case / tag), mode, str(clean), str(protect)]
            run = subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=60)
            (case / (tag + ".csv")).write_text(run.stdout)
            for row in csv.DictReader(run.stdout.splitlines()):
                if row["channel_minus1_all"] == "-1":
                    print(name, tag, row["label"], "error_dB=" + row["error_improvement_db"],
                          "projection=" + row["desired_projection"], "removed=" + row["removed_projection"],
                          "clean_only_error_dB=" + row["clean_only_error_db_relative"],
                          "idle_dB=" + row["idle_attenuation_db"], flush=True)
    print("Recorded-audio smoke completed; synthetic added noise; no subjective or release qualification.")


if __name__ == "__main__":
    main()
