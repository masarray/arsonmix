#!/usr/bin/env python3
"""P8.2 instrumented (not perceptual) geometry-late room comparison.
P8.0 and P8.2 are compiled from the same exact PR SHA. Room-only change
may NEVER modify direct-only tonal/side behavior or six-boundary early IR.
"""
import csv
import json
import math
import pathlib
import sys

def load(path, fields):
    with path.open(newline="",encoding="utf8") as stream:
        data=list(csv.DictReader(stream))
    if not data:
        raise ValueError(f"empty input: {path}")
    keyed={}
    for d in data:
        k=tuple(d[p] for p in fields)
        if k in keyed:
            raise ValueError(f"duplicate row {k}: {path}")
        keyed[k]=d
    return keyed

def main(folder):
    root=pathlib.Path(folder)
    tkeys=("source","room","azimuth","frequency_hz")
    t80=load(root/"sonku_P80_tone.csv",tkeys)
    t82=load(root/"sonku_P82_tone.csv",tkeys)
    if t80.keys()!=t82.keys():
        raise AssertionError("direct source matrix changed")
    max_direct_delta=0.
    for key in t80:
        for ch in ("left_amp","right_amp","mid_amp","side_amp"):
            old,new=float(t80[key][ch]),float(t82[key][ch])
            if not math.isfinite(old) or not math.isfinite(new):
                raise AssertionError("nonfinite direct transfer")
            max_direct_delta=max(max_direct_delta,abs(old-new))
    if max_direct_delta>2.e-7:
        raise AssertionError(f"room-only P8.2 altered dry/off direct PCM: {max_direct_delta}")
    r80=load(root/"sonku_P80_room.csv",("scene",))
    r82=load(root/"sonku_P82_room.csv",("scene",))
    if r80.keys()!=r82.keys():
        raise AssertionError("room scenes changed")
    field=("first20_energy","early20_100_energy","late100_1000_energy")
    for scene in ("SCENE_DRY","SCENE_ER_ONLY"):
        k=(scene,)
        for e in field:
            old,new=float(r80[k][e]),float(r82[k][e])
            if abs(old-new)>2.e-7:
                raise AssertionError(f"room-only P8.2 changed {scene} {e}: {old}, {new}")
    b=float(r80[("SCENE_LATE_ONLY",)]["early20_100_energy"])
    v=float(r82[("SCENE_LATE_ONLY",)]["early20_100_energy"])
    if not (b>0 and v>0 and v<b):
        raise AssertionError(f"late arrival was not attenuated early: old {b}, new {v}")
    ratio_db=10*math.log10(v/b)
    if ratio_db>-0.7:
        raise AssertionError(f"late remains dominant in the 20-100ms window: {ratio_db:.2f} dB")
    # Check late field still exists after the geometric propagation delay,
    # rather than passing by muting/zeroing the reverb altogether.
    late_tail=float(r82[("SCENE_LATE_ONLY",)]["late100_1000_energy"])
    if late_tail<=1.e-9:
        raise AssertionError("late tail was muted; this isn't a spatial fix")
    record={
        "status":"PASS_GEOMETRIC_LATE_ONSET_ONLY_NOT_DEARVR_PARITY",
        "source":"EXACT_PR_SOURCE_DUAL_COMPILE",
        "sample_rate":48000,
        "late_window_20_to_100ms":{
            "p80_energy":b,"p82_energy":v,
            "p82_relative_db":ratio_db},
        "p82_late_energy_100_to_1000ms":late_tail,
        "max_direct_off_amplitude_change":max_direct_delta,
        "independent_six_wall_er_unchanged":True,
        "not_proven":["naturalness","dearVR BRIR parity","stereo object positioning",
                      "ASIO 64/128/256","subjective externalization"]
    }
    (root/"sonku_p82_room_evidence.json").write_text(
        json.dumps(record,indent=2,sort_keys=True)+"\n",encoding="utf8")
    print(json.dumps(record,sort_keys=True,separators=(",",":")))
    return 0

if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("usage: sonku_p82_room_evidence.py DIRECTORY")
    raise SystemExit(main(sys.argv[1]))
