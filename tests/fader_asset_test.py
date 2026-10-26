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
silver=(root/"web/assets/faders/ARI-FADER-SILVER-30x70.png").read_bytes()
gold=(root/"web/assets/faders/ARI-FADER-GOLD-30x70.png").read_bytes()
assert len(silver)==2925 and hashlib.sha256(silver).hexdigest()=="88f3e7932466b12fbcffe8fb69353371496daea32fa19cda88dc413516d4bb34"
assert len(gold)==4357 and hashlib.sha256(gold).hexdigest()=="8bc265a895de882842817275f439bcbedfd117ade62ea142a71cc3ce3b33eb59"
for png in (silver,gold):
    assert png.startswith(bytes([137,80,78,71,13,10,26,10]))
    assert (int.from_bytes(png[16:20],"big"),int.from_bytes(png[20:24],"big"))==(30,70)
channel_uri=css.split('--ari-channel-cap:url("data:image/png;base64,',1)[1].split('")',1)[0]
assert base64.b64decode(channel_uri)==silver,"Rendered channel PNG differs from approved silver asset"
assert 'top:clamp(34px,var(--pos,50%),calc(100% - 36px))' in css
assert 'transform:translate(-50%,-34px)' in css
assert 'width:30px;height:70px' in css
assert 'distance:Math.max(24,r.height-70)' in js
master=(root/'web/assets/faders/FADER-RED2.png').read_bytes()
assert hashlib.sha1(b'blob '+str(len(master)).encode()+bytes([0])+master).hexdigest()=='2369e1b9002522600948909246918e57880da1ed'
needle='--master-cap-asset:url("data:image/png;base64,'
assert needle in css, 'Missing master cap CSS source'
master_css_legacy=css.split(needle,1)[1].split('")',1)[0]
assert base64.b64decode(master_css_legacy)==master, 'Original red MASTER fallback unexpectedly altered'
master_css=css.rsplit(needle,1)[1].split('")',1)[0]
assert base64.b64decode(master_css)==gold,'Rendered MASTER PNG differs from approved gold asset'
assert channel_uri != master_css,'Channel silver and MASTER gold must be distinct assets'
assert "<style>"+css+"</style>" in html, "Offline CSS is stale"
assert "<script>"+js+"</script>" in html, "Offline JS is stale"
assert ".cap::before,.cap::after{content:none}" in css
assert "translate(-50%,-35px)" in css
print("PASS: resized silver channels, gold MASTER, aligned detent and standalone/WebView2 parity")
