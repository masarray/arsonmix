#!/usr/bin/env python3
"""Rebuild the MIT-licensed original P7 elevation bank from locally supplied upstream headers.

This script has no network access and is never executed during product builds.
Download the PINNED files documented in SonKuMeasuredHrir3D.h from
jhonmark/mit-hrtf-lib source/normal/, verify their Git blob identities,
then run: python scripts/generate_sonku_measured_hrir3d.py 44.h 48.h OUTPUT.
The checked-in file remains the sole source for P7 offline native tests.
"""
import argparse
import pathlib
import re

ELEV=(-40,-30,-20,-10,0,10,20,30,40,50,60,70,80,90)
PAT=re.compile(r'\{\s*// H(-?\d+)e(\d+)a\.wav\s*\{([^{}]+)\}\s*,\s*\{([^{}]+)\}\s*\}')
def parse(path,rate):
    src=path.read_text(encoding="utf-8",errors="replace")
    results={e:[] for e in ELEV}
    for mat in PAT.finditer(src):
        e,az=int(mat[1]),int(mat[2])
        if e not in results: raise ValueError("Unexpected elevation")
        channels=[[int(s.strip()) for s in mat[k].split(",")] for k in (3,4)]
        assert all(len(ch)==(128 if rate==44100 else 140) and
                   all(-32768<=v<=32767 for v in ch) for ch in channels)
        results[e].append((az,channels))
    if sum(len(v) for v in results.values())!=368: raise ValueError("Not original 368 measured rows")
    for e,rows in results.items():
        if not rows or rows[0][0]!=0 or any(a[0]>=b[0] for a,b in zip(rows,rows[1:])):
            raise ValueError(f"Invalid azimuth positions elev={e}")
    return results

def elname(e):return ("m"+str(-e)) if e<0 else ("p"+str(e))
def generate(banks,template):
    begin="// BEGIN GENERATED MEASUREMENT ARRAYS"
    end="// END GENERATED MEASUREMENT ARRAYS"
    if template.count(begin)!=1 or template.count(end)!=1:
        raise ValueError("Checked-in KEMAR3D header missing generated sentinels")
    before=template[:template.index(begin)+len(begin)]
    after=template[template.index(end):]
    out=[]
    for e in ELEV:
        a=[row[0] for row in banks[44100][e]]
        if a!=[row[0] for row in banks[48000][e]]:raise ValueError("Mismatched azimuth positions")
        out.append(f"inline constexpr std::uint8_t az_{elname(e)}[{len(a)}]={{"+",".join(map(str,a))+"};\n")
    for rate in (44100,48000):
        for e in ELEV:
            if e==0:continue
            v=banks[rate][e]
            out.append(f"inline constexpr std::int16_t pcm{rate}_{elname(e)}[{len(v)}][2][hrir::taps]={{\n")
            for az,ch in v:
                channels=[c+[0]*(140-len(c)) for c in ch]
                out.append("  {{"+",".join(map(str,channels[0]))+"},{"+
                           ",".join(map(str,channels[1]))+f"}}}}, // azimuth {az} degrees\n")
            out.append("};\n")
        out.append(f"inline constexpr Row rows{rate}[elevationCount]={{\n")
        for e in ELEV:
            target=f"hrir::normal{rate}" if e==0 else f"pcm{rate}_{elname(e)}"
            out.append(f"  {{{e},{len(banks[rate][e])},az_{elname(e)},{target}}},\n")
        out.append("};\n")
    return before+"\n"+"".join(out)+after
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("source44100",type=pathlib.Path)
    ap.add_argument("source48000",type=pathlib.Path)
    ap.add_argument("output",type=pathlib.Path)
    a=ap.parse_args()
    banks={44100:parse(a.source44100,44100),48000:parse(a.source48000,48000)}
    template=a.output.read_text(encoding='utf-8')
    a.output.write_text(generate(banks,template),encoding='utf-8')
if __name__=="__main__":main()
