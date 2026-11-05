"""V20 reversible legacy CSS shadow-pruning ledger.

Reconstructs the exact V19 HTML/CSS from V20 by restoring 35 original source
slices at their original Unicode code-point offsets. No test-only CSS replicas.
"""
import json,re,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
LEDGER=ROOT/"tests/fixtures/css-v20-legacy-shadow-pruning.json"

def fingerprint(text):
    h=2166136261
    for c in text:h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V20"
    assert data["baseCommit"]=="80a155c3da4a436c59cb7819892b5843358f3f73"
    assert data["sourceBlock"]==2 and len(data["edits"])==35
    assert data["metrics"]=={
        "removedChars":3861,"shadowedDeclarations":53,
        "removedImportant":51,"removedWholeRules":7,"removedRuleParts":28}
    assert len(data["evidence"])==53
    return data

def reconstruct():
    data=manifest()
    # Reverse only the explicitly recorded M3 operator delta before checking
    # the immutable V20 CSS-only fingerprint. Reapply it to baseline HTML.
    from gui_m3_compat import transform
    from css_v21_restore import reconstruct as restore_pre_v21
    source,_=restore_pre_v21()
    current=transform(source,reverse=True,skip={"chrome version"})
    v19_chrome="assets/mixer-chrome-material.css?v=css-chrome-owner-v19"
    assert current.count(v19_chrome)==1
    assert (len(current),fingerprint(current))==(
        data["result"]["htmlLength"],data["result"]["htmlFNV"]
    ),"V20 live HTML changed outside approved source ledger"
    styles=list(re.finditer(r"<style>(.*?)</style>",current,re.S))
    assert len(styles)==3 and styles[0].group(1)==styles[1].group(1)==""
    style=styles[2].group(1)
    assert (len(style),fingerprint(style))==(
        data["result"]["cssLength"],data["result"]["cssFNV"]
    ),"V20 live inline CSS drift"
    assert style.count("!important")==data["result"]["cssImportant"]
    original=style
    for entry in sorted(data["edits"],key=lambda e:e["offset"]):
        at=entry["offset"]
        assert 0<=at<=len(original),("invalid V20 edit offset",at)
        original=original[:at]+entry["text"]+original[at:]
    assert (len(original),fingerprint(original))==(
        data["original"]["cssLength"],data["original"]["cssFNV"]
    ),"V19 original inline CSS must reconstruct byte-exactly"
    assert original.count("!important")==data["original"]["cssImportant"]
    assert len(original)-len(style)==data["metrics"]["removedChars"]
    for witness in data["evidence"]:
        assert witness["winnerRule"]>witness["losingRule"]
        assert witness["winnerRaw"] in style,(witness["property"],"later CSS winner removed")
        assert witness["losingRaw"] in original,(witness["property"],"original CSS removed incorrectly")
    match=styles[2]
    previous=current[:match.start(1)]+original+current[match.end(1):]
    assert (len(previous),fingerprint(previous))==(
        data["original"]["htmlLength"],data["original"]["htmlFNV"]
    ),"V19 approved HTML fingerprint cannot be re-baselined"
    restored=transform(previous,skip={"chrome version"})
    assert restored.count(v19_chrome)==1
    return restored

def mount_original():
    html=reconstruct()
    from css_v21_restore import mount_owner as mount_pre_v21_chrome
    html,old_owner_files=mount_pre_v21_chrome(html)
    f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
        prefix=".v20-before-shadow-cleanup-",dir=HTML.parent,delete=False)
    try:
        with f:f.write(html)
        return Path(f.name),old_owner_files+[Path(f.name)]
    except Exception:
        Path(f.name).unlink(missing_ok=True)
        for p in old_owner_files:p.unlink(missing_ok=True)
        raise
