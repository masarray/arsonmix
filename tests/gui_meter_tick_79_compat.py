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

def historical_meter_css():
    """Reconstruct pre-tweak M3 meter stylesheet for V13 immutable tail audits."""
    css=METER.read_text(encoding="utf-8")
    new_comment="/* Owner-approved M3 precision: 7px left and 9px right insets for every dBFS numeral. */"
    old_comment="/* Keep all dBFS values on one right-hand numeral baseline. */"
    old_rule="left:4px;right:0;top:-5.5px;text-align:right;"
    assert css.count(new_comment)==1 and css.count(RULE)==1
    prior=css.replace(new_comment,old_comment,1).replace(RULE,old_rule,1)
    assert prior.count(old_comment)==1 and prior.count(old_rule)==1
    return prior
