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
            results["pairs"][name]={"additionalVerticalReflectionRms":round(rmsDiff,8),
                                    "candidateRms":round(rms,8),"maxPeak":round(peak,8),
                                    "directPrefixSameFrames":96}
    (directory/"SONKU-P7-SIX-SURFACE-METRICS.json").write_text(
        json.dumps(results,indent=2,sort_keys=True)+"\n")
    print("PASS P7 four exact-source 3D KEMAR six-plane A/B pairs, causal direct parity, bounded PCM")
if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("Usage: sonku_p7_six_surface_audio_audit.py DIRECTORY")
    main(sys.argv[1])
