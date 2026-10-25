"""Build public Pages landing and interactive console from canonical standalone HTML."""
from pathlib import Path
import argparse,os,json,shutil,subprocess
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
source=ROOT/'AR-MIX-Live-FX-Host-Standalone-v0.3.html'
html=source.read_text(encoding='utf-8')
assert '<style>' in html and '<script>' in html and 'data-fader' in html
(dst/'preview').mkdir(exist_ok=True)
(dst/'preview'/'mixer.html').write_text(html,encoding='utf-8')
(dst/'preview'/'build.json').write_text(json.dumps({'sha':args.sha,'branch':args.branch,'source':source.name},indent=2),encoding='utf-8')
(dst/'.nojekyll').touch()
assert (dst/'index.html').exists() and (dst/'preview'/'index.html').exists()
print('Built GitHub Pages:',dst,'preview source:',source,'revision:',args.sha)
