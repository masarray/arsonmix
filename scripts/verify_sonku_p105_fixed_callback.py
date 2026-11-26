#!/usr/bin/env python3
"""Paired fixed-buffer qualification of the exact scratch candidate.
Hosted CPU timing is not physical ASIO/xrun evidence or a release gate.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import zipfile
from verify_sonku_p10_hotpath import ROOT, BASE, SUB, CORE, PATCH, FLAGS, blob, APPROVED_BLOB, CANDIDATE_BLOB

DRIVER=ROOT/"tests/sonku_p105_fixed_callback.cpp"
def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda:f.read(1024*1024),b""):h.update(b)
    return h.hexdigest(),path.stat().st_size

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--msvc",action="store_true")
    parser.add_argument("--out",type=Path,default=ROOT/"sonku-p105-rt")
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    assert blob(ROOT/CORE)==APPROVED_BLOB
    rows=[];hashes=[]
    with tempfile.TemporaryDirectory(prefix="sonku-rt-") as d:
        tmp=Path(d)
        archive=subprocess.check_output(["git","archive","--format=zip",BASE,SUB,
            "engine/builtin_fx/ImerSipP92Scene.h"],cwd=ROOT)
        with zipfile.ZipFile(io.BytesIO(archive)) as z:z.extractall(tmp/"reference")
        shutil.copytree(tmp/"reference",tmp/"candidate")
        subprocess.run(["git","apply","--check",str(PATCH)],cwd=tmp/"candidate",check=True)
        subprocess.run(["git","apply",str(PATCH)],cwd=tmp/"candidate",check=True)
        assert blob(tmp/"reference"/CORE)==APPROVED_BLOB
        assert blob(tmp/"candidate"/CORE)==CANDIDATE_BLOB
        exes={}
        for variant in ("reference","candidate"):
            inc=tmp/variant/SUB
            exe=tmp/(variant+(".exe" if args.msvc else ""))
            if args.msvc:
                cmd=["cl","/nologo","/std:c++20","/O2","/fp:strict","/EHsc",
                    "/I"+str(inc),"/I"+str(inc.parent),*["/D"+x+"=1" for x in FLAGS],
                    str(DRIVER),"/Fe:"+str(exe),"/Fo:"+str(tmp/(variant+".obj"))]
            else:
                cmd=["g++","-std=c++20","-O2","-ffp-contract=off","-I",str(inc),
                    "-I",str(inc.parent),*["-D"+x+"=1" for x in FLAGS],str(DRIVER),"-o",str(exe)]
            subprocess.run(cmd,cwd=tmp,check=True)
            exes[variant]=exe
        expected=None
        expected_bytes=sum((((rate+b-1)//b)+1024)*b*2*4
                           for rate in (44100,48000) for b in (64,128,256))*2*3
        for trial,order in enumerate((("reference","candidate"),("candidate","reference"),
                                      ("reference","candidate"),("candidate","reference"))):
            for variant in order:
                pcm=tmp/"pcm.f32"
                log=subprocess.check_output([str(exes[variant]),str(pcm)],text=True)
                h,size=digest(pcm);pcm.unlink()
                assert size==expected_bytes,(size,expected_bytes)
                if expected is None:expected=h
                assert h==expected,("PCM mismatch",trial,variant,h,expected)
                hashes.append({"trial":trial,"variant":variant,"sha256":h,"floats":size//4})
                count=0
                for line in log.splitlines():
                    if not line.startswith("RT,"):continue
                    _,brand,rate,block,scenario,n,p50,p95,p99,maxus,transition,missed=line.split(",")
                    brand,rate,block,scenario,n,missed=map(int,(brand,rate,block,scenario,n,missed))
                    assert n==1024
                    rows.append(dict(trial=trial,variant=variant,brand=brand,rate=rate,
                        block=block,scenario=scenario,n=n,p50=float(p50),p95=float(p95),
                        p99=float(p99),maxUs=float(maxus),first32MaxUs=float(transition),
                        deadlineMisses=missed,deadlineUs=1e6*block/rate))
                    count+=1
                assert count==36,count
                print("P105_RT_EXACT",trial,variant,size//4,h,flush=True)
    summary=[]
    for brand in (0,1):
        for rate in (44100,48000):
            for block in (64,128,256):
                for scenario in (0,1,2):
                    group=[r for r in rows if (r["brand"],r["rate"],r["block"],r["scenario"])==(brand,rate,block,scenario)]
                    assert len(group)==8
                    delta={}
                    for metric in ("p50","p95","p99","first32MaxUs"):
                        pairs=[]
                        for trial in range(4):
                            a,b=[next(r[metric] for r in group if r["trial"]==trial and r["variant"]==v)
                                 for v in ("reference","candidate")]
                            pairs.append(100*(b/a-1))
                        delta[metric]=statistics.median(pairs)
                    # Informational guard: do not equate noise with production admission.
                    summary.append(dict(brand=brand,rate=rate,block=block,scenario=scenario,
                        medianPairedDeltaPercent=delta,
                        candidateDeadlineMisses=sum(r["deadlineMisses"] for r in group if r["variant"]=="candidate"),
                        baselineDeadlineMisses=sum(r["deadlineMisses"] for r in group if r["variant"]=="reference")))
    report={"compiler":"MSVC strict" if args.msvc else "GNU no FMA contraction",
            "candidate":CANDIDATE_BLOB,"baseline":APPROVED_BLOB,"hashes":hashes,
            "rows":rows,"summary":summary,"timingPolicy":"informational hosted runner; hardware required"}
    (args.out/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    flagged=[x for x in summary if x["medianPairedDeltaPercent"]["p99"]>5]
    print("P105_RT_PASS",len(rows),"runs;",len(summary),"strata;",
          len(flagged),"strata with median paired p99 increase >5%; no hardware claim",flush=True)
    for brand in (0,1):
        for scenario in (0,1,2):
            group=[r for r in summary if r["brand"]==brand and r["scenario"]==scenario]
            print("P105_RT_SUMMARY",brand,scenario,
                  *[round(statistics.median(r["medianPairedDeltaPercent"][m] for r in group),3)
                    for m in ("p50","p95","p99")],flush=True)
    assert blob(ROOT/CORE)==APPROVED_BLOB
if __name__=="__main__":main()
