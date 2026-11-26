#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Matched old/new stereo PCM: strict legacy, bounded manual numeric parity."""
import array
import json
import math
from pathlib import Path
import sys

def main():
    if len(sys.argv)!=4:
        raise SystemExit("usage: compare_stereo_packed.py old.f32 new.f32 report.json")
    report=[]
    with open(sys.argv[1],"rb") as old,open(sys.argv[2],"rb") as new:
        for rate in (44100,48000,88200,96000):
            for manual in (False,True):
                for scenario in range(3):
                    n=rate*3*2
                    x,y=old.read(n*4),new.read(n*4)
                    if len(x)!=n*4 or len(y)!=n*4:
                        raise ValueError("truncated capture")
                    a,b=array.array("f"),array.array("f")
                    a.frombytes(x);b.frombytes(y)
                    if not all(math.isfinite(v) for v in a) or not all(math.isfinite(v) for v in b):
                        raise ValueError("nonfinite capture")
                    diff=[abs(u-v) for u,v in zip(a,b)]
                    peak=max(diff)
                    rms=math.sqrt(math.fsum(v*v for v in diff)/n)
                    right=b[1::2]
                    leakage=max(map(abs,right)) if scenario==2 else None
                    leakage_rms=math.sqrt(math.fsum(v*v for v in right)/len(right)) if scenario==2 else None
                    passed=(x==y if not manual else peak<=2e-6 and rms<=2e-7)
                    if scenario==2:
                        passed=passed and leakage<=1e-6 and leakage_rms<=1e-7
                    report.append(dict(rate=rate,manual=manual,scenario=scenario,peak=peak,rms=rms,
                        leakage=leakage,leakage_rms=leakage_rms,bit_exact=x==y,passed=passed))
        if old.read(1) or new.read(1):
            raise ValueError("unexpected extra capture")
    Path(sys.argv[3]).write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    print("PACKED_STEREO_SUMMARY",json.dumps(dict(cases=len(report),passed=sum(x["passed"] for x in report),
        max_peak=max(x["peak"] for x in report),max_rms=max(x["rms"] for x in report))))
    if len(report)!=24 or not all(x["passed"] for x in report):
        raise SystemExit(1)

if __name__=="__main__":
    main()
