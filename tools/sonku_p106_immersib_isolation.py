#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Compare historical pre-candidate and current real full ImmerSIB core."""
import pathlib
import subprocess
import tempfile
import tarfile
import io
import sys
from immersib_p102_geometry_cache_probe import MACROS, ROOT
BASE="a61749fbcfa3f11a3268528af4c7cf9fd780e740"
MSVC=sys.argv[1:]==["--msvc"]
if sys.argv[1:] and not MSVC: raise SystemExit("unsupported option")
def run(root,out):
    exe=out.with_suffix(".exe" if MSVC else ".bin")
    source=str(root/"tools/immersib_p102_fullcore_ab.cpp")
    cmd=(["cl.exe","/nologo","/std:c++20","/O2","/EHsc","/fp:precise","/W3","/utf-8",*["/D"+m+"=1" for m in MACROS],source,"/Fe:"+str(exe)]
         if MSVC else ["g++","-std=c++20","-O2","-ffp-contract=off","-Wall","-Wextra","-Werror",*["-D"+m+"=1" for m in MACROS],source,"-o",str(exe)])
    subprocess.run(cmd,check=True,cwd=out.parent,timeout=180)
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
