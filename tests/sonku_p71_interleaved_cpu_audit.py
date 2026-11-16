#!/usr/bin/env python3
"""Interleaved exact-source FIR benchmark, not a CI hard CPU-speed gate."""
import json
import pathlib
import re
import statistics
import subprocess
import sys

KEYS=("total_ms","p50_us","p95_us","p99_us")
def run(exe):
    output=subprocess.check_output([str(exe)],text=True,timeout=45)
    line=next((v for v in output.splitlines()
               if v.startswith("SONKU_P71_HOTPATH ")),None)
    if line is None:
        raise AssertionError(f"Missing native CPU results: {exe}")
    parsed=dict(re.findall(r"([a-z][a-z0-9_]*)=([0-9]+(?:\.[0-9]+)?)",line))
    record={key:float(parsed[key]) for key in KEYS}
    if any(x<=0 or x>100000 for x in record.values()):
        raise AssertionError(f"Bad CPU values {record}")
    return record

def main(baseline,fast,outdir):
    original=pathlib.Path(baseline)
    optimized=pathlib.Path(fast)
    reports={"old":[],"candidate":[]}
    # ABBA order cancels some warm/cold runner frequency, scheduler and
    # thermal monotonicity. The baseline is a separately compiled P7 source,
    # NOT a hardcoded speed claim or a different plugin engine.
    schedule=("old","candidate","candidate","old")*3
    for item in schedule:
        exe=original if item=="old" else optimized
        reports[item].append(run(exe))
    medians={name:{key:statistics.median(v[key] for v in records)
                   for key in KEYS} for name,records in reports.items()}
    speed=medians["old"]["total_ms"]/medians["candidate"]["total_ms"]
    medianp99=medians["old"]["p99_us"]/medians["candidate"]["p99_us"]
    evidence={
       "method":"same-source separate old/optimized P7 binaries, interleaved ABBA x3, warm steady DSP",
       "limitations":"shared Linux runner; not Windows ASIO, no hard real-time speed guarantee",
       "profile":"48kHz/128, 4 concurrent inserts, 60 degree spread, active ROOM",
       "median":medians,"pairedTotalSpeedup":round(speed,5),
       "p99RatioOldOverCandidate":round(medianp99,5),
       "trials":reports}
    outdir=pathlib.Path(outdir);outdir.mkdir(exist_ok=True,parents=True)
    (outdir/"SONKU-P71-FIR-INTERLEAVED-CPU.json").write_text(
        json.dumps(evidence,indent=2,sort_keys=True)+"\n")
    print(f"SONKU_P71_INTERLEAVED median_wallclock_speedup={speed:.4f} "
          f"median_p99_old_over_fast={medianp99:.4f} "
          "note=informational_linux_not_ASIO")
if __name__=="__main__":
    if len(sys.argv)!=4:
        raise SystemExit("Usage: sonku_p71_interleaved_cpu_audit.py ORIGINAL FAST OUTDIR")
    main(*sys.argv[1:])
