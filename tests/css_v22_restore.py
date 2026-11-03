"""V22 exact V21 restoration: legacy hardware-key material ownership.

Source-order migration of the V1/V18/V19/V20 button material into the
already adjacent mixer-chrome-material.css owner. The lone removed ::before
rule has an empty body; all declarations are preserved byte-for-byte.
"""
import json,re,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
CHROME=HTML.parent/"assets/mixer-chrome-material.css"
LEDGER=ROOT/"tests/fixtures/css-v22-legacy-key-material-migration.json"

def fingerprint(value):
    h=2166136261
    for c in value:
        h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V22"
    assert data["baseCommit"]=="aba0e2879fbc6cbfe42be42197aa249accb08abf"
    assert data["metrics"]=={
        "movedChars":11389,"importantRelocated":52,
        "removedEmptyRules":1,"removedCssChars":416,
        "removedDeclarations":0,"removedImportant":0,
        "createdStylesheets":0,"changedJsFiles":0
    }
    assert data["sourceInlineSlice"].startswith("/* ARSONMIX GLOBAL HARDWARE KEY V1")
    assert "::before{\n}\n" in data["deadEmptyRule"]
    assert data["deadEmptyRule"].count("!important")==0
    return data

def reconstruct():
    """Recover both exact V21 files, verifying immutable live V22 hashes."""
    data=manifest()
    html=HTML.read_text(encoding="utf-8")
    chrome=CHROME.read_text(encoding="utf-8")
    styles=list(re.finditer(r"<style>(.*?)</style>",html,re.S))
    assert len(styles)==3 and styles[0].group(1)==styles[1].group(1)==""
    inline=styles[2].group(1)
    assert data["newLink"] in html and data["oldLink"] not in html
    for name,text in (("html",html),("chrome",chrome),("inline",inline)):
        assert (len(text),fingerprint(text))==(
            data["current"][name+"Length"],data["current"][name+"FNV"]
        ),(name,"V22 source drift: never silently rebaseline")
    assert inline.count("!important")==data["current"]["inlineImportant"]
    original=data["sourceInlineSlice"]
    empty=data["deadEmptyRule"]
    assert original.count(empty)==1
    moved=original.replace(empty,"",1)
    size=data["m3ChromeTailSize"]
    tail=chrome[-size:]
    assert (len(tail),fingerprint(tail))==(size,data["m3ChromeTailFNV"])
    assert chrome[-size-len(moved):-size]==moved,"V22 material position changed"
    old_chrome=chrome[:-size-len(moved)]+tail
    assert (len(old_chrome),fingerprint(old_chrome))==(
        data["old"]["chromeLength"],data["old"]["chromeFNV"]
    ),"V21 original Chrome owner changed"
    assert inline.count(data["insertBefore"])==1
    old_inline=inline.replace(data["insertBefore"],original+data["insertBefore"],1)
    assert (len(old_inline),fingerprint(old_inline))==(
        data["old"]["inlineLength"],data["old"]["inlineFNV"]
    ),"V21 inline material cannot be reconstructed"
    assert old_inline.count("!important")==data["old"]["inlineImportant"]
    match=styles[2]
    previous=html[:match.start(1)]+old_inline+html[match.end(1):]
    assert previous.count(data["newLink"])==1
    previous=previous.replace(data["newLink"],data["oldLink"],1)
    assert (len(previous),fingerprint(previous))==(
        data["old"]["htmlLength"],data["old"]["htmlFNV"]
    ),"V21 original HTML drift"
    assert "url(" not in moved and "@import" not in moved
    return previous,old_chrome

def mount_original():
    """Mount the actual historical V21 page and Chrome material for Chromium."""
    previous,old_chrome=reconstruct()
    data=manifest()
    css=tempfile.NamedTemporaryFile(
        mode="w",encoding="utf-8",suffix=".css",
        prefix=".v22-legacy-owner-before-",dir=CHROME.parent,delete=False)
    try:
        with css:css.write(old_chrome)
        previous=previous.replace(data["oldLink"],
            "assets/"+Path(css.name).name+"?v=exact-v21-original",1)
        page=tempfile.NamedTemporaryFile(
            mode="w",encoding="utf-8",suffix=".html",
            prefix=".v22-before-material-",dir=HTML.parent,delete=False)
        with page:page.write(previous)
        return Path(page.name),[Path(css.name),Path(page.name)]
    except Exception:
        Path(css.name).unlink(missing_ok=True)
        raise
