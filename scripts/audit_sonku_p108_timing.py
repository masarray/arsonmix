#!/usr/bin/env python3
"""Audit saved P10.8 timing evidence; no build, DSP edit or hardware claim."""
import argparse
import itertools
import json
import math
import statistics
import zipfile
from pathlib import Path

STRATA = tuple(itertools.product(range(3), (44100, 48000), (64, 128, 256), range(3)))
VARIANTS = ('reference', 'candidate')
METRICS = ('p50', 'p95', 'p99', 'maxUs', 'first32MaxUs')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def audit(report):
    expected = set(itertools.product(range(4), VARIANTS, STRATA))
    rows = {}
    for row in report['rows']:
        stratum = tuple(row[k] for k in ('brand', 'rate', 'block', 'scenario'))
        key = (row['trial'], row['variant'], stratum)
        require(key in expected and key not in rows, f'Unexpected or duplicate row: {key}')
        budget = 1e6 * stratum[2] / stratum[1]
        require(row['n'] == 1024 and math.isclose(row['deadlineUs'], budget, rel_tol=1e-9), f'Bad budget/count: {key}')
        require(all(math.isfinite(row[m]) and row[m] > 0 for m in METRICS), f'Invalid timing: {key}')
        require(row['p50'] <= row['p95'] <= row['p99'] <= row['maxUs'] and row['first32MaxUs'] <= row['maxUs'], f'Inconsistent timing: {key}')
        misses = row['deadlineMisses']
        require(type(misses) is int and 0 <= misses <= row['n'], f'Bad misses: {key}')
        require((misses > 0) == (row['maxUs'] >= budget) or abs(row['maxUs'] - budget) <= 1e-6, f'Miss/max inconsistency: {key}')
        rows[key] = row
    require(rows.keys() == expected, 'Incomplete 432-row paired matrix')
    trials = []
    for trial, variant in itertools.product(range(4), VARIANTS):
        group = [rows[trial, variant, s] for s in STRATA]
        trials.append(dict(trial=trial, variant=variant, misses=sum(r['deadlineMisses'] for r in group),
                           worstP99BudgetPercent=max(100*r['p99']/r['deadlineUs'] for r in group),
                           worstMaxBudgetPercent=max(100*r['maxUs']/r['deadlineUs'] for r in group)))
    flags = []
    for s in STRATA:
        delta = statistics.median(100*(rows[t, 'candidate', s]['p99']/rows[t, 'reference', s]['p99']-1) for t in range(4))
        if delta > 5:
            flags.append(dict(stratum=s, medianPairedP99DeltaPercent=delta))
    events = [dict(trial=t, variant=v, stratum=s, misses=r['deadlineMisses'],
                   p99BudgetPercent=100*r['p99']/r['deadlineUs'], maxBudgetPercent=100*r['maxUs']/r['deadlineUs'],
                   first32BudgetPercent=100*r['first32MaxUs']/r['deadlineUs'])
              for (t,v,s),r in rows.items() if r['deadlineMisses']]
    totals = {v:sum(t['misses'] for t in trials if t['variant']==v) for v in VARIANTS}
    reasons = []
    if totals['candidate'] > totals['reference'] or any(rows[t,'candidate',s]['deadlineMisses'] > rows[t,'reference',s]['deadlineMisses'] for t,s in itertools.product(range(4),STRATA)):
        reasons.append('CANDIDATE_EXCESS_DEADLINES')
    if sum(totals.values()):
        reasons.append('HOSTED_DEADLINES_OBSERVED')
    if flags:
        reasons.append('PAIRED_P99_ABOVE_5_PERCENT')
    return dict(compiler=report['compiler'], timingDisposition='HOLD' if reasons else 'NO_HOSTED_TIMING_FLAGS',
                productionAdmission='NOT_EVALUATED', physicalASIO='NOT_RUN', listening='NOT_EVALUATED',
                reasons=reasons, totals=totals, trials=trials, p99Flags=flags, deadlineEvents=events)


def self_test():
    rows = []
    for t,v,s in itertools.product(range(4),VARIANTS,STRATA):
        b,rate,block,scenario=s
        rows.append(dict(trial=t,variant=v,brand=b,rate=rate,block=block,scenario=scenario,n=1024,
                         p50=100,p95=110,p99=120,maxUs=130,first32MaxUs=125,deadlineMisses=0,deadlineUs=1e6*block/rate))
    report=dict(compiler='fixture',rows=rows)
    assert audit(report)['timingDisposition']=='NO_HOSTED_TIMING_FLAGS'
    assert audit(report)['productionAdmission']=='NOT_EVALUATED'
    r=next(r for r in rows if r['variant']=='candidate')
    r.update(deadlineMisses=1,maxUs=r['deadlineUs']*2)
    assert 'CANDIDATE_EXCESS_DEADLINES' in audit(report)['reasons']
    r.update(deadlineMisses=0,maxUs=130)
    for r in rows:
        if r['variant']=='candidate': r.update(p99=129)
    assert 'PAIRED_P99_ABOVE_5_PERCENT' in audit(report)['reasons']
    for broken in (rows[:-1],rows+[rows[0]], [dict(rows[0],p99=float('nan'))]+rows[1:]):
        try: audit(dict(compiler='fixture',rows=broken))
        except ValueError: pass
        else: raise AssertionError('Invalid evidence accepted')
    print('PASS: clean, admission boundary, deadline, p99, missing, duplicate, NaN')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('report',nargs='?',type=Path)
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    if args.self_test: self_test(); return
    if args.report is None: parser.error('report JSON or artifact ZIP required')
    if args.report.suffix=='.zip':
        with zipfile.ZipFile(args.report) as z: data=json.loads(z.read('report.json'))
    else: data=json.loads(args.report.read_text(encoding='utf-8'))
    print(json.dumps(audit(data),indent=2,allow_nan=False))


if __name__=='__main__': main()
