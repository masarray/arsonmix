#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Matched callback timing on one runner, exact PCM hashes; no scheduling certification."""
import csv
import statistics
import subprocess
import sys

def run(binary):
    result = subprocess.run([binary, "--benchmark"], check=True, capture_output=True, text=True, timeout=90)
    rows = {}
    for line in result.stdout.splitlines():
        if line.startswith("ALLOC_PROBE,"):
            print(line, flush=True)
        if not line.startswith("RT,"):
            continue
        a = next(csv.reader([line]))
        key = (int(a[1]), int(a[2]), a[3])
        if key in rows or len(a) != 17:
            raise RuntimeError("invalid or duplicate result")
        rows[key] = (int(a[4]), *map(float, a[5:10]), *map(int, a[10:16]), float(a[16]))
    if len(rows) != 60:
        raise RuntimeError(f"expected 60 cases, got {len(rows)}")
    return rows

def main():
    baseline, candidate = sys.argv[1:]
    # Alternating order limits systematic warmup/scheduling bias.
    a, b = [], []
    for i in range(3):
        order = ((baseline, a), (candidate, b)) if i % 2 == 0 else ((candidate, b), (baseline, a))
        for binary, dest in order:
            dest.append(run(binary))
        # Fail immediately after a matched pair, with per-case diagnostics.
        # Never spend another two repeats on an already-invalid comparison.
        mismatches = []
        for key in sorted(a[-1]):
            old, new = a[-1][key], b[-1][key]
            if old[0] != new[0] or old[11] != new[11]:
                mismatches.append(key)
                print(f"PCM_MISMATCH,{key},old_hash={old[11]},new_hash={new[11]},old_atten={old[12]},new_atten={new[12]}", flush=True)
            if any(old[7:11]) or any(new[7:11]):
                raise RuntimeError(f"RT allocation {key}")
        if mismatches:
            raise RuntimeError(f"PCM hash gate failed: {len(mismatches)}/60 cases")
    print("MATCHED,rate,block,workload,callbacks,old_p50_us,new_p50_us,old_p99_us,new_p99_us,old_p999_us,new_p999_us,old_max_us,new_max_us,budget_us,old_misses,new_misses,old_atten_db,new_atten_db,pcm_hash")
    changes = []
    regressions = []
    old_misses = new_misses = callbacks = 0
    for key in sorted(a[0]):
        old, new = [x[key] for x in a], [x[key] for x in b]
        assert all(x[0] == old[0][0] and x[10] == old[0][10] for x in old + new), f"PCM mismatch {key}"
        # tuple: count,p50,p99,p999,max,budget,misses,cppAlloc,cppFree,cAlloc,cFree,hash,attenuation
        # Compare exact hash below; allocator counters are hard gates.
        assert all(x[11] == old[0][11] for x in old + new), f"PCM mismatch {key}"
        assert all(not any(x[7:11]) for x in old + new), f"RT allocation {key}"
        median = lambda rows, j: statistics.median(x[j] for x in rows)
        p50a,p50b = median(old,1),median(new,1)
        p99a,p99b = median(old,2),median(new,2)
        p999a,p999b = median(old,3),median(new,3)
        ma,mb = max(x[4] for x in old),max(x[4] for x in new)
        da,db = sum(x[6] for x in old),sum(x[6] for x in new)
        old_misses+=da;new_misses+=db;callbacks+=sum(x[0] for x in new)
        delta=100*(p99b/p99a-1)
        changes.append(delta)
        if delta>10:
            regressions.append((key,round(delta,2)))
        print("MATCHED,"+",".join(map(str,(*key,old[0][0],p50a,p50b,p99a,p99b,p999a,p999b,ma,mb,old[0][5],da,db,old[0][12],new[0][12],old[0][11]))))
    print(f"SUMMARY,60 cases,3 matched repeats,callbacks_per_revision={callbacks},old_deadline_misses={old_misses},new_deadline_misses={new_misses},median_p99_delta_pct={statistics.median(changes):.3f},p99_over_10pct={len(regressions)},PCM_HASH_MATCH=60/60,ALLOCATIONS=0")
    for key, delta in regressions:
        print(f"TIMING_INVESTIGATE,{key},{delta}%")
    # Shared runner timing is diagnostic, not a flaky hard deadline gate.

if __name__ == "__main__":
    main()
