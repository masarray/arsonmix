"""Contract: global BTN-180 owner-specified pad skin, transient 1px tactile press."""
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
 'inset 5px 5px 2px -1px rgba(0,0,0,.30)',
 'inset 0 0 1px 1px rgba(255,255,255,.14)',
 'inset 0 1px 20px rgba(0,0,0,.19)',
 'inset 0 -1px 2px rgba(0,0,0,.32)',
 '0 0 0 1px var(--b180-border)',
 'box-shadow:var(--b180-shadow-surface)',
 'transform:translateY(0px)!important',
 'transition:background-color 150ms ease-out, border-color 150ms ease-out, color 150ms ease-out, box-shadow 90ms ease-out, filter 120ms ease-out, transform 75ms ease-out',
 'background:var(--b180-color)',
 'display:inline-flex',
 'inset 5px 5px 2px -1px rgba(0,0,0,.30)'
): assert ''.join(rule.split()) in compact,rule
assert '--b180-off:#b3b3b3' in css
assert '--b180-edge:#636363' in css
assert '--b180-off-ink:#2b2b2b' in css
assert '--b180-ink:var(--b180-off-ink)' in css
assert '--b180-color:var(--b180-on-cyan)' in css
assert '--b180-color:var(--b180-on-magenta)' in css
assert '--b180-ink:#32151e' in css
assert '--b180-color:var(--b180-on-mute)' in css # coral MUTE
assert '--b180-ink:#2b210b' in css # ink on amber SOLO
assert 'data:font/woff2;base64,d09GMg' in css
assert '@font-face' in css and 'font-family:"ArSonMix Inter"' in css
assert 'SIL Open Font License' in license
assert not re.search(r'--(?:ar-btn-|skin-small|skin-long|skin-large)',css)
assert 'class="btn-180 candy mute' in js and 'class="btn-180' in page
assert 'class="fx-nav"' not in page
assert 'Button 19 / experimental acrylic' in css
assert '<style>'+css+'</style>' in preview
assert '<script>'+js+'</script>' in preview
assert preview[preview.index('<div class="toolbar-actions">'):preview.index('</header>')].count('class="btn-180')==7
assert preview[preview.index('<aside class="left-rail">'):preview.index('</aside>')].count('class="btn-180')==8
assert 'class="fx-nav' not in preview
assert 'height:40px!important;flex:0 0 40px!important' not in css
assert 'height:24px!important;min-height:24px!important' not in css
assert page.count('class="btn-180')==16
assert 'class="btn-180 spec spec-device"' in page
assert page[page.index('<div class="toolbar-actions">'):page.index('</header>')].count('class="btn-180')==7
assert page[page.index('<aside class="left-rail">'):page.index('</aside>')].count('class="btn-180')==8
assert page.count('class="btn-180')==16
assert '.toolbar .nav button.btn-180' in css
assert 'class="pushable' not in page and '--pb-face:' not in css
assert 'top:13px!important' in css
assert 'top:12px!important' in css
assert 'top:15px!important' in css
assert 'filter:brightness(1.32)!important' in css
assert 'border-color:#527789!important' not in css
assert css.count('inset 5px 5px 2px -1px rgba(0,0,0,.30)') == 1
assert 'box-shadow:var(--b180-shadow-surface)!important' in css
assert 'transform:translateY(3px)!important' not in css
assert 'transform:translateY(1px)!important' in css
assert 'inset 3px 2px 2px -1px rgba(0,0,0,.50)' in css
assert 'inset 0 0 1px 0 rgba(255,255,255,.14)' in css
assert css.count('box-shadow:var(--b180-shadow-surface)!important') == 1
assert 'background:linear-gradient(180deg,#526a78' not in css
assert '0 0 8px rgba(60,191,230,.24)' not in css
assert 'data-send' not in js and 'data-send' not in css
assert 'a:true,b:false' not in js
# Palette quality gate: stable perceptual lightness and readable dark captions.
# No runtime calculations or painting cost; this runs only in CI.
tokens={
  'cyan':('#83d6e0','#12323c'),
  'green':('#79dd9e','#113322'),
  'magenta':('#e3a6d8','#32172f'),
  'purple':('#c4a9e8','#26163c'),
  'mute':('#f38895','#32151e'),
  'solo':('#edc669','#2b210b'),
  'stereo':('#77baeb','#0a2639'),
}
owner=(root/'web/styles-src/20-button-180.css').read_text(encoding='utf-8')
def linear(u):
  return u/12.92 if u<=.04045 else ((u+.055)/1.055)**2.4
def rgb(hexcode):
  return [linear(int(hexcode[x:x+2],16)/255) for x in (1,3,5)]
def luminance(hexcode):
  return sum(c*w for c,w in zip(rgb(hexcode),(.2126,.7152,.0722)))
def contrast(face,ink):
  y1,y2=sorted((luminance(face),luminance(ink)))
  return (y2+.05)/(y1+.05)
def oklab_lightness(hexcode):
  r,g,b=rgb(hexcode)
  l=.4122214708*r+.5363325363*g+.0514459929*b
  m=.2119034982*r+.6806995451*g+.1073969566*b
  s=.0883024619*r+.2817188376*g+.6299787005*b
  return .2104542553*l**(1/3)+.793617785*m**(1/3)-.0040720468*s**(1/3)
lightness=[]
for role,(face,ink) in tokens.items():
  assert f'--b180-on-{role}:{face}' in owner,(role,face)
  assert contrast(face,ink)>=6,(role,contrast(face,ink))
  lightness.append(oklab_lightness(face))
assert max(lightness)-min(lightness)<.11,lightness
bridge=(root/'web/bridge.js').read_text(encoding='utf8')
native=(root/'web/native-bridge.js').read_text(encoding='utf8')
for name,src,flag in [('bridge',bridge,'d'),('native',native,'s')]:
  assert f'const ready=!!{flag}.connected && !{flag}.error;' in src,name
  assert 'deviceButton.classList.toggle("on",ready)' in src,name
  assert 'deviceButton.dataset.audioConnected=String(ready)' in src,name
assert '#app .toolbar .spec-device #headerDriver' in owner
assert '#app .toolbar .spec-device.on' in owner
assert '--b180-color:var(--b180-on-stereo)' in (root/'web/styles-src/42-mixer-premium.css').read_text(encoding='utf8')
print('PASS: balanced OKLab lightness, WCAG captions, device-green native safety and BTN-180 parity')
