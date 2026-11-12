"""Approved PNG buttons follow the latest *published* WIRE console without drift.

Checks generated GitHub Pages, not an obsolete copied HTML snapshot. WIRE
source, structure, CSS, JavaScript and knob mask must remain exactly identical
to the canonical source; only the physical MUTE/SOLO/PRE-POST visual renderer differs.
This is reference GUI evidence, never proof of native Windows PCM.
"""
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from playwright.sync_api import sync_playwright
from approved_publication_fixture import published_console

ROOT=Path(__file__).resolve().parents[1]
REFERENCE=ROOT/"site/preview-v2"
OWNER=ROOT/"site/preview-buttons/assets"
SPECIAL=("mixer-control-keys.css","mixer-control-keys.js")
SPRITE="harmonice-thin-flat-v8-57x30.png"


def verify_bundle(dist):
    generated=dist/"preview"
    entry=(dist/"preview/index.html").read_text(encoding="utf-8")
    assert "<iframe" not in entry, "Only one direct public preview GUI is allowed"
    assert (dist/"preview-buttons/index.html").is_file(), "Legacy button URL must redirect"
    original=(REFERENCE/"index.html").read_text(encoding="utf-8")
    html=(generated/"index.html").read_text(encoding="utf-8")
    meta=json.loads((generated/"build.json").read_text(encoding="utf-8"))
    assert meta["kind"]=="APPROVED_SINGLE_GUI"
    assert meta["live_audio"] is False
    assert meta["wire_source_sha256"]==hashlib.sha256(original.encode()).hexdigest()
    assert "url=../preview/" in (dist/"preview-v2/index.html").read_text(encoding="utf-8")
    assert "url=../preview/" in (dist/"preview-buttons/index.html").read_text(encoding="utf-8")
    assert 'http-equiv="refresh"' in (dist/"preview/mixer.html").read_text(encoding="utf-8")

    # The shipped page uses the current authoritative-FX transformation and
    # one compiled CSS bundle. The independent gate reconstructs/compares ALL
    # generated HTML and owner CSS bytes, including approved PNG revisions.
    assert published_console(dist)==generated/"index.html"
    for name in SPECIAL:
        expected=hashlib.sha256((OWNER/name).read_bytes()).hexdigest()[:16]
        assert meta["button_css_js_revisions"][name]==expected,name
        assert (generated/"assets"/name).read_bytes()==(OWNER/name).read_bytes(),name
    canonical={f.name for f in (REFERENCE/"assets").iterdir() if f.is_file()}
    published={f.name for f in (generated/"assets").iterdir() if f.is_file()}
    # The approved app branding now adds exactly three favicon resources to
    # the compiled /preview/ folder. Keep the asset set strict and verify
    # byte identity against this build's generated public root, not unrelated
    # developer-local files or an older locked GUI reference.
    favicons={"favicon.ico","favicon-16x16.png","favicon-32x32.png"}
    expected=canonical|{SPRITE,"arsonmix-approved-runtime.css"}|favicons
    assert published==expected,("asset set drift",published^expected)
    for name in favicons:
        assert (generated/"assets"/name).read_bytes()==(dist/name).read_bytes(),name
    for name in canonical-set(SPECIAL):
        # Compiled CSS is checked byte-for-byte by published_console above.
        # Uncompiled JS/SVG/font/artwork assets must remain exact source bytes.
        if not name.endswith(".css"):
            assert (generated/"assets"/name).read_bytes()==(REFERENCE/"assets"/name).read_bytes(),name

    png=(generated/"assets"/SPRITE).read_bytes()
    assert png[:8]==bytes.fromhex("89504e470d0a1a0a")
    assert struct.unpack(">II",png[16:24])==(228,90)
    assert png[25] in (3,6),"Sprite must preserve transparent alpha (palette tRNS or RGBA)"
    if png[25]==3: assert b"tRNS" in png,"Palette sprite must preserve transparency"
    assert "background-size:400% 300%" in (OWNER/SPECIAL[0]).read_text()
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
            assert row.locator('button[data-hw-kind="cue"]').count()==0
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
            assert [b["action"] for b in off]==["mute","solo","tapMode"],off
            assert [b["kind"] for b in off]==["mute","solo","tap"],off
            assert [b["pos"] for b in off[:2]]==["0% 50%","0% 100%"],off
            assert abs(float(off[2]["pos"].split("%")[0])-33.333333)<.05,off
            assert row.locator('button[data-tap-visual="idle"]').count()==1
            assert page.locator('#clearCue').is_visible() is False
            assert page.locator('#tracks .strip.master .mx-hw-row button').count()==2
            for b in off:
                assert SPRITE in b["bg"],b
                assert b["body"]=="rgba(0, 0, 0, 0)" and b["shadow"]=="none",b
                assert b["transform"]=="none",b
                assert abs(b["h"]-29)<.3,b
                assert abs(b["faceW"]/b["faceH"]-57/30)<.02,b
            for i in range(3): keys.nth(i).click()
            on=states()
            assert [b["state"] for b in on]==["true","true","false"],on
            assert all(abs(float(b["pos"].split("%")[0])-33.333333)<.05 for b in on[:2]),on
            assert on[2]["pos"]=="0% 0%",on
            assert row.locator('button[data-tap-visual="pre"]').count()==1
            for a,b in zip(off,on):
                assert all(abs(a[k]-b[k])<.1 for k in ("x","y","w","h","faceW","faceH")),(a,b)
            keys.nth(0).click()
            assert [b["state"] for b in states()]==["false","true","false"]
            # WIRE widgets remain operable independently of button latches.
            body=page.locator(".console-body")
            if body.evaluate("el=>el.classList.contains('collapsed-send')"):
                page.locator('.section-rail [data-section="send"]').click()
            assert page.locator('#tracks .strip[data-id="5"] .wire-node-out').is_visible()
            assert page.locator('#tracks .strip[data-id="3"] .wire-node-in').is_visible()
            if width==1680:
                # Real reference WIRE graph, no fabricated visual states:
                # zero sends = neutral POST, one POST send = dim-green lit,
                # conflicting send taps = MIX, atomic retarget all = POST.
                zero=page.locator('#tracks .strip[data-id="1"] [data-hw-kind="tap"]')
                zero.click()  # PRE default -> neutral POST default
                assert zero.get_attribute('data-tap-visual')=='idle'
                assert page.evaluate("""() => wireGraphCommit([{
                    type:'CONNECT',sourceId:1,destId:3,mode:'send'
                }])"""),"POST WIRE connect failed"
                page.evaluate("render()")
                active=page.locator('#tracks .strip[data-id="1"] [data-hw-kind="tap"]')
                assert active.get_attribute('data-tap-visual')=='post',"A real POST SEND must illuminate"
                assert page.evaluate("""() => wireGraphCommit([{
                    type:'CONNECT',sourceId:1,destId:4,mode:'split'
                }])"""),"PRE WIRE connect failed"
                page.evaluate("render()")
                mixed=page.locator('#tracks .strip[data-id="1"] [data-hw-kind="tap"]')
                assert mixed.get_attribute('data-tap-visual')=='mixed',"Mixed sends must not lie as POST"
                assert mixed.get_attribute('aria-pressed')=='mixed'
                mixed.click()
                assert page.locator('#tracks .strip[data-id="1"] [data-tap-visual="post"]').count()==1
                assert page.evaluate("""() => wireEdges.filter(e=>
                    e.enabled&&e.sourceId===1&&e.kind==='send').every(e=>e.mode==='send')""")
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
    print("PASS: Harmonice v8 truthful idle PRE/POST PNG, hidden PFL, no MASTER tap, stable MUTE/SOLO and WIRE")

if __name__=="__main__":
    main()
