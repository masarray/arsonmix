"""Pure CSS Crystal Candy replaces all stretched PNG button families."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
css=(root/'web/styles.css').read_text(encoding='utf8')
js=(root/'web/app.js').read_text(encoding='utf8')
preview=(root/'AR-MIX-Live-FX-Host-Standalone-v0.3.html').read_text(encoding='utf8')
assert 'ARSONMIX CRYSTAL CANDY / CSS-ONLY 3D CONTROLS' in css
assert 'linear-gradient(180deg,var(--cc-top)' in css
assert 'transition:filter 80ms ease-out,transform 70ms ease-out' in css
assert 'translateY(1px)!important' in css
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
