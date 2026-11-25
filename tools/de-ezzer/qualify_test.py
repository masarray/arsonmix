#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Synthetic integration regression for qualify.py using the production renderer."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile


def main():
    renderer = Path(sys.argv[1]).resolve(strict=True)
    script = Path(__file__).with_name('qualify.py')
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        rate = 48000
        t = np.arange(rate * 2) / rate
        x = .12 * np.sin(2*np.pi*800*t)
        x += .25 * np.sin(2*np.pi*7200*t) * ((t >= .5) & (t < 1.25))
        # Antiphase stereo proves energy aggregation is not a mono sum.
        dry = np.column_stack((x, -x)).astype(np.float32)
        wavfile.write(root/'fixture.wav', rate, dry)
        sha = hashlib.sha256((root/'fixture.wav').read_bytes()).hexdigest()
        clip = {'id':'synthetic', 'kind':'synthetic', 'split':'development', 'source':'generated test tone', 'license':'AGPL-3.0-only', 'sha256':sha, 'wav':'fixture.wav',
                'regions':[{'start_frame':4800,'end_frame':19200,'label':'protect'}, {'start_frame':33600,'end_frame':52800,'label':'excess_sibilance'}, {'start_frame':76800,'end_frame':91200,'label':'protect'}]}
        manifest = root/'manifest.json'
        def write():
            manifest.write_text(json.dumps({'schema':1,'clips':[clip]}))
        write()
        base = [sys.executable,str(script),'--renderer',str(renderer),'--manifest',str(manifest),'--source-sha','0'*40,'--mode','auto']
        def run(name, *extra):
            return subprocess.run(base+['--output',str(root/name),*extra],capture_output=True,text=True,check=False)
        result = run('pass')
        assert result.returncode == 0, (result.stdout,result.stderr)
        report = json.loads((root/'pass/report.json').read_text())
        assert report['measurement_pass'] and not report['release_qualified'] and report['heldout_real_vocal_clips']==0
        assert report['results'][0]['reconstruction_max_error'] < 2e-6
        assert report['results'][0]['regions'][1]['cut_db']['sibilance'] >= 2
        key = json.loads((root/'pass/blind-key.json').read_text())[0]
        for label in ('A','B'):
            _, pcm = wavfile.read(root/'pass/blind'/key['pair']/(label+'.wav'))
            expected = dry if key[label]=='dry' else wavfile.read(root/'pass/synthetic-auto/wet.wav')[1]
            assert np.array_equal(pcm,expected)
        result = run('fail','--min-cut','30')
        assert result.returncode==2 and not json.loads((root/'fail/report.json').read_text())['measurement_pass']
        assert run('pass').returncode==1 # never overwrite existing outputs
        clip['sha256']='0'*64;write();assert run('bad-hash').returncode==1 and not (root/'bad-hash').exists()
        clip['sha256']=sha;clip['regions'][1]['start_frame']=10000;write();assert run('overlap').returncode==1 and not (root/'overlap').exists()
        print('PASS: actual renderer, anti-phase energy, dry/wet/delta, blind A/B, explicit fail, input integrity and overlap rejection')


if __name__=='__main__':
    main()
