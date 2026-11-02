"""Regression: CSS knob mask must exist for the public Live GUI and Preview V2.

The reference knob was intentionally supplied as SVG. A missing CSS mask asset
makes the knob entirely transparent even though WIRE routing still works.
"""
from pathlib import Path
from base64 import b64decode
import re
from xml.etree import ElementTree as ET

root = Path(__file__).resolve().parents[1]
ref = root / "site/preview-v2/index.html"
svg = root / "site/preview-v2/assets/knob-ICON.svg"
legacy_preview = root / "site/preview/assets/knob-ICON.svg"
html = ref.read_text(encoding="utf-8")
# Inline the supplied SVG exactly: CSS masks loaded from file:// or another
# Pages subpath may otherwise render fully transparent with no JS error.
match = re.search(r'data:image/svg\+xml;base64,([A-Za-z0-9+/=]+)', html)
assert match, "Inline SVG silhouette missing from CSS mask"
assert svg.is_file() and svg.stat().st_size > 100, "Preview V2 SVG knob missing"
assert legacy_preview.is_file(), "Main Live GUI /preview/ SVG knob missing"
assert svg.read_bytes() == legacy_preview.read_bytes(), "Live preview knob differs from source"
assert b64decode(match.group(1)) == svg.read_bytes(), "Inline WIRE mask does not match supplied SVG"
assert 'mask-mode:alpha!important' in html, "Inline SVG must use explicit alpha mask mode"
xml = ET.parse(svg).getroot()
assert xml.tag.endswith("svg")
assert any(e.tag.endswith("path") for e in xml.iter()), "SVG knob silhouette absent"
print("PASS: inline SVG knob mask matches original asset, with identical Pages fallback assets")
