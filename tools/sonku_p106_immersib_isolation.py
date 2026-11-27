#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compare historical pre-candidate and current real full ImmerSIB core."""
import pathlib
import subprocess
import tempfile
import tarfile
import io
from immersib_p102_geometry_cache_probe import MACROS, ROOT
BASE="379e867985bb6bab8e07297ba7a8c44925b22b2d"
def run(root,out):
    exe=out.with_suffix(".bin")
    subprocess.run(["g++","-std=c++20","-O2","-ffp-contract=off","-Wall","-Wextra","-Werror",*["-D"+m+"=1" for m in MACROS],str(root/"tools/immersib_p102_fullcore_ab.cpp"),"-o",str(exe)],check=True)
    subprocess.run([str(exe),"--pcm",str(out)],check=True,timeout=180)
    return out.read_bytes()
with tempfile.TemporaryDirectory() as td:
    tmp=pathlib.Path(td)
    subprocess.run(["git","fetch","--no-tags","origin",BASE],cwd=ROOT,check=True)
    old=tmp/"base"
    old.mkdir()
    data=subprocess.check_output(["git","archive",BASE,"engine/builtin_fx/sonku","engine/builtin_fx/ImerSipP92Scene.h","tools/immersib_p102_fullcore_ab.cpp"],cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(data)) as f:
        f.extractall(old,filter="data")
    a=run(old,tmp/"before.f32")
    b=run(ROOT,tmp/"after.f32")
    if len(a)<500000 or a!=b:
        raise SystemExit("IMMERSIB_P106_ISOLATION_FAIL")
    print(f"IMMERSIB_P106_ISOLATION_BITEXACT_PASS bytes={len(a)} base={BASE}")
