"""Contract: original Button 180 geometry, dark OFF, persistent recessed ON and bundled Inter."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
page=(root/'web/index.html').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
license=(root/'web/assets/fonts/OFL-LICENSE-Inter.txt').read_text(encoding='utf8')
assert 'Button 180 — reference shadow geometry' in css
for rule in (
 'inset 0 30px 30px -15px rgba(255,255,255,.1)',
 'inset 0 0 0 1px rgba(255,255,255,.3)',
 '0 3px 2px rgba(0,0,0,.2)',
 '0 5px 10px rgba(0,0,0,.1)',
 '0 10px 20px rgba(0,0,0,.1)',
 'inset 0 16px 2px -15px rgba(0,0,0,0)',
 '0 0 0 2px rgba(255,255,255,.5)',
 'transform:translateY(3px)',
 'transition:150ms all',
 'background:var(--b180-color)',
 'display:inline-flex',
 'height:40px!important;flex:0 0 40px!important',
 'height:24px!important;min-height:24px!important'
): assert rule in css,rule
assert '--b180-off:#203541' in css
assert '--b180-text:#c9d8e1' in css
assert '--b180-text:#291018' in css # ink on coral mute
assert '--b180-text:#291d08' in css # ink on amber solo
assert 'data:font/woff2;base64,d09GMg' in css
assert '@font-face' in css and 'font-family:"ArSonMix Inter"' in css
assert 'SIL Open Font License' in license
assert not re.search(r'--(?:ar-btn-|skin-small|skin-long|skin-large)',css)
assert 'class="btn-180 candy mute' in js and 'class="btn-180' in page
assert '<style>'+css+'</style>' in preview
assert '<script>'+js+'</script>' in preview
print('PASS: recessed ON, dark raised OFF, Inter WOFF2 embedded, source/standalone parity')
