"""Pure CSS Crystal Candy replaces all stretched PNG button families."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
assert 'CRYSTAL CANDY v2' in css
assert '::before' in css and '::after' in css
assert 'top:var(--cc-depth)' in css
assert 'top:0;box-shadow:0 1px 2px' in css
assert 'border:2px solid #eda458' not in css
assert 'linear-gradient(to bottom,var(--cc-top)' in css
assert 'transition:top 65ms ease-out,filter 65ms ease-out,box-shadow 80ms ease-out' in css
assert 'top:var(--cc-depth);transform:none!important' in css
assert not re.search(r'--(?:ar-btn-|skin-small|skin-long|skin-large)',css)
assert not re.search(r'var\(--(?:ar-btn-|skin-)',css)
for family in ('.toolbar .nav button','.left-rail .tabs button','.strip .channel-actions .candy',
               '.strip .utility-row button','.strip .fader-tools button',
               '.left-rail .rail-presets button','.left-rail .rail-bottom button',
               '#masterLeft .master-tools button'):
 assert family in css,family
for state in ('.mute.active','.solo.active','button[data-hpf].active','.nav button.selected'):
 assert state in css,state
assert '<style>'+css+'</style>' in preview and '<script>'+js+'</script>' in preview
print('PASS: CSS-only candy 3D surfaces, semantic states, tactile 70ms press, preview parity')
