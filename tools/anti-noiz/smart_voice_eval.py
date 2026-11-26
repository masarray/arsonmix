#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Paired native manual/Smart Voice gate response on original raw wind recordings."""
import argparse
import csv
import hashlib
import json
import math
import subprocess
import tempfile
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    binary = args.binary.resolve(strict=True)
    report = {}
    for case in sorted(root.glob("*dB")):
        pairs = {}
        with tempfile.TemporaryDirectory() as tmp:
            for threshold,hp in [(-55,120),(-40,250)]:
                pair = {}
                for smart in (0,1):
                    prefix = Path(tmp)/f"{threshold}_{hp}_{smart}"
                    result = subprocess.run([str(binary), *[str(case/f) for f in
                        ("clean.wav","noise.wav","learn.wav","segments.tsv","provenance.tsv")],
                        str(prefix),"voice","0","1","0",str(threshold),str(hp),"9","12",str(smart)],
                        capture_output=True,text=True,check=True,timeout=60)
                    meta = [l.split() for l in result.stderr.splitlines() if l.startswith("SMART_GATE ")]
                    if len(meta)!=1 or meta[0][1]!="1":
                        raise ValueError("missing qualified detector reference")
                    noise,actual = map(float,meta[0][2:])
                    if not all(math.isfinite(x) and x>0 for x in (noise,actual)):
                        raise ValueError("invalid detector telemetry")
                    actual_db = 20*math.log10(actual)
                    if not threshold-.001<=actual_db<=threshold+12*smart+.001:
                        raise ValueError("threshold authority exceeded")
                    segments = {}
                    for r in csv.DictReader(result.stdout.splitlines()):
                        if r["channel_minus1_all"]!="-1": continue
                        fields = ("error_improvement_db","desired_projection","removed_projection",
                                  "clean_only_error_db_relative","idle_attenuation_db")
                        metrics = {k:float(r[k]) for k in fields if r[k]}
                        if not all(math.isfinite(v) for v in metrics.values()):
                            raise ValueError("nonfinite metrics")
                        segments[r["label"]] = metrics
                    if not {"active","weak","idle"}<=segments.keys():
                        raise ValueError("missing segments")
                    pair["smart" if smart else "manual"]={"effective_threshold_db":actual_db,
                        "learned_peak_db":20*math.log10(noise),"segments":segments,
                        "pcm_sha256":hashlib.sha256(Path(str(prefix)+"-processed.wav").read_bytes()).hexdigest()}
                if pair["manual"]["learned_peak_db"]!=pair["smart"]["learned_peak_db"]:
                    raise ValueError("learned sidechain depends on mode")
                pairs[f"{threshold}dB_{hp}Hz"]=pair
        report[case.name]=pairs
        print("AN_PRO_03_CASE",case.name,json.dumps(pairs,allow_nan=False),flush=True)
    if len(report)!=12: raise ValueError("expected 12 pinned raw cases")
    pairs=[p for c in report.values() for p in c.values()]
    summary={"pairs":len(pairs),
        "threshold_lifted":sum(p["smart"]["effective_threshold_db"]>p["manual"]["effective_threshold_db"]+.001 for p in pairs),
        "pcm_changed":sum(p["smart"]["pcm_sha256"]!=p["manual"]["pcm_sha256"] for p in pairs),
        "idle_improved":sum(p["smart"]["segments"]["idle"]["idle_attenuation_db"]>p["manual"]["segments"]["idle"]["idle_attenuation_db"]+.01 for p in pairs),
        "weak_projection_decreased":sum(p["smart"]["segments"]["weak"]["desired_projection"]<p["manual"]["segments"]["weak"]["desired_projection"]-.01 for p in pairs)}
    (root/"smart-voice-report.json").write_text(json.dumps({"scope":"paired native gate response, fixed settings, no spectral reduction; not qualification", "summary":summary,"cases":report},indent=2,allow_nan=False)+"\n")
    print("AN_PRO_03_SUMMARY",json.dumps(summary),flush=True)

if __name__=="__main__":
    main()
