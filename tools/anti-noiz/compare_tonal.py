# SPDX-License-Identifier: AGPL-3.0-only
"""Matched grid coverage and coincident-line guard; no source separation claim."""
import csv
import json
import math
import sys
from pathlib import Path

def index(path):
    rows={}
    with open(path,newline="") as f:
        reader=csv.DictReader(f)
        if reader.fieldnames!=["rate","line","scenario","noise_amplitude","foreground_amplitude"]:
            raise ValueError("tonal schema mismatch")
        for row in reader:
            key=(int(row["rate"]),float(row["line"]),int(row["scenario"]))
            vals=tuple(float(row[k]) for k in ("noise_amplitude","foreground_amplitude"))
            if key in rows or any(not math.isfinite(x) or x<=1e-12 for x in vals):
                raise ValueError("duplicate or invalid tonal row")
            rows[key]=vals
    if set(rows)!={(r,f,s) for r in (44100,48000,88200,96000) for f in (55.,60.,73.,100.,137.5) for s in (0,1)}:
        raise ValueError("incomplete tonal matrix")
    return rows

a,b=map(index,sys.argv[1:3])
report=[]
for (rate,line,shared),(noise,fg) in sorted(a.items()):
    new_noise,new_fg=b[rate,line,shared]
    cut=20*math.log10(noise/new_noise)
    foreground_delta=20*math.log10(new_fg/fg)
    passed=abs(foreground_delta)<.01 and abs(cut)<.01
    row=dict(rate=rate,line=line,shared=bool(shared),line_cut_delta_db=cut,
             off_line_foreground_delta_db=foreground_delta,passed=passed)
    print("AN08C_SINGLE",json.dumps(row,sort_keys=True))
    report.append(row)
Path(sys.argv[3]).write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
if not all(x["passed"] for x in report):raise SystemExit(1)
