"""Build public Pages landing and interactive console from canonical standalone HTML."""
from pathlib import Path
import argparse,os,json,shutil,subprocess,sys
from compile_approved_css import optimize_assets
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--out',default='dist-pages')
p.add_argument('--sha',default=os.environ.get('GITHUB_SHA','local'))
p.add_argument('--branch',default=os.environ.get('GITHUB_REF_NAME','local'))
args=p.parse_args()
dst=Path(args.out).resolve()
if dst==ROOT or dst==ROOT/'site': raise SystemExit('unsafe output path')
dst.mkdir(parents=True,exist_ok=True)
# Publish only the landing and ONE approved /preview/ GUI. Internal GUI seed
# folders remain in repo for design tests, never as separate public screens.
shutil.copy2(ROOT/'site/index.html',dst/'index.html')
# One logo source, one deterministic build for all web/PWA/favicon resources.
subprocess.run([sys.executable,str(ROOT/'scripts/generate_brand_icons.py'),
                '--source',str(ROOT/'assets/icon/brand-source-128.png'),
                '--out',str(dst)],check=True)

subprocess.run([sys.executable,str(ROOT/'scripts/build_ui_css.py'),'--check'],check=True)

# Build the ONLY owner-approved GUI at /preview/: internal base source plus
# exactly the approved PNG control material. No second public renderer.
buttons_source=ROOT/'site/preview-buttons/assets'
canonical=ROOT/'site/preview-v2'
buttons_dest=dst/'preview'
if buttons_dest.exists(): shutil.rmtree(buttons_dest)
# Public /preview/ is the only compiled GUI. Canonical base and PNG overrides
# are source inputs only; this output has ONE HTML, ONE CSS/JS/asset set.
(buttons_dest/'assets').mkdir(parents=True)
# Same small icon is shipped into JUCE's approved resource provider.
for name in ('favicon.ico','favicon-16x16.png','favicon-32x32.png'):
    shutil.copy2(dst/name,buttons_dest/'assets'/name)
canonical_html=(canonical/'index.html').read_text(encoding='utf-8')
special=('mixer-control-keys.css','mixer-control-keys.js')
sprite='mute-solo-pfl-56x37.png'
for src in sorted((canonical/'assets').iterdir()):
    if not src.is_file(): raise SystemExit('Unexpected nested preview-v2 asset: '+str(src))
    if src.name not in special:
        shutil.copy2(src,buttons_dest/'assets'/src.name)
for name in (*special,sprite):
    src=buttons_source/name
    if not src.is_file(): raise SystemExit('Approved PNG asset missing: '+str(src))
    shutil.copy2(src,buttons_dest/'assets'/name)
# Content-addressed URLs prevent browser caching of a superseded button skin.
import hashlib,re
revisions={}
for name in special:
    token='assets/'+name
    pattern=re.compile(re.escape(token)+r'(?:\?[^"\s>]+)?')
    matches=list(pattern.finditer(canonical_html))
    if len(matches)!=1:
        raise SystemExit(f'Expected one source link for {token}; found {len(matches)}')
    digest=hashlib.sha256((buttons_dest/'assets'/name).read_bytes()).hexdigest()[:16]
    revisions[name]=digest
    match=matches[0]
    canonical_html=canonical_html[:match.start()]+token+'?rev='+digest+canonical_html[match.end():]
# Add the opt-in, CSS-free performance probe in the ONE canonical assembler.
# Do not rewrite historical source HTML: CSS V28 exact-source checkpoints remain
# valid, and the generated /preview/ plus JUCE WebView2 bundle stay identical.
probe='gui-perf-probe.js'
if not (buttons_dest/'assets'/probe).is_file():
    raise SystemExit('Approved GUI performance probe asset missing')
probe_tag='<script defer src="assets/gui-perf-probe.js?v=p0-stage-a-v1"></script>'
if canonical_html.count('</body>')!=1 or probe_tag in canonical_html:
    raise SystemExit('Approved Pages profiler injection must have exactly one body')
canonical_html=canonical_html.replace('</body>',probe_tag+'\n</body>',1)
# Stage C: compile only DEPLOYED copies, never modify locked V28 source
# owners or approved PNG artwork. No new CSS/override or second renderer.
canonical_html,css_compilation=optimize_assets(canonical_html,buttons_dest/'assets',runtime_bundle=True)
print("Stage C approved CSS: removed",css_compilation["removedDeclarations"],
      "redundant declarations (cross-file",
      css_compilation["removedCrossFileDeclarations"],"), kept original",
      css_compilation["sourceStylesheetCount"],"CSS owners; zero new overrides")
(buttons_dest/'index.html').write_text(canonical_html,encoding='utf-8')
(buttons_dest/'build.json').write_text(json.dumps({
    'canonical_url':'/preview/',
    'sha':args.sha,'branch':args.branch,
    'kind':'APPROVED_SINGLE_GUI','live_audio':False,
    'wire_source':'site/preview-v2/index.html',
    'wire_source_sha256':hashlib.sha256((canonical/'index.html').read_bytes()).hexdigest(),
    'button_asset_source':'site/preview-buttons/assets/',
    'button_css_js_revisions':revisions,
    'css_compilation':css_compilation
},indent=2),encoding='utf-8')
# Fail closed if routing has no identical source files in the variant.
for name in ('mixer-wire-dots.css','knob-ICON.svg'):
    if (buttons_dest/'assets'/name).read_bytes()!=(canonical/'assets'/name).read_bytes():
        raise SystemExit('PNG preview WIRE dependency drift: '+name)
# The source directories /preview-v2/ and /preview-buttons/ MUST NOT ship
# alternative mixer pages. Preserve old bookmarks with redirect-only shells;
# all redirects lead to exactly the SAME approved /preview/ GUI.
legacy_paths=('preview-v2','preview-buttons','preview-v33','preview-v34')
redirect_html=('<!doctype html><html lang="en"><head><meta charset="utf-8">'
               '<meta http-equiv="refresh" content="0;url=../preview/">'
               '<link rel="canonical" href="../preview/">'
               '<title>ArSonMix — Official Preview</title></head>'
               '<body><a href="../preview/">Open the only approved ArSonMix preview</a></body></html>')
for legacy in legacy_paths:
    target=dst/legacy
    if target.exists(): shutil.rmtree(target)
    target.mkdir(parents=True)
    (target/'index.html').write_text(redirect_html,encoding='utf-8')
# Retire the old competing /preview/mixer.html body and its candidate metadata;
# they are never an alternate product skin.
(dst/'preview'/'mixer.html').write_text(
    '<!doctype html><meta http-equiv="refresh" content="0;url=./">'
    '<link rel="canonical" href="./">',encoding='utf-8')
for obsolete in ('candidate.json',):
    (dst/'preview'/obsolete).unlink(missing_ok=True)
(dst/'.nojekyll').touch()
assert (dst/'index.html').exists() and (dst/'preview'/'index.html').exists()
assert 'APPROVED_SINGLE_GUI' in (dst/'preview'/'build.json').read_text(encoding='utf-8')
print('Built ONE approved public GUI at /preview/:',dst/'preview',
      'from internal HTML + approved button assets', 'revision:',args.sha)
