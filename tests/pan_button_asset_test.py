"""Retain original metallic PAN image; button surfaces use CSS only."""
from pathlib import Path
import base64,re,struct
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
original=(root/'web/assets/pan/PAN-SLIDER.png').read_bytes()
assert original.startswith(bytes.fromhex('89504e470d0a1a0a'))
assert struct.unpack('>II',original[16:24])==(18,40)
match=re.search(r'--ar-pan:url\("data:image/png;base64,([A-Za-z0-9+/=]+)"\)',css)
assert match and base64.b64decode(match.group(1))==original
assert not re.search(r'--ar-btn-|--skin-small|--skin-long|--skin-large',css)
assert '<style>'+css+'</style>' in preview and '<script>'+js+'</script>' in preview
assert '.pan-track .pan-thumb{z-index:3!important}' in css
assert '.pan-track::after{' in css and 'z-index:1!important' in css
assert '.pan-track .pan-fill{z-index:2!important}' in css
print('PASS: original PAN thumb unchanged, CSS-only buttons, preview parity')
