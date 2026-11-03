"""V17 reversible, byte-exact component CSS migration."""
import json,re,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
OWNER=HTML.parent/"assets/mixer-plugin-browser.css"
LEDGER=ROOT/"tests/fixtures/css-v17-plugin-overlay-migration.json"

def fingerprint(value):
    h=2166136261
    for c in value:h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V17" and data["movedCharacters"]>3500
    return data

def reconstruct():
    data=manifest()
    from css_v18_restore import reconstruct as restore_v17_source
    live,_=restore_v17_source()
    css=OWNER.read_text(encoding="utf-8")
    assert (len(live),fingerprint(live))==(data["newHtmlLength"],data["newHtmlFNV"])
    assert (len(css),fingerprint(css))==(data["newOwnerLength"],data["newOwnerFNV"])
    assert css.endswith(data["ownerSuffix"])
    original_css=css[:-len(data["ownerSuffix"])]
    assert (len(original_css),fingerprint(original_css))==(data["oldOwnerLength"],data["oldOwnerFNV"])
    assert live.count(data["newLink"])==1 and data["oldLink"] not in live
    matches=list(re.finditer(r"<style>(.*?)</style>",live,re.S))
    assert len(matches)==3
    parts=[m.group(1) for m in matches]
    assert parts[0]==data["remainingFirst"] and parts[1]==data["remainingSecond"]
    parts[0]=data["originalFirstStyle"]
    parts[1]=data["originalSecondStyle"]
    original=live
    for i in reversed(range(2)):
        m=matches[i]
        original=original[:m.start(1)]+parts[i]+original[m.end(1):]
    original=original.replace(data["newLink"],data["oldLink"],1)
    assert (len(original),fingerprint(original))==(data["oldHtmlLength"],data["oldHtmlFNV"])
    return original,original_css

def mount_owner(original_html, original_css=None):
    """Mount exact V16 plugin owner beside assets for real browser regression."""
    data=manifest()
    assert original_html.count(data["oldLink"])==1
    if original_css is None:_,original_css=reconstruct()
    f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
        prefix=".v17-old-plugin-",dir=OWNER.parent,delete=False)
    try:
        with f:f.write(original_css)
        original_html=original_html.replace(data["oldLink"],
            "assets/"+Path(f.name).name+"?v=fx-owner-v8",1)
    except Exception:
        Path(f.name).unlink(missing_ok=True)
        raise
    return original_html,[Path(f.name)]

def mount_original():
    html,css=reconstruct()
    html,temporary=mount_owner(html,css)
    from css_v18_restore import mount_owner as mount_chrome
    html,old_chrome=mount_chrome(html)
    temporary.extend(old_chrome)
    try:
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
            prefix=".v17-old-console-",dir=HTML.parent,delete=False)
        with f:f.write(html)
        temporary.append(Path(f.name))
        return Path(f.name),temporary
    except Exception:
        for p in temporary:p.unlink(missing_ok=True)
        raise
