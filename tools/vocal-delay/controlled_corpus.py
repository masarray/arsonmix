#!/usr/bin/env python3
"""VD16 controlled real-vocal timbre smoke; not natural lyric/climax ground truth.
Fetches only pinned CC BY 4.0 source clips, verifies exact Git blob hash.
No dependencies on source corpus at runtime. No copyrighted TV performance.
"""
import array
import csv
import hashlib
import io
import math
import pathlib
import subprocess
import sys
import tempfile
import urllib.request
import wave

PIN = "c48ab544e94e9788fec11be52b365026bb4a017c"
SOURCE = "https://raw.githubusercontent.com/mblackman/vocalset-vowel-voicebanks/" + PIN + "/source/vocalset/"
CLIPS = {
    "f1_long_straight_a.wav": "b609083332a343eb984105b72eb9006108e5368c",
    "f1_long_straight_i.wav": "44a0e6aa906d541eb3d3538d8fc4617ab4ad2f58",
    "f6_long_straight_a.wav": "e21d4cc05bfe07e6f7cffb0f3e326c9bc2c3d7af",
    "f6_long_straight_i.wav": "d99f3fa2dd71c08cadf3a85aaa2c745da84018a1",
    "m1_long_straight_a.wav": "b3bfef541e00ea71a9da01c46156e8a006da44d1",
    "m1_long_straight_i.wav": "6b21881a24e0ec17afab623512937983b5049abd",
    "m6_long_straight_a.wav": "3d413a1fe0ba04ec54940e2ccd5d892611daa305",
    "m6_long_straight_i.wav": "3153f1a9264b1b11a283558eb7d43ed7281b2433",
}
RATE = 48000
N = RATE * 6
MODES = ["14 Idol Concert", "15 Grand Ballad", "16 Modern Pop", "17 Dangdut Throw",
         "18 Power Rock", "19 Ambient Throw", "20 Intimate Throw", "21 Manual Throw"]

def fetch_clip(name, digest, directory):
    req = urllib.request.Request(SOURCE + name, headers={"User-Agent": "ArSonMix-VD16-corpus"})
    with urllib.request.urlopen(req, timeout=25) as response:
        data = response.read(2_000_001)
    if len(data) > 2_000_000:
        raise ValueError("source exceeds bounded size")
    actual = hashlib.sha1(("blob %d\0" % len(data)).encode() + data).hexdigest()
    if actual != digest:
        raise ValueError("source Git blob mismatch: " + name)
    # Source is pinned PCM WAV. No system ffmpeg dependency; controlled
    # fixture-only linear resampling is not part of the shipping DSP.
    (directory / name).write_bytes(data)
    with wave.open(io.BytesIO(data),"rb") as wav:
        channels,width,rate=wav.getnchannels(),wav.getsampwidth(),wav.getframerate()
        if channels not in (1,2) or width not in (2,3,4) or not 8000<=rate<=192000:
            raise ValueError("unsupported source PCM: "+name)
        raw=wav.readframes(wav.getnframes())
    scale=float(1<<(width*8-1))
    step=channels*width
    samples=[sum(int.from_bytes(raw[i+k*width:i+(k+1)*width],"little",signed=True) for k in range(channels))/(channels*scale)
             for i in range(0,len(raw),step)]
    if rate==RATE:
        return samples
    result=[]
    for i in range(int((len(samples)-1)*RATE/rate)):
        at=i*rate/RATE
        left=int(at)
        result.append(samples[left]+(samples[left+1]-samples[left])*(at-left))
    return result

def stable_segment(samples):
    # Find a stable 700ms region using original 10ms RMS frames.
    # This is deterministic and does not inspect DSP results.
    frame = RATE // 100
    energies = [math.sqrt(sum(v*v for v in samples[i:i+frame])/frame)
                for i in range(0, len(samples)-frame+1, frame)]
    best = None
    for i in range(0, len(energies)-70+1):
        part = energies[i:i+70]
        lo, hi = min(part), max(part)
        if lo < .003 or hi <= 0:
            continue
        key = (lo / hi, sum(part)/70)
        if best is None or key > best[0]:
            best = (key, i)
    if best is None:
        raise ValueError("no stable voiced source section")
    return samples[best[1]*frame:(best[1]+70)*frame], best[1]/100

def fixture(segment, positive):
    samples = [0.0]*N
    def add(start, end, rms):
        a, b = round(start*RATE), round(end*RATE)
        length = b-a
        chunk = segment[:length]
        energy = math.sqrt(sum(x*x for x in chunk)/length)
        gain = min(rms/energy, .85/max(abs(x) for x in chunk))
        edge = int(.008*RATE)
        for i,x in enumerate(chunk):
            samples[a+i] = x*gain*min(1., i/edge, (length-1-i)/edge)
    for k in range(5):
        add(k*.24,k*.24+.18,.048)
    add(1.65,2.10,.050)
    add(2.10,2.55,.18 if positive else .050)
    add(4.20,4.80,.045)
    return array.array("h",(round(max(-1.,min(32767/32768,x))*32768) for x in samples))

def write(path, pcm):
    if sys.byteorder != "little":
        pcm = array.array("h",pcm)
        pcm.byteswap()
    with wave.open(str(path),"wb") as out:
        out.setparams((1,2,RATE,0,"NONE","not compressed"))
        out.writeframes(pcm.tobytes())

def read_stereo(path):
    with wave.open(str(path),"rb") as f:
        if (f.getnchannels(),f.getsampwidth(),f.getframerate())!=(2,2,RATE):
            raise ValueError("unexpected render format")
        a = array.array("h",f.readframes(f.getnframes()))
    if sys.byteorder != "little":
        a.byteswap()
    return a

def measure(dry,wet):
    first = None
    early = late = 0
    tail_energy = 0.
    tail_n = 0
    for i in range(0,len(dry),2):
        frame = i//2
        dl,dr = wet[i]-dry[i],wet[i+1]-dry[i+1]
        if dl or dr:
            if first is None: first=frame/RATE
            if frame < round(2.55*RATE): early += 1
            if frame >= round(4.23*RATE): late += 1
        if round(2.55*RATE)<=frame<round(4.20*RATE):
            tail_energy += dl*dl+dr*dr
            tail_n += 2
    return first,early,late,math.sqrt(tail_energy/tail_n)/32768

def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    out = pathlib.Path(sys.argv[2]).resolve()
    out.mkdir(parents=True,exist_ok=True)
    (out/"ATTRIBUTION.txt").write_text(
        "VocalSet: A Singing Voice Dataset, Wilkins, Seetharaman, Wahl & Pardo (ISMIR 2018).\n"
        "Original dataset v1.2: https://zenodo.org/records/1442513\n"
        "CC BY 4.0 https://creativecommons.org/licenses/by/4.0/\n"
        "Pinned redistribution: https://github.com/mblackman/vocalset-vowel-voicebanks/tree/"+PIN+
        "/source/vocalset\n"
        "Changes: resampled to mono 48k PCM16, cropped to stable vowel regions, assembled with fixed "
        "dynamic envelopes, processed by ArSonMix DSP; these are NOT natural song performances.\n"
        "No source WAVs are committed or shipped in the application.\n")
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        for name,digest in CLIPS.items():
            seg,start = stable_segment(fetch_clip(name,digest,work))
            stem = name.removesuffix(".wav")
            for positive in (False,True):
                label = "climax" if positive else "normal"
                wav = work/"input.wav"
                write(wav,fixture(seg,positive))
                render = work/"render"
                subprocess.run([str(binary),str(render),str(wav)],check=True,stdout=subprocess.DEVNULL)
                dry = read_stereo(render/"00 Dry reference.wav")
                for mode in MODES:
                    first,early,late,tail = measure(dry,read_stereo(render/(mode+".wav")))
                    rows.append(dict(clip=stem,source_blob=digest,source_start_sec=start,scenario=label,
                                     preset=mode,first_wet_sec="" if first is None else first,
                                     early_wet_frames=early,late_wet_frames=late,tail_rms=tail))
                # Only positive dry/Idol renders; keep artifact modest.
                if positive:
                    for mode in ("00 Dry reference","14 Idol Concert"):
                        (out/(stem+" "+mode+".wav")).write_bytes((render/(mode+".wav")).read_bytes())
    with (out/"results.csv").open("w",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]))
        writer.writeheader();writer.writerows(rows)
    idol=[r for r in rows if r["preset"]==MODES[0]]
    normal=[r for r in idol if r["scenario"]=="normal"]
    positive=[r for r in idol if r["scenario"]=="climax"]
    correct=sum(r["first_wet_sec"]!="" and r["tail_rms"]>.0005 and not r["early_wet_frames"] and not r["late_wet_frames"] for r in positive)
    false=sum(r["first_wet_sec"]!="" for r in normal)
    manual=[r for r in rows if r["preset"]==MODES[-1]]
    manual_false=sum(r["first_wet_sec"]!="" for r in manual)
    summary=f"VD16_CONTROLLED_HUMAN_VOWELS clips={len(CLIPS)} cases={len(rows)} idol_climax={correct}/{len(positive)} idol_normal_false={false}/{len(normal)} manual_false={manual_false}/{len(manual)}"
    print(summary);(out/"summary.txt").write_text(summary+"\n")
    # Conservative minimum engineering gate; no claim of natural phrase accuracy.
    if correct<6 or false or manual_false:
        raise SystemExit("VD16 controlled corpus gate failed; inspect results.csv")
if __name__=="__main__":
    main()
