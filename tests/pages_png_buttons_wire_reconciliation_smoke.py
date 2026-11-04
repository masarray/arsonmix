"""Approved PNG buttons follow the latest *published* WIRE console without drift.

Checks generated GitHub Pages, not an obsolete copied HTML snapshot. WIRE
source, structure, CSS, JavaScript and knob mask must remain exactly identical
to the canonical source; only the physical MUTE/SOLO/PFL visual renderer differs.
This is reference GUI evidence, never proof of native Windows PCM.
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
REFERENCE=ROOT/"site/preview-v2"
OWNER=ROOT/"site/preview-buttons/assets"
SPECIAL=("mixer-control-keys.css","mixer-control-keys.js")
SPRITE="mute-solo-pfl-56x37.png"


def verify_bundle(dist):
    generated=dist/"preview"
    entry=(generated/"index.html").read_text(encoding="utf-8")
    assert 'id="console"' not in entry, "Duplicate preview iframe is forbidden"
    assert 'class="console-body' in entry or 'console-body' in entry, "Approved console missing"
    for legacy in ("preview-v2","preview-buttons","preview-v33","preview-v34"):
        redirect=dist/legacy/"index.html"
        assert redirect.is_file() and 'url=../preview/' in redirect.read_text(encoding="utf-8")
        assert list((dist/legacy).iterdir())==[redirect], "Alternative GUI still published"
    original=(REFERENCE/"index.html").read_text(encoding="utf-8")
    html=(generated/"index.html").read_text(encoding="utf-8")
    meta=json.loads((generated/"build.json").read_text(encoding="utf-8"))
    assert meta["kind"]=="APPROVED_SINGLE_GUI" and meta["canonical_url"]=="/preview/"
    assert meta["live_audio"] is False
    assert meta["wire_source_sha256"]==hashlib.sha256(original.encode()).hexdigest()
    assert 'http-equiv="refresh"' in (dist/"preview/mixer.html").read_text(encoding="utf-8"),"Deprecated mixer alias must not render second GUI"

    # Strip only the two deliberate fingerprinted CSS/JS URLs. Everything
    # else, including the V46+ WIRE model and channel events, must be 1:1.
    stripped=html
    for name in SPECIAL:
        path="assets/"+name
        original_url=re.search(re.escape(path)+r'(?:\?[^"\s>]+)?',original)
        current_url=re.search(re.escape(path)+r'\?rev=([0-9a-f]{16})',stripped)
        assert original_url and current_url,(name,"missing cache-safe URL")
        expected=hashlib.sha256((OWNER/name).read_bytes()).hexdigest()[:16]
        assert current_url.group(1)==expected,(name,"wrong asset revision")
        stripped=stripped[:current_url.start()]+original_url.group(0)+stripped[current_url.end():]
        assert (generated/"assets"/name).read_bytes()==(OWNER/name).read_bytes(),name
    assert stripped==original,"User-approved button variant modified the real WIRE mixer"

    # Removing old snapshots makes every extra asset a current-source
    # dependency, never a cloned V45 file frozen across future WIRE updates.
    canonical={f.name for f in (REFERENCE/"assets").iterdir() if f.is_file()}
    published={f.name for f in (generated/"assets").iterdir() if f.is_file()}
    assert published==canonical|{SPRITE},("asset set drift",published^canonical)
    for name in canonical-set(SPECIAL):
        assert (generated/"assets"/name).read_bytes()==(REFERENCE/"assets"/name).read_bytes(),name

    png=(generated/"assets"/SPRITE).read_bytes()
    assert png==(ROOT/"site/preview-v34/assets"/SPRITE).read_bytes()
    assert png[:8]==bytes.fromhex("89504e470d0a1a0a")
    assert struct.unpack(">II",png[16:24])==(112,111)
    assert png[25]==6,"Must keep RGBA transparency"
    assert "background-size:200% 300%" in (OWNER/SPECIAL[0]).read_text()
    assert "aria-pressed=\"true\"" in (OWNER/SPECIAL[0]).read_text()
    return generated


def verify_browser(dist):
    html=dist/"preview/index.html"
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        # Public /preview/ is the user-facing route, not a separate PNG demo.
        entry=browser.new_page(viewport={"width":1680,"height":900})
        entry.goto((dist/"preview/index.html").as_uri())
        entry.locator('#tracks .strip[data-id="1"] .mx-hw-row').wait_for(timeout=15000)
        key=entry.locator('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]')
        assert SPRITE in key.evaluate("el=>getComputedStyle(el,'::before').backgroundImage"), "Public /preview/ did not load approved PNG"
        assert entry.locator("#wireLayer").count()==1, "Public /preview/ lost canonical WIRE"
        state=key.get_attribute("aria-pressed")
        key.click()
        assert key.get_attribute("aria-pressed")!=state, "Public /preview/ MUTE latch failed"
        entry.close()
        for width in (1366,1680,1920):
            page=browser.new_page(viewport={"width":width,"height":900})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.goto(html.as_uri())
            page.wait_for_selector('#tracks .strip[data-id="1"] .mx-hw-row')
            row=page.locator('#tracks .strip[data-id="1"] .mx-hw-row')
            keys=row.locator('button.mx-hw-key')
            assert keys.count()==3
            assert page.locator("#wireLayer").count()==1
            assert page.locator('#tracks .strip[data-id="3"] .wire-node-in').count()==1
            assert page.locator('#tracks .strip[data-id="5"] .wire-node-out').count()==1
            assert page.locator(".mx-hw-face,.mx-mute-face").count()==0

            def states():
                return keys.evaluate_all("""nodes=>nodes.map(b=>{
                    const r=b.getBoundingClientRect(),s=getComputedStyle(b),
                          face=getComputedStyle(b,'::before');
                    return {action:b.dataset.action,kind:b.dataset.hwKind,
                      state:b.getAttribute('aria-pressed'),
                      x:r.x,y:r.y,w:r.width,h:r.height,
                      faceW:parseFloat(face.width),faceH:parseFloat(face.height),
                      bg:face.backgroundImage,pos:face.backgroundPosition,
                      body:s.backgroundColor,shadow:s.boxShadow,transform:s.transform,
                      faceTransform:face.transform};
                })""")

            off=states()
            assert [b["action"] for b in off]==["mute","solo","cue"],off
            assert [b["kind"] for b in off]==["mute","solo","cue"],off
            assert [b["pos"] for b in off]==["0% 0%","0% 50%","0% 100%"],off
            for b in off:
                assert SPRITE in b["bg"],b
                assert b["body"]=="rgba(0, 0, 0, 0)" and b["shadow"]=="none",b
                assert b["transform"]=="none",b
                assert abs(b["h"]-29)<.3,b
                assert abs(b["faceW"]/b["faceH"]-56/37)<.02,b
            for i in range(3): keys.nth(i).click()
            on=states()
            assert [b["state"] for b in on]==["true"]*3,on
            assert [b["pos"] for b in on]==["100% 0%","100% 50%","100% 100%"],on
            for a,b in zip(off,on):
                assert all(abs(a[k]-b[k])<.1 for k in ("x","y","w","h","faceW","faceH")),(a,b)
            keys.nth(0).click()
            assert [b["state"] for b in states()]==["false","true","true"]
            # WIRE widgets remain operable independently of button latches.
            body=page.locator(".console-body")
            if body.evaluate("el=>el.classList.contains('collapsed-send')"):
                page.locator('.section-rail [data-section="send"]').click()
            assert page.locator('#tracks .strip[data-id="5"] .wire-node-out').is_visible()
            assert page.locator('#tracks .strip[data-id="3"] .wire-node-in').is_visible()
            if width==1680:
                page.screenshot(path="png-buttons-reconciled-wire-1680.png")
            assert not errors,(width,errors)
            page.close()
        browser.close()


def main():
    with tempfile.TemporaryDirectory(prefix="arsonmix-png-wire-") as tmp:
        dist=Path(tmp)/"site"
        subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
            "--out",str(dist),"--sha","png-wire-test","--branch","pr-qa"],
            cwd=ROOT,check=True,capture_output=True,text=True)
        verify_bundle(dist)
        verify_browser(dist)
    print("PASS: approved PNG buttons track latest canonical WIRE, asset byte parity, 3 viewports, ON/OFF stable")

if __name__=="__main__":
    main()
