"""Deterministically build/check runtime CSS and offline standalone HTML from source owners."""
from pathlib import Path
import argparse
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'web/styles-src'
MODULES=('00-font-and-assets.css','10-console-legacy.css','20-button-180.css','21-navigation-fx-power.css','30-selection-effects.css','40-rail-pan.css','41-mixer-performance.css','42-mixer-premium.css')
CSS=ROOT/'web/styles.css'
HTML=ROOT/'AR-MIX-Live-FX-Host-Standalone-v0.3.html'
def compile_css():
    return ''.join((SOURCE/name).read_text(encoding='utf-8') for name in MODULES)
def embed(page,css):
    a=page.index('<style>')+len('<style>')
    b=page.index('</style>',a)
    if '<style>' in page[a:b]: raise ValueError('unexpected nested stylesheet')
    return page[:a]+css+page[b:]
def embed_managed(page, tag, slot, content):
    """Bind optional G1/G2/L2 GUI modules to the same standalone source as WebView2."""
    start=f'<{tag} id="{slot}">'
    end=f'</{tag}>'
    if start in page:
        a=page.index(start)+len(start)
        b=page.index(end,a)
        if start in page[a:b]: raise ValueError('nested managed GUI asset')
        return page[:a]+content+page[b:]
    pos=page.index('</style>')+len('</style>') if tag=='style' else page.rfind('</script>')+len('</script>')
    if pos<0: raise ValueError('standalone missing injection anchor')
    return page[:pos]+'\\n'+start+content+end+page[pos:]

def embed_all(page,css):
    built=embed(page,css)
    # The first classic script is the source-owned app.js; do not leave a
    # stale, independently edited GUI controller in the downloadable preview.
    script_start=built.index('<script>')+len('<script>')
    script_end=built.index('</script>',script_start)
    if not built[script_start:script_end].startswith("'use strict';"):
        raise ValueError('unexpected standalone app.js anchor')
    built=(built[:script_start]+(ROOT/'web/app.js').read_text(encoding='utf-8')
           +built[script_end:])
    for tag,slot,path in (
        ('style','arsonmix-mixer-io-css','web/mixer-io.css'),
        ('style','arsonmix-mixer-layout-css','web/mixer-layout.css'),
        ('script','arsonmix-mixer-io-js','web/mixer-io.js'),
        ('script','arsonmix-mixer-layout-js','web/mixer-layout.js'),
    ):
        built=embed_managed(built,tag,slot,(ROOT/path).read_text(encoding='utf-8'))
    return built

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check',action='store_true')
    args=p.parse_args()
    css=compile_css()
    page=HTML.read_text(encoding='utf-8')
    built=embed_all(page,css)
    if args.check:
        drift=[name for name,ok in (('web/styles.css',CSS.read_text(encoding='utf-8')==css),('standalone HTML',built==page)) if not ok]
        if drift: raise SystemExit('Outdated generated GUI artifact: '+', '.join(drift)+'; run python scripts/build_ui_css.py')
        print('PASS: source-owned CSS, WebView2, offline HTML in byte parity')
    else:
        CSS.write_text(css,encoding='utf-8',newline='')
        if built!=page: HTML.write_text(built,encoding='utf-8',newline='')
        print('Generated offline CSS and standalone GUI')
if __name__=='__main__': main()
