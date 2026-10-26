"""Contract: shared OFF/ON Button 180 rest skin, transient 1px tactile press."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
page=(root/'web/index.html').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
license=(root/'web/assets/fonts/OFL-LICENSE-Inter.txt').read_text(encoding='utf8')
assert 'BUTTON 180 — one identical RESTING surface for OFF and ON' in css
# Keep a structural style check, not a historical comment header.
assert ':active:not(:disabled)' in css
assert '--b180-shadow-surface:' in css and '--b180-shadow-raised:' not in css
assert '--b180-depth:' not in css
compact=''.join(css.split())
for rule in (
 'inset 0 3px 6px -1px rgba(0,0,0,.72)',
 'inset 0 0 0 1px rgba(255,255,255,.14)',
 'inset 0 1px 20px rgba(0,0,0,.19)',
 'inset 0 -1px 2px rgba(0,0,0,.32)',
 '0 0 0 1px var(--b180-border)',
 'box-shadow:var(--b180-shadow-surface)',
 'transform:translateY(0px)!important',
 'transition:background-color 150ms ease-out, border-color 150ms ease-out, color 150ms ease-out, box-shadow 90ms ease-out, filter 120ms ease-out, transform 75ms ease-out',
 'background:var(--b180-color)',
 'display:inline-flex',
 'inset 0 3px 6px -1px rgba(0,0,0,.72)'
): assert ''.join(rule.split()) in compact,rule
assert '--b180-off:#858283' in css
assert '--b180-edge:#636363' in css
assert '--b180-off-ink:#2b2b2b' in css
assert '--b180-ink:var(--b180-off-ink)' in css
assert '--b180-ink:#291018' in css
assert '--b180-color:#e76f83' in css # ink on coral mute
assert '--b180-ink:#291d08' in css # ink on amber solo
assert 'data:font/woff2;base64,d09GMg' in css
assert '@font-face' in css and 'font-family:"ArSonMix Inter"' in css
assert 'SIL Open Font License' in license
assert not re.search(r'--(?:ar-btn-|skin-small|skin-long|skin-large)',css)
assert 'class="btn-180 candy mute' in js and 'class="btn-180' in page
assert '<style>'+css+'</style>' in preview
assert '<script>'+js+'</script>' in preview
assert 'height:40px!important;flex:0 0 40px!important' not in css
assert 'height:24px!important;min-height:24px!important' not in css
assert page.count('class="fx-nav')>=8
assert 'class="btn-180' not in page
assert '.toolbar .nav button.btn-180' in css
assert 'class="pushable' not in page and '--pb-face:' not in css
assert 'top:13px!important' in css
assert 'top:12px!important' in css
assert 'top:15px!important' in css
assert 'filter:brightness(1.32)!important' in css
assert 'border-color:#527789!important' not in css
assert css.count('inset 0 3px 6px -1px rgba(0,0,0,.72)') == 1
assert 'box-shadow:var(--b180-shadow-surface)!important' in css
assert 'transform:translateY(3px)!important' not in css
assert 'transform:translateY(1px)!important' in css
assert 'inset 0 4px 8px -1px rgba(0,0,0,.82)' in css
assert '0 0 0 rgba(0,0,0,0)' in css
assert css.count('box-shadow:var(--b180-shadow-surface)!important') == 1
assert 'background:linear-gradient(180deg,#526a78' not in css
assert '0 0 8px rgba(60,191,230,.24)' not in css
assert 'data-send' not in js and 'data-send' not in css
assert 'a:true,b:false' not in js
print('PASS: OFF/ON identical resting skin, transient 1px tactile press, gray palette and HTML parity')
