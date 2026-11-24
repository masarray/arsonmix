#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""P0 isolation lab: legacy golden vs branded FIR candidate; never edit production."""
from __future__ import annotations
import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from immersib_p0_active_fir_gate import (
    ROOT, PATH, APPROVED_BLOB, APPROVED_SOURCE_SHA, CANDIDATE_BLOB,
    MACROS, FAST_PATH, candidate, blob_hash, build, pcm,
)

# Exact macros in the existing P9.8 fast PCM workflow. Do not replace the
# actual golden test or update its accepted absolute FNV64 hash.
LEGACY = tuple(x for x in MACROS if x not in {
    "SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH",
    "ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING",
    "ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH",
    "ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH",
}) + ("SONKU_IMMERSIB_PCM_DIAGNOSTICS",)
GUARD = "#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH"
BRANDED = GUARD + " && defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) && ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH"

def legacy(root: Path, label: str, header: bytes, contract_off: bool) -> bool:
    dst = root / (label + ("-strict" if contract_off else "-default"))
    audio = dst / "engine/builtin_fx"
    audio.mkdir(parents=True)
    shutil.copytree(ROOT/"engine/builtin_fx/sonku", audio/"sonku")
    shutil.copyfile(ROOT/"engine/builtin_fx/ImerSipP92Scene.h",
                    audio/"ImerSipP92Scene.h")
    (dst/PATH).write_bytes(header)
    tests = dst/"tests"
    tests.mkdir()
    src = tests/"immersib_p98_native_sonic_gate.cpp"
    source = (ROOT/"tests"/src.name).read_text(encoding="utf-8")
    # Observation only: expose the actual hash immediately before the
    # unchanged assertion. No NDEBUG, relaxed threshold or golden reseal.
    needle = "        const auto base=render(rate,golden);"
    if source.count(needle)!=1:
        raise ValueError("P0_LEGACY_GOLDEN_ANCHOR_DRIFT")
    source = source.replace(needle, needle + '\n        std::fprintf(stderr,"P0_LEGACY_OBSERVED rate=%d hash=%016llx\\n",rate,static_cast<unsigned long long>(base.hash));')
    src.write_text(source, encoding="utf-8")
    out = dst/"legacy-golden"
    cmd = ["g++","-std=c++20","-O2","-Wall","-Wextra","-Werror","-pedantic"]
    if contract_off: cmd.append("-ffp-contract=off")
    cmd += ["-D"+x for x in LEGACY]+[str(src),"-o",str(out)]
    subprocess.run(cmd,cwd=dst,check=True,timeout=240)
    result=subprocess.run([str(out)],cwd=dst,text=True,capture_output=True,timeout=240)
    # Different codegen flags may change even baseline; record both independently.
    hashes=[x for x in result.stderr.splitlines() if x.startswith("P0_LEGACY_OBSERVED")]
    print(f"P0_LEGACY_CASE label={label} contractOff={contract_off} exit={result.returncode} hashes={hashes}",flush=True)
    if result.returncode:
        print("\n".join(result.stderr.splitlines()[-3:]),flush=True)
    return result.returncode==0

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--msvc",action="store_true")
    args=p.parse_args()
    subprocess.run([sys.executable,"scripts/verify_immersib_approved_pcm_sources.py"],
                   cwd=ROOT,check=True,timeout=30)
    current=(ROOT/PATH).read_bytes().replace(b"\r\n",b"\n")
    if blob_hash(current)!=CANDIDATE_BLOB:
        raise ValueError("P0_QUALIFIED_SOURCE_CHANGED")
    previous=candidate(current)
    if FAST_PATH.count(GUARD)!=1 or previous.count(FAST_PATH.encode())!=1:
        raise ValueError("P0_CANDIDATE_SCOPE_ANCHOR_DRIFT")
    scoped=previous.replace(FAST_PATH.encode(),FAST_PATH.replace(GUARD,BRANDED).encode(),1)
    print(f"P0_SCOPED_CANDIDATE_BLOB={blob_hash(scoped)} prior={blob_hash(previous)} shipping=NO",flush=True)
    approved=subprocess.run(["git","show",f"{APPROVED_SOURCE_SHA}:{PATH}"],cwd=ROOT,
                            check=True,capture_output=True,timeout=30).stdout
    if blob_hash(approved)!=APPROVED_BLOB:
        raise ValueError("P0_APPROVED_SOURCE_DRIFT")
    with tempfile.TemporaryDirectory(prefix="p0-legacy-isolation-") as td:
        root=Path(td)
        if not args.msvc:
            for strict in (False,True):
                baseline=legacy(root,"qualified",current,strict)
                raw=legacy(root,"unscoped",previous,strict)
                guarded=legacy(root,"branded-only",scoped,strict)
                if not strict and not baseline:
                    raise ValueError("P0_LEGACY_BASELINE_NOT_QUALIFIED")
                if guarded!=baseline:
                    raise ValueError("P0_SCOPED_LEGACY_GOLDEN_CHANGED")
                print(f"P0_LEGACY_SCOPE_RESULT contractOff={strict} baseline={baseline} unscoped={raw} scoped={guarded}",flush=True)
        variants=[build(root,label,header,args.msvc,False)
                  for label,header in (("historical",approved),("current",current),
                                       ("unscoped",previous),("scoped",scoped))]
        outputs=[pcm(dst,exes[0]) for dst,exes in variants]
        if not outputs[0]==outputs[1]==outputs[2]==outputs[3]:
            raise ValueError("P0_SCOPED_BRANDED_PCM_DRIFT")
        print(f"P0_SCOPED_BRANDED_PCM_PASS bytes={len(outputs[0])} sha256={hashlib.sha256(outputs[0]).hexdigest()} compiler={'MSVC' if args.msvc else 'GCC'} variants=4 shipping=NO",flush=True)
    subprocess.run([sys.executable,"scripts/verify_immersib_approved_pcm_sources.py"],
                   cwd=ROOT,check=True,timeout=30)
    return 0

if __name__=="__main__":
    sys.exit(main())
