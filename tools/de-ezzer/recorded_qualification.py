#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Recorded-source objective screen + blinded A/B. Not phonetic or listening certification."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import urllib.request

import numpy as np
from scipy.io import wavfile

from qualify import band_energy, attenuation, digest, read_pcm, require, MODES
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from harmony_vocalset_qualification import CLIPS, BASE, REV, download

CC0_REV = 'ed5ebcbbbc2d11f0adddc9b50b78d581c29f738c'
CC0_BASE = 'https://raw.githubusercontent.com/pdx-cs-sound/wavs/' + CC0_REV + '/'
CC0 = {
    'voice.wav': ('1e60e7eb3cd9a324b65dd09b4de8245b360729b1', 'speech'),
    'voice-note.wav': ('20188237a28f38cddb12ab7136989509a1f22070', 'sung_note'),
    'gc.wav': ('e1e382b27dbdc5ec30e796ca39405a03742b86b5', 'guitar'),
}
BASELINE = 'c6f02c05358526ccb16c2e02b0113c4da18903de'

def cc0_download(name, expected, dest):
    with urllib.request.urlopen(CC0_BASE + name, timeout=25) as response:
        data = response.read(8 * 1024 * 1024 + 1)
    require(len(data) <= 8 * 1024 * 1024, 'oversized source')
    require(hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == expected,
            'CC0 source blob mismatch: ' + name)
    dest.write_bytes(data)

def render(exe, source, folder, mode, block, dry, rate):
    folder.mkdir()
    wet_path, delta_path = folder/'wet.wav', folder/'delta.wav'
    subprocess.run([str(exe), str(source), str(wet_path), str(delta_path),
                    mode, '.5', '.5', '8', str(block)], check=True, timeout=90, capture_output=True)
    rw, wet = read_pcm(wet_path)
    rd, delta = read_pcm(delta_path)
    require(rw == rd == rate and wet.shape == delta.shape == dry.shape, 'render geometry changed')
    require(np.max(np.abs(dry-wet-delta)) <= 2e-6, 'reconstruction failed')
    return wet

def metrics(dry, wet, rate):
    cuts = {k: attenuation(v, band_energy(wet, rate)[k]) for k, v in band_energy(dry, rate).items()}
    # A fullband null measure plus 100ms window scan. Energy-selected windows
    # are not phonetic annotations; air/sibilance attenuation is not a PASS criterion.
    null = 10 * math.log10(max(float(np.mean((dry-wet)**2)), 1e-20) /
                          max(float(np.mean(dry**2)), 1e-20))
    width = rate//10
    body = []
    for a in range(0, len(dry)-width+1, width):
        d, w = band_energy(dry[a:a+width], rate), band_energy(wet[a:a+width], rate)
        if d['body'] >= 1e-6:
            body.append(abs(attenuation(d['body'], w['body'])))
    return {'cut_db': cuts, 'relative_null_db': null,
            'body_window_count': len(body), 'body_window_max_abs_db': max(body, default=None),
            'peak': float(np.max(np.abs(wet)))}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--renderer', type=Path, required=True)
    p.add_argument('--baseline-renderer', type=Path, required=True)
    p.add_argument('--source-sha', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    require(len(a.source_sha)==40 and all(c in '0123456789abcdef' for c in a.source_sha), 'exact source SHA')
    exe, old = a.renderer.resolve(strict=True), a.baseline_renderer.resolve(strict=True)
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source_dir, work, evidence, blind = (out/x for x in ('sources','work','evidence','blind'))
    for d in (source_dir, work, evidence, blind):
        d.mkdir()
    cc0_download('README.md', '109aede3214f6b8066a0ef68a9344ffe12c93936', source_dir/'README.md')
    items = []
    for name, (sha, size) in CLIPS.items():
        download(name, sha, size, source_dir/name)
        items.append((name, sha, 'sustained_vowel', BASE+name, 'CC BY 4.0'))
    for name, (sha, kind) in CC0.items():
        cc0_download(name, sha, source_dir/name)
        items.append((name, sha, kind, CC0_BASE+name, 'CC0 1.0'))
    rows, key = [], []
    rng = random.Random(3817)
    for name, sha, kind, url, license in items:
        rate, raw = read_pcm(source_dir/name)
        # Keep the first <=6 seconds, preserving timing and relative level.
        # Common input scale only if needed; no independent dry/wet normalization.
        dry = raw[:6*rate].astype(np.float32)
        peak = float(np.max(np.abs(dry)))
        require(peak > 1e-6 and len(dry) >= rate//10, 'silent/short source')
        input_gain = min(1., .8/peak)
        dry = (dry*input_gain).astype(np.float32)
        source = work/(Path(name).stem+'.wav')
        wavfile.write(source, rate, dry)
        for mode in MODES:
            tag = Path(name).stem+'-'+mode
            case = work/tag
            case.mkdir()
            baseline = render(old, source, case/'baseline', mode, 128, dry, rate)
            wet = render(exe, source, case/'current', mode, 128, dry, rate)
            alternate = render(exe, source, case/'partition', mode, 257, dry, rate)
            require(np.array_equal(wet, alternate), 'block-partition PCM mismatch: '+tag)
            mb, mc = metrics(dry, baseline, rate), metrics(dry, wet, rate)
            # Exploratory body-protection flag, NOT a listening verdict. Do not
            # hide pre-existing flags or turn unknown consonants into labels.
            flag = lambda m: m['body_window_max_abs_db'] is not None and m['body_window_max_abs_db'] > .5
            row = {'clip':name, 'kind':kind, 'mode':mode, 'source_url':url, 'license':license,
                   'source_git_blob':sha, 'source_sha256':digest(source_dir/name),
                   'prepared_sha256':digest(source), 'rate':rate, 'frames':len(dry), 'channels':dry.shape[1],
                   'input_gain':input_gain, 'baseline':mb, 'current':mc,
                   'baseline_body_flag':flag(mb), 'current_body_flag':flag(mc),
                   'new_body_flag':flag(mc) and not flag(mb),
                   'candidate_vs_baseline_max_abs':float(np.max(np.abs(wet-baseline))),
                   'partition_bit_exact':True}
            rows.append(row)
            pair = 'pair-%03d' % len(rows)
            folder = blind/pair
            folder.mkdir()
            order = ['dry','current']; rng.shuffle(order)
            # Same peak-safe listening gain for both members of this pair.
            gain = min(1., .7/max(float(np.max(np.abs(dry))), mc['peak'], 1e-9))
            for label, value in zip(('A','B'), order):
                wavfile.write(folder/(label+'.wav'), rate, ((dry if value=='dry' else wet)*gain).astype(np.float32))
            key.append({'pair':pair,'clip':name,'mode':mode,'A':order[0],'B':order[1],'common_playback_gain':gain})
            print(f"RECORDED {tag} {rate}Hz body_max={mc['body_window_max_abs_db']} new_flag={row['new_body_flag']}", flush=True)
    report = {'source_sha':a.source_sha,'baseline_sha':BASELINE,'renderer_sha256':digest(exe),
              'baseline_renderer_sha256':digest(old),'settings':{'amount':.5,'sensitivity':.5,'range_db':8},
              'clips':len(items),'cases':len(rows),'integrity_pass':True,
              'baseline_flags':sum(x['baseline_body_flag'] for x in rows),
              'current_flags':sum(x['current_body_flag'] for x in rows),
              'new_flags':sum(x['new_body_flag'] for x in rows),
              'listening_status':'NOT_RUN','release_qualified':False,'rows':rows}
    (evidence/'report.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n',encoding='utf-8')
    (evidence/'blind-key.json').write_text(json.dumps(key, indent=2)+'\n',encoding='utf-8')
    attribution = ('VocalSet: A Singing Voice Dataset, Wilkins, Seetharaman, Wahl & Pardo (2018), '
                   'https://zenodo.org/records/1442513, CC BY 4.0 https://creativecommons.org/licenses/by/4.0/.\n'
                   'Pinned source redistribution: '+BASE+'\n'
                   'Bart Massey (2024), PSU Computers, Sound and Music, CC0 1.0 '
                   'https://creativecommons.org/publicdomain/zero/1.0/, '+CC0_BASE+'README.md\n'
                   'Derived audio: first <=6 seconds, float PCM, common input/playback scaling, '
                   'one member processed by de-Ezzer. Pairs randomized; no independent normalization.\n')
    for d in (evidence, blind):
        (d/'ATTRIBUTION.txt').write_text(attribution,encoding='utf-8')
    (blind/'LISTEN.txt').write_text('Start at low playback volume. Listen to A and B in random order. '
        'Score harsh S, articulation/lisp, breath/air, pumping and preference before opening the separate evidence key. '
        'Vowels and note recordings are not labeled sibilant events. Do not infer musical certification.\n',encoding='utf-8')
    print(f"RECORDED COMPLETE clips={report['clips']} cases={report['cases']} baseline_flags={report['baseline_flags']} current_flags={report['current_flags']} new_flags={report['new_flags']} listening=NOT_RUN",flush=True)
    # A new exploratory protection flag must be visible, not waived as a pass.
    return 2 if report['new_flags'] else 0

if __name__ == '__main__':
    sys.exit(main())
