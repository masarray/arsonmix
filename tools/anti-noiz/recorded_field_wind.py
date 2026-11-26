#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Independent recorded breeze P0 field gate. Exact old/new native binaries,
same PCM stems, no post-render normalization. This is NOT raw mic wind."""
import argparse
import json
import math
import subprocess
from pathlib import Path

from recorded_environment import (RATE, CLIPS, BASE, README_SHA, download, read_pcm,
                                  write_pcm, recorded_noise, resample_clean, prepare, verify_rows)
from spectral_diagnostics import pcm
from historical_fp_contract import compare_default, self_test as fp_self_test

SOURCE = ("wind-breeze", "keweldog", "181801", "wind-breeze.wav",
          "v0.3.0-preview", 12256128,
          "9b6db638e3be9930d2425e7598d8f5d1e4031e3f91a467fe1a6feb9e0492ac53")


def windows(inp, out, channels, begin, end, rate=RATE):
    if len(inp) != len(out) or not 0 <= begin < end <= len(inp)//channels:
        raise ValueError("timeline mismatch")
    width = rate//20
    values = []
    for f in range(begin, end-width+1, width):
        a = math.fsum(v*v for v in inp[f*channels:(f+width)*channels])
        b = math.fsum(v*v for v in out[f*channels:(f+width)*channels])
        if a > 1e-12:
            values.append(10*math.log10(a/max(b, 1e-24)))
    if not values or not all(map(math.isfinite, values)):
        raise ValueError("no measurable idle windows")
    run = longest = 0
    for value in values:
        run = run+1 if value < 6 else 0
        longest = max(longest, run)
    ranked = sorted(values)
    p = lambda q: ranked[round(q*(len(ranked)-1))]
    return dict(windows=len(values), p10_db=p(.1), median_db=p(.5), p90_db=p(.9),
                longest_under_6db_ms=longest*50, attenuation_db=values)


def self_test():
    fp_self_test()
    x=[.01]*4800
    w=windows(x,[v*.1 for v in x],1,0,len(x))
    assert w["windows"]==2 and abs(w["p10_db"]-20)<1e-9 and w["longest_under_6db_ms"]==0
    w=windows(x,x,1,0,len(x))
    assert w["longest_under_6db_ms"]==100 and abs(w["median_db"])<1e-9
    print("FIELD_WIND_METRICS PASS")


def aggregate(rows, label, key):
    return float(next(r[key] for r in rows if r["label"]==label and r["channel_minus1_all"]=="-1"))


def foreground_pairs(old, new, failures, case):
    for tag in ("spectral", "max"):
        a,b=old[tag]["rows"],new[tag]["rows"]
        def idx(items):
            return {(r["label"],r["channel_minus1_all"],r["begin_frame"],r["end_frame"]):r for r in items}
        aa,bb=idx(a),idx(b)
        if len(aa)!=len(a) or len(bb)!=len(b) or aa.keys()!=bb.keys():
            raise ValueError("unpaired field rows")
        for key,x in aa.items():
            y=bb[key]
            if x["kind"]=="active":
                loss=abs(float(y["desired_projection"])-1)-abs(float(x["desired_projection"])-1)
                error=float(y["error_improvement_db"])-float(x["error_improvement_db"])
                clean=float(y["clean_only_error_db_relative"])-float(x["clean_only_error_db_relative"])
                if loss>(.03 if key[0]=="weak" else .02) or error<(-1 if key[0]=="weak" else -.5) or clean>1:
                    failures.append([case,tag,key,"foreground regression",loss,error,clean])

def compare(old, new, failures, case):
    foreground_pairs(old, new, failures, case)
    rows={tag:aggregate(new[tag]["rows"],"idle","idle_attenuation_db") for tag in ("spectral","max")}
    active=aggregate(new["max"]["rows"],"active","error_improvement_db")
    old_active=aggregate(old["max"]["rows"],"active","error_improvement_db")
    active_delta=active-old_active
    # Additional incremental-benefit gate; absolute P0 thresholds below
    # remain binding. Never promote just because this delta passes.
    if active_delta<1.:
        failures.append([case,"incremental active benefit",active_delta])
    print("FIELD_WIND_INCREMENTAL",case,"old",old_active,"new",active,"delta",active_delta,flush=True)
    retain=aggregate(new["max"]["rows"],"active","desired_projection")
    residual=rows["max"]-rows["spectral"]
    if rows["max"]<12 or residual<3:
        failures.append([case,"P0 idle target",rows["max"],residual])
    if active<10 or not .97<=retain<=1.03:
        failures.append([case,"active voice target",active,retain])
    if new["max"]["windows"]["longest_under_6db_ms"] > old["max"]["windows"]["longest_under_6db_ms"]:
        failures.append([case,"longer exposed noise run"])
    print("FIELD_WIND",case,"idle",rows["max"],"residual",residual,"active",active,"projection",retain,flush=True)


def compare_residual(prior, new, failures, case, default_fp_ok):
    # Separate incremental idle gate. Never replace absolute P0/active gates.
    foreground_pairs(prior, new, failures, case)
    before=aggregate(prior["max"]["rows"],"idle","idle_attenuation_db")
    after=aggregate(new["max"]["rows"],"idle","idle_attenuation_db")
    residual=after-aggregate(new["spectral"]["rows"],"idle","idle_attenuation_db")
    a,b=prior["max"]["windows"],new["max"]["windows"]
    if not default_fp_ok:
        failures.append([case,"Silence zero default FP contract exceeded"])
    if after<12 or residual<3 or after-before<3:
        failures.append([case,"incremental residual target",before,after,residual])
    if b["p10_db"]<6 or b["p10_db"]<a["p10_db"] or b["longest_under_6db_ms"]>a["longest_under_6db_ms"]:
        failures.append([case,"residual exposed-noise regression",a,b])
    print("FIELD_RESIDUAL",case,"idle",before,after,"residual",residual,
          "p10",a["p10_db"],b["p10_db"],"longest_ms",a["longest_under_6db_ms"],b["longest_under_6db_ms"],
          "default_fp_contract",default_fp_ok,flush=True)


def compare_active(prior, new, failures, case):
    """New spectral policy must earn its changes; preserve earlier idle gains."""
    foreground_pairs(prior, new, failures, case)
    a,b=prior["max"],new["max"]
    old_active=aggregate(a["rows"],"active","error_improvement_db")
    active=aggregate(b["rows"],"active","error_improvement_db")
    projection=aggregate(b["rows"],"active","desired_projection")
    old_idle=aggregate(a["rows"],"idle","idle_attenuation_db")
    idle=aggregate(b["rows"],"idle","idle_attenuation_db")
    extra=idle-aggregate(new["spectral"]["rows"],"idle","idle_attenuation_db")
    if active-old_active<.5 or not .97<=projection<=1.03:
        failures.append([case,"active incremental preservation/benefit",old_active,active,projection])
    if idle<12 or extra<3 or idle<old_idle:
        failures.append([case,"active change lost prior idle gain",old_idle,idle,extra])
    if b["windows"]["p10_db"]<a["windows"]["p10_db"] or b["windows"]["longest_under_6db_ms"]>a["windows"]["longest_under_6db_ms"]:
        failures.append([case,"active change exposed-noise regression"])
    print("FIELD_ACTIVE_INCREMENTAL",case,"active",old_active,active,"projection",projection,
          "idle",old_idle,idle,"residual",extra,"p10",a["windows"]["p10_db"],b["windows"]["p10_db"],
          "longest_ms",a["windows"]["longest_under_6db_ms"],b["windows"]["longest_under_6db_ms"],flush=True)


def compare_shape(prior, new, failures, case):
    """Development-set no-regression gate for the new noise-scale estimator."""
    foreground_pairs(prior, new, failures, case)
    a,b=prior["max"],new["max"]
    old_active=aggregate(a["rows"],"active","error_improvement_db")
    active=aggregate(b["rows"],"active","error_improvement_db")
    projection=aggregate(b["rows"],"active","desired_projection")
    old_weak=aggregate(a["rows"],"weak","desired_projection")
    weak=aggregate(b["rows"],"weak","desired_projection")
    idle=aggregate(b["rows"],"idle","idle_attenuation_db")
    old_idle=aggregate(a["rows"],"idle","idle_attenuation_db")
    residual=idle-aggregate(new["spectral"]["rows"],"idle","idle_attenuation_db")
    if active<old_active or not .97<=projection<=1.03 or abs(weak-1)>abs(old_weak-1):
        failures.append([case,"shape estimator active/weak regression",old_active,active,projection,old_weak,weak])
    if idle<old_idle or idle<12 or residual<3 or b["windows"]["p10_db"]<a["windows"]["p10_db"] or b["windows"]["longest_under_6db_ms"]>a["windows"]["longest_under_6db_ms"]:
        failures.append([case,"shape estimator idle regression",old_idle,idle,residual])
    print("FIELD_SHAPE",case,"active",old_active,active,"weak_projection",old_weak,weak,
          "idle",old_idle,idle,"p10",a["windows"]["p10_db"],b["windows"]["p10_db"],flush=True)


def run():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("old",type=Path,nargs="?")
    p.add_argument("new",type=Path,nargs="?")
    p.add_argument("output",type=Path,nargs="?")
    p.add_argument("--self-test",action="store_true")
    p.add_argument("--prior",type=Path,help="exact pre-residual broadband comparator")
    p.add_argument("--pre-active",type=Path,help="exact pre-active spectral-policy comparator")
    p.add_argument("--pre-shape",type=Path,help="exact previous noise-scale comparator")
    p.add_argument("--reference-probe",type=Path,help="offline reference-known mask diagnostic")
    args=p.parse_args()
    self_test()
    if args.self_test:return
    if not args.old or not args.new or not args.output:raise ValueError("old,new,output required")
    binaries={"old":args.old.resolve(strict=True),"new":args.new.resolve(strict=True)}
    if args.prior:binaries["prior"]=args.prior.resolve(strict=True)
    if args.pre_active:
        if not args.prior:raise ValueError("historical residual comparator required")
        binaries["pre-active"]=args.pre_active.resolve(strict=True)
    if args.pre_shape:binaries["pre-shape"]=args.pre_shape.resolve(strict=True)
    probe=args.reference_probe.resolve(strict=True) if args.reference_probe else None
    root=args.output.resolve();root.mkdir(parents=True,exist_ok=False)
    download("README.md",README_SHA,root/"SOURCE-README.md")
    stereo,credit=recorded_noise(SOURCE,root/SOURCE[3])
    report={"source":SOURCE,"scope":"processed CC0 breeze, not raw mic turbulence; matched native PCM", "cases":{}, "failures":[]}
    for name,file,sha,intent in CLIPS:
        if name not in ("speech","sung-note"):continue
        download(file,sha,root/file)
        rate,ch,clip=read_pcm(root/file)
        clip=resample_clean(clip,ch,rate)
        for snr in (0,10):
            key=f"{name}-{snr}dB";case=root/key;case.mkdir()
            clean,noise,learn,labels=prepare(clip,ch,stereo,snr)
            for filename,values in (("clean.wav",clean),("noise.wav",noise),("learn.wav",learn)):
                write_pcm(case/filename,RATE,ch,values)
            (case/"segments.tsv").write_text("".join(f"{a} {b} {c} {d}\n" for a,b,c,d in labels))
            (case/"provenance.tsv").write_text(
                f"clean\tCC0-1.0\tBart Massey; {BASE+file}; Git blob {sha}\n"
                f"noise\tCC0-1.0\t{credit}; test starts 8s; shared scale\n"
                f"learn\tCC0-1.0\t{credit}; independent [1,4)s; same scale\n")
            results={}
            _,begin,end,_=next(x for x in labels if x[0]=="idle")
            for lane,binary in binaries.items():
                results[lane]={}
                for tag,silence in (("spectral",0),("max",48)):
                    prefix=case/f"{lane}-{tag}"
                    cmd=[str(binary),*[str(case/f) for f in ("clean.wav","noise.wav","learn.wav","segments.tsv","provenance.tsv")],
                         str(prefix),"voice","24","1",str(silence)]
                    result=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
                    if result.returncode:
                        report["failures"].append([key,lane,tag,"render/Learn failure",result.stderr[:1000]])
                        continue
                    rows=verify_rows(result.stdout,labels,ch)
                    inp=pcm(str(prefix)+"-input.wav",RATE,ch);out=pcm(str(prefix)+"-processed.wav",RATE,ch)
                    results[lane][tag]={"rows":rows,"windows":windows(inp,out,ch,begin,end)}
            report["cases"][key]=results
            if all(len(v)==2 for v in results.values()):
                compare(results["old"],results["new"],report["failures"],key)
                if "pre-shape" in results:
                    compare_shape(results["pre-shape"],results["new"],report["failures"],key)
                if probe:
                    for segment in ("active","weak"):
                        _,lo,hi,_=next(x for x in labels if x[0]==segment)
                        diagnostic=subprocess.run([str(probe),str(case/"clean.wav"),str(case/"noise.wav"),str(lo),str(hi),str(case/"new-max-processed.wav")],capture_output=True,text=True,check=True,timeout=60)
                        (case/f"reference-mask-{segment}.csv").write_text(diagnostic.stdout)
                if "pre-active" in results:
                    compare_active(results["pre-active"],results["new"],report["failures"],key)
                if "prior" in results:
                    # Preserve the strict residual-only parity gate on the exact
                    # pair it was designed for; the new spectral policy is
                    # intentionally different and separately qualified above.
                    residual_lane="pre-active" if "pre-active" in results else "new"
                    parity=(case/"prior-spectral-processed.wav").read_bytes()==(case/f"{residual_lane}-spectral-processed.wav").read_bytes()
                    report["cases"][key]["spectral_pcm_exact"]=parity
                    x=pcm(str(case/"prior-spectral-processed.wav"),RATE,ch)
                    y=pcm(str(case/f"{residual_lane}-spectral-processed.wav"),RATE,ch)
                    detail=compare_default(x,y,results["prior"]["spectral"]["rows"],results[residual_lane]["spectral"]["rows"])
                    report["cases"][key]["spectral_pcm_difference"]=detail
                    print("FIELD_SPECTRAL_DIFF",key,detail,flush=True)
                    # Exact PCM remains a hard gate on the same frozen pair
                    # in compare_p0_controls.py, compiled with -ffp-contract=off.
                    compare_residual(results["prior"],results[residual_lane],report["failures"],key,detail["within_contract"])
    if len(report["cases"])!=4:raise ValueError("incomplete cases")
    (root/"report.json").write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    for fail in report["failures"]:print("FIELD_WIND_FAIL",fail,flush=True)
    print("FIELD_WIND_SUMMARY",len(report["cases"]),"cases",len(report["failures"]),"failures",flush=True)
    raise SystemExit(bool(report["failures"]))


if __name__=="__main__":run()
