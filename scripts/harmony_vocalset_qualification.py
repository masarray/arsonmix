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
        if len(data)!=4 or {x["variant"] for x in data}!={"live_baseline","H26","H27","H28_epoch"}:
            raise RuntimeError("incomplete matched variants")
        for row in data:
            row = {"clip":name,"blob_sha":sha,**row}
            rows.append(row)
            print("H27_REAL " + " ".join(f"{k}={v}" for k,v in row.items()),flush=True)
        with (folder/"renderer_loss_reasons.csv").open(newline="") as f:
            reasons = list(csv.DictReader(f))
        if len(reasons) != 5 or sum(int(x["losses"]) for x in reasons) != int(data[0]["total_losses"]):
            raise RuntimeError("incomplete renderer loss attribution")
        for row in reasons:
            print("H27_LOSS_REASON clip="+name+" "+" ".join(f"{k}={v}" for k,v in row.items()),flush=True)
        with (folder/"renderer_admission.csv").open(newline="") as f:
            admissions = list(csv.DictReader(f))
        if len(admissions)!=10:
            raise RuntimeError("incomplete admission accounting")
        for row in admissions:
            print("H28_ADMISSION clip="+name+" "+" ".join(f"{k}={v}" for k,v in row.items()),flush=True)
        # Do not redistribute source or derivative PCM in CI artifacts.
        # Keep local WAVs available for explicit operator listening.
    with (out/"real_vocal_summary.csv").open("w",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]))
        writer.writeheader();writer.writerows(rows)
    print(f"H27_REAL_COMPLETE clips={len(CLIPS)} variants={len(rows)} no_human_rating=true",flush=True)

if __name__ == "__main__":
    main()
