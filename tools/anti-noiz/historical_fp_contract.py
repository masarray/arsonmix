# SPDX-License-Identifier: AGPL-3.0-only
"""Bound default-FP drift; exact deterministic WAV/row parity stays mandatory."""
import math

PEAK_LIMIT = 2**-22
RMS_LIMIT = 1e-8
METRIC_LIMIT = 1e-6
METRICS = {'error_improvement_db', 'desired_projection', 'removed_projection',
           'clean_only_error_db_relative', 'idle_attenuation_db'}


def compare_default(x, y, old, new):
    if not x or len(x) != len(y) or len(old) != len(new) or not old:
        raise ValueError('historical comparison timeline/row mismatch')
    delta = [abs(a-b) for a,b in zip(x,y)]
    if not all(map(math.isfinite, delta)):
        raise ValueError('nonfinite historical PCM')
    peak, rms = max(delta), math.sqrt(math.fsum(d*d for d in delta)/len(delta))
    metric = 0.
    for a,b in zip(old,new):
        if a.keys()!=b.keys(): raise ValueError('historical metric schema mismatch')
        for k,v in a.items():
            w=b[k]
            if k not in METRICS or v=='' or w=='':
                if v!=w: raise ValueError('historical metric identity mismatch')
                continue
            d=abs(float(v)-float(w))
            if not math.isfinite(d): raise ValueError('nonfinite historical metric')
            metric=max(metric,d)
    return dict(samples=len(delta),changed_samples=sum(d!=0 for d in delta),peak_abs=peak,rms=rms,
                max_metric_delta=metric,within_contract=peak<=PEAK_LIMIT and rms<=RMS_LIMIT and metric<=METRIC_LIMIT)


def self_test():
    a=[0.]*1024; b=a.copy(); b[0]=2**-23
    r=[{'label':'active','desired_projection':'1'}]
    assert compare_default(a,b,r,r)['within_contract']
    b[0]=.001
    assert not compare_default(a,b,r,r)['within_contract']
    b=a.copy(); b[0]=float('nan')
    try: compare_default(a,b,r,r)
    except ValueError: pass
    else: raise AssertionError('nonfinite accepted')
    assert not compare_default(a,a,r,[{'label':'active','desired_projection':'0.999'}])['within_contract']
    try: compare_default(a,a,r,[{'label':'weak','desired_projection':'1'}])
    except ValueError: pass
    else: raise AssertionError('identity mismatch accepted')
