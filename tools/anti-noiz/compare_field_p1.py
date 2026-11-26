#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""P1 positive-only weak harmonic protection against exact P0 candidate."""
import sys
from compare_field_p0 import load


def compare(a, b):
    if a.keys() != b.keys():
        raise ValueError('paired identity mismatch')
    failures, gains = [], []
    for key, old in a.items():
        new = b[key]
        if key[0] == 'RESPONSE':
            if any(abs(x-y) > 1e-5 for x, y in zip(old, new)):
                failures.append((key, 'stationary response changed'))
            continue
        benefit = new[0]-old[0]
        projection = abs(old[1]-1)-abs(new[1]-1)
        onset = abs(new[2]-1)-abs(old[2]-1)
        clean = new[3]-old[3]
        if benefit < -.10 or projection < -.015 or onset > .05 or clean > 1.:
            failures.append((key, 'preservation', benefit, projection, onset, clean))
        if key[4]=='375' and key[3] in ('1500','4000'):
            gains.append(projection)
            if projection < .01:
                failures.append((key, 'weak harmonic projection gain', projection))
    if len(gains)!=16:
        raise ValueError('missing weak harmonic pairs')
    print('P1_COHERENCE', 'rows', len(a), 'weak projection delta', min(gains), max(gains),
          'mean', sum(gains)/len(gains), 'failures', len(failures))
    for row in failures:
        print('P1_COHERENCE_FAIL', row)
    return not failures


if __name__=='__main__':
    if len(sys.argv)!=3:
        raise SystemExit('usage: compare_field_p1.py old.csv new.csv')
    raise SystemExit(0 if compare(load(sys.argv[1]), load(sys.argv[2])) else 1)
