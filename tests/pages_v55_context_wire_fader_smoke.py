#!/usr/bin/env python3
"""V55 context-aware physical WIRE dot paint; preserve owner-restored V51 fader shadow."""
from pathlib import Path
from playwright.sync_api import sync_playwright
PAGE=Path(__file__).resolve().parents[1]/"site/preview/index.html"
def node(page,ch,direction):
    sel=".wire-node-out" if direction=="out" else ".wire-node-in"
    dot=".wire-pin" if direction=="out" else ".wire-arrow"
    return page.locator(f'#tracks .strip[data-id="{ch}"] {sel}').evaluate("""(el,dot)=>({
        paint:el.dataset.wirePaint,connected:el.getAttribute('data-wire-connected'),
        visible:el.getAttribute('data-wire-visible'),
        color:getComputedStyle(el.querySelector(dot)).backgroundColor})""",dot)
def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for w,h in ((1366,768),(1680,900),(1920,1080)):
            for density in ("comfort","standard","compact"):
                page=browser.new_page(viewport={"width":w,"height":h})
                page.goto(PAGE.as_uri())
                page.locator("#densitySelect").select_option(density)
                for ch in (1,2):
                    for direction in ("in","out"):
                        v=node(page,ch,direction)
                        assert v["paint"]=="idle" and v["visible"] is None,(w,density,ch,direction,v)
                        if direction=="in": assert v["connected"] is None,(w,density,"phantom receiving wire",ch,v)
                        else:
                            assert v["connected"] is None,(w,density,"MAIN must not light custom OUT",ch,v)
                            assert page.locator(f'#tracks .strip[data-id="{ch}"] .wire-node-out').get_attribute("data-wire-main-edge")
                        assert v["color"]=="rgb(75, 83, 91)",(w,density,ch,direction,v)
                cap=page.locator('#tracks .strip[data-id="1"] .fader-travel .cap').first
                shadow=cap.evaluate("(el)=>getComputedStyle(el).boxShadow")
                assert "2px 3px -2px" in shadow and "4px 3px -4px" in shadow,(w,density,"approved V51 small static thumb shadow",shadow)
                assert cap.evaluate("(el)=>getComputedStyle(el).filter")=="none"
                if density=="comfort":
                    if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                        page.locator('.section-rail [data-section="send"]').click()
                    page.locator('#tracks .strip[data-id="5"] .wire-node-out').click()
                    page.locator('#tracks .strip[data-id="3"] .wire-node-in').click()
                    a=node(page,5,"out");b=node(page,3,"in")
                    assert a["connected"]=="true" and a["paint"]=="connected",(w,a)
                    assert b["connected"]=="true" and b["paint"]=="connected",(w,b)
                    assert node(page,1,"out")["connected"] is None and node(page,1,"out")["paint"]=="idle"
                    page.locator('#tracks .strip[data-id="2"] .strip-identity-top').click()
                    hidden=node(page,3,"in")
                    assert hidden["connected"]=="true" and hidden["paint"]=="idle",(w,density,hidden)
                    assert hidden["color"]=="rgb(75, 83, 91)",(w,density,hidden)
                    page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
                    assert node(page,3,"in")["paint"]=="connected"
                    page.locator("#wireNavigatorToggle").click()
                    page.locator("#wireWsUndo").click()
                    assert node(page,5,"out")["connected"] is None and node(page,5,"out")["paint"]=="idle"
                    assert node(page,3,"in")["paint"]=="idle"
                page.close()
        browser.close()
    print("PASS V55: dark idle nodes, accepted WIRE paint and undo; V51 fader shadow rollback intact")
if __name__=="__main__":main()