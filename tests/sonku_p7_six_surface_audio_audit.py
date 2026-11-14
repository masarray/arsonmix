#!/usr/bin/env python3
"""P7 same-input P6 four-wall versus measured six-boundary research PCM."""
import json
import math
import pathlib
import struct
import sys
import wave

def read(path):
    with wave.open(str(path), "rb") as wav:
        shape = wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()
        if shape != (2,2,48000,144000):
            raise AssertionError(f"invalid 48k 3s stereo fixture {path}: {shape}")
        pcm = wav.readframes(wav.getnframes())
    return struct.unpack("<" + "h"*(len(pcm)//2),pcm)

# Exact-bin 1-second steady-interval harmonic evidence, no external numpy.
# Goertzel response is O(N) per frequency, only in offline CI, never PCM DSP.
def harmonics(pcm,ear,freq,start=48000,count=48000):
    phase=2*math.pi*freq/48000.0
    factor=2*math.cos(phase)
    q1=q2=0.0
    for i in range(start,start+count):
        q0=(pcm[2*i+ear]/32768.0)+factor*q1-q2
        q2=q1
        q1=q0
    re=q1-q2*math.cos(phase)
    im=q2*math.sin(phase)
    amplitude=2*math.hypot(re,im)/count
    return amplitude, math.atan2(im,re)

def wrap_deg(x):
    return (x+180.0)%360.0-180.0

def main(directory):
    directory=pathlib.Path(directory)
    results={"method":"same-source native P6 directional four-wall versus P7 3D KEMAR floor/ceiling",
             "release":"EXPERIMENTAL, not listening/dearVR or Windows ASIO proof",
             "pairs":{}}
    for side in ("front","rear"):
        for spread in (0,60):
            name=f"{side}-spread{spread}"
            old=read(directory/f"sonku-p7-four-wall-{name}.wav")
            new=read(directory/f"sonku-p7-six-surface-{name}.wav")
            # Both scenes have the same direct HRIR and existing walls.
            # First 96 frames cannot contain a causal wall/floor arrival.
            assert old[:96*2]==new[:96*2],f"early direct changed: {name}"
            delta=[(b-a)/32768.0 for a,b in zip(old[2500*2:],new[2500*2:])]
            rmsDiff=math.sqrt(sum(d*d for d in delta)/len(delta))
            rms=math.sqrt(sum((b/32768.)**2 for b in new)/len(new))
            peak=max(max(abs(v) for v in new),max(abs(v) for v in old))/32768.0
            assert all(map(math.isfinite,(rmsDiff,rms,peak)))
            assert rmsDiff>1e-6 and 1e-4<rms<.95 and peak<.95,(name,rmsDiff,rms,peak)
            # Musical-color and ITD/ILD trade-off evidence at the ORIGINAL
            # synthesized instrument frequencies. This records, but DOES NOT
            # filter out, potentially colored reflection candidates.
            harmonics_by_freq={}
            review=[]
            for freq in (220,660,940,3200,4700):
                old_lr=[harmonics(old,e,freq) for e in (0,1)]
                new_lr=[harmonics(new,e,freq) for e in (0,1)]
                db_per_ear=[]
                for ear in (0,1):
                    a=old_lr[ear][0]
                    b=new_lr[ear][0]
                    if a<1e-8 or b<1e-8:
                        db_per_ear.append(None)
                    else:
                        delta_db=20*math.log10(b/a)
                        db_per_ear.append(round(delta_db,5))
                        if abs(delta_db)>=1.5:
                            review.append(f"{freq}Hz-ear{ear+1}: {delta_db:+.2f}dB")
                old_ild=20*math.log10(max(old_lr[0][0],1e-8)/
                                       max(old_lr[1][0],1e-8))
                new_ild=20*math.log10(max(new_lr[0][0],1e-8)/
                                       max(new_lr[1][0],1e-8))
                old_phase=wrap_deg((old_lr[0][1]-old_lr[1][1])*180/math.pi)
                new_phase=wrap_deg((new_lr[0][1]-new_lr[1][1])*180/math.pi)
                harmonics_by_freq[str(freq)]={
                    "earLevelChangeDb":db_per_ear,
                    "interauralLevelChangeDb":round(new_ild-old_ild,5),
                    "interauralPhaseChangeDeg":round(wrap_deg(new_phase-old_phase),4)}
            results["pairs"][name]={"additionalVerticalReflectionRms":round(rmsDiff,8),
                                    "candidateRms":round(rms,8),"maxPeak":round(peak,8),
                                    "directPrefixSameFrames":96,
                                    "steadyInstrumentHz":harmonics_by_freq,
                                    "tonalityReviewFlags":review}
    (directory/"SONKU-P7-SIX-SURFACE-METRICS.json").write_text(
        json.dumps(results,indent=2,sort_keys=True)+"\n")
    print("PASS P7 four exact-source 3D KEMAR six-plane A/B pairs, causal direct parity, bounded PCM")
if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: sonku_p7_six_surface_audio_audit.py DIRECTORY")
    main(sys.argv[1])
