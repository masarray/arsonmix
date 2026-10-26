"""Deterministically build/check runtime CSS and offline standalone HTML from source owners."""
from pathlib import Path
import argparse
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'web/styles-src'
MODULES=('00-font-and-assets.css','10-console-legacy.css','20-button-180.css','25-fx-power-controls.css','30-selection-effects.css','40-rail-pan.css')
CSS=ROOT/'web/styles.css'
INDEX=ROOT/'web/index.html'
HTML=ROOT/'AR-MIX-Live-FX-Host-Standalone-v0.3.html'
def compile_css():
    return ''.join((SOURCE/name).read_text(encoding='utf-8') for name in MODULES)
def embed(page,css):
    a=page.index('<style>')+len('<style>')
    b=page.index('</style>',a)
    if '<style>' in page[a:b]: raise ValueError('unexpected nested stylesheet')
    return page[:a]+css+page[b:]
def synchronize_controls(page: str) -> str:
    """Always import both navigation and rail HTML from canonical WebView2 index."""
    source=INDEX.read_text(encoding='utf-8')
    for opening,closing in (('<div class="toolbar-actions">','</nav>'),
                            ('<aside class="left-rail">','</aside>')):
        a=page.index(opening)
        z=page.index(closing,a)+len(closing)
        b=source.index(opening)
        y=source.index(closing,b)+len(closing)
        page=page[:a]+source[b:y]+page[z:]
    return page
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check',action='store_true')
    args=p.parse_args()
    css=compile_css()
    page=HTML.read_text(encoding='utf-8')
    built=synchronize_controls(embed(page,css))
    if args.check:
        drift=[name for name,ok in (('web/styles.css',CSS.read_text(encoding='utf-8')==css),('standalone HTML',built==page)) if not ok]
        if drift: raise SystemExit('Outdated generated GUI artifact: '+', '.join(drift)+'; run python scripts/build_ui_css.py')
        print('PASS: source-owned CSS, WebView2, offline HTML in byte parity')
    else:
        CSS.write_text(css,encoding='utf-8',newline='')
        if built!=page: HTML.write_text(built,encoding='utf-8',newline='')
        print('Generated offline CSS and standalone GUI')
if __name__=='__main__': main()
