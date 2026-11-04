"""Retired CSS-only MUTE regression replaced by CURRENT approved PNG key contract."""
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/"site/preview/assets"
HTML=(ROOT/"site/preview/index.html").read_text(encoding="utf-8")
CSS=(ASSETS/"mixer-control-keys.css").read_text(encoding="utf-8")
JS=(ASSETS/"mixer-control-keys.js").read_text(encoding="utf-8")
SPRITE=(ASSETS/"mute-solo-pfl-56x37.png").read_bytes()

assert SPRITE[:8]==bytes.fromhex("89504e470d0a1a0a")
assert struct.unpack(">II",SPRITE[16:24])==(112,111)
assert SPRITE[25]==6
assert "assets/mixer-control-keys.css" in HTML
assert "assets/mixer-control-keys.js" in HTML
assert "mute-solo-pfl-56x37.png" in CSS
assert "background-size:200% 300%" in CSS
assert "aria-pressed" in CSS
assert HTML.count("mixer-control-keys.css")==1
assert JS.strip(),"approved input owner JS must not be missing"
assert not (ROOT/"site/preview-v2").exists()
assert not (ROOT/"site/preview-buttons").exists()
print("PASS: approved PNG MUTE/SOLO/PFL replaces rejected CSS-only reference")
