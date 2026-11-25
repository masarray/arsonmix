#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Actual full-core active FIR A/B, actual provisional production source vs previous header."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from immersib_p102_geometry_cache_probe import MACROS, ROOT
from immersib_p102_fir_shipping_gate import (
    APPROVED_BLOB, APPROVED_SOURCE_SHA, CANDIDATE_BLOB, PATH, blob_hash,
)

PREVIOUS_SOURCE_SHA = "2253001926e6ef318eb00c9673417dc1177df4d6"
PREVIOUS_BLOB = "94b90db3d699d9f26601448a38b2f489f51b5fb7"
ANCHOR = "                    if(fastContiguousHrir_ && idx>=hrir::taps){"
FAST_PATH = """#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
                    // Stationary, non-fused, contiguous FIR: hoist dispatch
                    // out of the tap loop, preserve ordered sums and bounds.
                    if(p.kernelStationary && !fused &&
                       fastContiguousHrir_ && idx>=hrir::taps){
                        const float* first=signal.data()+idx;
                        const float* hL=p.kernel[0].data();
                        const float* hR=p.kernel[1].data();
                        const float invFrac=1.f-frac;
                        for(int k=0;k<freshTaps;++k){
                            const float v=invFrac*first[-k]+frac*first[-k-1];
                            l+=v*hL[k];r+=v*hR[k];
                        }
                    }else
#endif
"""
CASES = [(44100,128,1,2),(44100,128,2,8),(44100,256,2,2),
         (44100,256,4,8),(48000,128,1,2),(48000,128,2,2),
         (48000,256,1,8),(48000,256,2,2),(48000,256,4,8),
         (48000,480,1,2),(48000,480,2,2),(48000,480,4,8)]


def previous_header() -> bytes:
    source=subprocess.run(["git","show",f"{PREVIOUS_SOURCE_SHA}:{PATH}"],
                          cwd=ROOT,check=True,capture_output=True,timeout=30).stdout
    if blob_hash(source)!=PREVIOUS_BLOB:
        raise ValueError("P0_PREVIOUS_SOURCE_DRIFT")
    return source

def candidate(source: bytes) -> bytes:
    if blob_hash(source) != PREVIOUS_BLOB:
        raise ValueError("P0_CURRENT_SOURCE_PIN_MISMATCH")
    text = source.decode("utf-8")
    if text.count(ANCHOR) != 1:
        raise ValueError("P0_FIR_ANCHOR_DRIFT")
    return text.replace(ANCHOR, FAST_PATH + ANCHOR, 1).encode("utf-8")


def run(args, cwd, timeout=180):
    return subprocess.run(args, cwd=cwd, check=True, text=True,
                          capture_output=True, timeout=timeout).stdout


def build(root, label, header, msvc, benchmark, *, contract_off=True, product_probe=False):
    dst = root / label
    audio = dst / "engine/builtin_fx"
    audio.mkdir(parents=True)
    shutil.copytree(ROOT / "engine/builtin_fx/sonku", audio / "sonku")
    shutil.copyfile(ROOT / "engine/builtin_fx/ImerSipP92Scene.h",
                    audio / "ImerSipP92Scene.h")
    (dst / PATH).write_bytes(header)
    tools = dst / "tools"
    tools.mkdir()
    names = ["immersib_p102_fullcore_ab.cpp"]
    if benchmark:
        names.append("immersib_p102_cpu_profile.cpp")
    if product_probe:
        names.append("immersib_p0_product_pcm.cpp")
    outputs = []
    for name in names:
        src = tools / name
        shutil.copyfile(ROOT / "tools" / name, src)
        out = dst / (src.stem + (".exe" if msvc else ""))
        if msvc:
            cmd = ["cl.exe","/nologo","/std:c++20","/O2","/EHsc",
                   "/fp:precise","/W3","/utf-8",
                   *["/D"+x+"=1" for x in MACROS], str(src), "/Fe:"+str(out)]
        else:
            cmd = ["g++","-std=c++20","-O2","-Wall","-Wextra","-Werror",
                   "-pedantic", *(["-ffp-contract=off"] if contract_off else []),
                   *["-D"+x+"=1" for x in MACROS], str(src), "-o",str(out)]
        subprocess.run(cmd,cwd=dst,check=True,timeout=240)
        outputs.append(out)
    return dst, outputs


def pcm(dst, exe):
    output = dst / "all-scenes.f32"
    print(run([str(exe),"--pcm",str(output)],dst),end="",flush=True)
    data = output.read_bytes()
    if len(data) < 500_000 or len(data) % 8:
        raise ValueError("P0_PCM_BAD_SIZE")
    return data


def case(dst, exe, cfg):
    stdout = run([str(exe),"--case",*map(str,cfg)],dst)
    lines = [x for x in stdout.splitlines() if x.startswith("IMMERSIB_P102_CPU,")]
    if len(lines)!=1:
        raise ValueError("P0_CPU_MISSING_CASE")
    values = dict(x.split("=",1) for x in lines[0].split(",")[1:])
    if tuple(int(values[k]) for k in ("rate","frames","inserts","scene")) != cfg:
        raise ValueError("P0_CPU_CASE_IDENTITY_MISMATCH")
    for key in ("p50_us","p95_us","p99_us"):
        if not 0 < float(values[key]) < 1e9:
            raise ValueError("P0_CPU_INVALID_METRIC")
    if len(values.get("stream_hash",""))!=16:
        raise ValueError("P0_CPU_MISSING_STREAM_HASH")
    return values


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msvc", action="store_true")
    parser.add_argument("--scoped", action="store_true", help="benchmark exact branded-only candidate")
    args=parser.parse_args()
    subprocess.run([sys.executable,"scripts/verify_immersib_approved_pcm_sources.py"],
                   cwd=ROOT,check=True,timeout=30)
    original=subprocess.run(["git","show",f"{APPROVED_SOURCE_SHA}:{PATH}"],
                            cwd=ROOT,check=True,capture_output=True,timeout=30).stdout
    if blob_hash(original)!=APPROVED_BLOB:
        raise ValueError("P0_ORIGINAL_SOURCE_PIN_MISMATCH")
    current=previous_header()
    production=(ROOT/PATH).read_bytes().replace(b"\r\n",b"\n")
    from immersib_p0_windows_fir_gate import windows_candidate
    if blob_hash(production)!=CANDIDATE_BLOB or production!=windows_candidate(current):
        raise ValueError("P0_PRODUCTION_SOURCE_NOT_EXACT_QUALIFIED_CANDIDATE")
    proposed=production
    if args.scoped:
        from immersib_p0_legacy_scope_gate import scoped_candidate
        proposed=scoped_candidate(current)
    print(f"P0_FIR_CANDIDATE_BLOB={blob_hash(proposed)} base={PREVIOUS_BLOB}",flush=True)
    with tempfile.TemporaryDirectory(prefix="immersib-p0-active-") as tmp:
        root=Path(tmp)
        variants=[build(root,"approved",original,args.msvc,False),
                  build(root,"current",current,args.msvc,True),
                  build(root,"candidate",proposed,args.msvc,True)]
        audio=[pcm(dst,outputs[0]) for dst,outputs in variants]
        if not audio[0]==audio[1]==audio[2]:
            raise ValueError("P0_FULL_CORE_PCM_NOT_BIT_EXACT")
        print(f"P0_PCM_PASS bytes={len(audio[0])} sha256={hashlib.sha256(audio[0]).hexdigest()} "
              "variants=approved_current_candidate scenes=7 rates=2",flush=True)
        for cfg in CASES:
            pair=[]
            for order in ((1,2),(2,1)):
                record={}
                for index in order:
                    dst,outputs=variants[index]
                    record[index]=case(dst,outputs[1],cfg)
                if record[1]["stream_hash"]!=record[2]["stream_hash"]:
                    raise ValueError(f"P0_ACTIVE_STREAM_PCM_DRIFT {cfg}")
                pair.append(record)
            ratios=[float(x[2]["p50_us"])/float(x[1]["p50_us"]) for x in pair]
            ratios99=[float(x[2]["p99_us"])/float(x[1]["p99_us"]) for x in pair]
            row={"rate":cfg[0],"frames":cfg[1],"instances":cfg[2],"scene":cfg[3],
                 "p50_ratio":sum(ratios)/2,"p99_ratio":sum(ratios99)/2,"runs":pair}
            print("P0_ACTIVE_AB "+json.dumps(row,separators=(",",":")),flush=True)
        print("P0_ACTIVE_QUALIFICATION_COMPLETE cases="+str(len(CASES))+
              " repeats=2 source=PROVISIONAL hardware=NOT_RUN",flush=True)
    subprocess.run([sys.executable,"scripts/verify_immersib_approved_pcm_sources.py"],
                   cwd=ROOT,check=True,timeout=30)
    return 0

if __name__=="__main__":
    sys.exit(main())
