# SPDX-License-Identifier: AGPL-3.0-only
"""Paired synthetic tonal isolation test; not a real-world listening claim."""
import csv
import json
import math
import sys
from pathlib import Path

def index(path):
    rows={}
    with open(path,newline="") as f:
        reader=csv.DictReader(f)
        if reader.fieldnames!=["rate","line","noise_amplitude","foreground_amplitude"]:
            raise ValueError("tonal schema mismatch")
        for row in reader:
            key=(int(row["rate"]),float(row["line"]))
            vals=tuple(float(row[k]) for k in ("noise_amplitude","foreground_amplitude"))
            if key in rows or any(not math.isfinite(x) or x<=1e-12 for x in vals):
                raise ValueError("duplicate or invalid tonal row")
            rows[key]=vals
    if set(rows)!={(r,f) for r in (44100,48000,88200,96000) for f in (55.,60.,100.)}:
        raise ValueError("incomplete tonal matrix")
    return rows

a,b=map(index,sys.argv[1:3])
report=[]
for (rate,line),(noise,fg) in sorted(a.items()):
    new_noise,new_fg=b[rate,line]
    cut=20*math.log10(noise/new_noise)
    foreground_delta=20*math.log10(new_fg/fg)
    passed=abs(foreground_delta)<.05 and (abs(cut)<.01 if line==55 else 1.5<cut<3.1)
    row=dict(rate=rate,line=line,additional_tonal_cut_db=cut,foreground_delta_db=foreground_delta,passed=passed)
    print("AN08A_TONAL",json.dumps(row,sort_keys=True))
    report.append(row)
Path(sys.argv[3]).write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
if not all(x["passed"] for x in report):raise SystemExit(1)
