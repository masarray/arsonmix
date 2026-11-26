#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Strict arithmetic paired control independence; no tolerance on WAV parity.
Default-compiler numerical-equivalence and all field quality gates remain binding.
"""
import argparse
import json
import subprocess
from pathlib import Path
from recorded_environment import read_pcm, verify_rows, transparent
from recorded_field_wind import aggregate


def require(ok, message):
    if not ok:
        raise ValueError(message)


def render(binary, case, label, clean, silence, labels, ch):
    prefix = case / label
    args = [str(binary), *[str(case / f) for f in
            ('clean.wav', 'noise.wav', 'learn.wav', 'segments.tsv', 'provenance.tsv')],
            str(prefix), 'voice', str(clean), '1', str(silence)]
    result = subprocess.run(args, check=True, capture_output=True, text=True, timeout=60)
    return verify_rows(result.stdout, labels, ch), Path(str(prefix) + '-processed.wav')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('old_controls', type=Path)
    p.add_argument('new_controls', type=Path)
    p.add_argument('historical_prior', type=Path)
    p.add_argument('historical_fastpath', type=Path)
    a = p.parse_args()
    root = a.root.resolve(strict=True)
    binaries = [v.resolve(strict=True) for v in (a.old_controls, a.new_controls,
                a.historical_prior, a.historical_fastpath)]
    old, new, historical, fastpath = binaries
    report, failures = [], []
    cases = sorted(root.glob('*dB'))
    require(len(cases) == 4, 'expected exactly four field cases')
    for case in cases:
        labels = [(n, int(lo), int(hi), kind) for n, lo, hi, kind in
                  (line.split() for line in (case / 'segments.tsv').read_text().splitlines())]
        _, ch, _ = read_pcm(case / 'clean.wav')
        prior_rows, prior_wav = render(historical, case, 'strict-historical-prior', 24, 0, labels, ch)
        fast_rows, fast_wav = render(fastpath, case, 'strict-historical-fastpath', 24, 0, labels, ch)
        historical_exact = prior_rows == fast_rows and prior_wav.read_bytes() == fast_wav.read_bytes()
        if not historical_exact:
            failures.append([case.name, 'strict historical parity'])
        unaffected = []
        for silence in (0, 48):
            x, xp = render(old, case, f'controls-old-24-{silence}', 24, silence, labels, ch)
            y, yp = render(new, case, f'controls-new-24-{silence}', 24, silence, labels, ch)
            exact = x == y and xp.read_bytes() == yp.read_bytes()
            unaffected.append(exact)
            if not exact:
                failures.append([case.name, 'existing Clean24 PCM changed', silence])
        unity, _ = render(new, case, 'controls-unity', 0, 0, labels, ch)
        transparent(unity)
        x, _ = render(old, case, 'controls-old-residual-only', 0, 48, labels, ch)
        y, _ = render(new, case, 'controls-new-residual-only', 0, 48, labels, ch)
        before = aggregate(x, 'idle', 'idle_attenuation_db')
        after = aggregate(y, 'idle', 'idle_attenuation_db')
        projection = aggregate(y, 'active', 'desired_projection')
        weak_before = aggregate(x, 'weak', 'desired_projection')
        weak_after = aggregate(y, 'weak', 'desired_projection')
        if after-before < 3 or after < 3 or not .97 <= projection <= 1.03 or abs(weak_after-1) > abs(weak_before-1)+.01:
            failures.append([case.name, 'residual-only benefit/foreground', before, after, projection, weak_before, weak_after])
        row = dict(case=case.name, historical_exact=historical_exact, existing_clean24_exact=unaffected,
                   residual_before_db=before, residual_after_db=after, projection=projection,
                   weak_before=weak_before, weak_after=weak_after)
        report.append(row)
        print('P0_CONTROLS', json.dumps(row, allow_nan=False), flush=True)
    (root / 'controls-report.json').write_text(json.dumps(dict(arithmetic='-ffp-contract=off; reference qualification only',
                cases=report, failures=failures), indent=2, allow_nan=False)+'\n')
    print('P0_CONTROLS_SUMMARY', len(report), 'cases', len(failures), 'failures', flush=True)
    raise SystemExit(bool(failures))


if __name__ == '__main__':
    main()
