"""V25: V51 physical button styling source-order-preserving recovery.

Original inline suffix is now an exact prefix of the immediately following
existing hardware-key CSS owner. The aggregate declaration stream is identical.
No older V10-V24 historical ledger is modified.
"""
import json,re,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview/index.html"
OWNER=HTML.parent/"assets/mixer-control-keys.css"
LEDGER=ROOT/"tests/fixtures/css-v25-physical-key-owner-migration.json"

def fnv(text):
    h=2166136261
    for c in text:h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def meta(text):
    return {"length":len(text),"fnv":fnv(text),"important":text.count("!important")}

def manifest():
    record=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert record["milestone"]=="V25"
    assert record["baseCommit"]=="885e7e8175c0f0cab288760d3cd60c0e77e04812"
    assert record["componentOwner"]=="mixer-control-keys.css"
    assert record["metrics"]["inlineRemovedChars"]==8746
    assert record["metrics"]["ownerAddedChars"]==8746
    assert record["metrics"]["movedImportant"]==97
    assert record["metrics"]["addedImportant"]==0
    assert record["metrics"]["deletedCssDeclarations"]==0
    assert record["metrics"]["createdStylesheets"]==0
    assert record["metrics"]["jsChanges"]==record["metrics"]["engineChanges"]==0
    return record

def inline_css(html):
    blocks=list(re.finditer(r"<style>([\s\S]*?)</style>",html))
    assert len(blocks)==3 and blocks[0].group(1)==blocks[1].group(1)==""
    return blocks[-1].group(1),blocks[-1]

def reconstruct():
    """Return exact V51 pre-V25 HTML and key CSS (no guessed prior baseline)."""
    data=manifest()
    modern_html=HTML.read_text(encoding="utf-8")
    modern_key=OWNER.read_text(encoding="utf-8")
    assert meta(modern_html)==data["htmlCurrent"],"V25 HTML drift"
    assert meta(modern_key)==data["keyCurrent"],"V25 physical-key owner drift"
    live_inline,match=inline_css(modern_html)
    assert meta(live_inline)==data["inlineCurrent"]
    moved=data["movedCss"]
    assert meta(moved)==data["movedMeta"]
    assert modern_key.startswith(moved),"The moved material must be the FIRST CSS owner slice"
    assert "url(" not in moved and "@import" not in moved and "@charset" not in moved
    assert ".mx-hw-key" not in moved and ".mx-hw-row" not in moved
    old_key=modern_key[len(moved):]
    old_inline=live_inline+moved
    assert meta(old_key)==data["keyPrior"]
    assert meta(old_inline)==data["inlinePrior"]
    assert meta(old_inline+old_key)==data["combined"]
    assert old_inline+old_key==live_inline+modern_key,"CRITICAL: CSS cascade order changed"
    assert modern_html.count(data["currentLink"])==1
    old_html=modern_html.replace(data["currentLink"],data["priorLink"],1)
    # Replace exactly the final inline style body. HTML/JS/DOM bytes remain intact.
    original_style="<style>"+live_inline+"</style>"
    recovered_style="<style>"+old_inline+"</style>"
    assert old_html.count(original_style)==1
    old_html=old_html.replace(original_style,recovered_style,1)
    assert meta(old_html)==data["htmlPrior"],"V51 baseline HTML must recover exactly"
    return old_html,old_key

def mount_original():
    """Produce a real V51 baseline in the same CSS and image resolution roots."""
    old_html,old_key=reconstruct()
    tmp=[]
    try:
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=OWNER.parent,
            prefix=".v25-old-key-",suffix=".css",delete=False)
        with f:f.write(old_key)
        tmp.append(Path(f.name))
        data=manifest()
        assert old_html.count(data["priorLink"])==1
        old_html=old_html.replace(data["priorLink"],
            "assets/"+Path(f.name).name+"?v=exact-v51",1)
        page=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",dir=HTML.parent,
            prefix=".v25-old-console-",suffix=".html",delete=False)
        with page:page.write(old_html)
        tmp.append(Path(page.name))
        return Path(page.name),tmp
    except Exception:
        for path in tmp:path.unlink(missing_ok=True)
        raise
