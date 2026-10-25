"""Byte-for-byte provenance and embedded asset integrity for PAN + five metal buttons."""
from pathlib import Path
import base64,re,struct
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf-8')
js=(root/'web/app.js').read_text(encoding='utf-8')
offline=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf-8')
sources={'pan/PAN-SLIDER.png':('ar-pan',(18,40)),'buttons/SO-M-BTN-TUR.png':('ar-btn-tur',(88,36)),
'buttons/SO-M-BTN-BLU.png':('ar-btn-blue',(88,36)),'buttons/SO-M-BTN-OFF.png':('ar-btn-off',(88,36)),
'buttons/SO-M-BTN-PNK.png':('ar-btn-pink',(88,36)),'buttons/SO-M-BTN-RED.png':('ar-btn-red',(88,36))}
for name,(token,expected) in sources.items():
    original=(root/'web/assets'/name).read_bytes()
    assert original.startswith(bytes.fromhex('89504e470d0a1a0a'))
    assert struct.unpack('>II',original[16:24])==expected,name
    found=re.search(r'--'+token+r':url\("data:image/png;base64,([A-Za-z0-9+/=]+)"\)',css)
    assert found, name+' not embedded'
    assert base64.b64decode(found.group(1))==original,name+' changed during embedding'
assert '<style>'+css+'</style>' in offline, 'CSS modular/standalone mismatch'
assert '<script>'+js+'</script>' in offline, 'JS modular/standalone mismatch'
assert 'ArSonMix' in offline
print('PASS: 6 original metal assets embedded exactly, PAN/brand and standalone parity')
