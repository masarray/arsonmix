"""Reversible CSS owner V15 source ledger. Strict original byte-position restoration."""
import json,re,tempfile
from pathlib import Path
from css_v16_restore import reconstruct,manifest as manifest_v16
ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview/index.html"
ASSETS=HTML.parent/"assets"
LEDGER=ROOT/"tests/fixtures/css-v15-cross-owner-removals.json"

def manifest():
    data=json.loads(LEDGER.read_text(encoding="utf-8"))
    assert data["milestone"]=="V15" and len(data["owners"])==6
    return data

def restore_previous_owners():
    data=manifest()
    previous={}
    count=rules=saved=imp=0
    for name,entry in data["owners"].items():
        assert "/" not in name and name.endswith(".css")
        if name=="mixer-chrome-material.css":
            from css_v18_restore import reconstruct as v18_previous
            current=v18_previous()[1]
        elif name=="mixer-section-controls.css":
            from css_v19_restore import reconstruct as pre_v19
            current=pre_v19()[1]
        else:
            current=(ASSETS/name).read_text(encoding="utf-8")
        assert len(current)==entry["currentSize"],name
        old=current
        for edit in sorted(entry["insertions"],key=lambda e:e["offset"]):
            at=edit["offset"]
            assert 0<=at<=len(old)
            old=old[:at]+edit["text"]+old[at:]
        assert len(old)==entry["previousSize"],name
        assert old.count("!important")==entry["previousImportant"],name
        assert current.count("!important")==entry["currentImportant"],name
        count+=entry["removedDeclarations"]
        rules+=entry["removedWholeRules"]
        saved+=len(old)-len(current)
        imp+=old.count("!important")-current.count("!important")
        previous[name]=old
    assert (count,rules,saved,imp)==(95,15,2552,37)
    assert saved==data["totals"]["savedChars"]
    assert imp==data["totals"]["importantOld"]-data["totals"]["importantNew"]
    return previous

def before_v15_html():
    html,_=reconstruct()
    for name,old_version in manifest()["originalLinkVersions"].items():
        key=f'assets/{name}?v=css-owner-v15'
        assert html.count(key)==1,name
        html=html.replace(key,f'assets/{name}?v={old_version}',1)
    return html

def original_assets_as_baseline(html,owners=None):
    owners=restore_previous_owners() if owners is None else owners
    temp=[]
    try:
        from css_v17_restore import mount_owner
        html,old_plugin=mount_owner(html)
        temp.extend(old_plugin)
        if manifest_v16()["sourceWireUrl"] in html:
            previous_wire=reconstruct()[1]
            f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
                prefix=".v15-original-wire-",dir=ASSETS,delete=False)
            with f:f.write(previous_wire)
            temp.append(Path(f.name))
            html=html.replace(manifest_v16()["sourceWireUrl"],
                              "assets/"+Path(f.name).name+"?v=v47",1)
        for name,css in owners.items():
            key=f'assets/{name}?v='
            assert html.count(key)==1,name
            f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".css",
                prefix=".v15-old-owner-",dir=ASSETS,delete=False)
            from gui_m3_compat import suffix
            with f:f.write(css+(suffix("chrome")[1] if name=="mixer-chrome-material.css" else ""))
            temp.append(Path(f.name))
            html=html.replace(key,f'assets/{Path(f.name).name}?v=',1)
        f=tempfile.NamedTemporaryFile(mode="w",encoding="utf-8",suffix=".html",
            prefix=".v15-old-console-",dir=HTML.parent,delete=False)
        with f:f.write(html)
        temp.append(Path(f.name))
        return Path(f.name),temp
    except Exception:
        for f in temp:f.unlink(missing_ok=True)
        raise

def restore_as_inline(html,owners):
    for name,css in owners.items():
        pattern=r'<link rel="stylesheet" href="assets/'+re.escape(name)+r'\?v=[^"]+">'
        assert len(re.findall(pattern,html))==1,name
        assert "url(" not in css,(name,"relative CSS URL cannot be inlined")
        html=re.sub(pattern,lambda _: "<style>"+css+"</style>",html,count=1)
    return html
