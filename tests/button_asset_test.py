"""Context-aware ArSonMix button family integrity and CSS design boundary tests."""
from pathlib import Path
from hashlib import sha256
import re, base64, struct
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf-8')
js=(root/'web/app.js').read_text(encoding='utf-8')
offline=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf-8')

families={
    'SO-S-SLV.png':('skin-small-off',(49,37)),
    'SO-S-YLW.png':('skin-small-solo',(49,37)),
    'SO-S-RED.png':('skin-small-mute',(49,37)),
    'SO-S-GRE.png':('skin-small-enable',(49,37)),
    'SO-S-BLU.png':('skin-small-phase',(49,37)),
    'SO-L-BTN-OFF.png':('skin-long-off',(192,36)),
    'SO-L-BTN-TUR.png':('skin-long-on',(192,36)),
    'SO-B-BTN-OFF.png':('skin-large-off',(88,56)),
    'SO-B-BTN-BLU.png':('skin-large-on',(88,56)),
}
for name,(token,expected) in families.items():
    original=(root/'web/assets/buttons'/name).read_bytes()
    assert original[:8] == bytes.fromhex('89504e470d0a1a0a'),name
    assert struct.unpack('>II',original[16:24]) == expected, name
    found=re.search(r'--'+token+r':url\("data:image/png;base64,([A-Za-z0-9+/=]+)"\)', css)
    assert found,name+' is missing from the intended CSS family'
    assert base64.b64decode(found.group(1)) == original,name+' embedded bytes mismatch'
assert '<style>'+css+'</style>' in offline, 'Offline CSS differs from modular CSS'
assert '<script>'+js+'</script>' in offline, 'Offline JS differs from modular JS'
# The previously blanket-styled nav/rack/plugin controls must stay separate.
assert ':is(.nav button,.seg button,.right-button' not in css
assert '.strip .rack .insert,.strip .rack .insert.empty' in css
assert '/* CONTEXTUAL ARSONMIX HARDWARE BUTTONS' in css
print('PASS: all 9 contextual button assets original & correct dimensions, no global override, offline parity')
