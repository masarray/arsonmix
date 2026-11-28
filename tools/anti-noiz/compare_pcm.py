#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Locate PCM differences without changing the strict benchmark gate."""
import array
import math
import sys

def load(path):
    a = array.array("I")
    with open(path, "rb") as f:
        a.frombytes(f.read())
    if a.itemsize != 4 or len(a) % 6:
        raise RuntimeError("invalid capture")
    return a, memoryview(a).cast("B").cast("f")

def measure(a, b, av, bv, start, end, i, j):
    mismatches = numeric = 0
    first = -1
    maxabs = e = ref = 0.0
    for frame in range(start, end):
        for c in range(2):
            x, y = frame * 6 + i + c, frame * 6 + j + c
            if a[x] != b[y]:
                mismatches += 1
                if first < 0:
                    first = frame
            p, q = av[x], bv[y]
            if not math.isfinite(p) or not math.isfinite(q):
                raise RuntimeError("nonfinite capture")
            d = abs(p-q)
            numeric += int(d != 0)
            maxabs = max(maxabs, d)
            e += d*d
            ref += p*p
    rms = math.sqrt(e / (2 * (end-start)))
    rel = 10*math.log10(e/ref) if e and ref else float("-inf") if not e else float("inf")
    return mismatches,numeric,first,maxabs,rms,rel

def main():
    old, new = sys.argv[1:]
    for rate in (44100, 96000):
        for mode in range(3):
            suffix = f"-{rate}-{mode}.f32"
            a,av = load(old+suffix)
            b,bv = load(new+suffix)
            if len(a) != len(b):
                raise RuntimeError("capture length mismatch")
            total = len(a)//6
            start = 32+3*rate
            for phase,lo,hi in (("settings",0,32),("learn",32,start),("active",start,total)):
                inp = measure(a,b,av,bv,lo,hi,0,0)
                if inp[0]:
                    raise RuntimeError(f"input mismatch {rate} {mode} {phase}: {inp}")
                for name,x,y,xv,yv,i,j in (
                    ("old-control-v-new-control",a,b,av,bv,2,2),
                    ("old-direct-v-new-direct",a,b,av,bv,4,4),
                    ("old-control-v-old-direct",a,a,av,av,2,4),
                    ("new-control-v-new-direct",b,b,bv,bv,2,4)):
                    m,n,first,peak,rms,rel = measure(x,y,xv,yv,lo,hi,i,j)
                    print(f"PCM_DIFF,{rate},{mode},{phase},{name},bit_diff={m},numeric_diff={n},first_frame={first},peak={peak:.9g},rms={rms:.9g},error_db={rel:.3f}",flush=True)
    print("PCM_DIFF_DIAGNOSTIC_COMPLETE,not an acceptance gate",flush=True)
if __name__ == "__main__":
    main()
