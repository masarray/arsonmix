"""Lock the exact original silver/gold PNG artwork and offline/WebView2 geometry."""
from pathlib import Path
import base64
import hashlib
import re

root=Path(__file__).resolve().parents[1]
css=(root/"web/styles.css").read_text(encoding="utf-8")
js=(root/"web/app.js").read_text(encoding="utf-8")
html=(root/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").read_text(encoding="utf-8")
source=(root/"web/styles-src/41-mixer-performance.css").read_text(encoding="utf-8")
assert css.endswith(source), "Generated WebView2 CSS differs from source module"
assert "<style>"+css+"</style>" in html, "Standalone CSS drift"
assert "<script>"+js+"</script>" in html, "Standalone JS drift"

# Old resized copies and duplicate artwork must not ship as active assets.
for obsolete in ("ARI-FADER-GOLD-30x70.png","ARI-FADER-SILVER-30x70.png","ARI-FADER.PNG"):
    assert not (root/"web/assets/faders"/obsolete).exists(),obsolete

assets=(
    ("FADER-ARI-SILVER.png","--ari-channel-cap",3801,"de0cd2f8250d8d4eef1ce48cb5c509f596f65134c9c618dce895eaadd54d6132"),
    ("FADER-ARI-GOLD.png","--ari-master-cap",3997,"8396598e7456cb7f132c9f91ee39cb68ff7c331ae57f04aab15c259ea4143f62"),
)
for filename,variable,length,expected_sha256 in assets:
    original=(root/"web/assets/faders"/filename).read_bytes()
    assert len(original)==length,filename
    assert hashlib.sha256(original).hexdigest()==expected_sha256,filename
    assert original[:8]==b"\\x89PNG\\r\\n\\x1a\\n",filename
    assert (int.from_bytes(original[16:20],"big"),int.from_bytes(original[20:24],"big"))==(36,77),filename
    pattern=re.escape(variable)+r':url\\("data:image/png;base64,([A-Za-z0-9+/=]+)"\\)'
    matched=re.search(pattern,css)
    assert matched and base64.b64decode(matched.group(1))==original,filename+" embedded PNG mismatch"

assert 'width:36px;height:77px' in source
assert 'background-size:36px 77px!important' in source
assert 'top:clamp(38px,var(--pos,50%),calc(100% - 39px))' in source
assert 'transform:translate(-50%,-38px)' in source
assert 'distance:Math.max(24,r.height-77)' in js
assert 'background-image:var(--ari-channel-cap)!important' in source
assert 'background-image:var(--ari-master-cap)!important' in source
assert ':not([data-fader="master"])' in source
assert '[data-fader="master"] > .cap' in source
assert 'filter:none!important' in source

# Historical art is retained for rollback; active heads must use new assets.
legacy=(root/"web/assets/faders/FADER-RED2.png").read_bytes()
assert hashlib.sha1(b"blob "+str(len(legacy)).encode()+b"\\0"+legacy).hexdigest()=="2369e1b9002522600948909246918e57880da1ed"
assert ".cap::before,.cap::after{content:none}" in css
print("PASS: untouched 36x77 silver channels + gold MASTER, correct detent and offline parity")
