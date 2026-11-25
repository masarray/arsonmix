#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Pinned, attributed real-singer qualification. No downloaded code executes."""
import csv
import hashlib
import pathlib
import subprocess
import sys
import urllib.request
import time
import array
import math
import wave

REV = "c48ab544e94e9788fec11be52b365026bb4a017c"
BASE = f"https://raw.githubusercontent.com/mblackman/vocalset-vowel-voicebanks/{REV}/source/vocalset/"
# Raw source WAV blobs, not processed 80ms voicebank clips.
CLIPS = {
    "f1_long_straight_a.wav": ("b609083332a343eb984105b72eb9006108e5368c",1042802),
    "f1_long_straight_i.wav": ("44a0e6aa906d541eb3d3538d8fc4617ab4ad2f58",969808),
    "m1_long_straight_a.wav": ("b3bfef541e00ea71a9da01c46156e8a006da44d1",897668),
    "m1_long_straight_i.wav": ("6b21881a24e0ec17afab623512937983b5049abd",897668),
    "m2_long_straight_a.wav": ("74b4a5f55c1a77139b89fe4adb41ddc132be389a",760274),
    "m2_long_straight_i.wav": ("11aeb935188a6f4917e6d6c01acc7c27a6113ea8",805234),
}
ATTRIBUTION = """Wilkins, J., Seetharaman, P., Wahl, A., & Pardo, B. (2018).
VocalSet: A Singing Voice Dataset. ISMIR 2018.
Source: https://zenodo.org/records/1442513
License: CC BY 4.0 https://creativecommons.org/licenses/by/4.0/
Redistribution: https://github.com/mblackman/vocalset-vowel-voicebanks/tree/c48ab544e94e9788fec11be52b365026bb4a017c/source/vocalset
Source WAVs unchanged; output WAVs are Harmonice processing and RMS-matched
audition derivatives. These are sustained vowels, not conversational singing.
No listening scores or ground-truth formant values are asserted.
"""
def download(name, sha, size, dst):
    for attempt in range(3):
        try:
            with urllib.request.urlopen(BASE + name, timeout=20) as response:
                data = response.read(size+1)
            if len(data) != size:
                raise ValueError("size mismatch")
            digest = hashlib.sha1(f"blob {size}\0".encode() + data).hexdigest()
            if digest != sha:
                raise ValueError("git blob mismatch")
            dst.write_bytes(data)
            return
        except (OSError, ValueError) as err:
            print(f"H27_DATA_RETRY {name} {attempt+1}: {err}", flush=True)
            if attempt < 2:
                time.sleep(1)
    raise RuntimeError(f"pinned source unavailable: {name}")

def make_guide(source, destination):
    """Independent synthetic C/F/G triads; source singer is never mixed in."""
    with wave.open(str(source), "rb") as src:
        rate, frames = src.getframerate(), src.getnframes()
    chords = ((60,64,67),(53,57,60),(55,59,62))
    period = 2*rate
    data = array.array("h")
    for at in range(frames):
        local = at % period
        chord = chords[(at//period)%len(chords)]
        time_s = local/rate
        ramp = min(1.,local/(.015*rate),(period-1-local)/(.015*rate))
        val = 0.
        for note in chord:
            phase = 2.*math.pi*440.*2.**((note-69)/12.)*time_s
            val += math.sin(phase)+.18*math.sin(2.*phase)
        data.append(round(32767.*.065*ramp*val))
    if sys.byteorder != "little":
        data.byteswap()
    with wave.open(str(destination),"wb") as dst:
        dst.setnchannels(1)
        dst.setsampwidth(2)
        dst.setframerate(rate)
        dst.writeframes(data.tobytes())
    return frames

def qualify_guide(renderer, out, name, sha):
    source = out/name
    folder = out/(source.stem+"_music")
    guide = out/(source.stem+"_synthetic_guide.wav")
    frames = make_guide(source,guide)
    subprocess.run([renderer,"--qualify-music",str(source),str(guide),str(folder),
                    "0","0","5","64"],check=True,timeout=90)
    with (folder/"guide_timeline.csv").open(newline="") as f:
        timeline=list(csv.DictReader(f))
    roots={int(row["chord_root"]) for row in timeline if int(row["chord_root"])>=0}
    if len(timeline)!=(frames+63)//64 or int(timeline[-1]["callback_end_sample"])!=frames or len(roots.intersection({0,5,7}))<2:
        raise RuntimeError("H28D independent guide timeline incomplete")
    if (folder/"dry_aligned.wav").read_bytes()!=(out/source.stem/"dry_aligned.wav").read_bytes():
        raise RuntimeError("H28D guide altered dry PCM")
    with (folder/"qualification.csv").open(newline="") as f:
        rows=list(csv.DictReader(f))
    if len(rows)!=5 or {x["variant"] for x in rows}!={"live_baseline","H26","H27","H28_epoch","H28_local"} or any(x["mode"]!="Music" for x in rows):
        raise RuntimeError("H28D guided qualification missing variants")
    for row in rows:
        print("H28_GUIDED clip="+name+" blob_sha="+sha+" "+" ".join(f"{k}={v}" for k,v in row.items()),flush=True)
    print(f"H28_GUIDE_COMPLETE clip={name} callbacks={len(timeline)} chord_roots={sorted(roots)} synthetic_guide=true dry_exact=true",flush=True)
    return [{"clip":name,"blob_sha":sha,**row} for row in rows]

# H29: validate structured evidence independently of the renderer, then
# publish only explicit non-audio files. No source/derived PCM or listener key.
VARIANTS = {"live_baseline", "H26", "H27", "H28_epoch", "H28_local"}
TRACED = {"live_baseline", "H28_epoch", "H28_local"}
GUIDED = ("f1_long_straight_a.wav", "m2_long_straight_i.wav")

def require(condition, message):
    if not condition:
        raise RuntimeError("H29 evidence: " + message)

def read_rows(path):
    with path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    require(bool(rows) and all(None not in r and None not in r.values() for r in rows),
            "empty or malformed CSV: " + path.name)
    return rows

def count(row, key):
    value = int(row[key])
    require(value >= 0, "negative count: " + key)
    return value

def validate_trace(summary, reasons, admissions):
    expected = {(v, i) for v in TRACED for i in range(5)}
    for rows in (reasons, admissions):
        keys = [(r["variant"], count(r, "role")) for r in rows]
        require(len(keys) == len(expected) and set(keys) == expected,
                "missing or duplicate trace role")
    for v in TRACED:
        rs = [r for r in reasons if r["variant"] == v]
        q = summary[v]
        require(sum(count(r, "losses") for r in rs) == count(q, "total_losses"),
                "total loss mismatch: " + v)
        require(count(next(r for r in rs if count(r, "role") == 2), "losses")
                == count(q, "high_losses"), "HIGH loss mismatch: " + v)
        for r in rs:
            require(count(r, "losses") == sum(count(r, k) for k in
                    ("loss_no_clock", "loss_causal", "loss_safe_unready")),
                    "loss reason mismatch: " + v)
    for r in admissions:
        require(count(r, "attempts") == sum(count(r, k) for k in
                ("missing_peak", "pool_blocked", "support_rejected", "admitted")),
                "admission conservation mismatch")
        require(count(r, "adaptive_accepted") <= count(r, "adaptive_attempts") and
                count(r, "local_accepted") <= count(r, "local_checks"),
                "recovery accounting mismatch")

def validate_timeline(rows, frames, block):
    require(len(rows) == (frames + block - 1) // block, "guide callback count")
    for i, row in enumerate(rows):
        require(count(row, "callback_start_sample") == i * block and
                count(row, "callback_end_sample") == min((i + 1) * block, frames),
                "guide timeline gap, overlap or reordered callback")
    roots = {int(r["chord_root"]) for r in rows}
    require(len(roots & {0, 5, 7}) >= 2, "expected guide roots absent")

EVENT_FIELDS = ("loss_no_clock", "loss_causal", "loss_safe_unready",
                "missing_peak", "pool_blocked", "support_rejected")

def validate_events(path, reasons, admissions, frames, block, rate):
    expected = {}
    for r in reasons:
        key = (r["variant"], count(r, "role"))
        expected[key] = [count(r, k) for k in EVENT_FIELDS[:3]]
    for r in admissions:
        key = (r["variant"], count(r, "role"))
        expected[key].extend(count(r, k) for k in EVENT_FIELDS[3:])
    sums = {k: [0] * len(EVENT_FIELDS) for k in expected}
    seen = set()
    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        require(reader.fieldnames is not None and
                set(EVENT_FIELDS).issubset(reader.fieldnames), "event schema missing")
        for row in reader:
            key = (row["variant"], count(row, "role"))
            start, end = count(row, "callback_start_sample"), count(row, "callback_end_sample")
            require(key in sums and start < frames and start % block == 0 and
                    end == min(start + block, frames), "invalid event callback")
            require((key, start) not in seen, "duplicate event callback")
            seen.add((key, start))
            delta = [count(row, k) for k in EVENT_FIELDS]
            require(any(delta), "empty event")
            sums[key] = [a + b for a, b in zip(sums[key], delta)]
            require(abs(float(row["output_time_s"]) - start / rate) < 1e-6 and
                    abs(float(row["nominal_dry_source_time_s"]) -
                        (start - math.floor(rate * .024 + .5)) / rate) < 1e-6,
                    "invalid event timestamps")
            for field in ("dry_rms_l", "dry_rms_r", "mix_peak_l", "mix_peak_r",
                          "backing_rms_l", "backing_rms_r", "backing_max_step_l", "backing_max_step_r"):
                value = float(row[field])
                require(math.isfinite(value) and value >= 0, "invalid event PCM evidence")
            for field in ("mix_over_unity_l", "mix_over_unity_r"):
                require(count(row, field) <= end - start, "invalid event overload count")
    require(sums == expected, "event conservation mismatch")

def export_evidence(out, source_sha):
    import json
    import re
    import shutil
    require(re.fullmatch(r"[0-9a-f]{40}", source_sha) is not None, "invalid source SHA")
    cases = []
    # Validate all inputs before creating the export directory.
    for name in CLIPS:
        for mode in ("Manual", "Music") if name in GUIDED else ("Manual",):
            stem = pathlib.Path(name).stem + ("_music" if mode == "Music" else "")
            folder = out / stem
            rows = read_rows(folder / "qualification.csv")
            summary = {r["variant"]: r for r in rows}
            require(len(rows) == len(VARIANTS) and set(summary) == VARIANTS,
                    "variant coverage: " + stem)
            with wave.open(str(out / name), "rb") as src:
                frames, rate = src.getnframes(), src.getframerate()
            for r in rows:
                require(r["mode"] == mode and count(r, "rate") == rate and
                        count(r, "frames") == frames and count(r, "voices") == 5 and
                        count(r, "block") == 64, "fixture metadata mismatch: " + stem)
                for k in ("mix_rms", "mix_peak", "backing_rms", "right_step_over_rms",
                          "p50_us", "p99_us", "max_us", "deadline_us"):
                    x = float(r[k])
                    require(math.isfinite(x) and x >= 0, "invalid metric: " + k)
                require(0 <= float(r["p50_us"]) <= float(r["p99_us"]) <= float(r["max_us"])
                        and count(r, "measured_blocks") > 0 and
                        count(r, "over_budget") <= count(r, "measured_blocks") and
                        abs(float(r["deadline_us"]) - 1e6 * 64 / rate) < .001,
                        "invalid callback timing evidence")
                require(count(r, "high_losses") <= count(r, "total_losses"),
                        "invalid HIGH loss count")
                count(r, "over_unity")
            names = ["qualification.csv", "renderer_loss_reasons.csv", "renderer_admission.csv", "renderer_events.csv"]
            reasons, admissions = read_rows(folder / names[1]), read_rows(folder / names[2])
            validate_trace(summary, reasons, admissions)
            validate_events(folder / names[3], reasons, admissions, frames, 64, rate)
            if mode == "Music":
                names.append("guide_timeline.csv")
                validate_timeline(read_rows(folder / names[-1]), frames, 64)
            cases.append((stem, name, mode, frames, rate, names))
    evidence = out / "evidence"
    evidence.mkdir(exist_ok=False)
    files = {}
    def copy(src, rel):
        target = evidence / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, target)
        files[rel] = hashlib.sha256(target.read_bytes()).hexdigest()
    copy(out / "ATTRIBUTION.txt", "ATTRIBUTION.txt")
    for stem, name, mode, frames, rate, names in cases:
        for file in names:
            copy(out / stem / file, stem + "/" + file)
    manifest = {"schema": 1, "source_sha": source_sha, "dataset_revision": REV,
                "human_listening": "not_performed", "hardware_asio": "not_performed",
                "timing": "isolated_engine_non_gating", "audio_included": False,
                "events": "sparse_callback_intervals_not_click_detections",
                "cases": [{"id": stem, "source_blob_sha": CLIPS[name][0],
                           "mode": mode, "frames": frames, "rate": rate}
                          for stem, name, mode, frames, rate, _ in cases],
                "sha256": files}
    (evidence / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                            encoding="utf-8")
    print(f"H29_EVIDENCE_COMPLETE source_sha={source_sha} cases={len(cases)} files={len(files)} audio=false", flush=True)

def self_test():
    from copy import deepcopy
    timeline = [{"callback_start_sample": "0", "callback_end_sample": "64", "chord_root": "0"},
                {"callback_start_sample": "64", "callback_end_sample": "100", "chord_root": "5"}]
    validate_timeline(timeline, 100, 64)
    summary = {v: {"total_losses": "5", "high_losses": "1"} for v in TRACED}
    reasons = [{"variant": v, "role": str(i), "losses": "1", "loss_no_clock": "0",
                "loss_causal": "0", "loss_safe_unready": "1"}
               for v in TRACED for i in range(5)]
    admissions = [{"variant": v, "role": str(i), "attempts": "2", "missing_peak": "1",
                   "pool_blocked": "0", "support_rejected": "0", "admitted": "1",
                   "adaptive_accepted": "0", "adaptive_attempts": "0",
                   "local_accepted": "0", "local_checks": "0"}
                  for v in TRACED for i in range(5)]
    validate_trace(summary, reasons, admissions)
    def rejects(fn):
        try:
            fn()
        except RuntimeError:
            return
        raise AssertionError("corrupt evidence accepted")
    bad = deepcopy(timeline); bad[1]["callback_start_sample"] = "63"
    rejects(lambda: validate_timeline(bad, 100, 64))
    rejects(lambda: validate_trace(summary, reasons[:-1] + reasons[:1], admissions))
    bad = deepcopy(admissions); bad[0]["admitted"] = "2"
    rejects(lambda: validate_trace(summary, reasons, bad))
    bad = deepcopy(reasons); bad[0]["loss_safe_unready"] = "0"
    rejects(lambda: validate_trace(summary, bad, admissions))
    bad = deepcopy(summary); bad["H28_local"]["high_losses"] = "0"
    rejects(lambda: validate_trace(bad, reasons, admissions))
    print("H29_EVIDENCE_SELF_TEST PASS (five malformed-evidence controls)")

def main():
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: harmony_vocalset_qualification.py renderer output_dir [source_sha]")
    renderer, out = str(pathlib.Path(sys.argv[1]).resolve()), pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    (out/"ATTRIBUTION.txt").write_text(ATTRIBUTION, encoding="utf-8")
    rows = []
    for name, (sha,size) in CLIPS.items():
        src = out/name
        download(name, sha, size, src)
        folder = out/src.stem
        subprocess.run([renderer,"--qualify",str(src),str(folder),"0","0","5","64"],check=True,timeout=60)
        with (folder/"qualification.csv").open(newline="") as f:
            data = list(csv.DictReader(f))
        if len(data)!=5 or {x["variant"] for x in data}!={"live_baseline","H26","H27","H28_epoch","H28_local"}:
            raise RuntimeError("incomplete matched variants")
        for row in data:
            row = {"clip":name,"blob_sha":sha,**row}
            rows.append(row)
            print("H27_REAL " + " ".join(f"{k}={v}" for k,v in row.items()),flush=True)
        with (folder/"renderer_loss_reasons.csv").open(newline="") as f:
            reasons = list(csv.DictReader(f))
        if len(reasons) != 15 or sum(int(x["losses"]) for x in reasons if x["variant"]=="live_baseline") != int(data[0]["total_losses"]):
            raise RuntimeError("incomplete renderer loss attribution")
        for row in reasons:
            print("H27_LOSS_REASON clip="+name+" "+" ".join(f"{k}={v}" for k,v in row.items()),flush=True)
        with (folder/"renderer_admission.csv").open(newline="") as f:
            admissions = list(csv.DictReader(f))
        if len(admissions)!=15:
            raise RuntimeError("incomplete admission accounting")
        for row in admissions:
            print("H28_ADMISSION clip="+name+" "+" ".join(f"{k}={v}" for k,v in row.items()),flush=True)
        # Do not redistribute source or derivative PCM in CI artifacts.
        # Keep local WAVs available for explicit operator listening.
    with (out/"real_vocal_summary.csv").open("w",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]))
        writer.writeheader();writer.writerows(rows)
    print(f"H27_REAL_COMPLETE clips={len(CLIPS)} variants={len(rows)} no_human_rating=true",flush=True)
    guided=[]
    for name in ("f1_long_straight_a.wav","m2_long_straight_i.wav"):
        guided.extend(qualify_guide(renderer,out,name,CLIPS[name][0]))
    with (out/"guided_vocal_summary.csv").open("w",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=list(guided[0]))
        writer.writeheader();writer.writerows(guided)
    print(f"H28_GUIDED_COMPLETE clips=2 variants={len(guided)} no_human_rating=true",flush=True)
    if len(sys.argv) == 4:
        export_evidence(out, sys.argv[3])

if __name__ == "__main__":
    main()
