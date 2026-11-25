# SPDX-License-Identifier: AGPL-3.0-only
"""Paired harmonic-stack regression: shared line is composite, not separated."""
import csv,json,math,sys
from pathlib import Path

def index(path):
    out={}
    with open(path,newline="") as f:
        reader=csv.DictReader(f)
        if reader.fieldnames!=["rate","base","shared","line","amplitude","foreground"]:
            raise ValueError("schema")
        for row in reader:
            key=tuple(int(row[k]) for k in ("rate","base","shared","line"))
            values=tuple(float(row[k]) for k in ("amplitude","foreground"))
            if key in out or any(not math.isfinite(v) or v<=1e-12 for v in values):
                raise ValueError("invalid row")
            out[key]=values
    if set(out)!={(r,f,s,j) for r in (44100,48000,88200,96000) for f in (50,60) for s in (0,1) for j in (1,2,3)}:
        raise ValueError("incomplete harmonic matrix")
    return out

a,b=map(index,sys.argv[1:3])
report=[]
for (rate,base,shared,line),(old,fg) in sorted(a.items()):
    new,new_fg=b[rate,base,shared,line]
    cut=20*math.log10(old/new)
    change=20*math.log10(new_fg/fg)
    # Primary parity, independent suppression of additional lines, no
    # additional strong same-line cut, off-line foreground parity.
    passed=abs(change)<.05 and (abs(cut)<.1 if line==1 or (shared and line==2) else 1.5<cut<3.3)
    row=dict(rate=rate,base=base,shared=bool(shared),harmonic=line,extra_cut_db=cut,foreground_delta_db=change,passed=passed)
    print("AN08C_HARMONIC",json.dumps(row,sort_keys=True))
    report.append(row)
Path(sys.argv[3]).write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
if not all(x["passed"] for x in report):raise SystemExit(1)
