"""Historical V34 key dimensions are now verified on the only approved GUI."""
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/"site/preview/index.html").read_text(encoding="utf-8")
CSS=(ROOT/"site/preview/assets/mixer-control-keys.css").read_text(encoding="utf-8")
SPRITE=(ROOT/"site/preview/assets/mute-solo-pfl-56x37.png").read_bytes()
assert SPRITE[:8]==b"\\x89PNG\\r\\n\\x1a\\n"
assert struct.unpack(">II",SPRITE[16:24])==(112,111)
assert SPRITE[25]==6
assert "switch-section mx-hw-row" in HTML
assert "mixer-control-keys.css" in HTML
assert "mute-solo-pfl-56x37.png" in CSS
assert "background-size:200% 300%" in CSS
assert "aria-pressed" in CSS
assert not (ROOT/"site/preview-v34").exists()
assert not (ROOT/"site/preview-v2").exists()
print("PASS: ONE approved PNG hardware-key component; no V34/V2 source renderer")
