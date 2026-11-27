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
    path.write_text(css, encoding="utf-8")
