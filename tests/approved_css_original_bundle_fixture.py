"""Only for CSS visual-parity tests: reconstruct original ordered CSS links.

Do not recreate another GUI. HTML+JS+PNG are copied from compiled /preview/,
and only the ONE generated runtime stylesheet link is exchanged for the
18 original owner links. Restore the sheets after operator UX composition
and before the CSS optimizer, so both sides have the same presentation.
"""
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "site/preview-v2/index.html"
sys.path.insert(0, str(ROOT / "scripts"))
from compile_approved_css import LINK
from compose_operator_ux import apply_operator_ux
from compose_lms import apply_lms
from approved_fx_rack_interaction import apply_fx_rack_interaction
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

def restore_original_css_bundle(bundle: Path) -> None:
    index = bundle / "index.html"
    assets = bundle / "assets"
    original_html = reconstructed_original_css_html(index.read_text(encoding="utf-8"))
    source_html = SOURCE.read_text(encoding="utf-8")
    for _, name, _, _ in LINK.findall(source_html):
        owner = "preview-buttons" if name == "mixer-control-keys.css" else "preview-v2"
        shutil.copyfile(ROOT / "site" / owner / "assets" / name, assets / name)
    # The shipped DOM already contains the same composition. Reapply it
    # only to reconstruct the CSS owner bytes, using its original HTML input.
    composed=apply_operator_ux(apply_fx_rack_interaction(source_html, assets), assets)
    apply_lms(composed, assets)  # same new owner bytes before CSS optimization
    index.write_text(original_html, encoding="utf-8")
