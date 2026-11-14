#!/usr/bin/env python3
"""Exact-engine P3 140-tap directional reflection equivalence test."""
import array
import json
import math
import sys
import wave
from pathlib import Path

RATE=48000
FRAMES=RATE*2
HARMONICS=(165,660,2200,4700)

def read(path):
    with wave.open(str(path),"rb") as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate(),
                f.getnframes())==(2,2,RATE,FRAMES),path
        payload=f.readframes(FRAMES)
    a=array.array("h")
    a.frombytes(payload)
    if sys.byteorder!="little":a.byteswap()
    return a

def amplitude(a,ear,hz):
    first,count=RATE//2,RATE
    re=im=0.0
    for i in range(first,first+count):
        phase=2*math.pi*hz*i/RATE
        x=a[2*i+ear]/32768.
        re+=x*math.cos(phase)
        im-=x*math.sin(phase)
    return 2*math.hypot(re,im)/count

def run(folder):
    report={"method":"same source native P2 versus P3 zero spread",
            "caveat":"not a listening or Windows ASIO proof",
            "scenes":{}}
    for direction in ("front","rear"):
        original=read(folder/f"sonku-p3-reference-{direction}.wav")
        fused=read(folder/f"sonku-p3-fused-{direction}.wav")
        differences=[a-b for a,b in zip(original,fused)]
        peak=max(map(abs,differences))
        rms=math.sqrt(sum(d*d for d in differences)/len(differences))/32768.
        assert peak<=2,(direction,"peak more than 2 PCM LSB",peak)
        assert rms<2e-5,(direction,"RMS drift",rms)
        harmonics={}
        for hz in HARMONICS:
            a=[amplitude(original,ear,hz) for ear in (0,1)]
            b=[amplitude(fused,ear,hz) for ear in (0,1)]
            assert min(a)>1e-7 and min(b)>1e-7
            delta=[20*math.log10(y/x) for x,y in zip(a,b)]
            assert max(map(abs,delta))<.05,(direction,hz,delta)
            harmonics[str(hz)]=delta
        report["scenes"][direction]={
            "maxSampleDifferenceLSB":peak,
            "differenceRms":rms,
            "harmonicChangeDb":harmonics}
    (folder/"SONKU-P3-PCM-EQUIVALENCE.json").write_text(
        json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    print("PASS P3 exact native PCM and harmonic equivalence",json.dumps(report))

if __name__=="__main__":
    run(Path(sys.argv[1]))
