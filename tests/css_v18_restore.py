"""V18 exact previous Chrome material and middle inline owner reconstruction."""
import json,re,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
CHROME=HTML.parent/"assets/mixer-chrome-material.css"
LEDGER=ROOT/"tests/fixtures/css-v18-chrome-inline-consolidation.json"

def fingerprint(text):
    h=2166136261
    for c in text:h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V18" and data["inlineMoved"]==17244
    assert len(data["removedDeclarations"])==data["removedImportant"]==4
    return data

def reconstruct():
    data=manifest()
    from css_v19_restore import reconstruct as restore_v18_source
    live,_,css=restore_v18_source()
    assert (len(live),fingerprint(live))==(data["newHtmlLength"],data["newHtmlFNV"])
    assert (len(css),fingerprint(css))==(data["newOwnerLength"],data["newOwnerFNV"])
    assert css.startswith(data["prefix"])
    old_css=css[len(data["prefix"]):]
    assert (len(old_css),fingerprint(old_css))==(data["oldOwnerLength"],data["oldOwnerFNV"])
    matches=list(re.finditer(r"<style>(.*?)</style>",live,re.S))
    assert len(matches)==3 and matches[1].group(1)==""
    m=matches[1]
    previous=live[:m.start(1)]+data["originalStyle"]+live[m.end(1):]
    assert previous.count(data["newLink"])==1
    previous=previous.replace(data["newLink"],data["oldLink"],1)
    assert (len(previous),fingerprint(previous))==(data["oldHtmlLength"],data["oldHtmlFNV"])
    return previous,old_css

def mount_owner(html,old_css=None):
    data=manifest()
    if old_css is None:_,old_css=reconstruct()
    assert html.count(data["oldLink"])==1
    f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
        prefix=".v18-previous-chrome-",dir=CHROME.parent,delete=False)
    try:
        with f:f.write(old_css)
        html=html.replace(data["oldLink"],"assets/"+Path(f.name).name+"?v=css-owner-v15",1)
    except Exception:
        Path(f.name).unlink(missing_ok=True)
        raise
    return html,[Path(f.name)]

def mount_original():
    old_html,old_css=reconstruct()
    html,files=mount_owner(old_html,old_css)
    from css_v19_restore import mount_owners as mount_pre_v19
    html,old_controls=mount_pre_v19(html,include_chrome=False)
    files.extend(old_controls)
    try:
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
            prefix=".v18-original-console-",dir=HTML.parent,delete=False)
        with f:f.write(html)
        files.append(Path(f.name))
        return Path(f.name),files
    except Exception:
        for p in files:p.unlink(missing_ok=True)
        raise
