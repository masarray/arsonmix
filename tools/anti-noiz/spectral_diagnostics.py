# SPDX-License-Identifier: AGPL-3.0-only
"""Offline diagnostic split at 500 Hz, not a brick-wall or perceptual filter."""
import array
import math
import struct
import sys
from pathlib import Path

def pcm(path, expected_rate, expected_channels):
    data = Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("invalid RIFF")
    fmt = raw = None
    pos = 12
    while pos+8 <= len(data):
        tag, size = struct.unpack_from("<4sI", data, pos)
        start = pos+8
        if start+size > len(data): raise ValueError("truncated WAV")
        if tag == b"fmt ": fmt = struct.unpack_from("<HHIIHH",data,start)
        if tag == b"data": raw = data[start:start+size]
        pos = start+size+(size&1)
    if fmt is None or raw is None: raise ValueError("missing PCM")
    kind,ch,rate,_,align,bits = fmt
    if (rate,ch)!=(expected_rate,expected_channels) or align!=ch*bits//8:
        raise ValueError("format mismatch")
    if (kind,bits)==(1,16): values=array.array("h",raw); scale=32768.
    elif (kind,bits)==(3,32): values=array.array("f",raw); scale=1.
    else: raise ValueError("unsupported diagnostic PCM")
    if sys.byteorder!="little": values.byteswap()
    return [v/scale for v in values]

def metrics(clean, mixture, output, clean_only, rate, channels, begin, end):
    if not len(clean)==len(mixture)==len(output)==len(clean_only) or not 0<=begin<end<=len(clean)//channels:
        raise ValueError("timeline mismatch")
    alpha=1-math.exp(-2*math.pi*500/rate)
    acc=[[0.]*5 for _ in range(2)]
    for ch in range(channels):
        state=[0.]*4
        for f in range(end):
            j=f*channels+ch
            a,b,c,d=clean[j],mixture[j],output[j],clean_only[j]
            state[0]+=alpha*(a-state[0]);state[1]+=alpha*(b-state[1])
            state[2]+=alpha*(c-state[2]);state[3]+=alpha*(d-state[3])
            if f<begin: continue
            for k,(x,u,y,z) in enumerate((state,(a-state[0],b-state[1],c-state[2],d-state[3]))):
                m=acc[k]
                m[0]+=x*x;m[1]+=(u-x)**2;m[2]+=(y-x)**2;m[3]+=x*y;m[4]+=(z-x)**2
    def db(a,b): return 10*math.log10(max(a,1e-24)/max(b,1e-24))
    result=[]
    for name,(ref,inp,out,proj,dist) in zip(("low_500","residual_500"),acc):
        if ref<=1e-12: raise ValueError("missing reference energy")
        row=dict(band=name,error_db=db(inp,out),projection=proj/ref,clean_only_error_db=db(dist,ref))
        if not all(math.isfinite(v) for k,v in row.items() if k!="band"): raise ValueError("nonfinite band")
        result.append(row)
    return result

def self_test():
    c=[math.sin(i*.17)*.2 for i in range(1000)]
    n=[math.cos(i*.7)*.03 for i in range(1000)]
    x=[a+b for a,b in zip(c,n)]
    y=[a+b*.5 for a,b in zip(c,n)]
    for row in metrics(c,x,y,c,48000,1,100,1000):
        if abs(row["error_db"]-6.020599913)>1e-7 or row["clean_only_error_db"]>-90:
            raise ValueError("band analytic regression")
