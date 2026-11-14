#!/usr/bin/env python3
"""P8.0 quantitative DSP guard: compare exact-source P7.5 vs P8.0 PCM.
No dearVR recording is available in CI. This does NOT pass headphone parity.
Only Python stdlib and deterministic source tables, no fabricated audio data.
"""
import csv
import json
import math
import pathlib
import sys

def rows(path, keys):
    with path.open(encoding="utf-8", newline="") as stream:
        result = list(csv.DictReader(stream))
    if not result:
        raise ValueError(f"empty evidence: {path}")
    index = {}
    for row in result:
        key = tuple(row[k] for k in keys)
        if key in index:
            raise ValueError(f"duplicate {key} in {path}")
        for col, value in row.items():
            if col in keys or col in {"mode", "source", "room", "scene"}:
                continue
            if not math.isfinite(float(value)):
                raise ValueError(f"nonfinite {col} in {path}: {row}")
        index[key] = row
    return index

def safe_db(ratio):
    if not math.isfinite(ratio) or ratio <= 0.:
        raise ValueError(f"not a finite positive gain ratio: {ratio}")
    return 20 * math.log10(ratio)

def main(directory):
    root = pathlib.Path(directory)
    keys = ("source", "room", "azimuth", "frequency_hz")
    base = rows(root/"sonku_P75_tone.csv", keys)
    p80 = rows(root/"sonku_P80_tone.csv", keys)
    if base.keys() != p80.keys():
        raise ValueError("P80 cases do not match P75 controls exactly")
    room_base = rows(root/"sonku_P75_room.csv", ("scene",))
    room_p80 = rows(root/"sonku_P80_room.csv", ("scene",))
    if room_base.keys() != room_p80.keys():
        raise ValueError("room scenes do not match")
    for k in room_base:
        for p in (room_base[k], room_p80[k]):
            for col in ("first20_energy", "early20_100_energy", "late100_1000_energy"):
                if float(p[col]) < 0:
                    raise ValueError("negative energy")
    report = {"status": "MEASUREMENT_ONLY_NOT_DEARVR_PARITY",
              "source_cases": len(base),
              "room_cases": len(room_base),
              "reference": "P75 same-commit alternate C++ compile",
              "tonal": {}, "side": {}, "room": {}}
    def q(source, azimuth, hz, field):
        k = (source, "OFF", str(azimuth), str(hz))
        if k not in base:
            raise ValueError(f"missing {k}")
        x, y = float(base[k][field]), float(p80[k][field])
        return safe_db(max(y, 1.e-12)/max(x, 1.e-12))

    for azimuth in (0, 180):
        freq_changes = {str(hz): q("MONO", azimuth, hz, "left_amp")
                        for hz in (80, 125, 250, 500, 1000, 2000, 4000, 8000)}
        lf_rel_mid = freq_changes["125"] - freq_changes["1000"]
        report["tonal"][str(azimuth)] = {
            "ear_left_relative_db": freq_changes,
            "LF125_vs_mid1000_improvement_db": lf_rel_mid,
        }
        # A measured common low shelf must be a measurable LF correction,
        # not a broad mid gain or unrestricted +10 dB spectral patch.
        if lf_rel_mid < 0.45 or lf_rel_mid > 5.0:
            raise AssertionError(f"P8 LF target not proven: {azimuth}: {lf_rel_mid:.3f} dB")
        if abs(freq_changes["8000"]) > 0.5:
            raise AssertionError(f"P8 erroneously colors treble: {azimuth}")
        if abs(freq_changes["1000"]) > 1.6:
            raise AssertionError(f"P8 changes vocal mid too much: {azimuth}")
        # An exact inverse-polarity source contains only SIDE; P8 must
        # leave its level alone. (Tests still check the PCM sample bitflow.)
        anti = [q("ANTIPHASE", azimuth, hz, "side_amp") for hz in (125, 1000, 4000)]
        report["side"][str(azimuth)] = anti
        if any(abs(value) > 0.04 for value in anti):
            raise AssertionError(f"anti-phase SIDE changed by correction: {azimuth}: {anti}")

    for key, scene in room_p80.items():
        # rows() intentionally keys even one-column lookups by a tuple.
        name = key[0]
        report["room"][name] = {
            key: float(scene[key]) for key in
            ("first20_energy", "early20_100_energy", "late100_1000_energy")
        }
    dry = report["room"]["SCENE_DRY"]
    er = report["room"]["SCENE_ER_ONLY"]
    late = report["room"]["SCENE_LATE_ONLY"]
    if er["early20_100_energy"] <= dry["early20_100_energy"] + 1.e-10:
        raise AssertionError("six-wall reflection 20..100ms energy not observable")
    if late["late100_1000_energy"] <= dry["late100_1000_energy"] + 1.e-10:
        raise AssertionError("late FDN >100ms energy not observable")
    report["status"] = "PASS_OBJECTIVE_SPECTRAL_STEREO_ROOM_PRESENCE_ONLY"
    (root/"sonku_p80_evidence.json").write_text(
        json.dumps(report, indent=2, sort_keys=True)+"\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "tonal":report["tonal"],
                      "side":report["side"], "room":report["room"]},
                     separators=(",", ":"), sort_keys=True))
    print("NOT TESTED: human music fidelity, naturalness, dearVR parity,"
          " actual ASIO XRUN, headphone/HRTF listener fit.")
    return 0

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: sonku_p80_evidence.py DIRECTORY")
    raise SystemExit(main(sys.argv[1]))
