#!/usr/bin/env python3
"""Exact same-compiler float PCM parity, not an ASIO or listening-quality claim."""
import hashlib
import io
from pathlib import Path
import subprocess
import tempfile
import zipfile
import shutil
import statistics

ROOT = Path(__file__).resolve().parents[1]
BASE = "9ad8fda4b481285056245aadcb2db1b91095f0e7"
SUB = "engine/builtin_fx/sonku"
DRIVER = ROOT / "tests/sonku_p10_hotpath_parity.cpp"
PATCH = ROOT / "research/sonku/p104/direct-fir-candidate.patch"
APPROVED_BLOB = "f9a02f3eb7c04b1d84bdbc14d3d270d94f5fec69"
CANDIDATE_BLOB = "14296ed656ba4898374d2e365669e553436e467c"
CORE = Path(SUB) / "SonKuSpatialCore.h"
def blob(path):
    data=path.read_bytes().replace(b"\r\n",b"\n")
    return hashlib.sha1(f"blob {len(data)}\0".encode("ascii")+data).hexdigest()
FLAGS = [
    "SONKU_P7_MEASURED_ELEVATION_RESEARCH",
    "SONKU_P71_DIRECT_XYZ_RESEARCH",
    "SONKU_P72_NATIVE_XYZ_RT",
    "SONKU_P75_MUSIC_SAFE_RESEARCH",
    "SONKU_P80_TRANSPARENT_BASS_RESEARCH",
    "SONKU_P82_MULTIBAND_ROOM_RESEARCH",
    "SONKU_P83_PHYSICAL_SPEAKER_RESEARCH",
    "SONKU_P84_SINGLE_CORE_FUSION_RESEARCH",
    "SONKU_P85_WORLD_COHERENCE_RESEARCH",
    "SONKU_P86_HORIZONTAL_FOCUS_RESEARCH",
    "SONKU_P87_MUSIC_BODY_RESEARCH",
    "SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH",
    "SONKU_P89_DEPTH_COHERENCE_RESEARCH",
    "SONKU_P90_TRUE_EXTERNAL_RESEARCH",
    "SONKU_P94_FRONT_BACK_DEPTH_RESEARCH",
    "SONKU_P95_MUSICAL_PRESENCE_RESEARCH",
    "SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH",
    "SONKU_P97_STAGE_IMPACT_RESEARCH",
    "SONKU_P98_SCENE_VOICING_RESEARCH",
    "SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH",
    "ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING",
    "SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH",
    "ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH",
    "ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH",
]
def run(*args, **kw):
    return subprocess.run(args, cwd=ROOT, check=True, **kw)

def main():
    if subprocess.run(["git","cat-file","-e",BASE+"^{commit}"],cwd=ROOT).returncode:
        run("git","fetch","--no-tags","--depth=1","origin",BASE)
    with tempfile.TemporaryDirectory(prefix="sonku-p10-") as d:
        tmp=Path(d)
        archive=run("git","archive","--format=zip",BASE,SUB,"engine/builtin_fx/ImerSipP92Scene.h",stdout=subprocess.PIPE).stdout
        with zipfile.ZipFile(io.BytesIO(archive)) as z:
            z.extractall(tmp/"reference")
        # Source protection is untouched. Test the exact candidate in an
        # isolated directory and fail if either byte identity is unexpected.
        assert blob(ROOT/CORE)==APPROVED_BLOB, "production core changed"
        assert blob(tmp/"reference"/CORE)==APPROVED_BLOB, "baseline drift"
        shutil.copytree(tmp/"reference",tmp/"candidate")
        subprocess.run(["git","apply","--check",str(PATCH)],cwd=tmp/"candidate",check=True)
        subprocess.run(["git","apply",str(PATCH)],cwd=tmp/"candidate",check=True)
        assert blob(tmp/"candidate"/CORE)==CANDIDATE_BLOB, "candidate patch drift"
        rows=[]
        for profile,flags in (("legacy",[]),("P99",FLAGS)):
            for name,include in (("reference",tmp/"reference"/SUB),("candidate",tmp/"candidate"/SUB)):
                exe=tmp/(profile+"-"+name)
                run("g++","-std=c++20","-O2","-ffp-contract=off",
                    "-I",str(include),"-I",str(include.parent),*["-D"+f+"=1" for f in flags],
                    str(DRIVER),"-o",str(exe))
            for iteration,order in enumerate((("reference","candidate"),("candidate","reference"))):
                result={}
                for name in order:
                    pcm=tmp/(profile+"-"+name+".pcm")
                    log=run(str(tmp/(profile+"-"+name)),str(pcm),stdout=subprocess.PIPE,text=True).stdout
                    print(profile,iteration,name,log,flush=True)
                    for line in log.splitlines():
                        if not line.startswith("P10,"):continue
                        _,brand,rate,stage,count,p50,p95,p99=line.split(",")
                        rows.append({"profile":profile,"iteration":iteration,"variant":name,
                                     "brand":int(brand),"rate":int(rate),"stage":int(stage),
                                     "p50":float(p50),"p95":float(p95),"p99":float(p99)})
                    result[name]=pcm.read_bytes()
                expected=2*2*12*8192*2*4 + 2*(44100+48000)*2*4
                assert len(result["reference"])==expected==len(result["candidate"])
                if result["reference"]!=result["candidate"]:
                    at=next(i for i,(a,b) in enumerate(zip(result["reference"],result["candidate"])) if a!=b)
                    raise AssertionError(f"{profile} PCM changed at float index {at//4}")
                print("P10_BIT_EXACT",profile,expected//4,"floats",
                      hashlib.sha256(result["candidate"]).hexdigest(),flush=True)
        # Same-runner, two run orders; separate steady state from edits.
        for profile in ("legacy","P99"):
            for brand in (0,1):
                for scope in ("all","warmed_static","moving_resume"):
                    group=[r for r in rows if r["profile"]==profile and r["brand"]==brand and
                        (scope=="all" or r["stage"]==(9 if scope=="warmed_static" else 10))]
                    delta=[]
                    for metric in ("p50","p95","p99"):
                        a=statistics.mean(r[metric] for r in group if r["variant"]=="reference")
                        b=statistics.mean(r[metric] for r in group if r["variant"]=="candidate")
                        delta.append(round(100*(b/a-1),3))
                    print("P10_TIMING_DELTA_PERCENT",profile,brand,scope,*delta,flush=True)
        assert blob(ROOT/CORE)==APPROVED_BLOB
if __name__=="__main__":
    main()
