#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Qualify a Windows MSVC-only fast path; preserve the reference path elsewhere."""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from immersib_p0_active_fir_gate import (
    ROOT, PATH, APPROVED_SOURCE_SHA, APPROVED_BLOB, CANDIDATE_BLOB,
    CASES, FAST_PATH, blob_hash, build, case, pcm,
)
from immersib_p0_legacy_scope_gate import BRANDED, GUARD, scoped_candidate, legacy

def windows_candidate(current):
    scoped=scoped_candidate(current)
    before=FAST_PATH.replace(GUARD,BRANDED).encode()
    after=FAST_PATH.replace(GUARD,BRANDED+
        " && defined(_MSC_VER) && !defined(__clang__)").encode()
    if scoped.count(before)!=1:
        raise ValueError("P0_WINDOWS_SCOPE_ANCHOR_DRIFT")
    return scoped.replace(before,after,1)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--msvc",action="store_true")
    args=parser.parse_args()
    subprocess.run([sys.executable,"scripts/verify_immersib_approved_pcm_sources.py"],
                   cwd=ROOT,check=True,timeout=30)
    current=(ROOT/PATH).read_bytes().replace(b"\r\n",b"\n")
    if blob_hash(current)!=CANDIDATE_BLOB:
        raise ValueError("P0_WINDOWS_QUALIFIED_SOURCE_DRIFT")
    proposed=windows_candidate(current)
    print(f"P0_WINDOWS_CANDIDATE_BLOB={blob_hash(proposed)} shipping=NO",flush=True)
    approved=subprocess.run(["git","show",f"{APPROVED_SOURCE_SHA}:{PATH}"],cwd=ROOT,
                            check=True,capture_output=True,timeout=30).stdout
    if blob_hash(approved)!=APPROVED_BLOB:
        raise ValueError("P0_WINDOWS_APPROVED_SOURCE_DRIFT")
    with tempfile.TemporaryDirectory(prefix="p0-windows-fir-") as td:
        root=Path(td)
        if not args.msvc:
            for strict in (False,True):
                baseline=legacy(root,"qualified",current,strict)
                guarded=legacy(root,"windows-only",proposed,strict)
                if (not strict and not baseline[0]) or baseline!=guarded:
                    raise ValueError("P0_WINDOWS_LEGACY_DRIFT")
                print(f"P0_WINDOWS_LEGACY_PASS contractOff={strict} result={baseline}",flush=True)
        for strict in ((True,) if args.msvc else (False,True)):
            family=root/("strict" if strict else "default")
            variants=[build(family,label,header,args.msvc,
                            args.msvc and label!="historical",
                            contract_off=strict,product_probe=True)
                      for label,header in (("historical",approved),
                                           ("current",current),("windows-only",proposed))]
            outputs=[pcm(dst,exes[0]) for dst,exes in variants]
            if not outputs[0]==outputs[1]==outputs[2]:
                raise ValueError("P0_WINDOWS_BRANDED_PCM_DRIFT")
            print(f"P0_WINDOWS_BRANDED_PCM_PASS bytes={len(outputs[0])} sha256={hashlib.sha256(outputs[0]).hexdigest()} contractOff={strict}",flush=True)
            mixed=[]
            for dst,exes in variants[1:]:
                out=dst/"mixed.f32"
                subprocess.run([str(exes[-1]),str(out)],cwd=dst,check=True,timeout=180)
                mixed.append(out.read_bytes())
            if len(mixed[0])!=1179648 or mixed[0]!=mixed[1]:
                raise ValueError("P0_WINDOWS_SHARED_PRODUCT_PCM_DRIFT")
            print(f"P0_WINDOWS_MIXED_PCM_PASS bytes={len(mixed[0])} sha256={hashlib.sha256(mixed[0]).hexdigest()} contractOff={strict}",flush=True)
            if args.msvc:
                for cfg in CASES:
                    pairs=[]
                    for order in ((1,2),(2,1)):
                        record={}
                        for index in order:
                            dst,exes=variants[index]
                            record[index]=case(dst,exes[1],cfg)
                        if record[1]["stream_hash"]!=record[2]["stream_hash"]:
                            raise ValueError(f"P0_WINDOWS_ACTIVE_PCM_DRIFT {cfg}")
                        pairs.append(record)
                    ratios=[float(x[2]["p50_us"])/float(x[1]["p50_us"]) for x in pairs]
                    ratios99=[float(x[2]["p99_us"])/float(x[1]["p99_us"]) for x in pairs]
                    print("P0_WINDOWS_ACTIVE_AB "+json.dumps({
                        "case":cfg,"p50_ratio":sum(ratios)/2,"p99_ratio":sum(ratios99)/2,
                        "runs":pairs},separators=(",",":")),flush=True)
        print("P0_WINDOWS_CANDIDATE_QUALIFICATION_COMPLETE shipping=NO hardware=NOT_RUN",flush=True)
    subprocess.run([sys.executable,"scripts/verify_immersib_approved_pcm_sources.py"],
                   cwd=ROOT,check=True,timeout=30)
    return 0
if __name__=="__main__":
    sys.exit(main())
