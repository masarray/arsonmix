"""Reversible M3 meter numeral tweak: preserve immutable V22 source fingerprints.

Only the meter asset's cache token changed in canonical HTML. Historical
CSS migration tests must compare the original V22 document, not relax hashes.
The meter component itself remains the single owner of the 7px/9px rule.
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
METER=HTML.parent/"assets/mixer-meter-display.css"
OLD="assets/mixer-meter-display.css?v=css-owner-v9-ruler-m3-left-70"
NEW=OLD+"-tick79"
RULE="left:7px;right:9px;top:-5.5px;text-align:right;"

def historical_html():
    css=METER.read_text(encoding="utf-8")
    assert css.count(RULE)==1, "M3 physical dBFS inset rule missing or duplicated"
    html=HTML.read_text(encoding="utf-8")
    assert html.count(NEW)==1 and html.count(OLD+'"')==0, "Meter CSS cache key drift"
    return html.replace(NEW,OLD,1)
