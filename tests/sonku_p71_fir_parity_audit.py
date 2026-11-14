#!/usr/bin/env python3
"""P7.1: compare every PCM sample from original P7 reference vs optimized FIR.
The same original C++ wav renderer is compiled once with each macro set.
Do not use duration-only heuristic: compare bit depth and 1-LSB worst case.
"""
import json
import pathlib
import struct
import sys
import wave

def read(path):
    with wave.open(str(path),'rb') as w:
        shape=(w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes())
        assert shape==(2,2,48000,144000),(path,shape)
        pcm=w.readframes(w.getnframes())
    return struct.unpack('<'+'h'*(len(pcm)//2),pcm)

def main(original,optimized):
    original,optimized=pathlib.Path(original),pathlib.Path(optimized)
    evidence={}
    for side in ('front','rear'):
        for spread in (0,60):
            name=f'{side}-spread{spread}'
            for profile in ('four-wall','six-surface'):
                filename=f'sonku-p7-{profile}-{name}.wav'
                a=read(original/filename);b=read(optimized/filename)
                differences=[abs(v-w) for v,w in zip(a,b)]
                peak=max(differences)
                changed=sum(x!=0 for x in differences)
                if peak>1:
                    raise AssertionError(f'FIR PCM regression: {filename}, max {peak} LSB')
                evidence[filename]={'maximumSampleDifferenceLsb':peak,
                                    'nonidenticalSamples':changed}
    dest=original/'SONKU-P71-FIR-P7-REFERENCE-PARITY.json'
    dest.write_text(json.dumps(evidence,indent=2,sort_keys=True)+'\n')
    print('PASS P7.1 FIR optimized 8 reference WAVs across all PCM: max 1 LSB, 48k FRONT/REAR spread 0/60')
if __name__=='__main__':
    if len(sys.argv)!=3:raise SystemExit('Usage: sonku_p71_fir_parity_audit.py REF_DIR FAST_DIR')
    main(sys.argv[1],sys.argv[2])
