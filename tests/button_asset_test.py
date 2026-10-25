"""The original RGBA switch images must be identical in repository and CSS data URIs."""
from pathlib import Path
from hashlib import sha256
import base64,re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf-8')
js=(root/'web/app.js').read_text(encoding='utf-8')
offline=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf-8')
checks={
    'SO-M-SLV.png':('switch-silver','c82ba13641a109109f4d5142ca53a09f352f5c9f5df03eabdd3e303faeeae401'),
    'SO-M-GRE.png':('switch-green','35c45ffc5922a07739747f8c4edc46548dfe0ba1a8b820bfb6c3d2a1052d50ff'),
    'SO-M-RED.png':('switch-red','8ad749af8c32ff1b7dcc9456f39f21dbb11b860848b7e4e8c30b564874fb4a4f'),
}
for name,(token,digest) in checks.items():
    src=(root/'web/assets/buttons'/name).read_bytes()
    assert sha256(src).hexdigest()==digest, name+' changed'
    found=re.search(r'--'+token+r':url\("data:image/png;base64,([A-Za-z0-9+/=]+)"\)',css)
    assert found, name+' not embedded'
    assert base64.b64decode(found.group(1))==src, name+' mismatch'
assert '<style>'+css+'</style>' in offline
assert '<script>'+js+'</script>' in offline
assert 'SO-M-' not in offline or 'data:image/png;base64,' in offline
print('PASS: 3 original metal button images, colors, SHA256, standalone build parity')
