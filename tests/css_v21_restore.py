"""V21: byte-exact reconstruction of V20 inline V9–V14 chassis geometry.

The V9–V14 physical console section geometry is appended verbatim to the
existing immediately preceding Chrome stylesheet; the original Cascade
position is unchanged. Does not move V15 WIRE or V1+ hardware material.
"""
import json,re,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
CHROME=HTML.parent/"assets/mixer-chrome-material.css"
LEDGER=ROOT/"tests/fixtures/css-v21-chassis-geometry-migration.json"

def fingerprint(text):
    h=2166136261
    for c in text:
        h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V21"
    assert data["baseCommit"]=="d9e0931321ca0a4b23f8fe8a8594e49c07c79353"
    assert data["sourceInlinePrefix"].startswith("/* V9: CONSOLE MECHANICAL LAYOUT")
    assert len(data["sourceInlinePrefix"])==13907
    assert data["metrics"]["movedChars"]==13907
    assert data["metrics"]["deletedCssChars"]==0
    assert data["metrics"]["createdStylesheets"]==0
    return data

def reconstruct():
    """Return the exact pre-V21 V20 HTML and pre-V21 Chrome owner."""
    data=manifest()
    # M3 is a later operator UI milestone: verify V21's historical ledger
    # against a *reversible, explicit* normalization of M3-only changes.
    # The approved V21 fingerprints below are never relaxed or rebaselined.
    from gui_m3_compat import transform,suffix
    from gui_v49_restore import historical_html
    source=historical_html()
    live=transform(source,reverse=True,skip={"chrome version"})
    m3_link="assets/mixer-chrome-material.css?v=css-chrome-owner-v21-m3-header"
    assert live.count(m3_link)==1
    live=live.replace(m3_link,data["newLink"],1)
    chrome,m3_tail=suffix("chrome")
    assert (len(live),fingerprint(live))==(
        data["current"]["htmlLength"],data["current"]["htmlFNV"]
    ),"V21 HTML drift: source contract must not be silently rebaselined"
    assert (len(chrome),fingerprint(chrome))==(
        data["current"]["chromeLength"],data["current"]["chromeFNV"]
    ),"V21 shared Chrome owner changed outside approved ledger"
    prefix=data["sourceInlinePrefix"]
    assert chrome.endswith(prefix),"V21 chassis geometry suffix missing"
    old_chrome=chrome[:-len(prefix)]
    assert (len(old_chrome),fingerprint(old_chrome))==(
        data["old"]["chromeLength"],data["old"]["chromeFNV"]
    ),"pre-V21 Chrome owner must reconstruct byte-exactly"
    styles=list(re.finditer(r"<style>(.*?)</style>",live,re.S))
    assert len(styles)==3 and styles[0].group(1)==styles[1].group(1)==""
    late=styles[2].group(1)
    assert late.startswith(data["boundaryMarker"]),"V15 WIRE boundary moved unexpectedly"
    assert (len(late),fingerprint(late))==(
        data["current"]["inlineLength"],data["current"]["inlineFNV"]
    ),"late V21 inline source drift"
    assert late.count("!important")==data["current"]["inlineImportant"]
    old_inline=prefix+late
    assert (len(old_inline),fingerprint(old_inline))==(
        data["old"]["inlineLength"],data["old"]["inlineFNV"]
    ),"V20 inline CSS must be restored byte-exactly"
    assert old_inline.count("!important")==data["old"]["inlineImportant"]
    assert "url(" not in prefix and "@import" not in prefix
    previous=live[:styles[2].start(1)]+old_inline+live[styles[2].end(1):]
    assert previous.count(data["newLink"])==1 and data["oldLink"] not in previous
    previous=previous.replace(data["newLink"],data["oldLink"],1)
    assert (len(previous),fingerprint(previous))==(
        data["old"]["htmlLength"],data["old"]["htmlFNV"]
    ),"V20 approved HTML fingerprint must stay immutable"
    return transform(previous,skip={"chrome version"}),old_chrome+m3_tail

def mount_owner(html,old_chrome=None):
    """Mount the original Chrome CSS next to a historical file:// baseline."""
    data=manifest()
    if old_chrome is None:
        _,old_chrome=reconstruct()
    assert html.count(data["oldLink"])==1
    css=tempfile.NamedTemporaryFile(
        mode="w",encoding="utf-8",suffix=".css",
        prefix=".v21-chrome-before-chassis-",dir=CHROME.parent,delete=False)
    try:
        with css:
            css.write(old_chrome)
        html=html.replace(
            data["oldLink"],"assets/"+Path(css.name).name+"?v=v21-original",1)
        return html,[Path(css.name)]
    except Exception:
        Path(css.name).unlink(missing_ok=True)
        raise

def mount_original():
    """Create a real previous HTML/CSS pair for Chromium visual parity."""
    previous,old_chrome=reconstruct()
    html,temporary=mount_owner(previous,old_chrome)
    try:
        f=tempfile.NamedTemporaryFile(
            mode="w",encoding="utf-8",suffix=".html",
            prefix=".v21-original-console-",dir=HTML.parent,delete=False)
        with f:
            f.write(html)
        temporary.append(Path(f.name))
        return Path(f.name),temporary
    except Exception:
        for path in temporary:
            path.unlink(missing_ok=True)
        raise
