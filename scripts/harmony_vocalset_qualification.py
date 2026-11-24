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

def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: harmony_vocalset_qualification.py renderer output_dir")
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

if __name__ == "__main__":
    main()
