"""Approved rounded micro key shape, composed in the existing CSS owner.

Keep the historical V28 source and original PNG state tiles byte-identical.
Only deployed dimensions/radius change; no event, native or audio state change.
"""
from pathlib import Path

def apply_rounded_micro_keys(assets: Path) -> None:
    path = assets / "mixer-utility-controls.css"
    css = path.read_text(encoding="utf-8")
    # Width/min-width occur in base, phase and remove rules. Height remains 17px.
    # The 25x15 format key retains its existing independent dimensions.
    anchors = (("width:17px;", "width:21px;", 6),
               ("border-radius:0;", "border-radius:4px;", 1))
    for before, after, count in anchors:
        if css.count(before) != count:
            raise ValueError("Rounded micro key CSS anchor drift: " + before)
        css = css.replace(before, after)
    css = css.replace("V20 true-square metal faces", "Rounded rectangular metal faces")
    # The vacant insert is not a control, but it reserves the exact same
    # horizontal lane as the instantiated 21x17 FX power key.
    rack = assets / "mixer-fx-rack.css"
    rack_css = rack.read_text(encoding="utf-8")
    old = "flex:0 0 17px;width:17px;height:17px;min-width:17px;"
    new = "flex:0 0 21px;width:21px;height:17px;min-width:21px;"
    if rack_css.count(old) != 1:
        raise ValueError("Rounded micro key FX placeholder anchor drift")
    rack_css = rack_css.replace(old, new, 1).replace(
        "Preserve its 17px hardware", "Preserve its 21px hardware", 1)
    path.write_text(css, encoding="utf-8")
    rack.write_text(rack_css, encoding="utf-8")
