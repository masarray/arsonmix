"""V26 exact recovery of approved V25 CSS without rebaselining history.

Delete only 11 rules whose *entire* selectors require the legacy DOM wrapper
.hw-switch-socket and one actually empty rule. Never rewrite :is() specificity.
"""
import json,re,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
KEY=HTML.parent/"assets/mixer-control-keys.css"
LEDGER=ROOT/"tests/fixtures/css-v26-retired-socket-removals.json"

def meta(text):
    h=2166136261
    for c in text:h=((h^ord(c))*16777619)&0xffffffff
    return {"length":len(text),"fnv":f"{h:08x}","important":text.count("!important")}

def manifest():
    d=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert d["milestone"]=="V26" and d["baseCommit"]=="2f01022370c589205d738366242d2d2aeb77b10d"
    assert d["owners"]==["inline","mixer-control-keys.css"]
    assert d["totals"]["deadRules"]==11
    assert d["totals"]["emptyRules"]==1
    assert d["totals"]["removedImportant"]==51
    assert d["totals"]["newRules"]==d["totals"]["newImportant"]==0
    assert d["totals"]["newStylesheets"]==0
    assert d["totals"]["modifiedJs"]==d["totals"]["modifiedEngine"]==0
    return d

def inline_css(text):
    blocks=list(re.finditer(r"<style>([\s\S]*?)</style>",text))
    assert len(blocks)==3 and blocks[0].group(1)==blocks[1].group(1)==""
    return blocks[-1].group(1)

def historical_v26_owner(modern,record):
    """Reconstruct the immutable V26 owner after the one audited PFL fix.

    The shipping hardware key has data-hw-kind="pfl"; V26's historic owner
    used "cue" for the LED selector. Keep the V26 ledger byte-exact rather
    than silently rewriting an old milestone fingerprint or allowing arbitrary
    CSS changes. Only this single selector substitution is admissible.
    """
    if meta(modern)==record["current"]:
        return modern
    live='.app .console-body .strip .mx-hw-row>button.mx-hw-key[data-hw-kind="pfl"]{--mx-hw-led:101,210,236}'
    historical=live.replace('data-hw-kind="pfl"','data-hw-kind="cue"')
    if modern.count(live)!=1 or modern.count(historical)!=0:
        raise AssertionError("V26 owner source changed unexpectedly")
    original=modern.replace(live,historical,1)
    assert meta(original)==record["current"],"Unapproved CSS drift outside exact PFL selector"
    return original

def restore_owner(modern,record):
    modern=historical_v26_owner(modern,record)
    assert meta(modern)==record["current"],"V26 owner source changed unexpectedly"
    old=modern
    for edit in sorted(record["edits"],key=lambda e:e["offset"]):
        p=edit["offset"]
        assert 0<=p<=len(old)
        old=old[:p]+edit["text"]+old[p:]
    assert meta(old)==record["old"],"V25 original owner not byte-exact"
    assert len(old)-len(modern)==record["metrics"]["chars"]
    assert old.count("!important")-modern.count("!important")==record["metrics"]["important"]
    return old

def reconstruct():
    """Return exact prior V25 HTML plus prior CSS key owner."""
    d=manifest()
    live_html=HTML.read_text(encoding="utf-8")
    live_key=KEY.read_text(encoding="utf-8")
    assert meta(live_html)==d["htmlCurrent"],"V26 current HTML drift"
    live_inline=inline_css(live_html)
    old_inline=restore_owner(live_inline,d["sources"]["inline"])
    old_key=restore_owner(live_key,d["sources"]["mixer-control-keys.css"])
    assert live_html.count(d["currentLink"])==1
    old_html=live_html.replace(d["currentLink"],d["oldLink"],1)
    new_style="<style>"+live_inline+"</style>"
    old_style="<style>"+old_inline+"</style>"
    assert old_html.count(new_style)==1
    old_html=old_html.replace(new_style,old_style,1)
    assert meta(old_html)==d["htmlOld"],"V25 baseline HTML reconstruction failed"
    return old_html,old_key

def mount_original():
    html,css=reconstruct()
    d=manifest()
    tmp=[]
    try:
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=KEY.parent,
            prefix=".v26-old-v25-key-",suffix=".css",delete=False)
        with f:f.write(css)
        tmp.append(Path(f.name))
        assert html.count(d["oldLink"])==1
        html=html.replace(d["oldLink"],
            "assets/"+Path(f.name).name+"?v=exact-v25",1)
        page=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=HTML.parent,
            prefix=".v26-old-v25-",suffix=".html",delete=False)
        with page:page.write(html)
        tmp.append(Path(page.name))
        return Path(page.name),tmp
    except Exception:
        for p in tmp:p.unlink(missing_ok=True)
        raise
