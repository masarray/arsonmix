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
assert source in css, "Base mixer performance CSS is missing from compiled build"
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
    assert original[:8]==bytes([137,80,78,71,13,10,26,10]),filename
    assert (int.from_bytes(original[16:20],"big"),int.from_bytes(original[20:24],"big"))==(36,77),filename
    marker=variable+':url("data:image/png;base64,'
    assert marker in css,filename+" CSS variable missing"
    encoded=css.split(marker,1)[1].split('")',1)[0]
    assert base64.b64decode(encoded)==original,filename+" embedded PNG mismatch"

assert 'width:36px;height:77px' in source
assert 'background-size:36px 77px!important' in source
assert 'top:var(--pos,50%)' in source
assert 'class="fader-travel"' in js
assert 'gain-scale-travel' in js
assert 'vu-scale-travel' in js
assert 'transform:translate(-50%,-38px)' in source
assert 'distance:Math.max(24,r.height-77)' in js
assert 'background-image:var(--ari-channel-cap)!important' in source
assert 'background-image:var(--ari-master-cap)!important' in source
assert ':not([data-fader="master"])' in source
assert '[data-fader="master"] .fader-travel > .cap' in source
assert 'filter:none!important' in source

# Historical art is retained for rollback; active heads must use new assets.
legacy=(root/"web/assets/faders/FADER-RED2.png").read_bytes()
assert hashlib.sha1(b"blob "+str(len(legacy)).encode()+bytes([0])+legacy).hexdigest()=="2369e1b9002522600948909246918e57880da1ed"
assert ".cap::before,.cap::after{content:none}" in css

# Muted silver / rose-gold master artwork is a new static 36x77 PNG,
# derived from the silver relief. Gold source is kept for rollback only.
rose=(root/"web/assets/faders/FADER-ARI-ROSEGOLD.png").read_bytes()
assert len(rose)==3961
assert hashlib.sha256(rose).hexdigest()=="a1d0962a25336d88a062263761d22b65ceca85d4b356e7643f1234afd6573913"
assert (int.from_bytes(rose[16:20],"big"),int.from_bytes(rose[20:24],"big"))==(36,77)
finish=(root/"web/styles-src/42-mixer-premium.css").read_text(encoding="utf-8")
assert css.endswith(finish), "Premium CSS module not compiled last"
marker='--ari-master-cap:url("data:image/png;base64,'
assert css.count(marker)==2, "Rose MASTER must override old gold master once"
embedded=css.rsplit(marker,1)[1].split('")',1)[0]
assert base64.b64decode(embedded)==rose, "Active MASTER finish must match rose-silver PNG"
assert css.count('background-image:var(--ari-master-cap)!important')>=1
assert "#app #mixer > .strip:not(.master-strip)" in finish
assert "grid-template-columns:repeat(2,minmax(0,1fr))" in finish
assert "max-width:none" in finish
assert "session.channels.length<channels.length" in js
assert "last.channels.slice(0,channels.length)" in js
assert 'const raw=[' in js and "['Monitor','◉'" not in js and "['Aux Out','◉'" not in js
assert "Deliberate double-click to edit a strip." in js
assert 'id="masterMode"' in js
assert 'fader-tools master-tools' not in js
assert 'masterMono' in js
assert 'class="btn-180 master-mode' in js
print("PASS: rose-gold MASTER, 10 channels, button mode/clip layout, offline parity")
