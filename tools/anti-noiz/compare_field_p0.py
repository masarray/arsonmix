#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Paired P0 partial response gate; never substitutes for the wind idle gate."""
import csv
import math
import sys
from pathlib import Path


def load(path):
    rows = {}
    for row in csv.reader(Path(path).read_text().splitlines()):
        kind = row[0]
        n = 5 if kind == 'ACTIVE' else 3 if kind == 'RESPONSE' else 0
        if not n or len(row) != (9 if kind == 'ACTIVE' else 8):
            raise ValueError(f'invalid row: {row}')
        key = tuple(row[:n])
        values = tuple(map(float, row[n:]))
        if key in rows or not all(map(math.isfinite, values)):
            raise ValueError(f'duplicate/nonfinite row: {key}')
        rows[key] = values
    if len(rows) != 72 or sum(k[0] == 'ACTIVE' for k in rows) != 64:
        raise ValueError('incomplete fixture')
    return rows


def compare(a, b):
    if a.keys() != b.keys():
        raise ValueError('paired identity mismatch')
    failures, moderate, strong, faster = [], [], [], []
    for key, old in a.items():
        new = b[key]
        if key[0] == 'ACTIVE':
            delta = new[0] - old[0]
            proj_loss = abs(new[1] - 1) - abs(old[1] - 1)
            onset_loss = abs(new[2] - 1) - abs(old[2] - 1)
            clean_loss = new[3] - old[3]
            if delta < -.25 or proj_loss > .03 or onset_loss > .05 or clean_loss > 1.:
                failures.append((key, 'foreground preservation', delta, proj_loss, onset_loss, clean_loss))
            if key[4] == '375' and key[3] == '12000':
                moderate.append(delta)
                if delta < .5:
                    failures.append((key, 'moderate active benefit', delta))
            if key[4] == '375' and key[3] == '40000':
                strong.append(delta)
                if delta < 1.:
                    failures.append((key, 'strong active benefit', delta))
        else:
            faster.append(old[1]-new[1])
            if old[0]-new[0] < 50 or old[1]-new[1] < 90 or new[2] != 0 or new[3] > .12 or new[4] < .99:
                failures.append((key, 'stationary expander response', old, new))
    print('P0_PARTIAL', 'rows', len(a), 'moderate active delta', min(moderate), max(moderate),
          'strong active delta', min(strong), max(strong), '20dB response improvement ms', min(faster), max(faster),
          'failures', len(failures))
    for failure in failures:
        print('P0_PARTIAL_FAIL', failure)
    return not failures


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: compare_field_p0.py baseline.csv candidate.csv')
    raise SystemExit(0 if compare(load(sys.argv[1]), load(sys.argv[2])) else 1)
