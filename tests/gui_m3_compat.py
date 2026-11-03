"""M3 reversible HTML deltas and component-owner CSS tails.

Historical V13–V18 fingerprint ledgers remain immutable. The M3 changes are
explicitly recorded, checked and applied to BOTH sides of CSS parity tests.
"""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LEDGER=ROOT/"tests/fixtures/m3-operator-html-reversible.json"

def fingerprint(s):
    h=2166136261
    for char in s:h=((h^ord(char))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    d=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert d["milestone"]=="M3" and len(d["patches"])==24
    assert (d["baselineHtmlLength"],d["baselineHtmlFNV"])==(230286,"8ad1f954")
    assert (d["currentHtmlLength"],d["currentHtmlFNV"])==(234049,"97cf85c2")
    return d

def transform(html,reverse=False,skip=()):
    d=manifest()
    edits=reversed(d["patches"]) if reverse else d["patches"]
    for entry in edits:
        if entry["name"] in skip:continue
        source=entry["after"] if reverse else entry["before"]
        target=entry["before"] if reverse else entry["after"]
        assert html.count(source)==1,(entry["name"],"non-reversible M3 HTML delta")
        html=html.replace(source,target,1)
    return html

def suffix(owner):
    d=manifest()
    assert owner in ("chrome","fxRack"),owner
    record=d[owner]
    file={"chrome":"mixer-chrome-material.css","fxRack":"mixer-fx-rack.css"}[owner]
    css=(ROOT/"site/preview-v2/assets"/file).read_text(encoding="utf-8")
    base=css[:record["baseSize"]]
    tail=css[record["baseSize"]:]
    assert (len(tail),fingerprint(tail))==(
        record["suffixSize"],record["suffixFNV"]),(file,"M3 owner tail drift")
    return base,tail
