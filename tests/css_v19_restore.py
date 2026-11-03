"""V19 exact V18 source restoration for adjacent Chrome and toolbar CSS owners.

The late V7/V8 Chrome sequence is appended to the immediately preceding Chrome
stylesheet. The first toolbar style is appended to preceding section controls.
Six shadowed important properties and ten empty legacy rules are deleted.
"""
import json,re,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
SECTIONS=HTML.parent/"assets/mixer-section-controls.css"
CHROME=HTML.parent/"assets/mixer-chrome-material.css"
LEDGER=ROOT/"tests/fixtures/css-v19-operator-owner-migration.json"

def fingerprint(value):
    h=2166136261
    for c in value:h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V19" and data["baseCommit"].startswith("4ed608890c")
    assert len(data["sourceFirst"])==1017 and len(data["movedChromeSuffix"])==9830
    assert len(data["removed"])==14 and data["removedImportant"]==6
    assert data["removedEmptyRules"]==10
    return data

def reconstruct():
    data=manifest()
    # V20 only prunes shadowed declarations: restore the exact approved V19
    # HTML before enforcing this immutable V19 owner-migration ledger.
    from css_v20_restore import reconstruct as restore_v19_source
    current=restore_v19_source()
    sections=SECTIONS.read_text(encoding="utf-8")
    # Chrome gained the V21 chassis suffix; compare only the exact V19
    # Chrome owner against its original immutable V19 source ledger.
    from css_v21_restore import reconstruct as restore_pre_v21_chrome
    _,chrome=restore_pre_v21_chrome()
    for name,source in (("html",current),("sections",sections),("chrome",chrome)):
        assert (len(source),fingerprint(source))==(data["newLengths"][name],data["newFNV"][name]),(name,"unapproved V19 live owner drift")
    assert sections.endswith(data["sourceFirst"])
    assert chrome.endswith(data["movedChromeSuffix"])
    old_sections=sections[:-len(data["sourceFirst"])]
    old_chrome=chrome[:-len(data["movedChromeSuffix"])]
    for name,source in (("sections",old_sections),("chrome",old_chrome)):
        assert (len(source),fingerprint(source))==(data["oldLengths"][name],data["oldFNV"][name]),name
    styles=list(re.finditer(r"<style>(.*?)</style>",current,re.S))
    assert len(styles)==3 and styles[0].group(1)=="" and styles[1].group(1)==""
    late=styles[2].group(1)
    assert (len(late),fingerprint(late))==(data["newLengths"]["late"],data["newFNV"]["late"])
    original_remaining=late
    for edit in sorted(data["removed"],key=lambda e:e["offset"]):
        at=edit["offset"]
        assert 0<=at<=len(original_remaining)
        original_remaining=original_remaining[:at]+edit["text"]+original_remaining[at:]
    original_late=data["movedChromeSuffix"]+original_remaining
    assert (len(original_late),fingerprint(original_late))==(data["oldLengths"]["late"],data["oldFNV"]["late"])
    prior=current
    for index,part in ((2,original_late),(0,data["sourceFirst"])):
        m=styles[index]
        prior=prior[:m.start(1)]+part+prior[m.end(1):]
    for old_link,new_link in zip(data["oldLinks"],data["newLinks"]):
        assert prior.count(new_link)==1 and old_link not in prior
        prior=prior.replace(new_link,old_link,1)
    assert (len(prior),fingerprint(prior))==(data["oldLengths"]["html"],data["oldFNV"]["html"])
    return prior,old_sections,old_chrome

def mount_owners(html,sections=None,chrome=None,include_sections=True,include_chrome=True):
    """Mount old V18 owners without pretending migrated declarations were deleted."""
    data=manifest()
    if sections is None or chrome is None:
        _,sections,chrome=reconstruct()
    temporary=[]
    try:
        pairs=((SECTIONS,sections,data["oldLinks"][0],include_sections),
               (CHROME,chrome,data["oldLinks"][1],include_chrome))
        for path,source,link,enabled in pairs:
            if not enabled:continue
            assert html.count(link)==1,(path.name,"original version link missing")
            f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
                prefix=".v19-original-owner-",dir=path.parent,delete=False)
            with f:f.write(source)
            temporary.append(Path(f.name))
            html=html.replace(link,"assets/"+Path(f.name).name+"?v=v19-original",1)
        return html,temporary
    except Exception:
        for path in temporary:path.unlink(missing_ok=True)
        raise

def mount_original():
    old_html,sections,chrome=reconstruct()
    html,files=mount_owners(old_html,sections,chrome)
    try:
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
            prefix=".v19-original-console-",dir=HTML.parent,delete=False)
        with f:f.write(html)
        files.append(Path(f.name))
        return Path(f.name),files
    except Exception:
        for path in files:path.unlink(missing_ok=True)
        raise
