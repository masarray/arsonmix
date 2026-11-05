"""Fail closed: GitHub Pages must expose ONE approved ArSonMix GUI at /preview/."""
from pathlib import Path
import json, re, sys, hashlib
from scripts.compile_approved_css import LINK, optimize_css
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
# The only compiled DOM changes are two hashed approved PNG links and the dormant profiling script.
revisions=build.get('button_css_js_revisions',{})
expected=source
for name in ('mixer-control-keys.css','mixer-control-keys.js'):
    require(name in revisions and re.fullmatch('[0-9a-f]{16}',revisions[name]) is not None,'missing versioned approved key')
    token='assets/'+name
    pattern=re.compile(re.escape(token)+r'(?:\?[^"\s>]+)?')
    require(len(pattern.findall(expected))==1,'approved key CSS/JS link missing or duplicated: '+name)
    expected=pattern.sub(token+'?rev='+revisions[name],expected)
    approved=ROOT/'site/preview-buttons/assets'/name
    target=gui/'assets'/name
    require(target.read_bytes()==approved.read_bytes(),'wrong PNG-button CSS/JS: '+name)
# The dormant Stage A profiler is added by that SAME deterministic assembler,
# without modifying the V28 source-HTML checkpoint or creating another UI.
probe='<script defer src="assets/gui-perf-probe.js?v=p0-stage-a-v1"></script>'
require(expected.count('</body>')==1,'approved source has multiple bodies')
expected=expected.replace('</body>',probe+'\n</body>',1)
require(html.count(probe)==1,'profiler missing or duplicated')
require((gui/'assets/gui-perf-probe.js').read_bytes()==
        (ROOT/'site/preview-v2/assets/gui-perf-probe.js').read_bytes(),
        'opt-in performance probe was not copied byte-for-byte')
# Stage C: the source HTML remains byte-exact, but the 18 deployed CSS files
# contain only proven cascade winners. Verify every *generated* byte and query
# hash; never approve an extra stylesheet, different owner or PNG mutation.
linked=[match.group(2) for match in LINK.finditer(expected)]
require(len(linked)==18 and len(set(linked))==18,'approved CSS owners changed')
source_css=[(name,(ROOT / ("site/preview-buttons/assets"
                               if name=="mixer-control-keys.css"
                               else "site/preview-v2/assets") / name)
                        .read_text(encoding="utf-8")) for name in linked]
compiled,counts=optimize_css(source_css)
record=build.get('css_compilation') or {}
require(counts['removedDeclarations']>0,'Stage C did not reduce actual runtime CSS')
for stat in ('sourceDeclarations','removedDeclarations',
             'removedCrossFileDeclarations','lockedCandidateDeclarations'):
    require(record.get(stat)==counts[stat],'CSS compiler accounting mismatch: '+stat)
require(record.get('sourceStylesheetCount')==18 and
        record.get('generatedStylesheetCount')==18 and
        record.get('addedOverrideStylesheets')==0,'CSS compiler created an override')
for name in linked:
    data=(gui/'assets'/name).read_bytes()
    require(data==compiled[name].encode('utf-8'),
            'Runtime CSS differs from source-derived winning declarations: '+name)
    digest=hashlib.sha256(data).hexdigest()[:16]
    require(record.get('shaByCssOwner',{}).get(name)==digest,
            'Runtime CSS digest mismatch: '+name)
def css_rewrite(match):
    prefix,name,query,end=match.groups()
    return prefix+name+(query or '')+('&' if query else '?')+'armixc='+record['shaByCssOwner'][name]+end
expected=LINK.sub(css_rewrite,expected)
require(html==expected,'rendered /preview/ DOM differs from source + approved buttons + nonvisual profiler')
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
