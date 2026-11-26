#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Offline raw-wind feasibility experiment; never a production DSP or release gate."""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt


def read(path):
    rate, x = wavfile.read(path)
    if x.ndim != 1 or rate != 48000 or not len(x):
        raise ValueError(f'expected mono 48 kHz fixture: {path}')
    if x.dtype.kind == 'i':
        x = x.astype(np.float64) / (2 ** (8*x.dtype.itemsize-1))
    elif x.dtype.kind == 'f':
        x = x.astype(np.float64)
    else:
        raise ValueError('unsupported PCM')
    if not np.all(np.isfinite(x)):
        raise ValueError('nonfinite PCM')
    return x


def score(s, n, y):
    if not (s.shape == n.shape == y.shape) or not np.all(np.isfinite(y)):
        raise ValueError('timeline mismatch or nonfinite output')
    power = float(s @ s)
    return dict(error_db=float(10*np.log10(max(float(n@n),1e-24)/max(float((y-s)@(y-s)),1e-24))),
                projection=float(s@y/power) if power>1e-24 else None)


def oracle(s, n, size, floor=10**(-24/20)):
    # Centered WOLA with known clean reference: optimistic diagnostic, NOT
    # a causal algorithm or a global time-domain upper bound.
    hop = size//2
    win = np.sqrt(.5-.5*np.cos(2*np.pi*np.arange(size)/size))
    a = np.pad(s, (hop,size)); b = np.pad(s+n, (hop,size))
    out = np.zeros_like(b)
    for i in range(0,len(s)+hop,hop):
        u = np.fft.rfft(a[i:i+size]*win)
        v = np.fft.rfft(b[i:i+size]*win)
        p = abs(v)**2
        g = np.clip(np.divide((u*v.conj()).real,p,out=np.ones_like(p),where=p>1e-30),floor,1)
        out[i:i+size] += np.fft.irfft(v*g,n=size)*win
    return out[hop:hop+len(s)]


def causal(s, rate=48000, block=128):
    # Estimate F0 from a separate, wind-resistant high-band history; fit
    # low-band harmonics to PAST samples only. No lookahead or offline oracle.
    low = sosfilt(butter(2, 400, fs=rate, output='sos'), s)
    high = sosfilt(butter(2, [450,2300], btype='bandpass',fs=rate,output='sos'),s)
    y = s.copy(); ds = 8; fr = rate//ds; hist = 2880
    for start in range(hist,len(s),block):
        stop = min(start+block,len(s)); h = high[start-hist:start:ds]
        h = h - h.mean()
        lags = np.arange(fr//500,fr//70+1)
        corr = np.array([np.dot(h[k:],h[:-k])/max(np.linalg.norm(h[k:])*np.linalg.norm(h[:-k]),1e-20) for k in lags])
        peaks = np.flatnonzero((corr[1:-1]>=corr[:-2])&(corr[1:-1]>=corr[2:]))+1
        if not len(peaks) or np.max(corr[peaks])<.65: continue
        k = next(j for j in peaks if corr[j]>=max(.65,.92*np.max(corr[peaks])))
        offset = .5*(corr[k-1]-corr[k+1])/min(corr[k-1]-2*corr[k]+corr[k+1],-1e-12)
        f0 = fr/(lags[k]+np.clip(offset,-.5,.5))
        harmonics = np.arange(1,int(400/f0)+1)
        if not len(harmonics): continue
        t = np.arange(-hist,0)/rate; q = np.arange(stop-start)/rate
        def basis(z):
            p = 2*np.pi*z[:,None]*f0*harmonics
            return np.column_stack((np.cos(p),np.sin(p)))
        a = basis(t); w = np.linspace(.1,1,hist)
        coef = np.linalg.solve(a.T@(w[:,None]*a)+np.eye(a.shape[1])*1e-6,a.T@(w*low[start-hist:start]))
        pred = basis(q)@coef
        confidence = np.clip((corr[k]-.65)/.3,0,.85)
        # Crossfade at block boundary without delaying or reading future data.
        mix = confidence*np.minimum(1,np.minimum(np.arange(stop-start)+1,block-np.arange(stop-start))/16)
        y[start:stop] += mix*(pred-low[start:stop])
    return y


def self_test():
    rng=np.random.default_rng(1987); t=np.arange(12000)/48000
    s=.1*np.sin(2*np.pi*120*t)+.03*np.sin(2*np.pi*600*t)+.02*np.sin(2*np.pi*720*t)
    n=.1*np.sin(2*np.pi*31*t)+.01*rng.normal(size=len(t))
    for size in (256,512,1024,2048,4096):
        assert np.max(abs(oracle(s,n,size,1)-s-n))<1e-12
        assert np.max(abs(oracle(s,n,size)-oracle(s,n,size)))==0
    y=causal(s+n); z=causal((s+n)[:8192]); assert np.max(abs(y[:8192]-z))<1e-12
    assert np.array_equal(causal(np.zeros(4000)),np.zeros(4000))
    print('WIND_RESOLUTION_SELFTEST PASS',score(s,n,y),flush=True)


def run(root):
    report={}
    for case in sorted(root.iterdir()):
        if not case.is_dir() or not (case/'segments.tsv').is_file(): continue
        s,n=read(case/'clean.wav'),read(case/'noise.wav')
        if s.shape!=n.shape: raise ValueError('stem timeline mismatch')
        lanes={'causal_highband_pitch':causal(s+n)}
        for size in (256,512,1024,2048,4096): lanes[f'oracle_{size}']=oracle(s,n,size)
        p=case/'new-max-processed.wav'
        if p.is_file(): lanes['production']=read(p)
        report[case.name]={}
        for line in (case/'segments.tsv').read_text().splitlines():
            label,lo,hi,_=line.split(); lo,hi=int(lo),int(hi)
            if label not in ('active','weak','idle'): continue
            if not 0<=lo<hi<=len(s): raise ValueError('invalid segment')
            report[case.name][label]={name:score(s[lo:hi],n[lo:hi],y[lo:hi]) for name,y in lanes.items()}
        if not {'active','weak','idle'}<=report[case.name].keys(): raise ValueError('missing segment')
        print('WIND_RESOLUTION',case.name,json.dumps(report[case.name],allow_nan=False),flush=True)
    if len(report)!=12: raise ValueError('expected 12 raw wind cases')
    (root/'resolution-probe.json').write_text(json.dumps({'scope':'offline diagnostics; no acceptance claim; oracle uses clean reference and lookahead','cases':report},indent=2,allow_nan=False)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('root',type=Path,nargs='?'); p.add_argument('--self-test',action='store_true'); a=p.parse_args()
    self_test()
    if not a.self_test:
        if a.root is None: p.error('root required')
        run(a.root)
