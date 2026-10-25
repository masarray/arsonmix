"""Contract: use user-supplied Button 180 shadows and pressed-state verbatim."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
page=(root/'web/index.html').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
assert 'BUTTON 180 — user-supplied button-180.html CSS' in css
for rule in (
 'inset 0 30px 30px -15px rgba(255, 255, 255, .1)',
 'inset 0 0 0 1px rgba(255, 255, 255, .3)',
 'inset 0 1px 20px rgba(0, 0, 0, 0)',
 '0 3px 2px rgba(0, 0, 0, .2)',
 '0 5px 10px rgba(0, 0, 0, .1)',
 '0 10px 20px rgba(0, 0, 0, .1)',
 'inset 0 16px 2px -15px rgba(0, 0, 0, 0)',
 '0 0 0 2px rgba(255, 255, 255, .5)',
 'transform:translateY(3px)',
 'transition:150ms all',
 'background:var(--b180-color)',
 'display:inline-flex'
):
 assert rule in css,rule
assert '/* CRYSTAL CANDY v2' not in css and '--cc-depth' not in css
assert 'class="btn-180 candy mute' in js and 'class="btn-180' in page
assert not re.search(r'--(?:ar-btn-|skin-small|skin-long|skin-large)',css)
assert '<style>'+css+'</style>' in preview and '<script>'+js+'</script>' in preview
print('PASS: exact Button 180 shadow and active, responsive geometry, preview parity')
