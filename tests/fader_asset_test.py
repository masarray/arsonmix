"""Regression guard for original user-supplied metallic fader PNG and offline build."""
from pathlib import Path
import base64
import hashlib
import re

root=Path(__file__).resolve().parents[1]
asset=(root/"web/assets/fader-silver2.png").read_bytes()
css=(root/"web/styles.css").read_text(encoding="utf-8")
js=(root/"web/app.js").read_text(encoding="utf-8")
html=(root/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").read_text(encoding="utf-8")
data=re.search(r'data:image/png;base64,([A-Za-z0-9+/=]+)',css)
assert data, "Embedded metallic asset missing from web/styles.css"
assert base64.b64decode(data.group(1))==asset, "Embedded thumb differs from source PNG"
assert (len(asset),asset[:8])==(2786,b"\x89PNG\r\n\x1a\n")
assert hashlib.sha256(asset).hexdigest()=="0f45fe5aebc8db7b65c4163ffad083cbe3965d59cde7056add94812ac067a899"
assert "<style>"+css+"</style>" in html, "Offline CSS is stale"
assert "<script>"+js+"</script>" in html, "Offline JS is stale"
assert ".cap::before,.cap::after{content:none}" in css
assert "translate(-50%,-35px)" in css
print("PASS: supplied 31x90 PNG unchanged, detent anchor, modular and offline assets synced")
