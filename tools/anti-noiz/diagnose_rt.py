#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Verify actual benchmark streams; distinguish exact and independent-build contracts."""
import argparse
import csv
import json
import pathlib
import subprocess
import tempfile
from compare_rt import run

# Cross-build numerical envelope, selected after the controlled experiment
# (8839721: 2.24e-8 peak, -141.86 dB overall/-141.85 active).
# Learning remains bit-exact; active differences must satisfy BOTH limits.
PEAK_LIMIT=1e-7
RELATIVE_LIMIT_DB=-120.0

def require(ok,message):
    if not ok:
        raise RuntimeError(message)

def validate(fields,gate):
    require(int(fields[4])==0,"input mismatch")
    require(int(fields[11])==0,"settings/Learn PCM mismatch")
    if gate=="exact":
        require(int(fields[5])==0 and int(fields[6])==0 and int(fields[12])==0 and
                float(fields[8])==0 and float(fields[13])==0,"controlled exact PCM mismatch")
    elif gate=="numerical":
        require(0<=float(fields[8])<=PEAK_LIMIT and 0<=float(fields[13])<=PEAK_LIMIT and
                float(fields[10])<=RELATIVE_LIMIT_DB and float(fields[14])<=RELATIVE_LIMIT_DB,
                "independent-build numerical envelope exceeded")

def self_test():
    # Positive and negative controls for the acceptance logic, not audio evidence.
    a=["CAPTURE","48000","128","weak-auto","0","2","2","100","2e-8","1e-9","-140","0","2","2e-8","-139","1","2"]
    validate(a,"numerical")
    for index,value in ((4,"1"),(11,"1"),(8,"1.01e-7"),(10,"-119"),(13,"nan"),(14,"-119")):
        bad=a.copy();bad[index]=value
        try:
            validate(bad,"numerical")
        except RuntimeError:
            continue
        raise RuntimeError(f"gate negative control not rejected: {index}")
    try:
        validate(a,"exact")
    except RuntimeError:
        pass
    else:
        raise RuntimeError("exact gate negative control not rejected")
    exact=a.copy()
    for i in (5,6,8,9,12,13):
        exact[i]="0"
    exact[10]=exact[14]="-inf"
    validate(exact,"exact")
    print("NUMERICAL_GATE_SELF_TEST,PASS",flush=True)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("comparator")
    parser.add_argument("--gate",choices=("numerical","exact"),required=True)
    parser.add_argument("--evidence",required=True)
    args=parser.parse_args()
    self_test()
    with tempfile.TemporaryDirectory(prefix="anti-noiz-rt-") as directory:
        a,b=str(pathlib.Path(directory)/"old"),str(pathlib.Path(directory)/"new")
        x,y=run(args.old,a),run(args.new,b)
        result=subprocess.run([args.comparator,a,b],check=True,capture_output=True,text=True,timeout=90)
        seen=set()
        max_peak=0.
        worst_relative=worst_active=float("-inf")
        mismatches=0
        hashes={}
        for line in result.stdout.splitlines():
            fields=next(csv.reader([line]))
            require(len(fields)==17 and fields[0]=="CAPTURE",f"unexpected capture report {line}")
            key=(int(fields[1]),int(fields[2]),fields[3])
            require(key not in seen and key in x and key in y,f"invalid capture case {key}")
            seen.add(key)
            print(line,flush=True)
            validate(fields,args.gate)
            require(int(fields[15])==x[key][11] and int(fields[16])==y[key][11],f"capture fingerprint integrity {key}")
            require(x[key][0]==y[key][0] and not any(x[key][7:11]) and not any(y[key][7:11]),f"callback count or RT allocation {key}")
            hashes[",".join(map(str,key))]=[x[key][11],y[key][11]]
            mismatches+=int(int(fields[5])!=0)
            max_peak=max(max_peak,float(fields[8]))
            worst_relative=max(worst_relative,float(fields[10]))
            worst_active=max(worst_active,float(fields[14]))
        require(len(seen)==60,"missing capture cases")
        print(f"CAPTURE_SUMMARY,60 cases,identical_inputs=60,exact_outputs={60-mismatches},max_peak={max_peak:.12g},worst_relative_db={worst_relative:.6g},worst_active_relative_db={worst_active:.6g},hash_integrity=PASS,allocations=0",flush=True)
    if args.gate=="numerical":
        # Verify that adding capture does not change output and each independent
        # executable is bit-deterministic across a second noncapture run.
        u,v=run(args.old),run(args.new)
        for key in seen:
            require([u[key][11],v[key][11]]==hashes[",".join(map(str,key))],f"same-build repeat mismatch {key}")
            require(u[key][0]==v[key][0]==x[key][0] and not any(u[key][7:11]) and not any(v[key][7:11]),f"repeat callback count or allocation {key}")
        print("INDEPENDENT_BUILD_REPEAT,60/60 per revision,bit-identical capture vs noncapture,allocations=0",flush=True)
    with open(args.evidence,"w",encoding="utf-8") as out:
        json.dump({"gate":args.gate,"hashes":hashes},out)
    print(f"NUMERICAL_ACCEPTANCE,{args.gate},PASS",flush=True)

if __name__=="__main__":
    main()
