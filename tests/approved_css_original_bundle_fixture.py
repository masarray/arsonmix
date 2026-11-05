"""Only for CSS visual-parity tests: reconstruct original ordered CSS links.

Do not recreate another GUI. HTML+JS+PNG are copied from compiled /preview/,
and only the ONE generated runtime stylesheet link is exchanged for the
18 original source owner links before testing the original owner CSS files.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "site/preview-v2/index.html"
ORIGINAL_LINK = re.compile(
    r'<link\s+rel="stylesheet"\s+href="assets/[^"]+\.css(?:\?[^"]*)?">'
)
RUNTIME_LINK = re.compile(
    r'<link\s+rel="stylesheet"\s+href="assets/arsonmix-approved-runtime\.css\?sha=[a-f0-9]{16}">'
)


def reconstructed_original_css_html(shipped_html: str) -> str:
    original = ORIGINAL_LINK.findall(SOURCE.read_text(encoding="utf-8"))
    if len(original) != 18 or len(set(original)) != 18:
        raise ValueError("Approved source CSS ownership changed unexpectedly")
    if len(RUNTIME_LINK.findall(shipped_html)) != 1:
        raise ValueError("Approved runtime CSS must be one content-addressed link")
    if len(ORIGINAL_LINK.findall(shipped_html)) != 1:
        raise ValueError("Unexpected extra runtime CSS stack")
    return RUNTIME_LINK.sub("\n".join(original), shipped_html, count=1)
