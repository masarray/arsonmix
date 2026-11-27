#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
import pathlib
import subprocess
import tempfile
from immersib_p102_geometry_cache_probe import MACROS, ROOT
with tempfile.TemporaryDirectory() as td:
    tmp=pathlib.Path(td)
    exe=tmp/"sonku-p106.exe"
    subprocess.run(["cl.exe","/nologo","/std:c++20","/O2","/EHsc","/fp:precise","/W3","/utf-8",*["/D"+m+"=1" for m in MACROS],str(ROOT/"tests/sonku_p106_rt_musical_candidate.cpp"),"/Fe:"+str(exe)],cwd=tmp,check=True,timeout=180)
    subprocess.run([str(exe)],cwd=tmp,check=True,timeout=180)
