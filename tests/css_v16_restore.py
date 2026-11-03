"""V16 exact pre-migration WIRE CSS reconstruction.

The move is structural: 241 identical WIRE rules relocated from inline
style #2 into the front of the existing mixer-wire-dots.css. Reconstruct all
original bytes from recorded original offsets and prefixed source, including
original asset source order and query string; no synthetic CSS.
"""
import json,re,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
WIRE=HTML.parent/"assets/mixer-wire-dots.css"
LEDGER=ROOT/"tests/fixtures/css-v16-wire-inline-migration.json"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V16" and data["movedRules"]==241
    assert data["movedChars"]==42895
    return data

def reconstruct():
    data=manifest()
    current=HTML.read_text(encoding="utf-8")
    now_wire=WIRE.read_text(encoding="utf-8")
    assert len(now_wire)==data["afterWireSize"],"WIRE owner changed"
    prefix=data["movedPrefix"]
    assert now_wire.startswith(prefix),"WIRE original moved rule prefix changed"
    old_wire=now_wire[len(prefix):]
    assert len(old_wire)==data["beforeWireSize"]
    assert current.count(data["newWireUrl"])==1
    old=current.replace(data["newWireUrl"],data["sourceWireUrl"],1)
    tags=list(re.finditer(r"<style>(.*?)</style>",old,re.S))
    assert len(tags)==3
    block=tags[data["inlineBlock"]].group(1)
    assert len(block)==data["newInlineSize"]
    original=block
    for part in sorted(data["insertions"],key=lambda e:e["offset"]):
        at=part["offset"]
        assert 0<=at<=len(original),(at,len(original))
        original=original[:at]+part["source"]+original[at:]
    assert len(original)==data["sourceInlineSize"]
    assert sum(len(x["source"]) for x in data["insertions"])==data["movedChars"]
    assert prefix.endswith("".join(x["source"] for x in data["insertions"])+"\n")
    match=tags[data["inlineBlock"]]
    old=old[:match.start(1)]+original+old[match.end(1):]
    return old,old_wire

def mount_original(html,wire=None):
    """Return old HTML path + paths to remove; CSS is stored beside assets."""
    if wire is None:_,wire=reconstruct()
    original=manifest()["sourceWireUrl"]
    assert html.count(original)==1
    temporary=[]
    try:
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
            prefix=".v16-previous-wire-",dir=WIRE.parent,delete=False)
        with f:f.write(wire)
        temporary.append(Path(f.name))
        html=html.replace(original,"assets/"+Path(f.name).name+"?v=v47",1)
        page=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
            prefix=".v16-previous-console-",dir=HTML.parent,delete=False)
        with page:page.write(html)
        temporary.append(Path(page.name))
        return Path(page.name),temporary
    except Exception:
        for f in temporary:f.unlink(missing_ok=True)
        raise
