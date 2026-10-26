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
ari=(root/"web/assets/faders/ARI-FADER.PNG").read_bytes()
assert len(ari)==3801 and ari.startswith(bytes([137,80,78,71,13,10,26,10]))
assert hashlib.sha256(ari).hexdigest()=="de0cd2f8250d8d4eef1ce48cb5c509f596f65134c9c618dce895eaadd54d6132"
assert (int.from_bytes(ari[16:20],"big"),int.from_bytes(ari[20:24],"big"))==(36,77)
channel_uri=css.split('--ari-channel-cap:url("data:image/png;base64,',1)[1].split('")',1)[0]
assert base64.b64decode(channel_uri)==ari,"Channel asset differs from ARI-FADER.PNG"
assert 'top:clamp(38px,var(--pos,50%),calc(100% - 39px))' in css
assert 'transform:translate(-50%,-38px)' in css
master=(root/'web/assets/faders/FADER-RED2.png').read_bytes()
assert hashlib.sha1(b'blob '+str(len(master)).encode()+bytes([0])+master).hexdigest()=='2369e1b9002522600948909246918e57880da1ed'
needle='--master-cap-asset:url("data:image/png;base64,'
assert needle in css, 'Missing master cap CSS source'
master_css=css.split(needle,1)[1].split('")',1)[0]
assert base64.b64decode(master_css)==master, 'Master red cap differs from original user PNG'
assert "<style>"+css+"</style>" in html, "Offline CSS is stale"
assert "<script>"+js+"</script>" in html, "Offline JS is stale"
assert ".cap::before,.cap::after{content:none}" in css
assert "translate(-50%,-35px)" in css
print("PASS: original ARI channels, red MASTER PNG, detent anchors and offline parity")
