#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""AN-07A recorded environment baseline; objective report, not listening/FAT."""
import argparse
import csv
import hashlib
import json
import math
import subprocess
import urllib.request
import wave
from pathlib import Path

from recorded_smoke import CLIPS, BASE, README_SHA, download, read_pcm, write_pcm, rms

RATE = 48000
# Original CC0 field recordings; pinned processed PCM releases (not raw
# microphone originals, and NOT the library's synthesized sounds).
SOURCES = (
    ("aircon", "TimBahrij", "234918", "home-air-conditioner.wav",
     "v0.3.0-preview", 11014700, "dbdf5a668ae6fff670af45f0e0eda20f55c2b98c95fba5e894fc74743a5f3f2d"),
    ("rain", "barkenov", "640655", "rain-soft.wav",
     "v0.1.0-preview", 7488044, "bfd2eceddd04eeac960c5cdbada2da3b2c880d0382d2165f4fa35796bc173f7d"),
)
POLICIES = (("unity", "auto", 0, 1, 0),
            ("music", "music", 14, 1, 32),
            ("auto", "auto", 14, 1, 0),
            ("intent", None, 14, 1, 0),
            ("intent-idle", None, 14, 1, 32))
COLUMNS = ("label", "kind", "channel_minus1_all", "begin_frame", "end_frame",
           "error_improvement_db", "desired_projection", "removed_projection",
           "clean_only_error_db_relative", "idle_attenuation_db")

def require(ok, message):
    if not ok:
        raise ValueError(message)

def resample_clean(samples, channels, rate):
    # Deterministic linear up/down interpolation for this baseline only;
    # source rates are reported. Not a production resampler or listening claim.
    require(rate in (44100, 48000), "expected 44.1/48 kHz source")
    if rate == RATE:
        return samples
    frames = len(samples) // channels
    out = []
    for i in range((frames * RATE) // rate):
        j, frac = divmod(i * rate, RATE)
        k = min(j + 1, frames - 1)
        for c in range(channels):
            a, b = samples[j*channels+c], samples[k*channels+c]
            out.append(a + (b-a)*frac/RATE)
    return out

def recorded_noise(source, path):
    name, creator, fsid, file, tag, size, sha = source
    url = f"https://github.com/DNAABR/ambient-sounds-library/releases/download/{tag}/{file}"
    req = urllib.request.Request(url, headers={"User-Agent": "ArSonMix-AN07-corpus/1.0"})
    with urllib.request.urlopen(req, timeout=35) as response:
        data = response.read(size+1)
    require(len(data)==size and hashlib.sha256(data).hexdigest()==sha,
            f"{name}: pinned release bytes/hash mismatch")
    path.write_bytes(data)
    with wave.open(str(path), "rb") as w:
        require((w.getframerate(),w.getnchannels(),w.getsampwidth())==(RATE,2,2),
                f"{name}: source format mismatch")
        require(w.getnframes()>=20*RATE, "recording too short")
        raw = w.readframes(20*RATE)
    pcm = [int.from_bytes(raw[i:i+2],"little",signed=True)/32768
           for i in range(0,len(raw),2)]
    return pcm, f"CC0-1.0 {creator}; https://freesound.org/people/{creator}/sounds/{fsid}/; {url}; SHA256={sha}; distributed DC-removed/spliced/normalized PCM"

def slice_noise(stereo, start, frames, channels):
    require(channels in (1,2) and start>=0 and start+frames<=len(stereo)//2,
            "noise excerpt out of bounds")
    # Mono uses recorded L, not a phase-canceling downmix.
    return [stereo[2*f+c] for f in range(start,start+frames) for c in range(channels)]

def prepare(clip, channels, stereo, snr):
    frames = len(clip)//channels
    lead, tail = RATE//2, 2*RATE
    clean = [0.]*(lead*channels)+clip+[0.]*(tail*channels)
    # Learn [1,4)s; test [8,8+duration)s; no common source samples.
    learn = slice_noise(stereo,RATE,3*RATE,channels)
    noise = slice_noise(stereo,8*RATE,lead+frames+tail,channels)
    active_noise=noise[lead*channels:(lead+frames)*channels]
    level=rms(clip)/(10**(snr/20)*rms(active_noise))
    noise=[v*level for v in noise]
    learn=[v*level for v in learn]
    # Common scale across all three stems preserves SNR and Learn level.
    peak=max(max(map(abs,clean)),max(map(abs,noise)),max(map(abs,learn)),
             max(abs(a+b) for a,b in zip(clean,noise)))
    scale=min(1.,.92/peak)
    clean,noise,learn=([v*scale for v in a] for a in (clean,noise,learn))
    labels=[("active",lead,lead+frames,"active"),
            ("idle",lead+frames+RATE,lead+frames+tail,"idle")]
    width=RATE//10
    windows=[(rms(clip[i*channels:(i+width)*channels]),i)
             for i in range(0,frames-width+1,width)]
    if windows:
        top=max(x[0] for x in windows)
        weak=[x for x in windows if x[0]>top*.01]
        if weak:
            _,i=min(weak)
            labels.append(("weak",lead+i,lead+i+width,"active"))
    return clean,noise,learn,labels

def verify_rows(stdout, labels, channels):
    reader=csv.DictReader(stdout.splitlines())
    require(tuple(reader.fieldnames or ())==COLUMNS,"qualifier schema mismatch")
    rows=list(reader)
    expect={x[0]:x[1:] for x in labels}
    seen=set()
    for row in rows:
        require(None not in row and all(v is not None for v in row.values()),"bad CSV row")
        label=row["label"]; ch=int(row["channel_minus1_all"])
        require(label in expect and -1<=ch<channels and (label,ch) not in seen,"unexpected metric identity")
        seen.add((label,ch))
        begin,end,kind=expect[label]
        require((int(row["begin_frame"]),int(row["end_frame"]),row["kind"])==(begin,end,kind),
                "metric segment mismatch")
        metrics=COLUMNS[5:9] if kind=="active" else COLUMNS[9:]
        for key in metrics:
            require(math.isfinite(float(row[key])),f"nonfinite {key}")
    require(all((label,-1) in seen for label in expect),"missing aggregate row")
    return rows

def transparent(rows):
    for row in rows:
        if row["kind"]=="active":
            require(abs(float(row["error_improvement_db"]))<.001 and
                    float(row["clean_only_error_db_relative"])<-90,
                    "transparent policy changed active PCM")
        else:
            require(abs(float(row["idle_attenuation_db"]))<.001,"transparent policy changed idle PCM")

def target_flags(row, intent, tag):
    if row["channel_minus1_all"]!="-1":
        return []
    flags=[]
    if row["kind"]=="active" and tag in ("auto","intent") and not (tag=="intent" and intent=="music"):
        projection=float(row["desired_projection"])
        if not .97<=projection<=1.03:
            flags.append("FOREGROUND_PROJECTION_TARGET")
        if row["label"]=="active" and intent=="auto-mic" and tag=="intent":
            if float(row["error_improvement_db"])<10:
                flags.append("VOICE_ACTIVE_10DB_TARGET")
    if row["kind"]=="idle" and tag=="intent-idle" and intent=="auto-mic":
        if float(row["idle_attenuation_db"])<25:
            flags.append("VOICE_IDLE_25DB_TARGET")
    return flags

def self_test():
    s=[float(i) for i in range(16)]
    require(slice_noise(s,2,3,1)==[4.,6.,8.],"mono selection")
    require(slice_noise(s,2,2,2)==[4.,5.,6.,7.],"stereo selection")
    require(resample_clean([0.,1.,2.],1,48000)==[0.,1.,2.],"identity resampling")
    require(len(resample_clean([0.]*441,1,44100))==480,"resampling length")
    print("AN07_HELPERS PASS")

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("qualifier",type=Path,nargs="?")
    p.add_argument("output",type=Path,nargs="?")
    p.add_argument("--self-test",action="store_true")
    args=p.parse_args()
    self_test()
    if args.self_test:return
    require(args.qualifier and args.output,"qualifier and new output directory required")
    binary=args.qualifier.resolve(strict=True)
    root=args.output.resolve()
    root.mkdir(parents=True,exist_ok=False)
    download("README.md",README_SHA,root/"SOURCE-README.md")
    clean_sources=[]
    for name,file,sha,intent in CLIPS:
        src=root/file
        download(file,sha,src)
        rate,ch,clip=read_pcm(src)
        clean_sources.append((name,file,sha,intent,rate,ch,resample_clean(clip,ch,rate)))
    summary=[]
    diagnostics=[]
    for source in SOURCES:
        noise_name=source[0]
        stereo,credit=recorded_noise(source,root/source[3])
        for name,file,sha,intent,rate,ch,clip in clean_sources:
            for snr in (0,10):
                case=root/f"{noise_name}-{name}-{snr}dB"
                case.mkdir()
                clean,noise,learn,labels=prepare(clip,ch,stereo,snr)
                for filename,values in (("clean.wav",clean),("noise.wav",noise),("learn.wav",learn)):
                    write_pcm(case/filename,RATE,ch,values)
                (case/"segments.tsv").write_text("".join(f"{a} {b} {c} {d}\n" for a,b,c,d in labels))
                (case/"provenance.tsv").write_text(
                    f"clean\tCC0-1.0\tBart Massey 2024; {BASE+file}; Git blob {sha}; source {rate}Hz; deterministic interpolation if needed, peak scale, PCM16\n"
                    f"noise\tCC0-1.0\t{credit}; test starts 8s; shared SNR scale, PCM16\n"
                    f"learn\tCC0-1.0\t{credit}; independent [1,4)s; same scale, PCM16\n")
                diag=subprocess.run([str(binary),"--learn-diagnostics",str(case/"learn.wav")],
                                    check=True,capture_output=True,text=True,timeout=30)
                observation=json.loads(diag.stdout)
                diagnostics.append(dict(noise=noise_name,clip=name,snr=snr,**observation))
                print("AN07_LEARN_DIAG",noise_name,name,snr,json.dumps(observation,separators=(",",":")),flush=True)
                for tag,mode,amount,protect,silence in POLICIES:
                    mode=mode or intent
                    prefix=case/tag
                    cmd=[str(binary),*[str(case/f) for f in ("clean.wav","noise.wav","learn.wav","segments.tsv","provenance.tsv")],
                         str(prefix),mode,str(amount),str(protect),str(silence)]
                    # Learning rejection is evidence, not a reason to hide a case.
                    result=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
                    if result.returncode:
                        summary.append(dict(noise=noise_name,clip=name,snr=snr,policy=tag,mode=mode,
                                            error=result.stderr.strip()[:1000]))
                        print("AN07_CASE_ERROR",noise_name,name,snr,tag,result.stderr.strip(),flush=True)
                        continue
                    rows=verify_rows(result.stdout,labels,ch)
                    if tag in ("music","unity"):transparent(rows)
                    for row in rows:
                        flags=target_flags(row,intent,tag)
                        summary.append(dict(noise=noise_name,clip=name,snr=snr,policy=tag,mode=mode,
                                            source_rate=rate,channels=ch,flags=flags,**row))
                        if row["channel_minus1_all"]=="-1":
                            print("AN07",noise_name,name,snr,tag,row["label"],
                                  row["error_improvement_db"],row["desired_projection"],
                                  row["idle_attenuation_db"]," ".join(flags) or "observed",flush=True)
                    for f in case.glob(tag+"-*.wav"):f.unlink()
                for f in case.glob("*.wav"):f.unlink()
    errors=[x for x in summary if "error" in x]
    flagged=[x for x in summary if x.get("flags")]
    report={"scope":"recorded noise mixed with recorded foreground; objective baseline only",
            "cases":len(SOURCES)*len(CLIPS)*2,"policy_runs":len(SOURCES)*len(CLIPS)*2*len(POLICIES),
            "sources":[dict(name=s[0],creator=s[1],freesound=s[2],file=s[3],tag=s[4],bytes=s[5],sha256=s[6]) for s in SOURCES],
            "errors":len(errors),"target_flag_rows":len(flagged),"learn_diagnostics":diagnostics,"rows":summary,
            "quality_release_gate":"NOT QUALIFIED; listening, full corpus and hardware required"}
    (root/"report.json").write_text(json.dumps(report,indent=2,allow_nan=False)+"\n")
    print(f"AN07_SUMMARY cases={report['cases']} runs={report['policy_runs']} errors={len(errors)} target_flag_rows={len(flagged)}",flush=True)
    require(not errors,"one or more corpus renders rejected; see report")

if __name__=="__main__":main()
