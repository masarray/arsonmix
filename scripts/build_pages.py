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
    assert candidate['kind']=='DRAFT_GUI_QA_ONLY'
    assert candidate['source_pr']==271
    assert candidate['source_branch']=='work/operator-console-v6'
    assert len(candidate['source_sha'])==40 and candidate['ci_conclusion']=='success'
    source=ROOT/'site/preview/mixer.html'
    assert source.is_file()
    metadata={'sha':candidate['source_sha'],'branch':candidate['source_branch'],
              'source':'PR #271 approved-screen QA snapshot','kind':'DRAFT_GUI_QA_ONLY',
              'publisher_main_sha':args.sha,'ci_run':candidate['ci_run']}
html=source.read_text(encoding='utf-8')
assert '<style>' in html and '<script>' in html and 'data-fader' in html
(dst/'preview').mkdir(exist_ok=True)
(dst/'preview'/'mixer.html').write_text(html,encoding='utf-8')
(dst/'preview'/'build.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
(dst/'.nojekyll').touch()
assert (dst/'index.html').exists() and (dst/'preview'/'index.html').exists()
print('Built GitHub Pages:',dst,'preview source:',source,'revision:',args.sha)
