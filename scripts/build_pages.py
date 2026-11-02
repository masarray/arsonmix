"""Build public Pages landing and interactive console from canonical standalone HTML."""
from pathlib import Path
import argparse,os,json,shutil,subprocess,sys,hashlib,re
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--out',default='dist-pages')
p.add_argument('--sha',default=os.environ.get('GITHUB_SHA','local'))
p.add_argument('--branch',default=os.environ.get('GITHUB_REF_NAME','local'))
args=p.parse_args()
dst=Path(args.out).resolve()
if dst==ROOT or dst==ROOT/'site': raise SystemExit('unsafe output path')
dst.mkdir(parents=True,exist_ok=True)
shutil.copytree(ROOT/'site',dst,dirs_exist_ok=True)
# Pinned PR GUI candidate at public /preview/mixer.html, without merging
# incomplete native ENGINE work. Never substitute this for a Windows release.
source=ROOT/'AR-MIX-Live-FX-Host-Standalone-v0.3.html'
subprocess.run([sys.executable,str(ROOT/'scripts/build_ui_css.py'),'--check'],check=True)
candidate_manifest=ROOT/'site/preview/candidate.json'
metadata={'sha':args.sha,'branch':args.branch,'source':source.name,'kind':'MAIN_SOURCE'}
if candidate_manifest.is_file():
    candidate=json.loads(candidate_manifest.read_text(encoding='utf-8'))
    assert candidate['kind'] in ('DRAFT_GUI_QA_ONLY','REFERENCE_WORKFLOW_QA_ONLY')
    assert len(candidate['source_sha'])==40 and candidate['ci_conclusion']=='success'
    if candidate['kind']=='DRAFT_GUI_QA_ONLY':
        assert candidate['source_pr']==271
        assert candidate['source_branch']=='work/operator-console-v6'
    else:
        # User-approved PR278 workflow is the PUBLIC design authority.
        # No ASIO/VST3/native capability can be inferred from this HTML.
        assert candidate['source_pr']==278
        assert candidate['source_branch']=='main/site/preview-v2'
    if candidate['kind']=='REFERENCE_WORKFLOW_QA_ONLY':
        # Copy directly from the *same* tested main commit as /preview-v2/.
        # Prevent independent snapshot drift when other GUI threads update V8+.
        source=ROOT/'site/preview-v2/index.html'
    else:
        source=ROOT/'site/preview/mixer.html'
    assert source.is_file()
    metadata={'sha':args.sha if candidate['source_pr']==278 else candidate['source_sha'],
              'branch':candidate['source_branch'],
              'source':('PR #285 Smart WIRE (PR #278 baseline) · SIMULATED / NO REAL AUDIO'
                        if candidate.get('feature_pr')==285 else
                        'PR #278 active operator workflow · SIMULATED / NO REAL AUDIO'
                        if candidate['source_pr']==278 else 'PR #271 draft GUI snapshot'),
              'kind':candidate['kind'],'live_audio':False,
              'publisher_main_sha':args.sha,
              'reference_baseline_sha':candidate['source_sha']}
html=source.read_text(encoding='utf-8')
assert '<style>' in html and '<script>' in html and ('data-fader' in html or 'data-field="level"' in html)
# Every Pages CSS/JS URL gets its content hash as a stable cache-busting URL.
# The previous fixed ?v=inter-utility-v7 URL persisted across unrelated
# deployments, letting browsers reuse obsolete PNG-backed MUTE styling.
# One owner produces the SAME fingerprinted HTML for preview/ and preview-v2/.
asset_sources={}
asset_url_pattern=re.compile(
    r"""(?P<lead>\b(?:href|src)=["'])(?P<url>assets/(?P<name>[A-Za-z0-9._-]+\.(?:css|js)))(?:\?[^"']*)?(?P<tail>["'])"""
)
def version_asset(match):
    name=match.group('name')
    asset=source.parent/'assets'/name
    if not asset.is_file():
        raise SystemExit(f'Missing referenced Pages asset: {asset}')
    asset_sources[name]=asset
    revision=hashlib.sha256(asset.read_bytes()).hexdigest()[:16]
    return f"{match.group('lead')}{match.group('url')}?rev={revision}{match.group('tail')}"
published_html=asset_url_pattern.sub(version_asset,html)
# A versioned MUTE style and its JS renderer must be published together.
if 'assets/mixer-control-keys.css' in html:
    assert 'MUTE is rendered entirely from CSS/HTML' in (source.parent/'assets'/'mixer-control-keys.css').read_text(encoding='utf-8')
    assert 'mx-mute-face' in (source.parent/'assets'/'mixer-control-keys.js').read_text(encoding='utf-8')
    assert 'mixer-control-keys.css' in asset_sources and 'mixer-control-keys.js' in asset_sources
(dst/'preview').mkdir(exist_ok=True)
(dst/'preview'/'mixer.html').write_text(published_html,encoding='utf-8')
# Direct /preview-v2/ route and /preview/mixer.html must never diverge.
if source.resolve()==(ROOT/'site/preview-v2/index.html').resolve():
    (dst/'preview-v2'/'index.html').write_text(published_html,encoding='utf-8')
# All referenced CSS/JS files are shipped automatically. No hand-maintained
# allowlist, so newly added WIRE/component styles cannot silently 404.
for name,asset in sorted(asset_sources.items()):
    target=dst/'preview'/'assets'/name
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(asset,target)
    if target.read_bytes()!=asset.read_bytes():
        raise SystemExit(f'Pages asset copy mismatch: {name}')
# Original SVG knob is referenced from inside inline WIRE CSS, not HTML tags.
if 'assets/knob-ICON.svg' in html:
    source_knob=source.parent/'assets'/'knob-ICON.svg'
    if not source_knob.is_file():
        raise SystemExit(f'WIRE knob mask missing beside source HTML: {source_knob}')
    preview_knob=dst/'preview'/'assets'/'knob-ICON.svg'
    preview_knob.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source_knob,preview_knob)
    if preview_knob.read_bytes()!=source_knob.read_bytes():
        raise SystemExit('WIRE knob mask copy mismatch in generated Pages preview')
(dst/'preview'/'build.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
(dst/'.nojekyll').touch()
assert (dst/'index.html').exists() and (dst/'preview'/'index.html').exists()
print('Built GitHub Pages:',dst,'preview source:',source,'revision:',args.sha)
