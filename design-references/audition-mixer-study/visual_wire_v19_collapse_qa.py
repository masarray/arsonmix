#!/usr/bin/env python3
"""V19 collapsed rack indicators and restrained 28px DAW section rail."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html")

def ensure(value,message):
    if not value: raise AssertionError(message)

def center(locator):
    box=locator.bounding_box()
    ensure(box is not None,"Missing channel node")
    return box["x"]+box["width"]/2,box["y"]+box["height"]/2

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda err:errors.append(str(err)))
            page.goto(PAGE.as_uri())
            body=page.locator('.console-body')
            ensure("collapsed-send" in body.get_attribute("class"),f"{width}: WIRE not collapsed by default")
            rail=page.locator('.section-rail')
            sections=rail.locator('button.rail-cell[data-section]')
            ensure(sections.count()==3,f"{width}: I/O/FX/W sections changed")
            for klass in ("route","fx","send"):
                btn=rail.locator(f'button.rail-cell.{klass}')
                icon=btn.locator('svg.rail-svg-icon')
                ensure(icon.count()==1 and icon.is_visible(),
                       f"{width}: reference vector {klass} icon not visible")
                size=icon.bounding_box()
                ensure(size is not None and size["width"]>=18 and size["height"]>=18,
                       f"{width}: SVG {klass} icon too small")
            r=rail.bounding_box()
            ensure(r is not None and r["width"]<=34,f"{width}: left rail became too wide")
            wire=rail.locator('button[data-section="send"]')
            chevron=wire.evaluate("(el)=>getComputedStyle(el,'::after').content")
            ensure(chevron in ('""',"''","none"),f"{width}: text OPEN/PATCH still occupies rail")
            fold1=page.locator('#tracks .strip[data-id="1"] .wire-fold-summary')
            fold3=page.locator('#tracks .strip[data-id="3"] .wire-fold-summary')
            ensure(fold1.is_visible() and fold3.is_visible(),f"{width}: folded state blank")
            ensure(fold1.locator('svg.fold-link-icon.has-out').count()==1,
                   f"{width}: existing legacy send has no graphical link indicator")
            ensure(fold3.locator('svg.fold-link-icon').count()==0,
                   f"{width}: unlinked channel should have no visible patch symbol")
            ensure(fold1.inner_text().strip()=="" and fold3.inner_text().strip()=="",
                   f"{width}: numeric send badges or captions clutter the collapsed bar")
            for id in (1,3):
                section=page.locator(f'#tracks .strip[data-id="{id}"] .wire-section')
                ensure(not section.locator('.wire-tray').is_visible() and
                       not section.locator('.wire-deck').is_visible(),
                       f"{width}: live sockets and deck leaked outside collapsed WIRE compartment")
            ensure(fold1.locator('svg.fold-link-icon').bounding_box()["height"]<=12,
                   f"{width}: status graphic is oversized")
            foldHeight=fold3.bounding_box()["height"]
            ensure(18<=foldHeight<=23,f"{width}: folded summary changes row height")
            sectionHeight=page.locator('#tracks .strip[data-id="3"] .send-section').bounding_box()["height"]
            ensure(abs(sectionHeight-23)<=1,f"{width}: collapsed WIRE no longer 23px")

            # Entire compact indicator is the disclosure target, not a dead zone.
            fold3.click()
            ensure("collapsed-send" not in body.get_attribute("class"),
                   f"{width}: summary click did not expand patch rack")
            ensure(not fold3.is_visible(),f"{width}: folded marker overlaps expanded jacks")
            # Create an actual custom edge and close the rack via its left rail.
            out=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
            receiving=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
            x0,y0=center(out);x1,y1=center(receiving)
            page.mouse.move(x0,y0);page.mouse.down()
            page.mouse.move(x1,y1,steps=12);page.mouse.up()
            ensure(receiving.get_attribute("aria-pressed")=="true",
                   f"{width}: ordinary OUT→IN routing regressed")
            pan=page.locator('#tracks .strip[data-id="3"] .pan-section').bounding_box()
            fader=page.locator('#tracks .strip[data-id="3"] .fader-section').bounding_box()
            wire.click()
            ensure(fold1.is_visible() and fold3.is_visible(),f"{width}: WIRE not collapsed")
            ensure(fold1.locator('svg.fold-link-icon.has-out').count()==1 and
                   fold3.locator('svg.fold-link-icon.has-in').count()==1,
                   f"{width}: live IN/OUT graphics failed to reflect graph edit")
            ensure(not page.locator('#tracks .strip[data-id="3"] .wire-tray').is_visible(),
                   f"{width}: linked socket/knob still visible when WIRE is folded")
            ensure(fold1.inner_text().strip()=="" and fold3.inner_text().strip()=="",
                   f"{width}: folded WIRE exposed tiny counts or text")
            ensure(not page.locator('#wireLayer').is_visible(),
                   f"{width}: cable layer should hide in collapsed WIRE")
            ensure(abs(page.locator('#tracks .strip[data-id="3"] .pan-section').bounding_box()["height"]-pan["height"])<1,
                   f"{width}: PAN height shifted")
            ensure(abs(page.locator('#tracks .strip[data-id="3"] .fader-section').bounding_box()["height"]-fader["height"])<1,
                   f"{width}: physical fader height changed")
            page.screenshot(path=str(ROOT/f"preview-v19-collapsed-{width}.png"))
            ensure(not errors,f"{width}: JS exceptions {errors}")
            page.close()
        browser.close()
        print("PASS V19 Chromium: 28px icon rail, compact linked status, expand-on-click and fixed PAN/fader sizing at 1366/1680/1920")

if __name__=="__main__":main()
