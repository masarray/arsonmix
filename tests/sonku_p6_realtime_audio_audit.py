#!/usr/bin/env python3
"""P6 actual native SQUARE ROOM fixture: no phase/time/source confounds."""
import json
import math
import pathlib
import struct
import sys
import wave

def read_wav(path):
    with wave.open(str(path), "rb") as audio:
        if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(),
            audio.getnframes()) != (2, 2, 48000, 144000):
            raise AssertionError(f"unexpected WAV layout: {path}")
        raw = audio.readframes(audio.getnframes())
    sample = struct.unpack("<" + "h" * (len(raw)//2), raw)
    return sample

def main(directory):
    directory = pathlib.Path(directory)
    result = {"method": "same-source actual native ROOM LIGHT vs prepared P6",
              "qualification": "machine PCM only; not ASIO / dearVR listening",
              "pairs": {}}
    for side in ("front", "rear"):
        for spread in (0, 60):
            name = f"{side}-spread{spread}"
            light = read_wav(directory/f"sonku-p6-light-{name}.wav")
            full = read_wav(directory/f"sonku-p6-directional-{name}.wav")
            assert len(light) == len(full)
            # Physically causal reflections cannot affect the immediate
            # native direct HRTF; the same stereo musical source is used.
            assert light[:96*2] == full[:96*2], name
            delta = [(a-b)/32768.0 for a,b in zip(light[2500*2:],full[2500*2:])]
            rms_delta = math.sqrt(sum(x*x for x in delta)/len(delta))
            max_peak = max(max(abs(x) for x in light),
                           max(abs(x) for x in full))/32768.0
            full_rms = math.sqrt(sum((x/32768.0)**2 for x in full)/len(full))
            if not (math.isfinite(rms_delta) and rms_delta>1e-6
                    and 1e-4<full_rms<0.95 and max_peak<0.95):
                raise AssertionError(f"bad P6 PCM {name}: "
                    f"delta={rms_delta}, rms={full_rms}, peak={max_peak}")
            result["pairs"][name] = {
                "afterReflectionRmsDifference": round(rms_delta,8),
                "candidateRms": round(full_rms,8),
                "maxSamplePeak": round(max_peak,8),
                "identicalDirectPrefixFrames": 96
            }
    output=directory/"SONKU-P6-REALTIME-PCM-METRICS.json"
    output.write_text(json.dumps(result,indent=2,sort_keys=True)+"\n")
    print("PASS P6 4 native ROOM A/B pairs: 48k 16bit stereo, "
          "causal prefix, finite headroom, measured reflected difference")
if __name__ == "__main__":
    if len(sys.argv)!=2:
        raise SystemExit("Usage: sonku_p6_realtime_audio_audit.py DIRECTORY")
    main(sys.argv[1])
