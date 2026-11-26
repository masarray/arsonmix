#!/usr/bin/env python3
"""Focused P10.12 wall/work separation. Offline only, not physical FAT."""
import argparse
import csv
import hashlib
import json
import math
import random
import statistics
import subprocess
import tempfile
from pathlib import Path
from verify_sonku_p104_externalization import ROOT, FX, FLAGS, candidate_copy
from verify_sonku_p108_acoustic_callback import identities

CASES = [(0,48000,b,s) for b in (64,128) for s in range(3)] + [
    (brand,48000,64,s) for brand in (1,2) for s in (1,2)]
N=1024
TRIALS=6
DRIVER=ROOT/"tests/sonku_p112_callback_trace.cpp"


def quantile(values,p):
    return sorted(values)[math.ceil(len(values)*p)-1]


def parse(log):
    rows=[]
    for line in log.splitlines():
        tag,i,wall,work,over_wall,over_work=line.split(",")
        if tag!="TRACE" or int(i)!=len(rows):
            raise ValueError("Missing, unordered or unknown callback trace")
        row=(float(wall),int(work),float(over_wall),int(over_work))
        if not all(math.isfinite(x) and x>=0 for x in row) or row[0]<=0:
            raise ValueError("Invalid timer sample")
        rows.append(row)
    if len(rows)!=N:
        raise ValueError("Incomplete callback trace")
    return rows


def summarize(rows,budget):
    wall,work,ow,oc=zip(*rows)
    if statistics.median(work)<=0 or quantile(work,.99)<=0:
        raise ValueError("Thread timer has insufficient resolution")
    return dict(wallP50=quantile(wall,.5),wallP99=quantile(wall,.99),
                wallMax=max(wall),first32WallMax=max(wall[:32]),
                workP50=quantile(work,.5),workP99=quantile(work,.99),
                workMax=max(work),clockWallP50=quantile(ow,.5),
                clockWorkP50=quantile(oc,.5),deadlineMisses=sum(x>=budget for x in wall),
                missIndices=[i for i,x in enumerate(wall) if x>=budget])


def compare(rows):
    if len(rows)!=TRIALS*len(CASES)*2:
        raise ValueError("Incomplete paired matrix")
    index={(r["trial"],tuple(r["case"]),r["variant"]):r for r in rows}
    if len(index)!=len(rows):
        raise ValueError("Duplicate paired row")
    results=[]
    for case in CASES:
        delta={}
        for metric in ("wallP99","workP99","workP50"):
            delta[metric]=statistics.median(100*(index[t,case,"candidate"][metric]/
                                      index[t,case,"reference"][metric]-1) for t in range(TRIALS))
        misses={v:sum(index[t,case,v]["deadlineMisses"] for t in range(TRIALS))
                for v in ("reference","candidate")}
        results.append(dict(case=case,medianPairedDeltaPercent=delta,deadlineMisses=misses))
    flags=[r for r in results if r["medianPairedDeltaPercent"]["wallP99"]>5
           or r["medianPairedDeltaPercent"]["workP99"]>5 or sum(r["deadlineMisses"].values())]
    return results, "HOLD" if flags else "NO_HOSTED_TIMING_FLAGS"


def self_test():
    trace="\n".join(f"TRACE,{i},100,1000,1,10" for i in range(N))
    rows=parse(trace)
    assert summarize(rows,1333)["deadlineMisses"]==0
    rows[5]=(1500,1000,1,10)
    assert summarize(rows,1333)["missIndices"]==[5]
    for bad in (trace+"\nTRACE,0,1,1,1,1",trace.replace("TRACE,0,100","TRACE,0,nan",1),trace.split("\n",1)[1]):
        try: parse(bad)
        except ValueError: pass
        else: raise AssertionError("Malformed trace accepted")
    matrix=[dict(trial=t,case=c,variant=v,wallP99=100,workP99=1000,workP50=900,deadlineMisses=0)
            for t in range(TRIALS) for c in CASES for v in ("reference","candidate")]
    assert compare(matrix)[1]=="NO_HOSTED_TIMING_FLAGS"
    for row in matrix:
        if row["variant"]=="candidate":row["workP99"]=1100
    assert compare(matrix)[1]=="HOLD"
    print("P112_SELF_TEST_PASS")


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--msvc",action="store_true")
    p.add_argument("--self-test",action="store_true")
    p.add_argument("--out",type=Path,default=ROOT/"sonku-p112-trace")
    a=p.parse_args()
    self_test()
    if a.self_test:return
    a.out.mkdir(parents=True,exist_ok=True)
    baseline=identities(FX)
    rows=[];hashes={}
    with tempfile.TemporaryDirectory(prefix="sonku-p112-") as d:
        tmp=Path(d)
        candidate_copy(tmp/"candidate")
        candidate=identities(tmp/"candidate")
        assert all(baseline[k]!=candidate[k] for k in baseline)
        exes={}
        for v,folder in (("reference",FX),("candidate",tmp/"candidate")):
            exe=tmp/("bench-"+v+(".exe" if a.msvc else ""))
            inc=folder/"sonku"
            cmd=(["cl","/nologo","/std:c++20","/O2","/fp:strict","/EHsc","/I"+str(inc),
                  "/I"+str(folder),*["/D"+f+"=1" for f in FLAGS],str(DRIVER),"/Fe:"+str(exe),
                  "/Fo:"+str(tmp/(v+".obj"))] if a.msvc else
                 ["g++","-std=c++20","-O2","-ffp-contract=off","-I",str(inc),"-I",str(folder),
                  *["-D"+f+"=1" for f in FLAGS],str(DRIVER),"-o",str(exe)])
            subprocess.run(cmd,cwd=tmp,check=True)
            exes[v]=exe
        rng=random.Random(1122026)
        with (a.out/"callbacks.csv").open("w",newline="",encoding="utf-8") as f:
            writer=csv.writer(f)
            writer.writerow(["trial","brand","rate","block","scenario","variant","callback","wall_us","thread_work","clock_wall_us","clock_work"])
            for trial in range(TRIALS):
                cases=list(CASES);rng.shuffle(cases)
                for case in cases:
                    brand,rate,block,scenario=case
                    # Alternate A/B within each stratum, not whole matrices.
                    order=("reference","candidate") if trial%2==0 else ("candidate","reference")
                    for v in order:
                        pcm=tmp/"pcm.f32"
                        trace=parse(subprocess.check_output([str(exes[v]),str(pcm),*map(str,case)],text=True))
                        data=pcm.read_bytes();pcm.unlink()
                        assert len(data)==((rate+block-1)//block+N)*block*8
                        h=hashlib.sha256(data).hexdigest()
                        key=(case,v)
                        assert hashes.setdefault(key,h)==h,("nondeterministic PCM",key)
                        for i,row in enumerate(trace):writer.writerow([trial,*case,v,i,*row])
                        rows.append(dict(trial=trial,case=case,variant=v,sha256=h,**summarize(trace,1e6*block/rate)))
                    if brand:assert hashes[case,"reference"]==hashes[case,"candidate"],("protected PCM",case)
                    print("P112_PAIR",trial,case,flush=True)
        assert any(hashes[c,"reference"]!=hashes[c,"candidate"] for c in CASES if c[0]==0)
    assert identities(FX)==baseline
    summary,disposition=compare(rows)
    report=dict(baseline=baseline,candidate=candidate,workUnit="thread_cycles" if a.msvc else "thread_cpu_ns",
                rows=rows,summary=summary,timingDisposition=disposition,productionAdmission="NOT_EVALUATED",
                physicalASIO="NOT_RUN",listening="NOT_EVALUATED",
                caveat="Timer overhead included. Windows cycle counts are not CPU elapsed time or proof of scheduler causality.")
    (a.out/"report.json").write_text(json.dumps(report,indent=2,allow_nan=False)+"\n",encoding="utf-8")
    print("P112_NUMERICAL_PASS",len(rows)*N,"callbacks",disposition,flush=True)


if __name__=="__main__":main()
