"""Contract: raised dark-crystal OFF, recessed ON and instant press with fixed geometry."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
page=(root/'web/index.html').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
license=(root/'web/assets/fonts/OFL-LICENSE-Inter.txt').read_text(encoding='utf8')
assert 'BUTTON 180 — raised dark-crystal cube OFF' in css
assert '--b180-shadow-raised:' in css and '--b180-shadow-pressed:' in css
compact=''.join(css.split())
for rule in (
 'inset 0 1px 0 rgba(246,251,255,.52)',
 'inset 0 5px 5px -3px rgba(153,201,231,.28)',
 'inset 1px 0 1px rgba(163,199,225,.24)',
 'inset 0 -2px 2px rgba(0,0,0,.63)',
 '0 2px 0 var(--b180-border)',
 '0 3px 0 rgba(4,8,13,.92)',
 'inset 0 3px 6px -1px rgba(0,0,0,.72)',
 '--b180-depth:var(--b180-shadow-pressed)',
 'transform:translateY(0px)!important',
 'transition:background-color 150ms ease-out, border-color 150ms ease-out, color 150ms ease-out, box-shadow 90ms ease-out, filter 120ms ease-out',
 'background:var(--b180-color)',
 'display:inline-flex',
 'inset 0 3px 6px -1px rgba(0,0,0,.72)'
): assert ''.join(rule.split()) in compact,rule
assert '--b180-off:#202832' in css
assert '--b180-edge:#070c13' in css
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
assert 'filter:brightness(1.32)!important' in css
assert 'border-color:#527789!important' not in css
assert css.count('inset 0 3px 6px -1px rgba(0,0,0,.72)') == 1
assert 'box-shadow:var(--b180-depth)!important' in css
assert 'transform:translateY(3px)!important' not in css
assert 'background:linear-gradient(180deg,#526a78' not in css
assert '0 0 8px rgba(60,191,230,.24)' not in css
assert 'data-send' not in js and 'data-send' not in css
assert 'a:true,b:false' not in js
print('PASS: dark cube raised OFF, recessed ON and brighter hover, embedded Inter, parity')
