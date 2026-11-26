#!/usr/bin/env python3
"""P12: original P99, only existing Signature late-mid relief varied.
Never modify shipping source; optional authorized local 31s stereo recording.
"""
import argparse
import array
import csv
import hashlib
import json
import math
import random
import shutil
import subprocess
import tempfile
from pathlib import Path
from verify_sonku_p104_externalization import ROOT, FX, FLAGS, replace_once, read_pcm, metrics
from verify_sonku_p105_program_material import original, measure, export, RATE

DRIVER=ROOT/"tests/sonku_p12_musical.cpp"
CASES=("continuous-orbit","front","rear","orbit-static")
DURATION=30
PROFILE="sonku/SonKuP99MusicalVocalDepth.h"

def require(ok, message):
    if not ok: raise ValueError(message)

def source_identities(folder):
    paths=sorted((folder/"sonku").rglob("*.h"))+[folder/"ImerSipP92Scene.h"]
    return {p.relative_to(folder).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}

def musical_copy(root):
    # One controlled parameter; no extra filters, branches, taps or renderer.
    shutil.copytree(FX/"sonku",root/"sonku")
    shutil.copy2(FX/"ImerSipP92Scene.h",root)
    p=root/PROFILE
    p.write_text(replace_once(p.read_text(),
        "{{.112f,.200f,.105f,.108f,.55f,.25f}}",
        "{{.112f,.200f,.105f,.135f,.55f,.25f}}"))
    before,after=source_identities(FX),source_identities(root)
    require(before.keys()==after.keys(),"source inventory changed")
    require([k for k in before if before[k]!=after[k]]==[PROFILE],"unexpected acoustic source change")

def geometry(path):
    rows=list(csv.DictReader(path.open(encoding="utf-8",newline="")))
    result={}
    for row in rows:
        c,t,a=int(row["case"]),int(row["second"]),float(row["azimuth"])
        require(math.isfinite(a) and (c,t) not in result,"invalid geometry")
        result[c,t]=a
    expected={(c,t) for c in range(16) for t in range((30 if c<4 else 6)+1)}
    require(set(result)==expected,"incomplete geometry")
    def step(a,b):return (b-a+180)%360-180
    delta=[step(result[0,t],result[0,t+1]) for t in range(30)]
    require(all(abs(abs(x)-30)<.1 for x in delta) and abs(sum(delta))>=899,
            ("continuous orbit did not complete 2.5 revolutions",delta))
    require(all(abs(step(result[3,t],result[3,t+1]))<.1 for t in range(12,20)),
            "static period still moves")
    require(all(abs(abs(step(result[3,t],result[3,t+1]))-30)<.1
                for t in list(range(12))+list(range(20,30))),"orbit resume missing")
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out",type=Path,default=ROOT/"sonku-p12-musical")
    p.add_argument("--input",type=Path,help="authorized stereo recording, decoded locally only")
    p.add_argument("--start",type=float,default=0)
    p.add_argument("--msvc",action="store_true")
    p.add_argument("--verify-only",action="store_true")
    a=p.parse_args()
    require(math.isfinite(a.start) and a.start>=0,"invalid start")
    require(not a.out.exists() or not any(a.out.iterdir()),"output must be empty")
    a.out.mkdir(parents=True,exist_ok=True)
    baseline=source_identities(FX)
    report=[];key=[]
    with tempfile.TemporaryDirectory(prefix="sonku-p12-") as d:
        tmp=Path(d);source=tmp/"input.f32"
        if a.input:
            probe=json.loads(subprocess.check_output(["ffprobe","-v","error","-select_streams","a:0",
                "-show_entries","stream=channels","-of","json",str(a.input)],text=True))
            require(probe["streams"][0]["channels"]==2,"input must be stereo")
            subprocess.run(["ffmpeg","-v","error","-ss",str(a.start),"-i",str(a.input),
                "-map","0:a:0","-t","31","-ar",str(RATE),"-ac","2","-f","f32le",str(source)],check=True)
        else:original(source,31)
        _,pcm=read_pcm(source)
        require(len(pcm)==31*RATE*2,"need 31 seconds of stereo audio")
        peak=max(map(abs,pcm));require(peak>1e-6,"silent source")
        gain=min(1.,.35/peak)
        if gain<1:source.write_bytes(array.array("f",(x*gain for x in pcm)).tobytes())
        input_hash=hashlib.sha256(source.read_bytes()).hexdigest()
        musical_copy(tmp/"candidate-src")
        identities={"reference":baseline,"candidate":source_identities(tmp/"candidate-src")}
        for v,inc in (("reference",FX),("candidate",tmp/"candidate-src")):
            exe=tmp/("render-"+v+(".exe" if a.msvc else ""))
            cmd=(["cl","/nologo","/std:c++20","/O2","/fp:strict","/EHsc",
                "/I"+str(inc/"sonku"),"/I"+str(inc),*["/D"+x+"=1" for x in FLAGS],
                str(DRIVER),"/Fe:"+str(exe),"/Fo:"+str(tmp/(v+".obj"))] if a.msvc else
                ["g++","-std=c++20","-O2","-ffp-contract=off","-I",str(inc/"sonku"),"-I",str(inc),
                *["-D"+x+"=1" for x in FLAGS],str(DRIVER),"-o",str(exe)])
            subprocess.run(cmd,cwd=tmp,check=True)
            subprocess.run([str(exe),str(source),str(tmp/v)],check=True)
        g={v:geometry(tmp/v/"geometry.csv") for v in ("reference","candidate")}
        require(g["reference"]==g["candidate"],"world trajectory changed")
        shutil.copy2(tmp/"reference/geometry.csv",a.out/"geometry.csv")
        for c in range(16):
            paths={v:tmp/v/(str(c)+".f32") for v in ("reference","candidate")}
            data={v:read_pcm(path) for v,path in paths.items()}
            duration=30 if c<4 else 6
            require(all(len(x[1])==duration*RATE*2 and max(map(abs,x[1]))<=.981 for x in data.values()),("PCM bounds",c))
            same=data["reference"][0]==data["candidate"][0]
            require(same==(c>=4),("protected PCM or missing effect",c))
            row=dict(case=c,duration=duration,exact=same,sha256={v:hashlib.sha256(x[0]).hexdigest() for v,x in data.items()},
                     metrics={v:metrics(x[1]) for v,x in data.items()})
            if c<4:
                ref,new=(data[v][1] for v in ("reference","candidate"))
                energy=sum(float(x)*x for x in ref)
                residual=sum((float(x)-y)**2 for x,y in zip(ref,new))
                row["residualDb"]=10*math.log10(max(residual,1e-30)/max(energy,1e-30))
                require(row["residualDb"]<=-20,("excessive signal modification",c,row["residualDb"]))
                if not a.verify_only:
                    levels={v:measure(path,raw=True) for v,path in paths.items()}
                    target=min(-18.,*(m["input_i"] for m in levels.values()),
                               *(m["input_i"]-1.5-m["input_tp"] for m in levels.values()))
                    order=["reference","candidate"];random.SystemRandom().shuffle(order)
                    final=[]
                    for label,v in zip("AB",order):
                        filename=CASES[c]+"-"+label+".wav"
                        m=export(paths[v],a.out/filename,target-levels[v]["input_i"])
                        require(m["input_tp"]<=-1.,("true peak",filename))
                        final.append(m);key.append(dict(file=filename,variant=v))
                    row["matchErrorLu"]=abs(final[0]["input_i"]-final[1]["input_i"])
                    require(row["matchErrorLu"]<=.1,("loudness mismatch",c))
            report.append(row)
    require(source_identities(FX)==baseline,"approved source mutated")
    result=dict(candidate="P12_SIGNATURE_LATE_MID_RELIEF_25_PERCENT",sources=identities,
        inputSha256=input_hash,inputGain=gain,material="authorized local stereo recording" if a.input else "synthetic instrumental",
        compiler="MSVC" if a.msvc else "GNU",cases=report,protectedExactCases=12,geometryExact=True,
        productionAdmission="NOT_EVALUATED",physicalASIO="NOT_RUN",listening="NOT_EVALUATED")
    (a.out/"report.json").write_text(json.dumps(result,indent=2,allow_nan=False)+"\n")
    if key:
        (a.out/"blind-answer-key.json").write_text(json.dumps(key,indent=2)+"\n")
        (a.out/"README.txt").write_text("P12 research only: four 30s A/B pairs, original P99 vs one late-mid parameter. Continuous orbit 2.5 turns; transition moves 0-12s, static 12-20s, resumes 20-30s. No key until listening. Synthetic material unless authorized local input used.\n")
    print("P12_MUSICAL_PCM_PASS",len(report),"cases, 12 protected exact, geometry exact",flush=True)

if __name__=="__main__":main()
