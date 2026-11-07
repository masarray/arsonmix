"""Fail closed: GitHub Pages must expose ONE approved ArSonMix GUI at /preview/."""
from pathlib import Path
import json, re, sys, hashlib, shutil, tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from compile_approved_css import LINK, optimize_css, compile_runtime_bundle, link_runtime_bundle, RUNTIME_CSS
from approved_fx_rack_power import apply_authoritative_fx_power
from approved_fx_rack_interaction import apply_fx_rack_interaction
from compose_native_peq import apply_native_peq
from compose_operator_ux import apply_operator_ux
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
require('console-body' in html and RUNTIME_CSS in html and 'mixer-control-keys.js' in html,'not the actual approved console')
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
expected=apply_authoritative_fx_power(expected)
expected=apply_fx_rack_interaction(expected)
expected=apply_native_peq(expected)
# V29 edits only the generated owners, after the historical V28 source lock.
# Build the exact same two temporary pre-optimizer owners without mutating
# the source repository or the already optimized distribution.
with tempfile.TemporaryDirectory(prefix="armix-operator-contract-") as td:
    owned=Path(td)
    owners=("mixer-chrome-material.css","mixer-plugin-browser.css","mixer-fx-rack.css")
    for name in owners:
        shutil.copy2(ROOT/"site/preview-v2/assets"/name,owned/name)
    # Recreate the SAME handle-free FX CSS owner as the production assembler,
    # then run the existing operator composer. Historic V28 assets stay locked.
    apply_fx_rack_interaction((ROOT/"site/preview-v2/index.html").read_text(encoding="utf-8"),owned)
    expected=apply_operator_ux(expected,owned)
    composed_css={name:(owned/name).read_text(encoding="utf-8") for name in owners}
# Stage C: the source HTML remains byte-exact, but the 18 deployed CSS files
# contain only proven cascade winners. Verify every *generated* byte and query
# hash; never approve an extra stylesheet, different owner or PNG mutation.
linked=[match.group(2) for match in LINK.finditer(expected)]
require(len(linked)==18 and len(set(linked))==18,'approved CSS owners changed')
source_css=[(name, composed_css[name] if name in composed_css else
                 (ROOT / ("site/preview-buttons/assets"
                          if name=="mixer-control-keys.css"
                          else "site/preview-v2/assets") / name)
                    .read_text(encoding="utf-8")) for name in linked]
compiled,counts=optimize_css(source_css)
record=build.get('css_compilation') or {}
for stat in ('sourceDeclarations','removedDeclarations',
             'removedCrossFileDeclarations','lockedCandidateDeclarations',
             'preservedFallbackDeclarations'):
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
# Stage D: exactly ONE CSS link used by the Pages/Windows WebView2 runtime,
# while 18 source owners remain ordered and inspectable (not 18 CSSOM sheets).
runtime=compile_runtime_bundle([(name,compiled[name]) for name in linked])
digest=hashlib.sha256(runtime.encode('utf-8')).hexdigest()[:16]
require((gui/'assets'/RUNTIME_CSS).read_bytes()==runtime.encode('utf-8'),
        'actual runtime stylesheet differs from source-owned winner sequence')
require(record.get('runtimeBundleCssSHA256')==digest and
        record.get('runtimeBundleBytes')==len(runtime.encode('utf-8')),
        'runtime stylesheet integrity/provenance mismatch')
require(record.get('runtimeStylesheetCount')==1 and
        record.get('removedStylesheetRequests')==17 and
        record.get('addedOverrideStylesheets')==0,
        'Stage D loaded an extra CSS layer or a second renderer')
expected=link_runtime_bundle(expected,digest)
shipped=list(LINK.finditer(html))
require(len(shipped)==1 and shipped[0].group(2)==RUNTIME_CSS,
        'Only the single compiled runtime CSS is allowed to load')
require(html==expected,'rendered /preview/ DOM differs from source + approved buttons + one runtime CSS + nonvisual profiler')
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
