"""V27 precise pre-change GUI CSS restoration and cascade proof.

All candidate deletions lose to a later declaration with the identical entire
selector, at-rule context, property, and !important priority in the same file.
The sole global change uses :where() to keep CSS specificity unchanged.
"""
import json,re,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
ASSETS=HTML.parent/"assets"
LEDGER=ROOT/"tests/fixtures/css-v27-scoped-material-pruning.json"

def meta(text):
    h=2166136261
    for c in text:h=((h^ord(c))*16777619)&0xffffffff
    return {"length":len(text),"important":text.count("!important"),"fnv":f"{h:08x}"}

def manifest():
    d=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert d["milestone"]=="V27" and d["baseCommit"]=="0215986fbbe2c67971680952f75f069950ac653e"
    assert d["ownerOrder"]==["mixer-chrome-material.css","mixer-plugin-browser.css","mixer-pan-controls.css","mixer-fader-assembly.css","mixer-meter-display.css","mixer-utility-controls.css"]
    assert d["totals"]["removedDeclarations"]==37
    assert d["totals"]["newStylesheets"]==d["totals"]["addedImportant"]==0
    assert d["totals"]["changedJS"]==d["totals"]["changedEngine"]==0
    return d

def reconstruct():
    d=manifest()
    page=HTML.read_text(encoding="utf-8")
    assert meta(page)==d["currentHTML"],"V27 HTML drift"
    original=page
    for change in d["linkChanges"]:
        assert original.count(change["now"])==1
        original=original.replace(change["now"],change["old"],1)
    assert meta(original)==d["originalHTML"],"V27 pre-change HTML mismatch"
    owners={}
    for name in d["ownerOrder"]:
        record=d["owners"][name]
        current=(ASSETS/name).read_text(encoding="utf-8")
        assert meta(current)==record["current"],("CSS owner drift",name)
        old=current
        if name=="mixer-chrome-material.css":
            replace=record["globalScope"]
            assert old.count(replace["now"])==1
            old=old.replace(replace["now"],replace["old"],1)
            assert replace["oldSpecificity"]==replace["newSpecificity"]
            assert replace["excludedOutsideApp"] is True
        else:
            for edit in sorted(record["edits"],key=lambda e:e["offset"]):
                offset=edit["offset"]
                assert 0<=offset<=len(old)
                old=old[:offset]+edit["text"]+old[offset:]
            assert len(record["edits"])==record["metrics"]["declarations"]
            assert old.count("!important")-current.count("!important")==record["metrics"]["removedImportant"]
            for edit in record["edits"]:
                assert edit["winnerOffset"]>edit["offset"]
                assert edit["winnerText"] in old
                assert edit["selector"] and edit["property"]
        assert meta(old)==record["old"],("Cannot recover exact old CSS",name)
        owners[name]=old
    return original,owners

def mount_original():
    """Real accepted main #363 HTML with its exact six original linked owners."""
    html,owners=reconstruct()
    d=manifest()
    temporary=[]
    try:
        for name,source in owners.items():
            css=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=ASSETS,
                prefix=".v27-previous-"+name[:-4]+"-",suffix=".css",delete=False)
            with css:css.write(source)
            temporary.append(Path(css.name))
            entry=next(e for e in d["linkChanges"] if e["name"]==name)
            assert html.count(entry["old"])==1
            html=html.replace(entry["old"],"assets/"+Path(css.name).name+"?v=exact-before-v27",1)
        page=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=HTML.parent,
            prefix=".v27-original-",suffix=".html",delete=False)
        with page:page.write(html)
        temporary.append(Path(page.name))
        return Path(page.name),temporary
    except Exception:
        for path in temporary:path.unlink(missing_ok=True)
        raise
