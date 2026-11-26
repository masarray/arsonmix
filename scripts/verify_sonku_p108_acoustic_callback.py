#!/usr/bin/env python3
"""Paired fixed-buffer qualification of the exact scratch candidate.
Hosted CPU timing is not physical ASIO/xrun evidence or a release gate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
from verify_sonku_p104_externalization import ROOT, FX, FLAGS, candidate_copy

DRIVER=ROOT/"tests/sonku_p108_acoustic_callback.cpp"
SOURCES=("SonKuSpatialCore.h","SonKuP90StereoDecoder.h","SonKuP94FrontBackDepthCue.h")
def identities(folder):
    return {name:hashlib.sha256((folder/"sonku"/name).read_bytes()).hexdigest() for name in SOURCES}
def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda:f.read(1024*1024),b""):h.update(b)
    return h.hexdigest(),path.stat().st_size

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--msvc",action="store_true")
    parser.add_argument("--out",type=Path,default=ROOT/"sonku-p108-acoustic-rt")
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    baseline=identities(FX)
    rows=[];hashes=[]
    with tempfile.TemporaryDirectory(prefix="sonku-rt-") as d:
        tmp=Path(d)
        candidate_copy(tmp/"candidate")
        candidate=identities(tmp/"candidate")
        assert all(baseline[k]!=candidate[k] for k in SOURCES)
        exes={}
        for variant in ("reference","candidate"):
            inc=(FX if variant=="reference" else tmp/"candidate")/"sonku"
            exe=tmp/("bench-"+variant+(".exe" if args.msvc else ""))
            if args.msvc:
                cmd=["cl","/nologo","/std:c++20","/O2","/fp:strict","/EHsc",
                    "/I"+str(inc),"/I"+str(inc.parent),*["/D"+x+"=1" for x in FLAGS],
                    str(DRIVER),"/Fe:"+str(exe),"/Fo:"+str(tmp/(variant+".obj"))]
            else:
                cmd=["g++","-std=c++20","-O2","-ffp-contract=off","-I",str(inc),
                    "-I",str(inc.parent),*["-D"+x+"=1" for x in FLAGS],str(DRIVER),"-o",str(exe)]
            subprocess.run(cmd,cwd=tmp,check=True)
            exes[variant]=exe
        expected={}
        # Driver writes one contiguous region per brand, with the same
        # ordered rate/buffer/scenario matrix. Check exact bytes per region.
        brand_bytes=sum((((rate+b-1)//b)+1024)*b*2*4
                           for rate in (44100,48000) for b in (64,128,256))*3
        expected_bytes=brand_bytes*3
        for trial,order in enumerate((("reference","candidate"),("candidate","reference"),
                                      ("reference","candidate"),("candidate","reference"))):
            for variant in order:
                pcm=tmp/"pcm.f32"
                log=subprocess.check_output([str(exes[variant]),str(pcm)],text=True)
                h,size=digest(pcm)
                assert size==expected_bytes,(size,expected_bytes)
                with pcm.open("rb") as f:
                    for brand in (0,1,2):
                        data=f.read(brand_bytes)
                        assert len(data)==brand_bytes
                        bh=hashlib.sha256(data).hexdigest()
                        key=(variant,brand)
                        if key in expected:assert expected[key]==bh,("nondeterministic PCM",key,trial)
                        else:expected[key]=bh
                        if variant=="candidate" and brand in (1,2):
                            assert bh==expected[("reference",brand)],("protected PCM mismatch",brand)
                        hashes.append({"trial":trial,"variant":variant,"brand":brand,"sha256":bh,"floats":len(data)//4})
                    assert not f.read(1)
                pcm.unlink()
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
                assert count==54,count
                print("P108_RT_RENDER",trial,variant,size//4,h,flush=True)
    assert expected[("reference",0)]!=expected[("candidate",0)],"candidate had no PCM effect"
    summary=[]
    for brand in (0,1,2):
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
            "candidate":candidate,"baseline":baseline,"hashes":hashes,
            "rows":rows,"summary":summary,"timingPolicy":"informational hosted runner; hardware required"}
    (args.out/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    flagged=[x for x in summary if x["medianPairedDeltaPercent"]["p99"]>5]
    print("P108_RT_PASS",len(rows),"runs;",len(summary),"strata;",
          len(flagged),"strata with median paired p99 increase >5%; no hardware claim",flush=True)
    for brand in (0,1,2):
        for scenario in (0,1,2):
            group=[r for r in summary if r["brand"]==brand and r["scenario"]==scenario]
            print("P108_RT_SUMMARY",brand,scenario,
                  *[round(statistics.median(r["medianPairedDeltaPercent"][m] for r in group),3)
                    for m in ("p50","p95","p99")],flush=True)
    assert identities(FX)==baseline
if __name__=="__main__":main()
