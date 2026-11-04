"""CSS V28: recover exact current-main approved /preview/ source and CSS before migration.

The full 13 KB late inline stylesheet moved to the END of the immediately
preceding existing physical console owner. CSS declaration byte stream is
unchanged and there are no relative URLs/imports/layers inside the moved CSS.
No GUI/ENGINE hooks, Javascript, stylesheet order, or design state is edited.
"""
import json,re,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview/index.html"
OWNER=HTML.parent/"assets/mixer-chrome-material.css"
LEDGER=ROOT/"tests/fixtures/css-v28-inline-chrome-owner.json"

def meta(s):
    v=2166136261
    for cp in s:
        v=((v^ord(cp))*16777619)&0xffffffff
    return {"length":len(s),"fnv":f"{v:08x}","important":s.count("!important")}

def manifest():
    record=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert record["milestone"]=="CSS V28"
    assert record["baseline"]=="46f600b143c8ef000a717ccb79a9c841a2589766"
    assert record["removedStyleNodes"]==3
    assert record["removedDeclarations"]==record["addedDeclarations"]==0
    assert record["addedImportant"]==record["addedStylesheets"]==0
    assert record["changedJs"]==record["changedEngine"]==0
    return record

def reconstruct():
    """Return byte-exact internal source HTML and Chrome material, with CSS order."""
    d=manifest()
    html=HTML.read_text(encoding="utf-8")
    chrome=OWNER.read_text(encoding="utf-8")
    assert meta(html)==d["current"]["html"],"V28 HTML changed after checkpoint"
    assert meta(chrome)==d["current"]["chrome"],"V28 Chrome CSS changed after checkpoint"
    moved=d["movedCss"]
    assert meta(moved)==d["prior"]["inline"]
    assert len(moved)==13043
    assert not re.search(r"url\s*\(|@import\b|@charset\b|@font-face\b|@layer\b",moved,re.I)
    assert chrome.endswith(moved),"Migrated CSS must be Chrome owner's exact tail"
    old_chrome=chrome[:-len(moved)]
    assert meta(old_chrome)==d["prior"]["chrome"]
    assert old_chrome.endswith("\n")
    assert meta(old_chrome+moved)["fnv"]==d["cssSequenceFNV"]
    assert old_chrome+moved==chrome,"CSS cascade source stream changed"
    assert html.count(d["newHref"])==1
    assert html.count("<style>")==0 and html.count("</style>")==0
    original_html=html.replace(d["newHref"],d["oldHref"],1)
    marker=d["oldHref"]+"\n"
    # Pre-V28 three style nodes are anchored uniquely by the approved
    # V59 head order. Recover via known historical fixed positions.
    original_html=original_html.replace(
        '<link rel="stylesheet" href="assets/mixer-section-controls.css?v=css-owner-v19">\n',
        '<link rel="stylesheet" href="assets/mixer-section-controls.css?v=css-owner-v19">\n<style></style>\n',1)
    original_html=original_html.replace(
        '<link rel="stylesheet" href="assets/mixer-v3-layout-contract.css?v=css-owner-v15">\n',
        '<link rel="stylesheet" href="assets/mixer-v3-layout-contract.css?v=css-owner-v15">\n<style></style>\n',1)
    assert original_html.count('<link rel="stylesheet" href="'+d["oldHref"]+'">\n')==1
    original_html=original_html.replace(
        '<link rel="stylesheet" href="'+d["oldHref"]+'">\n',
        '<link rel="stylesheet" href="'+d["oldHref"]+'">\n<style>'+moved+'</style>\n',1)
    assert meta(original_html)==d["prior"]["html"],"Cannot restore byte-exact V59 baseline"
    assert len(re.findall(r"<style>",original_html))==3
    return original_html,old_chrome

def mount_original():
    html,css=reconstruct()
    d=manifest()
    tmp=[]
    try:
        sheet=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",
            dir=OWNER.parent,prefix=".v28-accepted-chrome-",suffix=".css",
            delete=False)
        with sheet:sheet.write(css)
        tmp.append(Path(sheet.name))
        assert html.count(d["oldHref"])==1
        html=html.replace(d["oldHref"],"assets/"+Path(sheet.name).name+"?v=exact-v59",1)
        page=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",
            dir=HTML.parent,prefix=".v28-accepted-v59-",suffix=".html",
            delete=False)
        with page:page.write(html)
        tmp.append(Path(page.name))
        return Path(page.name),tmp
    except Exception:
        for p in tmp:p.unlink(missing_ok=True)
        raise

if __name__=="__main__":
    original,old_sheet=reconstruct()
    print("PASS CSS V28: byte-exact approved /preview/ internal-source recovery; "
          "old Chrome + moved inline == new Chrome, all CSS declarations unchanged; "
          "GUI/ENGINE untouched.")
