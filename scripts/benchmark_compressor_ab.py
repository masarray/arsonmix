#!/usr/bin/env python3
"""Side-by-side native Compressor A/B benchmark on the SAME host/compiler.

Performance is NON-GATING: noisy CI clocks cannot certify speedup. Checksums
ARE gating for deterministic equivalence. No JUCE, installer or audio device.
"""
import argparse
import math
import os
import re
import shutil
import statistics
import subprocess
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HEADER="engine/builtin_fx/ChannelCompressor.h"
DRIVER=ROOT/"tests"/"native_compressor_benchmark.cpp"
PATTERN=re.compile(r"^SCENE (\w+) ([\d.]+) ([-\d.]+)$",re.M)

def run(*args):
    return subprocess.check_output(args,cwd=ROOT,text=True,stderr=subprocess.STDOUT)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--reference",required=True,help="40-character accepted baseline commit")
    args=ap.parse_args()
    if not re.fullmatch(r"[a-f0-9]{40}",args.reference):
        ap.error("reference must be immutable 40-character sha")
    current=(ROOT/HEADER).read_text(encoding="utf-8")
    try:
        reference=run("git","show",f"{args.reference}:{HEADER}")
    except subprocess.CalledProcessError:
        run("git","fetch","--no-tags","--depth=1","origin",args.reference)
        reference=run("git","show",f"{args.reference}:{HEADER}")
    compiler=os.environ.get("CXX","g++")
    with tempfile.TemporaryDirectory(prefix="compressor-ab-") as temp:
        tmp=Path(temp)
        executables={}
        for name,content in [("baseline",reference),("candidate",current)]:
            incl=tmp/name
            incl.mkdir()
            (incl/"ChannelCompressor.h").write_text(content,encoding="utf-8")
            exe=tmp/(name+(".exe" if os.name=="nt" else ""))
            command=[compiler,"-std=c++20","-O3","-DNDEBUG","-fno-fast-math",
                     "-I",str(incl),str(DRIVER),"-o",str(exe)]
            subprocess.check_call(command,cwd=ROOT)
            executables[name]=exe
        metrics={name:{} for name in executables}
        # Alternate the order to reduce warm-up/CPU clock drift bias.
        for name in ["baseline","candidate","candidate","baseline",
                     "baseline","candidate","candidate","baseline"]:
            output=subprocess.check_output([str(executables[name])],text=True)
            found=PATTERN.findall(output)
            if len(found)!=5:
                raise RuntimeError(f"{name} missing benchmark scenes: {output}")
            for scene,ns,checksum in found:
                metrics[name].setdefault(scene,[]).append((float(ns),float(checksum)))
        print("Compressor A/B -- same compiler, same runner, 16 instances, "
              "48 kHz / 128 frames, 950 blocks per trial")
        print("scene                   baseline ns/frame    candidate ns/frame    speedup")
        for scene in metrics["baseline"]:
            base=metrics["baseline"][scene]
            opt=metrics["candidate"][scene]
            reference_sum=base[0][1]
            if any(not math.isfinite(v[0]) or not math.isfinite(v[1])
                   for v in base+opt):
                raise RuntimeError(f"nonfinite timing/checksum: {scene}")
            if any(abs(v[1]-reference_sum)>max(1.e-5,abs(reference_sum)*1.e-6)
                   for v in base+opt):
                raise RuntimeError(f"PCM benchmark checksum mismatch: {scene}")
            b=statistics.median(v[0] for v in base)
            c=statistics.median(v[0] for v in opt)
            print(f"{scene:<23} {b:>13.3f} {c:>20.3f} {((b/c)-1)*100:>9.2f}%")
        print("Timing is informational only; PASS means checksum parity, not a CPU guarantee.")

if __name__=="__main__":
    main()
