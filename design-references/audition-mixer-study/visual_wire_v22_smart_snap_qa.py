#!/usr/bin/env python3
"""V22 browser acceptance: SVG rail icons and no-sticky magnetic cable drag."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html")

def check(ok,why):
    if not ok: raise AssertionError(why)

def center(el):
    r=el.bounding_box()
    check(r is not None,"Channel connector is not visible")
    return r["x"]+r["width"]*.5,r["y"]+r["height"]*.5

def drag_begin(page,source):
    x,y=center(source)
    page.mouse.move(x,y);page.mouse.down()
    return x,y

def move(page,x,y):
    page.mouse.move(x,y,steps=12)

def confirm_free_tip(page,x,y,context):
    vp=page.locator("#viewport").bounding_box()
    check(vp is not None,"Missing reference viewport")
    path=page.locator("#wireLayer .wire-drag-line").last
    d=path.get_attribute("d")
    import re
    match=re.search(r",\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*$",d or "")
    check(match is not None,f"{context}: cannot parse cable Bezier endpoint: {d}")
    px,py=map(float,match.groups())
    check(abs(px-(x-vp["x"]))<2 and abs(py-(y-vp["y"]))<2,
          f"{context}: free cable tip hard-snapped away from mouse")

def confirm_docked_tip(page,socket,context):
    x,y=center(socket)
    vp=page.locator("#viewport").bounding_box()
    check(vp is not None,"Missing reference viewport")
    d=page.locator("#wireLayer .wire-drag-line").last.get_attribute("d")
    import re
    m=re.search(r",\s*(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\s*$",d or "")
    check(m is not None,f"{context}: no docked wire Bezier endpoint")
    px,py=map(float,m.groups())
    check(abs(px-(x-vp["x"]))<2 and abs(py-(y-vp["y"]))<2,
          f"{context}: lit socket ring but cable endpoint is not physically docked")

def main():
 with sync_playwright() as pw:
    browser=pw.chromium.launch(headless=True)
    for width,height in ((1366,768),(1680,900),(1920,1080)):
        page=browser.new_page(viewport={"width":width,"height":height})
        errors=[]
        page.on("pageerror",lambda error:errors.append(str(error)))
        page.goto(PAGE.as_uri())
        body=page.locator(".console-body")
        # These are actual user-approved image silhouettes, not CSS text FX/W.
        for klass in ("route","fx","send"):
            icon=page.locator(f'.section-rail button.rail-cell.{klass} svg.rail-svg-icon')
            check(icon.count()==1 and icon.is_visible(),f"{width}: missing {klass} vector icon")
        check(page.locator('.section-rail .fx-icon path').count()==1,
              f"{width}: FX image-derived glyph absent")
        check(page.locator('.section-rail .wire-icon path[fill-rule="evenodd"]').count()==1,
              f"{width}: dual-plug loop icon absent")
        page.locator('.section-rail button[data-section="send"]').click()
        src=page.locator('#tracks .strip[data-id="6"] .wire-node-out')
        dst=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
        x0,y0=drag_begin(page,src)
        x1,y1=center(dst)
        # 20px near socket: ring lights AND cable tip docks as a provisional snap.
        move(page,x1+18,y1-8)
        check(page.locator('#tracks .strip[data-id="3"] .wire-node-in.is-snap-target').count()==1,
              f"{width}: close-in socket has no candidate guide")
        confirm_docked_tip(page,dst,f"{width} near socket")
        check(dst.get_attribute("aria-pressed")=="false",f"{width}: hover prematurely committed connection")
        # Continue past socket and out of its short magnetic radius.
        move(page,x1+38,y1)
        confirm_free_tip(page,x1+38,y1,f"{width} beyond socket")
        check(page.locator('#tracks .strip[data-id="3"] .wire-node-in.is-snap-target').count()==0,
              f"{width}: sticky highlighted socket after moving away")
        page.mouse.up()
        check(dst.get_attribute("aria-pressed")=="false",
              f"{width}: drop 38px from socket illegally committed connection")
        # Intentional 20px release commits precisely on mouse-up.
        drag_begin(page,src)
        move(page,x1+18,y1-8)
        check(dst.get_attribute("aria-pressed")=="false",
              f"{width}: pre-release already audible")
        page.mouse.up()
        check(dst.get_attribute("aria-pressed")=="true",f"{width}: intended snap release failed")
        # Replug from IN: passing over a valid target then moving on must NOT
        # reroute there. Release away intentionally unplugs the original link.
        x3,y3=center(dst)
        dst4=page.locator('#tracks .strip[data-id="4"] .wire-node-in')
        x4,y4=center(dst4)
        page.mouse.move(x3,y3);page.mouse.down()
        move(page,x4,y4)
        check(dst.get_attribute("aria-pressed")=="true" and dst4.get_attribute("aria-pressed")=="false",
              f"{width}: replug candidate modified graph before release")
        check(page.locator('#wireLayer .wire-floating-plug').count()==1,
              f"{width}: replug cable lacks independent free plug")
        confirm_docked_tip(page,dst4,f"{width} replug candidate")
        move(page,x4+40,y4)
        confirm_free_tip(page,x4+40,y4,f"{width} crossing another socket")
        page.mouse.up()
        check(dst4.get_attribute("aria-pressed")=="false" and dst.get_attribute("aria-pressed")=="false",
              f"{width}: pass-through socket incorrectly retained route")
        check(not errors,f"{width}: JS errors {errors}")
        page.screenshot(path=str(ROOT/f"preview-v22-smart-snap-{width}.png"))
        page.close()
    browser.close()
    print("PASS V23 SVG and smart snap: illuminated socket docks endpoint, free after exit, graph commits only on release; 1366/1680/1920")

if __name__=="__main__":main()
