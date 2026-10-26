"""Frozen Brushed Metal reference: integrity + non-destructive preview assembly.

This deliberately does not certify multi-theme runtime integration. Execute
with: python tests/theme_reference_fixture_test.py
"""
import hashlib
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CSS = ROOT / "design-references/brushed-metal-v1.0/adapter.css"
SOURCE = ROOT / "AR-MIX-Live-FX-Host-Standalone-v0.3.html"
RENDERER = ROOT / "scripts/render_brushed_reference.py"
EXPECTED_CSS_SHA256 = "d4d3b13fa5ceba0f29665f902735ad0e48c4df44e4107cb00d2ff2f65ec678b6"

spec = importlib.util.spec_from_file_location("armix_brushed_reference", RENDERER)
module = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(module)

raw_css = CSS.read_bytes()
assert hashlib.sha256(raw_css).hexdigest() == EXPECTED_CSS_SHA256, (
    "Reference CSS changed. Intentional design revisions MUST create a new "
    "versioned reference; do not quietly rewrite v1.0."
)
css = raw_css.decode("utf-8")
html = SOURCE.read_text(encoding="utf-8")
assert "FX-GLASS MATERIAL OWNER" in css
assert ".pan-track .pan-thumb{top:3px!important;z-index:3!important}" in css
assert ".strip .channel-actions .mute.active" in css
assert ".strip .channel-actions .solo.active" in css
assert ".strip .utility-row button[data-hpf].active" in css
assert "height:29px!important;min-height:29px!important;max-height:29px!important" in css
assert "data:image/png" not in css  # PAN/fader art is owned by canonical GUI.
assert "<script" in html and "id=\"app\"" in html

result = module.overlay(html, css)
injected = '<style id="arsonmix-brushed-metal-adapter">\n' + css + '\n</style>'
assert result.count(injected) == 1
assert result.replace(injected, "") == html, "Preview must preserve all canonical HTML/JS."
assert result.count("<script") == html.count("<script")
assert "FX-GLASS MATERIAL OWNER" in result
try:
    module.overlay(result, css)
except ValueError:
    pass
else:
    raise AssertionError("Duplicate metal adapter injection was not rejected")
print("PASS: frozen Brushed Metal CSS SHA256, canonical HTML/JS parity, no duplicate style")
