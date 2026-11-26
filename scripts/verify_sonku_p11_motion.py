#!/usr/bin/env python3
"""Owner-guided motion-aware cue research; never edits shipped DSP."""
import argparse
import hashlib
import json
import os
import random
import subprocess
import tempfile
from pathlib import Path
from verify_sonku_p104_externalization import ROOT, FX, FLAGS, candidate_copy, replace_once, read_pcm, metrics
from verify_sonku_p105_program_material import original, measure, export, RATE
from verify_sonku_p108_acoustic_callback import identities

DRIVER=ROOT/"tests/sonku_p105_program_material.cpp"
VARIANTS=("reference","full","static")
PROTECTED=(0,*range(3,14))
EDGES=(3,5,7,9,11,12)

def static_copy(root):
    candidate_copy(root)
    p=root/"sonku/SonKuSpatialCore.h"
    p.write_text(replace_once(p.read_text(),
        "if(p99ForSonKuInsert_ && settings_.sceneVoicing==1 && settings_.spatialMix>0.f) {",
        "if(p99ForSonKuInsert_ && settings_.sceneVoicing==1 && settings_.spatialMix>0.f && !settings_.moving) {"))

def digest(data):
    return hashlib.sha256(data).hexdigest()

def require(ok,message):
    if not ok: raise ValueError(message)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out",type=Path,default=ROOT/"sonku-p11-motion")
    p.add_argument("--msvc",action="store_true")
    p.add_argument("--verify-only",action="store_true")
    a=p.parse_args()
    if a.out.exists() and any(a.out.iterdir()):p.error("output must be empty")
    a.out.mkdir(parents=True,exist_ok=True)
    baseline=identities(FX)
    key=[];results=[]
    with tempfile.TemporaryDirectory(prefix="sonku-p11-") as d:
        tmp=Path(d)
        candidate_copy(tmp/"full-src")
        static_copy(tmp/"static-src")
        folders={"reference":FX,"full":tmp/"full-src","static":tmp/"static-src"}
        sources={v:identities(folders[v]) for v in VARIANTS}
        require(sources["full"]!=sources["static"],"static candidate not distinct")
        source=tmp/"input.f32"
        original(source)
        for v in VARIANTS:
            inc=folders[v]; exe=tmp/("render-"+v+(".exe" if a.msvc else ""))
            cmd=(["cl","/nologo","/std:c++20","/O2","/fp:strict","/EHsc",
                "/I"+str(inc/"sonku"),"/I"+str(inc),"/DSONKU_P11_MOTION_AB=1",
                *["/D"+x+"=1" for x in FLAGS],str(DRIVER),"/Fe:"+str(exe),"/Fo:"+str(tmp/(v+".obj"))]
                if a.msvc else ["g++","-std=c++20","-O2","-ffp-contract=off",
                "-I",str(inc/"sonku"),"-I",str(inc),"-DSONKU_P11_MOTION_AB=1",
                *["-D"+x+"=1" for x in FLAGS],str(DRIVER),"-o",str(exe)])
            subprocess.run(cmd,cwd=tmp,check=True)
            subprocess.run([str(exe),str(source),str(tmp/v)],check=True)
        for case in range(15):
            paths={v:tmp/v/(str(case)+".f32") for v in VARIANTS}
            pcm={v:read_pcm(paths[v]) for v in VARIANTS}
            require(all(len(pcm[v][1])==12*RATE*2 and max(map(abs,pcm[v][1]))<=.981 for v in VARIANTS),("PCM bounds",case))
            same=pcm["reference"][0]==pcm["static"][0]
            if case in PROTECTED:require(same,("protected orbit/scene PCM changed",case))
            if case in (1,2):
                require(pcm["full"][0]==pcm["static"][0],("static cue no longer matches auditioned candidate",case))
                require(not same,("static cue has no effect",case))
            row=dict(case=case,staticEqualsReference=same,staticEqualsFull=pcm["static"][0]==pcm["full"][0],
                     sha256={v:digest(pcm[v][0]) for v in VARIANTS},metrics={v:metrics(pcm[v][1]) for v in VARIANTS})
            if case==14:
                require(not same and not row["staticEqualsFull"],"motion candidate has no distinct effect")
                base=pcm["reference"][1]; new=pcm["static"][1]
                row["motionEdges"]=[dict(second=s,additionalStep=max(abs((float(new[2*((s-1)*RATE)+e])-base[2*((s-1)*RATE)+e])-
                    (float(new[2*((s-1)*RATE-1)+e])-base[2*((s-1)*RATE-1)+e])) for e in (0,1))) for s in EDGES]
            if not a.verify_only and case in (1,2,14):
                pair={1:"front",2:"rear",14:"motion-transition"}[case]
                levels={v:measure(paths[v],raw=True) for v in ("reference","static")}
                target=min(-18.,*(m["input_i"] for m in levels.values()),
                           *(m["input_i"]-1.5-m["input_tp"] for m in levels.values()))
                order=["reference","static"];random.SystemRandom().shuffle(order)
                final=[]
                for label,v in zip("AB",order):
                    name=pair+"-"+label+".wav"
                    m=export(paths[v],a.out/name,target-levels[v]["input_i"])
                    require(m["input_tp"]<=-1.0,("peak",name))
                    final.append(m);key.append(dict(file=name,variant=v))
                row["matchErrorLu"]=abs(final[0]["input_i"]-final[1]["input_i"])
                require(row["matchErrorLu"]<=.1,("LUFS mismatch",case))
            results.append(row)
    require(identities(FX)==baseline,"approved sources mutated")
    report=dict(candidate="static-only P10.7 cue; original orbit",sources=sources,cases=results,
                protectedExactCases=len(PROTECTED),productionAdmission="NOT_EVALUATED",physicalASIO="NOT_RUN",
                listening="NOT_EVALUATED",material="synthetic instrumental",compiler="MSVC" if a.msvc else "GNU")
    (a.out/"report.json").write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    if key:
        (a.out/"blind-answer-key.json").write_text(json.dumps(key,indent=2)+"\n")
        (a.out/"README.txt").write_text("Research only. Listen to front, rear and motion-transition pairs before opening key. Fresh orbit uses exact baseline PCM. Live transition residual is reported, not certified inaudible. No production changes.\n")
    print("P11_MOTION_PCM_PASS",len(results),"cases",len(PROTECTED),"protected exact",flush=True)

if __name__=="__main__":main()
