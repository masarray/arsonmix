#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Paired AN-07B vs candidate comparison; never equate render success with quality."""
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
        voice = row["mode"] in ("voice", "auto-mic") and row["policy"] == "intent-idle"
        if row["kind"] == "active":
            delta = metric(new,"error_improvement_db") - metric(old,"error_improvement_db")
            loss = abs(metric(new,"desired_projection")-1) - abs(metric(old,"desired_projection")-1)
            row.update(error_delta_db=delta, projection_error_delta=loss,
                       clean_only_error_delta_db=metric(new,"clean_only_error_db_relative")-metric(old,"clean_only_error_db_relative"))
            # Full-active projection is more stable than a weak 100ms proxy;
            # preserve all weak/per-channel values as evidence, not a claim.
            if voice and row["label"] == "active" and (delta < -.5 or loss > .02):
                regressions.append(row)
            if not voice and (abs(delta)> .001 or abs(loss)> .0001):
                regressions.append(row)
        else:
            delta = metric(new,"idle_attenuation_db")-metric(old,"idle_attenuation_db")
            row.update(idle_delta_db=delta)
            if (voice and delta < -1.) or (not voice and abs(delta) > .001):
                regressions.append(row)
        changes.append(row)
    return {"baseline_flags":base["target_flag_rows"], "candidate_flags":candidate["target_flag_rows"],
            "paired_rows":len(changes), "regressions":regressions, "changes":changes,
            "release_qualified":False,
            "scope":"objective paired comparison; targets and listening/FAT remain independent"}

if __name__ == "__main__":
    base, candidate, out = map(Path, sys.argv[1:])
    report = compare(json.loads(base.read_text()), json.loads(candidate.read_text()))
    out.write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    print("AN07D", "paired", report["paired_rows"], "flags", report["baseline_flags"],
          "->", report["candidate_flags"], "regressions", len(report["regressions"]))
    for row in report["regressions"]:
        print("AN07D_REGRESSION", json.dumps(row, sort_keys=True))
    if report["regressions"]:
        raise SystemExit(1)
