#!/usr/bin/env python3
"""Private offline listening: user-owned stereo music OR original synthetic arrangement.
Measure BS.1770 via FFmpeg, apply one constant gain, remeasure final 24-bit WAV.
No compression, limiting, online service or modification of shipped DSP.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import random
import re
import subprocess
import tempfile
from verify_sonku_p104_externalization import ROOT, FX, FLAGS, candidate_copy, read_pcm, metrics

RATE=48000
DRIVER=ROOT/"tests/sonku_p105_program_material.cpp"
def run(*cmd,**kw):return subprocess.run(cmd,check=True,**kw)
def hz(n):return 440*2**((n-69)/12)
def original(path):
    # Original deterministic 4-bar pop/jazz instrumental proxy, no samples.
    # Real music supplied by the operator is preferred for final listening.
    rng=random.Random(1052026)
    chords=((57,60,64,67),(53,57,60,64),(60,64,67,71),(55,59,62,65))
    melody=(69,72,76,74,72,69,67,65,67,72,71,67,65,69,74,71)
    tau=2*math.pi;beat=.5
    pcm=array.array("f")
    for i in range(13*RATE):
        t=i/RATE; b=int(t/beat);phase=t%beat; bar=(b//4)%4
        chord=chords[bar]; env=min(1,phase/.015)*math.exp(-phase*3.5)
        kick=.09*math.exp(-phase*30)*math.sin(tau*(49*phase+2*(1-math.exp(-phase*30))))
        bass=.075*env*math.sin(tau*hz(chord[0]-24)*phase)
        eighth=t%.25
        noise=rng.uniform(-1,1)
        hats=.018*noise*math.exp(-eighth*45)
        snare=(.035*noise*math.exp(-phase*25)) if b%2 else 0
        note=hz(melody[b%len(melody)])
        lead=.055*env*(math.sin(tau*note*phase)+.27*math.sin(tau*2*note*phase)+
                        .09*math.sin(tau*3*note*phase))
        pad=[.013*math.sin(tau*hz(n)*t)+.005*math.sin(tau*(hz(n)+.35)*t) for n in chord]
        guitar=.035*math.exp(-eighth*10)*min(1,eighth/.006)*math.sin(tau*hz(chord[(b*2+int(phase/.25))%4]+12)*eighth)
        mid=kick+bass+snare+lead
        fade=min(1,t/.03,(13-t)/.03)
        pcm.extend((fade*(mid+pad[0]+pad[2]+.45*pad[1]+guitar+.7*hats),
                    fade*(mid+pad[1]+pad[3]+.45*pad[0]+.25*guitar+hats)))
    path.write_bytes(pcm.tobytes())

def measure(path,raw=False):
    opts=["-f","f32le","-ar",str(RATE),"-ac","2"] if raw else []
    p=run("ffmpeg","-hide_banner","-nostats",*opts,"-i",str(path),
        "-af","loudnorm=I=-18:TP=-1:LRA=20:print_format=json","-f","null","-",
        stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
    blocks=re.findall(r"\{[^{}]*\}",p.stderr)
    x=json.loads(blocks[-1])
    vals={k:float(x[k]) for k in ("input_i","input_tp","input_lra","input_thresh")}
    assert all(math.isfinite(v) for v in vals.values()),vals
    return vals
def export(raw,path,gain):
    run("ffmpeg","-v","error","-y","-f","f32le","-ar",str(RATE),"-ac","2",
        "-i",str(raw),"-map_metadata","-1","-af",f"volume={gain:.8f}dB",
        "-c:a","pcm_s24le",str(path))
    return measure(path)
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--input",type=Path,help="local stereo file you are authorized to use")
    p.add_argument("--start",type=float,default=0)
    p.add_argument("--out",type=Path,default=Path("sonku-p105-program"))
    args=p.parse_args()
    if not math.isfinite(args.start) or args.start<0:p.error("invalid start")
    if args.out.exists() and any(args.out.iterdir()):p.error("output directory must be empty")
    args.out.mkdir(parents=True,exist_ok=True)
    rng=random.SystemRandom() if args.input else random.Random(1052026)
    rows=[];key=[]
    with tempfile.TemporaryDirectory(prefix="sonku-program-") as d:
        tmp=Path(d);source=tmp/"input.f32"
        if args.input:
            probe=json.loads(subprocess.check_output(["ffprobe","-v","error","-select_streams","a:0",
                "-show_entries","stream=channels","-of","json",str(args.input)],text=True))
            if probe["streams"][0]["channels"]!=2:p.error("input must be stereo")
            run("ffmpeg","-v","error","-y","-ss",str(args.start),"-i",str(args.input),
                "-map","0:a:0","-t","13","-ar",str(RATE),"-ac","2","-f","f32le",str(source))
        else:original(source)
        raw,pcm=read_pcm(source)
        assert len(pcm)==13*RATE*2,"need at least 13 seconds of stereo program"
        # Use the same transparent input attenuation for both variants.
        peak=max(map(abs,pcm));assert peak>1e-6
        input_gain=min(1,.35/peak)
        if input_gain<1:
            pcm=array.array("f",(x*input_gain for x in pcm));source.write_bytes(pcm.tobytes())
        candidate_copy(tmp/"candidate")
        for name,inc in (("reference",FX),("candidate",tmp/"candidate")):
            exe=tmp/(name+"-program")
            run("g++","-std=c++20","-O2","-ffp-contract=off","-I",str(inc/"sonku"),
                "-I",str(inc),*["-D"+x+"=1" for x in FLAGS],str(DRIVER),"-o",str(exe))
            run(str(exe),str(source),str(tmp/name))
        for case in range(13):
            paths=[tmp/v/(str(case)+".f32") for v in ("reference","candidate")]
            (old,a),(new,b)=map(read_pcm,paths)
            assert len(a)==len(b)==12*RATE*2
            if case>=3:assert old==new,("protected program PCM mismatch",case)
            row={"case":case,"bitExact":old==new,
                 "baseline":metrics(a),"candidate":metrics(b),
                 "sha256":[hashlib.sha256(x).hexdigest() for x in (old,new)]}
            if case<3:
                base=[measure(x,raw=True) for x in paths]
                # Target <= -18 LUFS and never require boost or peak limiting.
                target=min(-18.,*(x["input_i"] for x in base),
                    *(x["input_i"]-1.5-x["input_tp"] for x in base))
                indices=[0,1];rng.shuffle(indices);final=[]
                for label,idx in zip("AB",indices):
                    dest=args.out/f"pair-{case+1}-{label}.wav"
                    gain=target-base[idx]["input_i"]
                    m=export(paths[idx],dest,gain)
                    assert m["input_tp"]<=-1.0,m
                    final.append(m)
                    key.append({"file":dest.name,"variant":("reference","candidate")[idx]})
                err=abs(final[0]["input_i"]-final[1]["input_i"])
                assert err<=.1,(case,err,final)
                row.update(targetLufs=target,matchErrorLu=err,blindOutputStats=final)
            rows.append(row)
        assert any(not r["bitExact"] for r in rows[:3])
        report={"source":"local stereo program" if args.input else "original synthetic instrumental",
            "decodedInputSha256":hashlib.sha256(raw).hexdigest(),"inputGainLinear":input_gain,
            "warmupSeconds":1,"auditionSeconds":12,"sampleRate":RATE,"cases":rows}
    (args.out/"metrics.json").write_text(json.dumps(report,indent=2)+"\n")
    (args.out/"blind-answer-key.json").write_text(json.dumps(key,indent=2)+"\n")
    (args.out/"README.txt").write_text(
        "Listen before opening the key or metrics. Pair 1: continuous orbit; 2: FRONT; 3: REAR.\n"
        "Same decoded stereo input; 1s warmup omitted, 12s clips, constant gain only.\n"
        "Score outside-head distance, front/back, vocal/melody body, bass, clarity and preference.\n"
        "Integrated loudness measured via FFmpeg loudnorm (BS.1770/EBU R128), <=0.1 LU mismatch.\n"
        "Original generated arrangement is not recorded music or a subjective pass.\n"
        "No production change. For private local audio, do not upload resulting files without permission.\n")
    print("P105_PROGRAM_PASS",len(rows),"cases;",
          sum(r["bitExact"] for r in rows[3:]),"protected exact;",
          max(r["matchErrorLu"] for r in rows[:3]),"max LU error;",
          max(m["input_tp"] for r in rows[:3] for m in r["blindOutputStats"]),"max dBTP",flush=True)
if __name__=="__main__":main()
