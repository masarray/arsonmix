#!/usr/bin/env python3
"""V31 gravity WIRE acceptance: nodes above suspended cables, real pointer routing intact."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).with_name("index.html")
def yes(condition,message):
    if not condition: raise AssertionError(message)

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for w,h in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":w,"height":h})
            errors=[]
            page.on("pageerror", lambda e:errors.append(str(e)))
            page.goto(PAGE.as_uri())
            rail=page.locator('.section-rail [data-section="send"]')
            rail.click()
            def ch(id):
                return page.locator(f'#tracks .strip[data-id="{id}"]')
            def select(id):
                ch(id).locator('.strip-identity-top').click()
            def connect(src,dst):
                select(src)
                ch(dst).locator('.wire-node-in').click()
                yes(ch(dst).locator('.wire-node-in').get_attribute('aria-pressed')=='true',
                    f"{w}: CH{src}->{dst} failed")
            # Deliberately use actual user controls for both directions.
            connect(1,3)
            ch(1).locator('.wire-node-out').wait_for(state="visible")
            panel=ch(1).locator('.wire-section').bounding_box()
            tray=ch(1).locator('.wire-tray').bounding_box()
            deck=ch(1).locator('.wire-deck').bounding_box()
            out=ch(1).locator('.wire-node-out').bounding_box()
            yes(tray["y"]<deck["y"] and out["y"]<deck["y"],
                f"{w}: OUT/IN must be above the hanging cable stage")
            yes(abs((deck["y"]+deck["height"])-(panel["y"]+panel["height"]))<=2,
                f"{w}: bottom cable stage must end at WIRE compartment, not invade PAN")
            def cable(src,dst):
                return page.locator(f'#wireLayer path.wire-outgoing-line[data-from="{src}"][data-to="{dst}"]')
            path=cable(1,3)
            yes(path.count()==1,f"{w}: missing CH01->03 cable")
            def shape(p):
                return p.evaluate("""p=>{const d=p.getAttribute('d');
                    const parts=d.match(/-?\\d+(?:\\.\\d+)?/g).map(Number);
                    const middle=p.getPointAtLength(p.getTotalLength()/2);
                    const box=p.getBBox();
                    return {parts,midY:middle.y,maxY:box.y+box.height,d};}""")
            a=shape(path)
            yes(a["parts"][3]>a["parts"][1] and a["parts"][5]>a["parts"][7],
                f"{w}: control handles must point DOWN from OUT and IN")
            yes(a["midY"]>max(a["parts"][1],a["parts"][7])+10,
                f"{w}: cable does not sag under gravity: {a['d']}")
            yes(a["maxY"]<=panel["y"]-page.locator('#viewport').bounding_box()["y"]+panel["height"]+2,
                f"{w}: hanging wire overflows into PAN")
            # Right-to-left needs the exact same gravity and stable socket positions.
            connect(5,2)
            right=shape(cable(5,2))
            yes(right["midY"]>max(right["parts"][1],right["parts"][7])+10,
                f"{w}: reverse wire must also droop below its endpoints")
            page.locator('#wireView').select_option('all')
            yes(page.locator('#wireLayer path.wire-context-line').count()>=1,
                f"{w}: All mode has lost unrelated cable context")
            rail.click()
            yes(page.locator('#wireLayer path').count()==0,
                f"{w}: folding WIRE leaks hanging cables")
            yes(not ch(1).locator('.wire-tray').is_visible(),
                f"{w}: folding WIRE exposes patch hardware")
            yes(not errors,f"{w}: page script errors: {errors}")
            page.close()
        browser.close()
    print("PASS V31 real Chromium top nodes + gravity sag across 1366/1680/1920")

if __name__=="__main__": main()
