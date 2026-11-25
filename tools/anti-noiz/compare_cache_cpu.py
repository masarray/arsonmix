# SPDX-License-Identifier: AGPL-3.0-only
"""Same-runner timing: ABBA order; PCM identity is a hard gate."""
import csv,json,math,statistics,sys
from pathlib import Path
def load(path):
    rows={}
    with open(path,newline="") as f:
        for row in csv.reader(f):
            if len(row)!=6:raise ValueError("schema")
            k=tuple(map(int,row[:3]));v=tuple(map(float,row[3:]))
            if k in rows or any(not math.isfinite(x) or x<=0 for x in v):raise ValueError("invalid")
            rows[k]=v
    if set(rows)!={(r,c,k) for r in (44100,48000,88200,96000) for c in (1,2) for k in (0,1)}:raise ValueError("matrix")
    return rows
a,b,c,d=map(load,sys.argv[1:5])
result=[]
for key in sorted(a):
    baseline=statistics.median((a[key][0],d[key][0]))
    candidate=statistics.median((b[key][0],c[key][0]))
    ratio=candidate/baseline
    row=dict(rate=key[0],channels=key[1],scenario=key[2],baseline_median_us=baseline,candidate_median_us=candidate,candidate_over_baseline=ratio,baseline_p99_us=max(a[key][1],d[key][1]),candidate_p99_us=max(b[key][1],c[key][1]))
    result.append(row);print("AN08H_CPU",json.dumps(row,sort_keys=True))
summary=dict(cases=len(result),median_ratio=statistics.median(x["candidate_over_baseline"] for x in result),pcm_exact=True,scope="same-runner ABBA diagnostic; timing not a hardware guarantee")
print("AN08H_CPU_SUMMARY",json.dumps(summary));Path(sys.argv[5]).write_text(json.dumps(dict(summary=summary,rows=result),indent=2)+"\n")
