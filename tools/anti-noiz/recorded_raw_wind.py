#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Independent 16/48 kHz microphone wind corpus, source-hash verified.
First discovery is diagnostic, not a qualification or listening claim.
"""
import argparse
import hashlib
import json
import subprocess
import wave
from pathlib import Path

from recorded_environment import (RATE, CLIPS, BASE, README_SHA, download, read_pcm,
                                  write_pcm, resample_clean, prepare, verify_rows)
from recorded_field_wind import aggregate, compare, windows
from spectral_diagnostics import pcm


def require(ok, message):
    if not ok:
        raise ValueError(message)


def upsample_mono(raw, width, channels, rate=16000):
    """First microphone only, 16→48k linear interpolation; offline fixture.
    Does not claim to recreate frequencies above the source Nyquist.
    """
    require(rate in (16000, RATE), "unsupported native wind sample rate")
    stride = width * channels
    require(len(raw) % stride == 0, 'truncated PCM')
    scale = 2 ** (8 * width - 1)
    mono = [int.from_bytes(raw[i:i+width], 'little', signed=True)/scale
            for i in range(0, len(raw), stride)]
    require(len(mono) >= 2, 'insufficient microphone samples')
    if rate == RATE:
        return [v for sample in mono for v in (sample,sample)]
    out = []
    for i, x in enumerate(mono):
        y = mono[min(i+1, len(mono)-1)]
        for f in (0, 1, 2):
            v = x + (y-x)*f/3
            out.extend((v, v))  # compatibility with existing mono prepare()
    return out


def source_pcm(path, meta):
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == meta['sha256'], 'source WAV SHA mismatch')
    with wave.open(str(path), 'rb') as w:
        require((w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()) ==
                (meta['rate'], meta['channels'], meta['sample_bytes'], meta['frames']), 'source format mismatch')
        raw = w.readframes(min(w.getnframes(), 20*meta['rate']))
    return upsample_mono(raw, meta['sample_bytes'], meta['channels'], meta['rate'])


def self_test():
    raw = b'\x00\x00\x01\x00\x00\x40\xff\x7f'  # two stereo frames; only L
    x = upsample_mono(raw, 2, 2)
    require(len(x) == 12 and x[:2] == [0., 0.] and x[-2:] == [.5, .5], 'mono selection/length')
    require(abs(x[2] - 1/6) < 1e-12 and x[2] == x[3], 'interpolation')
    require(upsample_mono(raw,2,2,48000)==[0.,0.,.5,.5], 'native rate exact samples')
    print('RAW_WIND_UPSAMPLE_SELFTEST PASS')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('intake', type=Path, nargs='?')
    p.add_argument('old', type=Path, nargs='?')
    p.add_argument('new', type=Path, nargs='?')
    p.add_argument('output', type=Path, nargs='?')
    p.add_argument('--self-test', action='store_true')
    a = p.parse_args()
    self_test()
    if a.self_test:
        return
    require(all((a.intake, a.old, a.new, a.output)), 'intake, old/new binaries and output required')
    intake = a.intake.resolve(strict=True)
    manifest = json.loads((intake/'manifest.json').read_text())
    require(set(manifest['selected']) == {'indoor','outdoor-normal','outdoor-strong'} and
            len({x['sha256'] for x in manifest['selected'].values()}) == 3, 'incomplete or duplicated source intake')
    root = a.output.resolve(); root.mkdir(parents=True, exist_ok=False)
    binaries = {'old': a.old.resolve(strict=True), 'new': a.new.resolve(strict=True)}
    report = dict(scope='16/48kHz microphone wind, offline mixed foreground; not live mic/ASIO acceptance',
                  archive_sha256=manifest['archive_sha256'], pinned=manifest['pinned'], cases={}, failures=[], baseline_learn_rejections=[])
    download('README.md', README_SHA, root/'SOURCE-README.md')
    clips = []
    for name, file, sha, _ in CLIPS:
        if name not in ('speech', 'sung-note'):
            continue
        download(file, sha, root/file)
        rate, ch, clip = read_pcm(root/file)
        require(ch == 1, 'independent mono source only; no duplicated stereo claim')
        clips.append((name, file, sha, resample_clean(clip, ch, rate)))
    for kind, meta in sorted(manifest['selected'].items()):
        require(meta['file'] == f'{kind}.wav' and kind in ('indoor','outdoor-normal','outdoor-strong'), 'unexpected source identity')
        stereo = source_pcm(intake/meta['file'], meta)
        for name, file, sha, clip in clips:
            for snr in (0, 10):
                key = f'{kind}-{name}-{snr}dB'; case = root/key; case.mkdir()
                clean, noise, learn, labels = prepare(clip, 1, stereo, snr)
                for filename, values in (('clean.wav',clean),('noise.wav',noise),('learn.wav',learn)):
                    write_pcm(case/filename, RATE, 1, values)
                (case/'segments.tsv').write_text(''.join(f'{n} {lo} {hi} {v}\n' for n,lo,hi,v in labels))
                provenance = f"{manifest['archive_sha256']}; {meta['member']}; SHA256={meta['sha256']}; first mic, source {meta['rate']}Hz to {RATE}Hz (linear only when needed)"
                (case/'provenance.tsv').write_text(
                    f'clean\tCC0-1.0\tBart Massey; {BASE+file}; Git blob {sha}\n'
                    f'noise\tMIT (RWTH archive license text retained)\t{provenance}; independent [8s,...); shared SNR scale\n'
                    f'learn\tMIT (RWTH archive license text retained)\t{provenance}; independent [1,4)s; same scale\n')
                results = {}; _,lo,hi,_ = next(x for x in labels if x[0]=='idle')
                for lane,binary in binaries.items():
                    results[lane] = {}
                    for tag,silence in (('spectral',0),('max',48)):
                        prefix=case/f'{lane}-{tag}'
                        cmd=[str(binary),*[str(case/f) for f in ('clean.wav','noise.wav','learn.wav','segments.tsv','provenance.tsv')],
                             str(prefix),'voice','24','1',str(silence)]
                        result=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
                        if result.returncode:
                            detail=[key,lane,tag,'render/Learn failure',result.stderr[:1000]]
                            if lane=='old' and 'noise learning incomplete or rejected' in result.stderr:
                                report['baseline_learn_rejections'].append(detail)
                                print('RAW_MIC_WIND_BASELINE_UNAVAILABLE',detail,flush=True)
                            else:
                                report['failures'].append(detail)
                            continue
                        rows=verify_rows(result.stdout,labels,1)
                        x=pcm(str(prefix)+'-input.wav',RATE,1);y=pcm(str(prefix)+'-processed.wav',RATE,1)
                        results[lane][tag]={'rows':rows,'windows':windows(x,y,1,lo,hi)}
                report['cases'][key]=results
                if all(len(v)==2 for v in results.values()):
                    compare(results['old'],results['new'],report['failures'],key)
                elif len(results['new'])==2:
                    # A rejected baseline Learn does not waive the absolute
                    # candidate quality requirements; paired delta unavailable.
                    a=results['new']['max']['rows']; b=results['new']['spectral']['rows']
                    idle=aggregate(a,'idle','idle_attenuation_db')
                    residual=idle-aggregate(b,'idle','idle_attenuation_db')
                    active=aggregate(a,'active','error_improvement_db')
                    projection=aggregate(a,'active','desired_projection')
                    if idle<12 or residual<3:
                        report['failures'].append([key,'P0 idle target',idle,residual])
                    if active<10 or not .97<=projection<=1.03:
                        report['failures'].append([key,'active voice target',active,projection])
                    print('RAW_MIC_WIND_NEW_LEARN',key,'idle',idle,'residual',residual,'active',active,'projection',projection,flush=True)
    require(len(report['cases']) == len(manifest['selected'])*4, 'missing cases')
    report['qualification'] = 'NOT_QUALIFIED_UNPINNED' if not manifest['pinned'] else ('FAIL' if report['failures'] else ('OBJECTIVE_PASS_BASELINE_PARTIAL' if report['baseline_learn_rejections'] else 'OBJECTIVE_PASS_ONLY'))
    (root/'report.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    for failure in report['failures']:
        print('RAW_MIC_WIND_FAIL',failure,flush=True)
    print('RAW_MIC_WIND_SUMMARY',len(report['cases']),'cases',len(report['failures']),'failures',report['qualification'],flush=True)
    # Never pretend a first unpinned source discovery is qualified.
    raise SystemExit(bool(report['failures']) or not manifest['pinned'])


if __name__ == '__main__':
    main()
