#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compare actual benchmark streams and verify capture hashes against driver."""
import csv
import pathlib
import subprocess
import sys
import tempfile
from compare_rt import run

def main():
    old, new, comparator = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="anti-noiz-rt-") as directory:
        a, b = str(pathlib.Path(directory)/"old"), str(pathlib.Path(directory)/"new")
        x, y = run(old,a), run(new,b)
        result = subprocess.run([comparator,a,b], check=True, capture_output=True,text=True,timeout=90)
        seen=set()
        max_peak=0.
        worst_relative=float("-inf")
        mismatches=0
        for line in result.stdout.splitlines():
            fields=next(csv.reader([line]))
            if len(fields)!=17 or fields[0]!="CAPTURE":
                raise RuntimeError(f"unexpected capture report {line}")
            key=(int(fields[1]),int(fields[2]),fields[3])
            if key in seen or key not in x or key not in y:
                raise RuntimeError(f"invalid capture case {key}")
            seen.add(key)
            assert int(fields[4])==0, f"input mismatch {key}"
            assert int(fields[15])==x[key][11] and int(fields[16])==y[key][11], f"capture fingerprint integrity {key}"
            assert not any(x[key][7:11]) and not any(y[key][7:11]), f"RT allocation {key}"
            mismatches+=int(int(fields[5])!=0)
            max_peak=max(max_peak,float(fields[8]))
            worst_relative=max(worst_relative,float(fields[10]))
            print(line,flush=True)
        assert len(seen)==60
        print(f"CAPTURE_SUMMARY,60 cases,identical_inputs=60,exact_outputs={60-mismatches},max_peak={max_peak:.12g},worst_relative_db={worst_relative:.6g},hash_integrity=PASS,allocations=0",flush=True)
        # This is a diagnostic, not a relaxed numerical acceptance gate.
        # Normal compare_rt retains its strict cross-revision hash requirement.

if __name__=="__main__":
    main()
