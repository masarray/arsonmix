"""V49 reversible adapter: preserve every old V10–V21 CSS integrity fixture.

The live operator GUI is newer than the historical CSS-migration snapshots.
Recreate the *exact* pre-V49 HTML and WIRE component owner before historical
comparisons. Do not regenerate or relax immutable legacy source hashes.
"""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
WIRE=HTML.parent/"assets/mixer-wire-dots.css"
LEDGER=ROOT/"tests/fixtures/gui-v49-historical-source-deltas.json"

def fingerprint(text):
    h=2166136261
    for c in text:
        h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V49"
    assert data["baseCommit"]=="5efa4f08ca609acf134c23c2ccc9aa5d6e01796e"
    assert len(data["hunks"])==32
    return data

def historical_html():
    data=manifest()
    live=HTML.read_text(encoding="utf-8")
    assert (len(live),fingerprint(live))==(data["current"]["length"],data["current"]["fnv"]), (
        "V49 live HTML changed; update reversible ledger rather than bypass historical gates")
    for h in reversed(data["hunks"]):
        assert live.count(h["new"])==1,"V49 reverse hunk missing/ambiguous"
        live=live.replace(h["new"],h["old"],1)
    assert (len(live),fingerprint(live))==(data["source"]["length"],data["source"]["fnv"]), (
        "V49 failed to reconstruct exact pre-V49 HTML")
    return live

def historical_wire_css():
    owner=manifest()["wireOwner"]
    live=WIRE.read_text(encoding="utf-8")
    assert (len(live),fingerprint(live))==(owner["liveLength"],owner["liveFNV"]), (
        "V49 WIRE CSS changed outside reversible ledger")
    assert live.endswith(owner["suffix"]),"V49 component suffix missing"
    previous=live[:-len(owner["suffix"])]
    assert (len(previous),fingerprint(previous))==(
        owner["historicalLength"],owner["historicalFNV"]),("V49 historical WIRE CSS mismatch")
    return previous
