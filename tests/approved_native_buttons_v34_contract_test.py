"""Approved native-sized PNG buttons stay pixel-aligned and retain mixer intent."""
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/"site/preview-v34/index.html").read_text(encoding="utf-8")
SPRITE=(ROOT/"site/preview-v34/assets/mute-solo-pfl-56x37.png").read_bytes()

assert SPRITE[:8]==b"\x89PNG\r\n\x1a\n", "sprite must be PNG"
assert struct.unpack(">II",SPRITE[16:24])==(112,111), "2x3 56x37 frames"
assert SPRITE[25]==6, "sprite must be RGBA for transparent housing"
assert "codepen-buttons-wrapper" not in HTML, "obsolete layered mock controls remain"
assert HTML.count('class="ar-native-key ar-native-key--')==3
for kind,action,y in (("mute","mute","0"),("solo","solo","50%"),("pfl","cue","100%")):
    assert f'ar-native-key--{kind}" data-action="{action}"' in HTML
    assert f'ar-native-key--{kind}[aria-pressed="true"]' in HTML
assert 'background-color:transparent!important' in HTML
assert 'background-size:200% 300%!important' in HTML
assert 'transform:none!important' in HTML
print("PASS approved native MUTE/SOLO/PFL sprite: geometry, alpha, actions, states")
