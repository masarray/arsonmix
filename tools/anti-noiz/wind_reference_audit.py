#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Parallel reference integrity / pitch-period experiment; legacy gate unchanged."""
import argparse
import json
import subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt, sosfiltfilt
from wind_resolution_probe import read, score

RATE = 48000
HOP = 128
DELAY = 256


def correct_reference(s, begin, end):
    # Only offline clean stem, not the mixture, noise or production audio.
    # 10 Hz Butterworth forward/backward; preserve the vocal passband.
    z = sosfiltfilt(butter(2, 10, btype='highpass', fs=RATE, output='sos'), s)
    z[:begin] = 0
    z[end:] = 0
    return z


def period_mean(x):
    """Aligned pitch-period mean subtraction; at most 241 future samples.
    High-band pitch uses only history. Prefix sums make each mean O(1).
    Prototype only: mono 48 kHz, no production state/CPU qualification.
    """
    ds, hist = 8, 1920
    high = sosfilt(butter(2, [350, 3000], btype='bandpass', fs=RATE, output='sos'), x)
    hp = np.pad(high, (hist, 0))
    prefix = np.r_[0., np.cumsum(np.pad(x, (DELAY, DELAY)))]
    y = x.copy()
    lags = np.arange(12, 61)
    prev = 480.
    for t in range(0, len(x), HOP):
        h = hp[t:t+hist:ds].copy()
        h -= h.mean()
        corr = np.array([np.dot(h[k:], h[:-k]) / max(np.linalg.norm(h[k:])*np.linalg.norm(h[:-k]), 1e-20) for k in lags])
        peaks = np.flatnonzero((corr[1:-1] >= corr[:-2]) & (corr[1:-1] >= corr[2:])) + 1
        conf = max(corr[peaks]) if len(peaks) else 0.
        period = 480.
        if conf > .65:
            k = next(j for j in peaks if corr[j] >= max(.65, .92*conf))
            offset = .5*(corr[k-1]-corr[k+1])/min(corr[k-1]-2*corr[k]+corr[k+1], -1e-12)
            period = np.clip(ds*(lags[k]+np.clip(offset, -.5, .5)), 96., 480.)
        count = min(HOP, len(x)-t)
        width = prev + (period-prev)*np.arange(count)/HOP
        prev = period
        center = np.arange(t, t+count)+DELAY+.5
        def at(pos):
            i = pos.astype(int)
            return prefix[i]+(pos-i)*(prefix[i+1]-prefix[i])
        y[t:t+count] -= (at(center+width/2)-at(center-width/2))/width
    return y


def quality(s, n, y, clean_only, lo, hi):
    a, b, c, d = (v[lo:hi] for v in (s, n, y, clean_only))
    result = score(a, b, c)
    energy = float(a@a)
    if energy > 1e-24:
        result['clean_only_projection'] = float(a@d/energy)
        result['clean_only_error_db'] = float(10*np.log10(max(float((d-a)@(d-a)), 1e-24)/energy))
    return result


def self_test():
    t = np.arange(RATE)/RATE
    s = .1*np.sin(2*np.pi*120*t) + .03*np.sin(2*np.pi*600*t)
    z = correct_reference(s+.05, 0, len(s))
    mid = slice(14400, -14400)
    assert np.max(abs(z[mid]-s[mid])) < 2e-4
    x = s+.12*np.sin(2*np.pi*21*t)
    y = period_mean(x)
    p = period_mean(x[:16384])
    assert np.max(abs(y[:len(p)-DELAY]-p[:-DELAY])) < 1e-12
    assert np.array_equal(period_mean(np.zeros(1024)), np.zeros(1024))
    print('WIND_REFERENCE_AUDIT_SELFTEST PASS', flush=True)


def run(root, native=None):
    output = root/'dc-reference-audit'
    output.mkdir(exist_ok=False)
    cases = {}
    for case in sorted(root.glob('*dB')):
        s, n, learn = (read(case/f) for f in ('clean.wav', 'noise.wav', 'learn.wav'))
        labels = {name: (int(lo), int(hi), kind) for name,lo,hi,kind in
                  (line.split() for line in (case/'segments.tsv').read_text().splitlines())}
        a,b,_ = labels['active']
        z = correct_reference(s, a, b)
        ratio = float(np.linalg.norm(z[a:b])/np.linalg.norm(s[a:b]))
        # Match original active SNR and independent Learn scaling; no post-render normalization.
        nc, lc = n*ratio, learn*ratio
        scale = min(1., .99/max(np.max(abs(z+nc)), np.max(abs(lc))))
        z, nc, lc = z*scale, nc*scale, lc*scale
        target = output/case.name; target.mkdir()
        for name, data in (('clean.wav',z),('noise.wav',nc),('learn.wav',lc)):
            wavfile.write(target/name, RATE, data.astype(np.float32))
        (target/'segments.tsv').write_text((case/'segments.tsv').read_text())
        prov = (case/'provenance.tsv').read_text().splitlines()
        (target/'provenance.tsv').write_text('\n'.join(line+'; offline clean-only 10Hz DC correction; matched SNR scaling' for line in prov)+'\n')
        y, clean_y = period_mean(z+nc), period_mean(z)
        original, original_clean = period_mean(s+n), period_mean(s)
        row = dict(dc_energy_fraction=float(np.mean(s[a:b])**2/np.mean(s[a:b]**2)),
                   clean_energy_removed_fraction=float(np.sum((s[a:b]-z[a:b]/scale)**2)/np.sum(s[a:b]**2)),
                   noise_scale=ratio, shared_clip_scale=scale, original={}, corrected={})
        for label,(lo,hi,_) in labels.items():
            row['original'][label] = quality(s,n,original,original_clean,lo,hi)
            row['corrected'][label] = quality(z,nc,y,clean_y,lo,hi)
        if native:
            row['native_corrected'] = {}
            for silence in (0,48):
                prefix = target/f'native-{silence}'
                cmd=[str(native),*[str(target/f) for f in ('clean.wav','noise.wav','learn.wav','segments.tsv','provenance.tsv')],str(prefix),'voice','24','1',str(silence)]
                result=subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                row['native_corrected'][str(silence)]={'returncode':result.returncode,'metrics_csv':result.stdout,'stderr':result.stderr[:1000]}
        cases[case.name] = row
        print('WIND_REFERENCE_AUDIT',case.name,json.dumps(row,allow_nan=False),flush=True)
    if len(cases) != 12:
        raise ValueError('expected exactly 12 raw cases')
    (output/'report.json').write_text(json.dumps(dict(scope='parallel diagnostic only; legacy raw gate unchanged; no production approval',cases=cases),indent=2,allow_nan=False)+'\n')


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path,nargs='?')
    p.add_argument('--native',type=Path)
    p.add_argument('--self-test',action='store_true')
    a=p.parse_args();self_test()
    if not a.self_test:
        if a.root is None: p.error('root required')
        run(a.root,a.native.resolve(strict=True) if a.native else None)
