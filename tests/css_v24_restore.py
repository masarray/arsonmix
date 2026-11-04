"""V24 exact previous V23 source recovery: three mature component owners.

The immutable ledger records each original declaration and UTF-8-safe
Python character offset, plus every shadowing winner. No CSS duplication.
"""
import json,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview/index.html"
ASSETS=HTML.parent/"assets"
LEDGER=ROOT/"tests/fixtures/css-v24-three-component-owner-pruning.json"

def fingerprint(s):
    h=2166136261
    for ch in s:h=((h^ord(ch))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V24" and data["baseCommit"]=="4a2005825bc57ef586aa452f1b29523a9be0295f"
    assert data["ownerOrder"]==["mixer-fader-assembly.css",
        "mixer-meter-display.css","mixer-fx-rack.css"]
    assert data["totals"]["removedDeclarations"]==26
    assert data["totals"]["removedImportant"]==25
    assert data["totals"]["newStylesheets"]==0
    assert data["totals"]["modifiedJsFiles"]==0
    assert data["totals"]["changedEngineFiles"]==0
    return data

def reconstruct():
    data=manifest()
    page=HTML.read_text(encoding="utf-8")
    assert (len(page),fingerprint(page))==(
        data["htmlCurrent"]["length"],data["htmlCurrent"]["fnv"]
    ),"V24 current HTML drift: update reverse history before new milestone"
    original_html=page
    for change in data["linkChanges"]:
        assert original_html.count(change["now"])==1
        original_html=original_html.replace(change["now"],change["old"],1)
    assert (len(original_html),fingerprint(original_html))==(
        data["htmlOld"]["length"],data["htmlOld"]["fnv"]
    ),"V23 original HTML must be byte-exact"
    originals={}
    for owner in data["ownerOrder"]:
        record=data["owners"][owner]
        modern=(ASSETS/owner).read_text(encoding="utf-8")
        assert (len(modern),fingerprint(modern),modern.count("!important"))==(
            record["current"]["length"],record["current"]["fnv"],
            record["current"]["important"]
        ),(owner,"V24 CSS source drift")
        original=modern
        for edit in sorted(record["edits"],key=lambda e:e["offset"]):
            offset=edit["offset"]
            assert 0<=offset<=len(original)
            original=original[:offset]+edit["text"]+original[offset:]
        assert (len(original),fingerprint(original),original.count("!important"))==(
            record["old"]["length"],record["old"]["fnv"],
            record["old"]["important"]
        ),(owner,"previous source cannot be reconstructed")
        assert len(record["evidence"])==record["metrics"]["declarations"]
        assert len(original)-len(modern)==record["metrics"]["removedChars"]
        originals[owner]=original
    return original_html,originals

def mount_original():
    """Real V23 HTML and 3 original CSS files in the same assets directory."""
    html,owners=reconstruct()
    temporary=[]
    try:
        for name,source in owners.items():
            css=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",
                prefix=".v24-original-"+name[:-4]+"-",suffix=".css",
                dir=ASSETS,delete=False)
            with css:css.write(source)
            temporary.append(Path(css.name))
            entry=next(e for e in manifest()["linkChanges"]
                       if e["old"].startswith("assets/"+name+"?"))
            assert html.count(entry["old"])==1
            html=html.replace(entry["old"],
                "assets/"+Path(css.name).name+"?v=exact-v23",1)
        page=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",
            prefix=".v24-before-",suffix=".html",dir=HTML.parent,delete=False)
        with page:page.write(html)
        temporary.append(Path(page.name))
        return Path(page.name),temporary
    except Exception:
        for f in temporary:f.unlink(missing_ok=True)
        raise
