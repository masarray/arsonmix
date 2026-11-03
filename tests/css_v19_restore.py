"""V19 exact-byte source restoration for operator key final material and toolbar owners.

Ownership migration is source-order neutral:
 * first inline toolbar recipe appended to immediately preceding section-controls owner;
 * final V30 hardware material prepended to immediately following control-keys owner;
 * six exact property-level shadowed declarations and ten empty rules removed.
"""
import json,re,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
SECTIONS=HTML.parent/"assets/mixer-section-controls.css"
KEYS=HTML.parent/"assets/mixer-control-keys.css"
LEDGER=ROOT/"tests/fixtures/css-v19-operator-owner-migration.json"

def fingerprint(value):
    h=2166136261
    for c in value:h=((h^ord(c))*16777619)&0xffffffff
    return f"{h:08x}"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V19" and data["baseCommit"].startswith("4ed608890c")
    assert len(data["sourceFirst"])==1017 and len(data["movedKeyPrefix"])==8746
    assert len(data["removed"])==14 and data["removedImportant"]==6
    assert data["removedEmptyRules"]==10
    return data

def reconstruct():
    data=manifest()
    current=HTML.read_text(encoding="utf-8")
    sections=SECTIONS.read_text(encoding="utf-8")
    keys=KEYS.read_text(encoding="utf-8")
    for name,source in (("html",current),("sections",sections),("keys",keys)):
        assert (len(source),fingerprint(source))==(data["newLengths"][name],data["newFNV"][name]),(name,"V19 live owner drift")
    assert sections.endswith(data["sourceFirst"]),"V19 toolbar tail missing"
    assert keys.startswith(data["movedKeyPrefix"]),"V19 final key material prefix missing"
    original_sections=sections[:-len(data["sourceFirst"])]
    original_keys=keys[len(data["movedKeyPrefix"]):]
    for name,source in (("sections",original_sections),("keys",original_keys)):
        assert (len(source),fingerprint(source))==(data["oldLengths"][name],data["oldFNV"][name]),name
    styles=list(re.finditer(r"<style>(.*?)</style>",current,re.S))
    assert len(styles)==3 and styles[0].group(1)=="" and styles[1].group(1)==""
    late=styles[2].group(1)
    assert (len(late),fingerprint(late))==(data["newLengths"]["late"],data["newFNV"]["late"])
    original_prefix=late
    for entry in sorted(data["removed"],key=lambda e:e["offset"]):
        at=entry["offset"]
        assert 0<=at<=len(original_prefix)
        original_prefix=original_prefix[:at]+entry["text"]+original_prefix[at:]
    original_late=original_prefix+data["movedKeyPrefix"]
    assert (len(original_late),fingerprint(original_late))==(data["oldLengths"]["late"],data["oldFNV"]["late"])
    prior=current
    for index,part in ((2,original_late),(0,data["sourceFirst"])):
        m=styles[index]
        prior=prior[:m.start(1)]+part+prior[m.end(1):]
    for old_link,new_link in zip(data["oldLinks"],data["newLinks"]):
        assert prior.count(new_link)==1 and old_link not in prior
        prior=prior.replace(new_link,old_link,1)
    assert (len(prior),fingerprint(prior))==(data["oldLengths"]["html"],data["oldFNV"]["html"])
    return prior,original_sections,original_keys

def mount_owners(html,sections=None,keys=None,include_sections=True):
    """Mount original owner CSS alongside live CSS for fair legacy visual tests."""
    data=manifest()
    if sections is None or keys is None:
        _,sections,keys=reconstruct()
    temporary=[]
    try:
        candidates=((SECTIONS,sections,data["oldLinks"][0]),
                    (KEYS,keys,data["oldLinks"][1]))
        for path,source,link in (candidates if include_sections else candidates[1:]):
            assert html.count(link)==1,(path.name,"expected exact original link")
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
    source,sections,keys=reconstruct()
    html,files=mount_owners(source,sections,keys)
    try:
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
            prefix=".v19-original-console-",dir=HTML.parent,delete=False)
        with f:f.write(html)
        files.append(Path(f.name))
        return Path(f.name),files
    except Exception:
        for path in files:path.unlink(missing_ok=True)
        raise
