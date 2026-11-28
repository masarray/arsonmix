#!/usr/bin/env python3
"""Build and verify isolated SonKu audition binaries on Windows with MSVC.
No product installation, no audio device, no network, no approved source edit.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from verify_sonku_p104_externalization import ROOT, FX, FLAGS, candidate_copy

def main():
    out=ROOT/"sonku-p107-windows"
    out.mkdir(exist_ok=False)
    bins=out/"bin";bins.mkdir()
    driver=ROOT/"tests/sonku_p105_program_material.cpp"
    sha=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    with tempfile.TemporaryDirectory(prefix="sonku-msvc-") as d:
        tmp=Path(d)
        candidate_copy(tmp/"candidate")
        hashes={}
        for variant,inc in (("reference",FX),("candidate",tmp/"candidate")):
            exe=bins/(variant+"-program.exe")
            subprocess.run(["cl","/nologo","/std:c++20","/O2","/fp:strict","/EHsc","/MT",
                "/I"+str(inc/"sonku"),"/I"+str(inc),*["/D"+f+"=1" for f in FLAGS],
                str(driver),"/Fe:"+str(exe),"/Fo:"+str(tmp/(variant+".obj"))],cwd=tmp,check=True)
            hashes[exe.name]=hashlib.sha256(exe.read_bytes()).hexdigest()
        (bins/"manifest.json").write_text(json.dumps({"format":1,"gitHead":sha,
            "compiler":"MSVC /fp:strict /MT","sha256":hashes},indent=2)+"\n")
        # Verify the actual distributed binaries, not another compilation.
        for script in ("verify_sonku_p104_externalization.py","verify_sonku_p105_program_material.py"):
            shutil.copy2(ROOT/"scripts"/script,out/script)
        subprocess.run([sys.executable,str(out/"verify_sonku_p105_program_material.py"),
            "--verify-only","--out",str(out/"qualification")],check=True,cwd=out)
    (out/"START_HERE.txt").write_text(
        "SONKU P10.7 - OFFLINE RESEARCH AUDITION (not ArSonMix installer)\n"
        "Requires 64-bit Windows, Python 3, FFmpeg and FFprobe on PATH for listening WAVs.\n"
        "No compiler, admin permissions or online service needed. Extract entire ZIP.\n"
        "Open terminal in this folder. To render your stereo music, run:\n"
        '  python verify_sonku_p105_program_material.py --input "C:\\Music\\song.wav" --start 30 --out listening\n'
        "At least 13 seconds of stereo audio must follow the selected start.\n"
        "Use a new empty output folder on each run. WAV/FLAC/MP3 decoding via FFmpeg.\n"
        "Listen to four blind pairs before opening metrics or answer key.\n"
        "Same source and volume, score externalization, front/back, vocal body, bass and clarity.\n"
        "All audio stays local. For field audition only; no production DSP changed.\n"
        "Source and license: https://github.com/masarray/arsonmix/tree/"+sha+"\n")
    # Source link points to the exact tested checkout; include project license.
    for name in ("LICENSE","LICENSE.md","COPYING"):
        if (ROOT/name).is_file():
            shutil.copy2(ROOT/name,out/name)
            break
    print("P107_WINDOWS_AUDITION_VERIFIED",sha,flush=True)
if __name__=="__main__":main()
