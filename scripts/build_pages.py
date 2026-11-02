"""Build public Pages landing and interactive console from canonical standalone HTML."""
from pathlib import Path
import argparse,os,json,shutil,subprocess,sys
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
(dst/'preview').mkdir(exist_ok=True)
(dst/'preview'/'mixer.html').write_text(html,encoding='utf-8')
# The same WIRE knob-ICON.svg is referenced by CSS relative to mixer.html.
# /preview-v2/ has assets/ beside index.html; the generated /preview/ must
# receive the matching asset beside mixer.html (no rewrite or second knob).
if 'assets/knob-ICON.svg' in html:
    source_knob=source.parent/'assets'/'knob-ICON.svg'
    if not source_knob.is_file():
        raise SystemExit(f'WIRE knob mask missing beside source HTML: {source_knob}')
    preview_knob=dst/'preview'/'assets'/'knob-ICON.svg'
    preview_knob.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source_knob,preview_knob)
    if preview_knob.read_bytes()!=source_knob.read_bytes():
        raise SystemExit('WIRE knob mask copy mismatch in generated Pages preview')
# Control material has ONE owner shared by /preview-v2/ and the public
# /preview/mixer.html alias; never leave the alias with stale or missing assets.
for control_asset in ('mixer-control-keys.css','mixer-control-keys.js'):
    if 'assets/'+control_asset in html:
        src=source.parent/'assets'/control_asset
        if not src.is_file(): raise SystemExit(f'Missing shared mixer key asset: {src}')
        target=dst/'preview'/'assets'/control_asset
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(src,target)
        if src.read_bytes()!=target.read_bytes():
            raise SystemExit(f'Published asset differs from preview source: {control_asset}')
(dst/'preview'/'build.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
(dst/'.nojekyll').touch()
assert (dst/'index.html').exists() and (dst/'preview'/'index.html').exists()
print('Built GitHub Pages:',dst,'preview source:',source,'revision:',args.sha)
