#!/usr/bin/env python3
"""P7.1: same stereo source, fixed gain, three DIRECT elevation angles.
Only numeric diagnostics; musical preference/externalization needs human A/B.
No numpy and no audio generation in this audit: WAVs are native C++ outputs.
"""
import json
import math
import pathlib
import struct
import sys
import wave

RATE=48000
FRAMES=RATE*3
FREQUENCIES=(220,660,940,3200,4700)
def read(path):
    with wave.open(str(path),'rb') as wav:
        shape=(wav.getnchannels(),wav.getsampwidth(),
               wav.getframerate(),wav.getnframes())
        if shape!=(2,2,RATE,FRAMES):
            raise AssertionError(f'unexpected P7.1 source WAV {path}: {shape}')
        buf=wav.readframes(FRAMES)
    return struct.unpack('<'+'h'*(len(buf)//2),buf)

def spectral(pcm,ear,frequency):
    # Goertzel at an exact 1-second integer-bin frequency; no FFT dependency.
    phase=2*math.pi*frequency/RATE
    a=2*math.cos(phase)
    p1=p2=0.
    for frame in range(RATE,2*RATE):
        x=pcm[2*frame+ear]/32768.
        q=x+a*p1-p2
        p2,p1=p1,q
    re=p1-p2*math.cos(phase)
    im=p2*math.sin(phase)
    return 2*math.hypot(re,im)/RATE,math.atan2(im,re)
def delta_phase(a,b):
    return (a-b+math.pi)%(2*math.pi)-math.pi
def rms_delta(a,b,begin):
    count=2*(FRAMES-begin)
    return math.sqrt(sum(((a[i]-b[i])/32768.)**2
                         for i in range(begin*2,FRAMES*2))/count)
def main(directory):
    directory=pathlib.Path(directory)
    report={'evidence':'P7.1 original native measured KEMAR direct XYZ A/B/C',
            'limitations':'not a dearVR/headphone preference or Windows ASIO test',
            'pairs':{}}
    for side in ('front','rear'):
        for spread in (0,60):
            ident=f'{side}-spread{spread}'
            samples={kind:read(directory/f'sonku-p71-{kind}-{ident}.wav')
                     for kind in ('horizontal','up25','down25')}
            baseline=samples['horizontal']
            entry={}
            for kind in ('up25','down25'):
                candidate=samples[kind]
                immediate=rms_delta(baseline,candidate,0)
                full=rms_delta(baseline,candidate,2500)
                # The first 96 direct frames precede any causal reflection.
                # Nonzero measured direct elevation MUST alter that prefix.
                prefix=sum(abs(baseline[i]-candidate[i])
                           for i in range(96*2))
                peak=max(abs(i) for i in candidate)/32768.
                rms=math.sqrt(sum((x/32768.)**2 for x in candidate)/len(candidate))
                assert full>1e-6 and immediate>1e-6 and prefix>0,(ident,kind,full,prefix)
                assert math.isfinite(rms) and 1e-4<rms<.95 and peak<.95
                freq_data={}
                flags=[]
                for hz in FREQUENCIES:
                    prev=[spectral(baseline,e,hz) for e in (0,1)]
                    current=[spectral(candidate,e,hz) for e in (0,1)]
                    levels=[]
                    for e in (0,1):
                        before=prev[e][0]
                        after=current[e][0]
                        db=20*math.log10(max(after,1e-9)/max(before,1e-9))
                        levels.append(round(db,4))
                        if abs(db)>=1.5:
                            flags.append(f'{hz}Hz ear{e+1} {db:+.2f} dB')
                    entryPhase=delta_phase(current[0][1],current[1][1])
                    oldPhase=delta_phase(prev[0][1],prev[1][1])
                    freq_data[str(hz)]={
                        'earGainChangeDb':levels,
                        'interauralLevelDifferenceChangeDb':round(levels[0]-levels[1],4),
                        'interauralPhaseChangeDeg':round(
                            delta_phase(entryPhase,oldPhase)*180/math.pi,4)}
                entry[kind]={'fullRmsDifference':round(full,8),
                             'sourceRms':round(rms,8),
                             'samplePeak':round(peak,8),
                             'directPrefixChangedSamplesLsb':prefix,
                             'instrumentSpectralAndEarMetrics':freq_data,
                             'tonalityReviewFlags':flags}
            report['pairs'][ident]=entry
    target=directory/'SONKU-P71-XYZ-DIRECT-MUSIC-METRICS.json'
    target.write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
    print('PASS P7.1 12 native FRONT/REAR XYZ measured direct WAVs, headroom, causal direct change, interaural/spectral evidence')
if __name__=='__main__':
    if len(sys.argv)!=2:raise SystemExit('Usage: sonku_p71_xyz_audio_audit.py DIRECTORY')
    main(sys.argv[1])
