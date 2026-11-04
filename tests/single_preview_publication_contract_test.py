"""Fail closed: GitHub Pages must expose ONE approved ArSonMix GUI at /preview/."""
from pathlib import Path
import json, re, sys, hashlib
ROOT=Path(__file__).resolve().parents[1]
DST=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else ROOT/'dist-pages'
gui=DST/'preview'
def require(ok,why):
    if not ok: raise AssertionError('SINGLE APPROVED PREVIEW FAILED: '+why)
html=(gui/'index.html').read_text(encoding='utf-8')
source=(ROOT/'site/preview-v2/index.html').read_text(encoding='utf-8')
build=json.loads((gui/'build.json').read_text(encoding='utf-8'))
require(build.get('kind')=='APPROVED_SINGLE_GUI' and build.get('canonical_url')=='/preview/','wrong public GUI identity')
require(build.get('live_audio') is False,'browser must not claim PCM')
require('id="console"' not in html and 'preview-buttons/index.html' not in html,'old iframe variant still published')
require('console-body' in html and 'mixer-control-keys.css' in html,'not the actual approved console')
# The only DOM change from source is the two content-hashed approved PNG-button links.
revisions=build.get('button_css_js_revisions',{})
normalized=html
for name in ('mixer-control-keys.css','mixer-control-keys.js'):
    require(name in revisions and re.fullmatch('[0-9a-f]{16}',revisions[name]) is not None,'missing versioned approved key')
    normalized=normalized.replace('assets/'+name+'?rev='+revisions[name],'assets/'+name)
    approved=ROOT/'site/preview-buttons/assets'/name
    target=gui/'assets'/name
    require(target.read_bytes()==approved.read_bytes(),'wrong PNG-button CSS/JS: '+name)
require(normalized==source,'rendered /preview/ DOM diverges from current source or has a fork')
for name in ('mixer-wire-dots.css','knob-ICON.svg'):
    require((gui/'assets'/name).read_bytes()==(ROOT/'site/preview-v2/assets'/name).read_bytes(),'WIRE source drift: '+name)
require((gui/'assets/mute-solo-pfl-56x37.png').read_bytes()==(ROOT/'site/preview-buttons/assets/mute-solo-pfl-56x37.png').read_bytes(),'approved PNG sprite mismatch')
require(not (gui/'candidate.json').exists(),'legacy preview candidate metadata still published')
require('http-equiv="refresh"' in (gui/'mixer.html').read_text(encoding='utf-8'),'old mixer.html must redirect, never be another GUI')
for old in ('preview-v2','preview-buttons','preview-v33','preview-v34'):
    d=DST/old
    require((d/'index.html').is_file(),'missing safe legacy redirect: '+old)
    redirect=(d/'index.html').read_text(encoding='utf-8')
    require('url=../preview/' in redirect and 'console-body' not in redirect,'legacy GUI not redirected: '+old)
    require(sorted(x.name for x in d.iterdir())==['index.html'],'legacy route still contains independent GUI assets: '+old)
print('PASS: ONE publicly rendered GUI = /preview/; all old preview URLs redirect-only; exact approved hardware keys and current WIRE')
