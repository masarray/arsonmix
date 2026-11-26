#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Acquire independent RWTH microphone-wind data, not a DSP acceptance test.
First discovery records the archive SHA; a pinned SHA is required for reuse.
No archive paths are extracted. Preserve license and exact selected WAV bytes.
"""
import argparse
import hashlib
import io
import json
import tempfile
import urllib.request
import wave
import zipfile
from pathlib import Path, PurePosixPath

URL = ('https://www.iks.rwth-aachen.de/fileadmin/user_upload/downloads/'
       'forschung/tools-downloads/wind_noise_database.zip')
MAX_ARCHIVE = 256 * 1024 * 1024
MAX_MEMBER = 32 * 1024 * 1024
MAX_LICENSE = 64 * 1024
MIN_FRAMES = 17 * 16000


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def acquire(dest):
    req = urllib.request.Request(URL, headers={'User-Agent': 'ArSonMix-P0-raw-wind/1.0'})
    with urllib.request.urlopen(req, timeout=35) as response, dest.open('xb') as out:
        size = 0
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            size += len(block)
            require(size <= MAX_ARCHIVE, 'archive size limit')
            out.write(block)


def inspect(archive, root, expected=None):
    require(0 < archive.stat().st_size <= MAX_ARCHIVE, 'archive size limit')
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    require(expected is None or digest == expected.lower(), 'pinned archive SHA mismatch')
    root.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(archive) as z:
        entries = sorted(z.infolist(), key=lambda x: x.filename)
        require(len(entries) <= 10000, 'archive entry limit')
        require(len({x.filename for x in entries}) == len(entries), 'duplicate archive path')
        licenses = []
        for x in entries:
            if x.is_dir() or 'license' not in PurePosixPath(x.filename).name.lower():
                continue
            require(0 < x.file_size <= MAX_LICENSE, 'license size limit')
            b = z.read(x)
            text = b.decode('utf-8-sig').lower()
            require('permission is hereby granted' in text and 'without warranty' in text,
                    'unexpected license, manual review needed')
            name = f'LICENSE-{len(licenses)}.txt'
            (root / name).write_bytes(b)
            licenses.append({'member': x.filename, 'sha256': sha(b), 'file': name})
        require(licenses, 'no MIT license text in archive; do not redistribute')
        selected = {}
        manifest = []
        for x in entries:
            if x.is_dir() or not x.filename.lower().endswith('.wav'):
                continue
            path = x.filename.lower().replace('\\', '/')
            if '/varying_wind/' in '/' + path:
                category = 'varying'
            elif '/constant_wind/' in '/' + path:
                category = 'constant'
            elif '/outdoor' in '/' + path:
                category = 'outdoor'
            else:
                continue
            if x.file_size > MAX_MEMBER:
                continue
            b = z.read(x)
            try:
                with wave.open(io.BytesIO(b), 'rb') as w:
                    rate, ch, width, frames = (w.getframerate(), w.getnchannels(),
                                               w.getsampwidth(), w.getnframes())
            except (wave.Error, EOFError):
                continue
            meta = dict(member=x.filename, category=category, rate=rate,
                        channels=ch, sample_bytes=width, frames=frames,
                        sha256=sha(b), bytes=len(b))
            manifest.append(meta)
            if category in selected or rate != 16000 or ch not in (1, 2) or width not in (2, 3, 4) or frames < MIN_FRAMES:
                continue
            # Select first valid recording by lexical path, not by DSP score.
            # Flat controlled name: no user/archive-provided extraction path.
            target = f'{category}.wav'
            (root / target).write_bytes(b)
            selected[category] = dict(meta, file=target)
        result = dict(source=URL, archive_sha256=digest, archive_bytes=archive.stat().st_size,
                      pinned=expected is not None, scope='source intake only; not audio acceptance',
                      licenses=licenses, selected=selected, recordings=manifest)
        (root / 'manifest.json').write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
        print('RAW_WIND_INTAKE', digest, 'pinned', result['pinned'], 'selected', sorted(selected), flush=True)
        require('varying' in selected and 'outdoor' in selected,
                'missing eligible varying/outdoor microphone recording; inspect manifest')
        return result


def self_test():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        b = io.BytesIO()
        with wave.open(b, 'wb') as w:
            w.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
            w.writeframes(b'\x01\x00' * MIN_FRAMES)
        archive = root / 'fixture.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr('LICENSE.txt', 'Permission is hereby granted. WITHOUT WARRANTY. Test fixture only.')
            z.writestr('audio_booth/varying_wind/mic_dist_2cm/a.wav', b.getvalue())
            z.writestr('outdoors/a.wav', b.getvalue())
        actual = sha(archive.read_bytes())
        result = inspect(archive, root / 'out', actual)
        require(result['pinned'] and len(result['selected']) == 2, 'fixture selection')
        require((root / 'out' / 'varying.wav').read_bytes() == b.getvalue(), 'byte preservation')
        try:
            inspect(archive, root / 'bad', '0' * 64)
        except ValueError:
            pass
        else:
            raise AssertionError('wrong SHA accepted')
    print('RAW_WIND_INTAKE_SELFTEST PASS')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path, nargs='?')
    p.add_argument('--archive', type=Path)
    p.add_argument('--sha256')
    p.add_argument('--self-test', action='store_true')
    a = p.parse_args()
    if a.self_test:
        self_test()
        return
    require(a.output is not None, 'output required')
    if a.archive:
        inspect(a.archive, a.output, a.sha256)
    else:
        with tempfile.TemporaryDirectory() as d:
            archive = Path(d) / 'wind.zip'
            acquire(archive)
            inspect(archive, a.output, a.sha256)


if __name__ == '__main__':
    main()
