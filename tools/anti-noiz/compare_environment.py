#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Paired frozen AN-08D vs candidate; never equate render success with quality."""
import json
import math
import sys
from pathlib import Path

IDENTITY = ("noise", "clip", "snr", "policy", "mode", "label",
            "channel_minus1_all", "begin_frame", "end_frame", "kind",
            "source_rate", "channels")

def index(report):
    if report["errors"] or report["cases"] != 12 or report["policy_runs"] != 60:
        raise ValueError("incomplete or failed recorded corpus")
    result = {}
    for row in report["rows"]:
        key = tuple(row[k] for k in IDENTITY)
        if key in result:
            raise ValueError(f"duplicate row {key}")
        result[key] = row
    return result

def metric(row, key):
    v = float(row[key])
    if not math.isfinite(v):
        raise ValueError(f"nonfinite metric {key}")
    return v

def compare(base, candidate):
    if base["sources"] != candidate["sources"]:
        raise ValueError("corpus source mismatch")
    a, b = index(base), index(candidate)
    if a.keys() != b.keys() or not a:
        raise ValueError("paired row identity mismatch")
    changes, regressions = [], []
    for key, old in a.items():
        new = b[key]
        row = dict(zip(IDENTITY, key))
        voice = row["mode"] in ("voice", "auto-mic") and row["policy"] in ("intent", "intent-idle")
        if row["kind"] == "active":
            delta = metric(new,"error_improvement_db") - metric(old,"error_improvement_db")
            loss = abs(metric(new,"desired_projection")-1) - abs(metric(old,"desired_projection")-1)
            row.update(error_delta_db=delta, projection_error_delta=loss,
                       clean_only_error_delta_db=metric(new,"clean_only_error_db_relative")-metric(old,"clean_only_error_db_relative"))
            # Full-active projection is more stable than a weak 100ms proxy;
            # preserve all weak/per-channel values as evidence, not a claim.
            if voice and row["label"] == "active" and (delta < -.5 or loss > .02 or row["clean_only_error_delta_db"] > 1.):
                regressions.append(row)
            if voice and row["label"] == "weak" and (loss > .03 or delta < -1.):
                regressions.append(row)
            if not voice and (abs(delta)> .001 or abs(loss)> .0001):
                regressions.append(row)
        else:
            delta = metric(new,"idle_attenuation_db")-metric(old,"idle_attenuation_db")
            row.update(idle_delta_db=delta)
            if (voice and delta < -1.) or (not voice and abs(delta) > .001):
                regressions.append(row)
        changes.append(row)
    # Band diagnostics are not psychoacoustic metrics. Both sides use the
    # same offline filter and exact timeline; require finite paired values.
    def bands(report):
        rows = {}
        for row in report["band_metrics"]:
            key = tuple(row[k] for k in ("noise","snr","policy","band"))
            if key in rows: raise ValueError("duplicate band identity")
            rows[key] = row
        return rows
    ba, bb = bands(base), bands(candidate)
    if ba.keys() != bb.keys() or len(ba) != 16:
        raise ValueError("incomplete paired speech band diagnostics")
    band_changes = []
    for key, old in ba.items():
        new = bb[key]
        d = {k:new[k] for k in ("noise","snr","policy","band")}
        for field in ("error_db","projection","clean_only_error_db"):
            d[field+"_delta"] = metric(new,field)-metric(old,field)
        band_changes.append(d)
    idle_voice=[x["idle_delta_db"] for x in changes if x["kind"]=="idle" and x["policy"]=="intent-idle" and x["mode"] in ("voice","auto-mic") and x["clip"]=="speech" and int(x["channel_minus1_all"])==-1]
    if len(idle_voice)!=4: raise ValueError(f"expected four speech idle pairs, got {len(idle_voice)}")
    improvement=sum(idle_voice)/len(idle_voice)
    benefit=improvement>=.25 and candidate["target_flag_rows"]<=base["target_flag_rows"]
    return {"mean_speech_idle_delta_db":improvement,"benefit_gate":benefit,"band_changes":band_changes,"baseline_flags":base["target_flag_rows"], "candidate_flags":candidate["target_flag_rows"],
            "paired_rows":len(changes), "regressions":regressions, "changes":changes,
            "release_qualified":False,
            "scope":"objective paired comparison; targets and listening/FAT remain independent"}

if __name__ == "__main__":
    base, candidate, out = map(Path, sys.argv[1:])
    report = compare(json.loads(base.read_text()), json.loads(candidate.read_text()))
    out.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    print("AN08E", "paired", report["paired_rows"], "flags", report["baseline_flags"],
          "->", report["candidate_flags"], "regressions", len(report["regressions"]))
    for row in report["band_changes"]:
        print("AN08E_BAND", json.dumps(row, sort_keys=True))
    for row in report["regressions"]:
        print("AN08E_REGRESSION", json.dumps(row, sort_keys=True))
    print("AN08E_BENEFIT",json.dumps({"mean_speech_idle_delta_db":report["mean_speech_idle_delta_db"],"passed":report["benefit_gate"]}))
    if report["regressions"] or not report["benefit_gate"]:
        raise SystemExit(1)
