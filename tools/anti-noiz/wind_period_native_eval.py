#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Native streaming experiment on original + parallel DC-corrected raw fixtures."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path
import numpy as np
from wind_resolution_probe import read, score
from wind_reference_audit import correct_reference, quality


def render(binary, x, tmp):
    source, target = tmp/'input.raw', tmp/'output.raw'
    x.astype('<f4').tofile(source)
    target.unlink(missing_ok=True)
    subprocess.run([str(binary), '48000', str(source), str(target)], check=True, timeout=60)
    y = np.fromfile(target, dtype='<f4').astype(np.float64)
    if y.shape != x.shape or not np.all(np.isfinite(y)):
        raise ValueError('native candidate output mismatch')
    return y


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('binary', type=Path)
    a = p.parse_args(); root = a.root.resolve(strict=True); binary = a.binary.resolve(strict=True)
    report = {}; totals = {'original': 0, 'corrected': 0}
    with tempfile.TemporaryDirectory() as name:
        tmp = Path(name)
        for case in sorted(root.glob('*dB')):
            s,n = [read(case/f) for f in ('clean.wav','noise.wav')]
            labels = {k:(int(lo),int(hi)) for k,lo,hi,_ in (v.split() for v in (case/'segments.tsv').read_text().splitlines())}
            if not {'active','weak','idle'} <= labels.keys(): raise ValueError('missing segments')
            lo,hi = labels['active']; z = correct_reference(s,lo,hi)
            ratio = np.linalg.norm(z[lo:hi])/np.linalg.norm(s[lo:hi]); nc = n*ratio
            scale = min(1., .99/max(np.max(abs(z+nc)),1e-12))
            row = {}
            for lane,u,v in [('original',s,n),('corrected',z*scale,nc*scale)]:
                y,clean = render(binary,u+v,tmp),render(binary,u,tmp)
                row[lane] = {k:quality(u,v,y,clean,b,e) for k,(b,e) in labels.items()}
                m = row[lane]['active']; ok = m['error_db']>=10 and .97<=m['projection']<=1.03
                row[lane]['active_target_met'] = ok; totals[lane] += int(ok)
            old = case/'new-max-processed.wav'
            if old.exists():
                y = read(old); row['production_original'] = score(s[lo:hi],n[lo:hi],y[lo:hi])
            report[case.name] = row
            print('WIND_PERIOD_NATIVE_CASE',case.name,json.dumps(row,allow_nan=False),flush=True)
    if len(report)!=12: raise ValueError('expected 12 raw cases')
    (root/'native-period-report.json').write_text(json.dumps(dict(scope='experimental native standalone; not production qualification',active_targets_met=totals,cases=report),indent=2,allow_nan=False)+'\n')
    print('WIND_PERIOD_NATIVE_SUMMARY',totals,flush=True)


if __name__=='__main__': main()
