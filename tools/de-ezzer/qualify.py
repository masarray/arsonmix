#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Offline, annotated de-Ezzer corpus measurement; never a listening PASS."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import re
import subprocess
import sys

import numpy as np
from scipy.io import wavfile

MODES = ('auto', 'voice', 'auto-focused', 'voice-focused')
BANDS = {'body': (150, 2000), 'sibilance': (4000, 10000), 'air': (10000, 16000)}


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read_pcm(path):
    rate, data = wavfile.read(path)
    if data.ndim == 1:
        data = data[:, None]
    require(data.ndim == 2 and data.shape[1] in (1, 2) and 44100 <= rate <= 96000 and len(data), 'unsupported WAV shape/rate')
    if data.dtype.kind == 'i':
        data = data.astype(np.float64) / (2 ** (data.dtype.itemsize * 8 - 1))
    else:
        require(data.dtype.kind == 'f', 'expected signed PCM or float')
        data = data.astype(np.float64)
    require(np.isfinite(data).all() and np.max(np.abs(data)) <= 16, 'invalid PCM')
    return rate, data


def band_energy(pcm, rate):
    # One Hann-window spectrum per annotated interval, summed power across
    # channels (no cancellation). Relative comparisons use identical windows.
    win = np.hanning(len(pcm))[:, None]
    power = np.abs(np.fft.rfft(pcm * win, axis=0)) ** 2
    freq = np.fft.rfftfreq(len(pcm), 1 / rate)
    norm = np.sum(win ** 2) * len(pcm) * pcm.shape[1]
    return {name: float(2 * power[(freq >= lo) & (freq < hi)].sum() / norm)
            for name, (lo, hi) in BANDS.items()}


def attenuation(dry, wet):
    # Below -80 dBFS band RMS, there is no reliable attenuation denominator.
    return None if dry < 1e-8 else 10 * math.log10(dry / max(wet, 1e-20))


def load_manifest(path):
    m = json.loads(path.read_text(encoding='utf-8'))
    require(m.get('schema') == 1 and isinstance(m.get('clips'), list) and m['clips'], 'manifest schema/clips')
    seen = set()
    for clip in m['clips']:
        name = clip['id']
        require(isinstance(name, str) and re.fullmatch(r'[A-Za-z0-9_-]{1,64}', name) and name not in seen, 'invalid/duplicate clip id')
        seen.add(name)
        require(clip['split'] in ('development', 'heldout') and clip['kind'] in ('speech', 'singing', 'instrument', 'synthetic'), 'invalid clip class')
        require(all(isinstance(clip.get(k), str) and clip[k].strip() for k in ('source', 'license', 'sha256', 'wav')), 'missing source/license/hash/path')
        file = (path.parent / clip['wav']).resolve()
        require(file.is_file() and digest(file) == clip['sha256'], 'input hash mismatch: ' + name)
        rate, pcm = read_pcm(file)
        last = 0
        require(isinstance(clip.get('regions'), list) and clip['regions'], 'missing regions')
        for region in clip['regions']:
            a, b = region['start_frame'], region['end_frame']
            require(type(a) is int and type(b) is int and last <= a < b <= len(pcm) and b-a >= math.ceil(.020*rate), 'unordered/overlapping/short region: ' + name)
            last = b
            require(region['label'] in ('excess_sibilance', 'protect'), 'invalid region label')
        yield clip, file


def run(args):
    require(re.fullmatch('[0-9a-f]{40}', args.source_sha), 'exact source SHA required')
    require(0 <= args.amount <= 1 and 0 <= args.sensitivity <= 1 and 0 <= args.range_db <= 18 and 1 <= args.block <= 4096, 'invalid render controls')
    require(len(set(args.mode)) == len(args.mode), 'duplicate mode')
    require(all(math.isfinite(x) and x >= 0 for x in (args.min_cut, args.max_body, args.max_protect)), 'invalid gate')
    renderer = args.renderer.resolve(strict=True)
    manifest = args.manifest.resolve(strict=True)
    manifest_hash = digest(manifest)
    # Validate all annotations and hashes before any output is created.
    clips = list(load_manifest(manifest))
    args.output.mkdir(parents=True, exist_ok=False)
    rng = random.Random(args.seed)
    report = {'source_sha': args.source_sha, 'renderer_sha256': digest(renderer), 'manifest_sha256': manifest_hash,
              'settings': {'amount': args.amount, 'sensitivity': args.sensitivity, 'range_db': args.range_db, 'block': args.block},
              'gates_db': {'min_sibilance_cut': args.min_cut, 'max_abs_body_change': args.max_body, 'max_abs_protected_change': args.max_protect},
              'listening_status': 'NOT_RUN', 'results': []}
    key = []
    for clip, file in clips:
        require(digest(file) == clip['sha256'], 'input changed after validation')
        rate, dry = read_pcm(file)
        for mode in args.mode:
            target = args.output / (clip['id'] + '-' + mode)
            target.mkdir()
            wet_path, delta_path = target / 'wet.wav', target / 'delta.wav'
            subprocess.run([str(renderer), str(file), str(wet_path), str(delta_path), mode,
                            str(args.amount), str(args.sensitivity), str(args.range_db), str(args.block)], check=True, timeout=180, capture_output=True)
            rw, wet = read_pcm(wet_path)
            rd, delta = read_pcm(delta_path)
            require(rw == rd == rate and dry.shape == wet.shape == delta.shape, 'renderer changed shape/rate')
            error = float(np.max(np.abs(dry - wet - delta)))
            require(error <= 2e-6, 'dry/wet/delta reconstruction failed')
            rows = []
            for region in clip['regions']:
                a, b = region['start_frame'], region['end_frame']
                d, w = band_energy(dry[a:b], rate), band_energy(wet[a:b], rate)
                cuts = {k: attenuation(d[k], w[k]) for k in BANDS}
                if region['label'] == 'excess_sibilance':
                    checks = {'sibilance': cuts['sibilance'] is not None and cuts['sibilance'] >= args.min_cut,
                              'body': cuts['body'] is None or abs(cuts['body']) <= args.max_body}
                else:
                    measurable = [v for v in cuts.values() if v is not None]
                    checks = {'protected': bool(measurable) and all(abs(v) <= args.max_protect for v in measurable)}
                rows.append({**region, 'cut_db': cuts, 'checks': checks, 'pass': all(checks.values())})
            report['results'].append({'clip': clip['id'], 'kind': clip['kind'], 'split': clip['split'], 'mode': mode,
                                      'input_sha256': clip['sha256'], 'source': clip['source'], 'license': clip['license'], 'reconstruction_max_error': error, 'regions': rows,
                                      'pass': all(x['pass'] for x in rows)})
            # Blind pair: no independent normalization, no label in WAV filename.
            # Use identical float format for both sides to remove format cues.
            pair = 'pair-%04d' % (len(key) + 1)
            folder = args.output / 'blind' / pair
            folder.mkdir(parents=True)
            order = ['dry', 'wet']; rng.shuffle(order)
            for label, source in zip(('A', 'B'), order):
                wavfile.write(folder / (label + '.wav'), rate, (dry if source == 'dry' else wet).astype(np.float32))
            key.append({'pair': pair, 'clip': clip['id'], 'mode': mode, 'A': order[0], 'B': order[1]})
    report['measurement_pass'] = all(row['pass'] for row in report['results'])
    report['summary'] = {split: {mode: {'passed': sum(r['pass'] for r in report['results'] if r['split'] == split and r['mode'] == mode),
                                      'total': sum(1 for r in report['results'] if r['split'] == split and r['mode'] == mode)}
                                for mode in args.mode} for split in ('development', 'heldout')}
    real = [r for r in report['results'] if r['split'] == 'heldout' and r['kind'] in ('speech', 'singing')]
    report['heldout_real_vocal_clips'] = len({r['clip'] for r in real})
    report['release_qualified'] = False  # objective gates cannot certify musical listening or physical FAT
    (args.output / 'blind-key.json').write_text(json.dumps(key, indent=2) + '\n', encoding='utf-8')
    (args.output / 'report.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(f"{len(report['results'])} clip/mode results; measurement {'PASS' if report['measurement_pass'] else 'FAIL'}; listening NOT_RUN")
    return 0 if report['measurement_pass'] else 2


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--renderer', type=Path, required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--source-sha', required=True)
    p.add_argument('--mode', choices=MODES, action='append', required=True)
    p.add_argument('--amount', type=float, default=.5)
    p.add_argument('--sensitivity', type=float, default=.5)
    p.add_argument('--range-db', type=float, default=8)
    p.add_argument('--block', type=int, default=128)
    p.add_argument('--min-cut', type=float, default=2)
    p.add_argument('--max-body', type=float, default=.5)
    p.add_argument('--max-protect', type=float, default=.5)
    p.add_argument('--seed', type=int, default=3817)
    try:
        return run(p.parse_args())
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as e:
        print('qualification aborted:', e, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
