"""Regression: CSS knob mask must exist for the public Live GUI and Preview V2.

The reference knob was intentionally supplied as SVG. A missing CSS mask asset
makes the knob entirely transparent even though WIRE routing still works.
"""
from pathlib import Path
from xml.etree import ElementTree as ET

root = Path(__file__).resolve().parents[1]
ref = root / "site/preview-v2/index.html"
svg = root / "site/preview-v2/assets/knob-ICON.svg"
legacy_preview = root / "site/preview/assets/knob-ICON.svg"
html = ref.read_text(encoding="utf-8")
assert 'assets/knob-ICON.svg' in html, "SVG knob reference was silently removed"
assert svg.is_file() and svg.stat().st_size > 100, "Preview V2 SVG knob missing"
assert legacy_preview.is_file(), "Main Live GUI /preview/ SVG knob missing"
assert svg.read_bytes() == legacy_preview.read_bytes(), "Live preview knob differs from source"
xml = ET.parse(svg).getroot()
assert xml.tag.endswith("svg")
assert any(e.tag.endswith("path") for e in xml.iter()), "SVG knob silhouette absent"
print("PASS: exact SVG knob silhouette exists in both Live GUI and Preview V2 routes")
