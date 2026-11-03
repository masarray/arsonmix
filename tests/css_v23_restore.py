"""V23: exact V22 restoration of Chrome and WIRE cascade winner pruning.

All edits are reversible exact old-source offsets, not a synthetic stylesheet.
No semantic selector rewrites and no live/current owner ownership ambiguity.
"""
import json,re,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
ASSETS=HTML.parent/"assets"
LEDGER=ROOT/"tests/fixtures/css-v23-cascade-winners.json"

def fingerprint(s):
    h=2166136261
    for c in s:h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    x=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert x["milestone"]=="V23" and x["baseCommit"]=="9b286f486bc59cd0e7a663912787b00b73a30a18"
    assert x["ownerOrder"]==["mixer-chrome-material.css","mixer-wire-dots.css"]
    assert x["totals"]["removedDeclarations"]==25
    assert x["totals"]["removedImportant"]==20
    assert x["totals"]["removedWholeRules"]==6
    assert x["totals"]["createdStylesheets"]==x["totals"]["changedJsFiles"]==0
    return x

def original_owner(name):
    """Return byte-exact V22 source of one of the two touched CSS owners."""
    data=manifest()
    assert name in data["ownerOrder"]
    record=data["owners"][name]
    current=(ASSETS/name).read_text(encoding="utf-8")
    assert (len(current),fingerprint(current),current.count("!important"))==(
        record["current"]["length"],record["current"]["fnv"],
        record["current"]["important"]),(name,"V23 owner source changed")
    old=current
    for e in sorted(record["edits"],key=lambda v:v["offset"]):
        p=e["offset"]
        assert 0<=p<=len(old)
        old=old[:p]+e["text"]+old[p:]
    assert (len(old),fingerprint(old),old.count("!important"))==(
        record["old"]["length"],record["old"]["fnv"],
        record["old"]["important"]),(name,"V22 old owner cannot be restored")
    assert len(old)-len(current)==record["metrics"]["removedChars"]
    assert len(record["evidence"])==record["metrics"]["declarations"]
    return old

def reconstruct():
    """Return exact pre-V23 HTML plus pre-V23 Chrome and WIRE CSS."""
    data=manifest()
    live=HTML.read_text(encoding="utf-8")
    assert (len(live),fingerprint(live))==(
        data["htmlCurrent"]["length"],data["htmlCurrent"]["fnv"]
    ),"V23 HTML drift"
    previous=live
    for change in data["linkChanges"]:
        assert previous.count(change["now"])==1 and change["old"] not in previous
        previous=previous.replace(change["now"],change["old"],1)
    assert (len(previous),fingerprint(previous))==(
        data["htmlOld"]["length"],data["htmlOld"]["fnv"]
    ),"V22 original HTML must reconstruct byte-exactly"
    return previous,{name:original_owner(name) for name in data["ownerOrder"]}

def mount_original():
    """Mount approved actual V22 HTML and owners for browser A/B comparisons."""
    html,owners=reconstruct()
    temporary=[]
    try:
        for name,contents in owners.items():
            f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
                prefix=".v23-prior-"+name[:-4]+"-",dir=ASSETS,delete=False)
            with f:f.write(contents)
            temporary.append(Path(f.name))
            old_link=next(e["old"] for e in manifest()["linkChanges"]
                          if e["old"].startswith("assets/"+name+"?"))
            assert html.count(old_link)==1
            html=html.replace(old_link,"assets/"+Path(f.name).name+"?v=exact-v22",1)
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
            prefix=".v23-original-console-",dir=HTML.parent,delete=False)
        with f:f.write(html)
        temporary.append(Path(f.name))
        return Path(f.name),temporary
    except Exception:
        for file in temporary:file.unlink(missing_ok=True)
        raise
