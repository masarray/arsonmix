"""Contract: Button 180 black-crystal OFF geometry, persistent recessed ON and bundled Inter."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
page=(root/'web/index.html').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
license=(root/'web/assets/fonts/OFL-LICENSE-Inter.txt').read_text(encoding='utf8')
assert 'BUTTON 180 — single component owner' in css
compact=''.join(css.split())
for rule in (
 'inset 0 3px 6px -1px rgba(0,0,0,.72)',
 'inset 0 0 0 1px rgba(255,255,255,.14)',
 'inset 0 1px 20px rgba(0,0,0,.19)',
 'inset 0 -1px 2px rgba(0,0,0,.32)',
 '0 0 0 1px rgba(0,0,0,.46)',
 'transform:translateY(0px)!important',
 'transition:background-color 150ms, border-color 150ms, color 150ms, filter 120ms',
 'background:var(--b180-color)',
 'display:inline-flex',
 'inset 0 3px 6px -1px rgba(0,0,0,.72)'
): assert ''.join(rule.split()) in compact,rule
assert '--b180-off:#292d34' in css
assert '--b180-edge:#0f1319' in css
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
assert page.count('class="btn-180')>=8
assert 'class="pushable' not in page and '--pb-face:' not in css
assert 'top:13px!important' in css
assert 'top:12px!important' in css
assert 'top:15px!important' in css
assert 'filter:brightness(1.18)!important' in css
assert 'border-color:#527789!important' not in css
assert css.count('inset 0 3px 6px -1px rgba(0,0,0,.72)') == 1
assert 'transform:translateY(3px)!important' not in css
assert 'background:linear-gradient(180deg,#526a78' not in css
assert '0 0 8px rgba(60,191,230,.24)' not in css
assert 'data-send' not in js and 'data-send' not in css
assert 'a:true,b:false' not in js
print('PASS: color-only OFF/ON, same physical inset and brightness-only hover, embedded Inter, parity')
