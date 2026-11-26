#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Measure native manual control response on pinned original raw cases."""
import argparse
import csv
import hashlib
import json
import math
import subprocess
import tempfile
from pathlib import Path

SETTINGS = {
    "unity": (0, 0, .5, .5),
    "reduction12": (12, 0, .5, .5),
    "reduction40": (40, 0, .5, .5),
    "threshold_minus12": (24, -12, .5, .5),
    "threshold_plus12": (24, 12, .5, .5),
    "artifact0": (24, 0, 0, .5),
    "artifact100": (24, 0, 1, .5),
    "protection0": (24, 0, .5, 0),
    "protection100": (24, 0, .5, 1),
}
METRICS = ("error_improvement_db", "desired_projection", "removed_projection",
           "clean_only_error_db_relative", "idle_attenuation_db")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    binary = args.binary.resolve(strict=True)
    report = {}
    for case in sorted(root.glob("*dB")):
        row = {}
        with tempfile.TemporaryDirectory() as tmp:
            for name, settings in SETTINGS.items():
                reduction, offset, artifact, protection = settings
                prefix = Path(tmp)/name
                result = subprocess.run([str(binary), *[str(case/f) for f in
                    ("clean.wav","noise.wav","learn.wav","segments.tsv","provenance.tsv")],
                    str(prefix), "voice", str(reduction), "1", "0",
                    str(offset),str(artifact),str(protection)],
                    check=True,capture_output=True,text=True,timeout=60)
                rows = {}
                for record in csv.DictReader(result.stdout.splitlines()):
                    if record["channel_minus1_all"] != "-1":
                        continue
                    values = {k:float(record[k]) for k in METRICS if record[k]}
                    if not all(math.isfinite(v) for v in values.values()):
                        raise ValueError("nonfinite metric")
                    rows[record["label"]] = values
                if not {"active","weak","idle"} <= rows.keys():
                    raise ValueError("missing segments")
                if name=="unity" and (abs(rows["active"]["error_improvement_db"])>.01 or
                                      abs(rows["idle"]["idle_attenuation_db"])>.01):
                    raise ValueError("unity control regression")
                row[name] = {"settings":settings,"segments":rows,"pcm_sha256":
                    hashlib.sha256(Path(str(prefix)+"-processed.wav").read_bytes()).hexdigest()}
        report[case.name] = row
        print("AN_PRO_02_CASE",case.name,json.dumps(row,allow_nan=False),flush=True)
    if len(report)!=12:
        raise ValueError("expected all 12 original raw cases")
    pairs = [("reduction12","reduction40"),("threshold_minus12","threshold_plus12"),
             ("artifact0","artifact100"),("protection0","protection100")]
    differences = {a+"__"+b:sum(row[a]["pcm_sha256"]!=row[b]["pcm_sha256"]
                               for row in report.values()) for a,b in pairs}
    if not all(differences.values()):
        raise ValueError("control had no effect across entire corpus")
    (root/"manual-spectral-report.json").write_text(json.dumps(
        {"scope":"native manual controls, original raw corpus, no expander, diagnostic not automatic qualification",
         "changed_pcm_case_counts":differences,"cases":report},indent=2,allow_nan=False)+"\n")
    print("AN_PRO_02_SUMMARY",json.dumps(differences),flush=True)

if __name__ == "__main__":
    main()
