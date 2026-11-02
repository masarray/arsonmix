#!/usr/bin/env python3
"""V18 live connected-IN unplug/replug acceptance; GUI reference, not native PCM."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html")

def check(value,why):
    if not value: raise AssertionError(why)

def center(locator):
    r=locator.bounding_box()
    check(r is not None,"Missing live jack geometry")
    return r["x"]+r["width"]/2,r["y"]+r["height"]/2

def drag(page,a,b,offset=(0,0)):
    x0,y0=center(a);x1,y1=center(b)
    page.mouse.move(x0,y0);page.mouse.down()
    page.mouse.move(x1+offset[0],y1+offset[1],steps=14)
    page.mouse.up()

def source_selected(page,id=6):
    return page.locator(f'#tracks .strip[data-id="{id}"] .wire-node-out')

def socket(page,id):
    return page.locator(f'#tracks .strip[data-id="{id}"] .wire-node-in')

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1680,900),(1366,768),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.goto(PAGE.as_uri())
            page.locator('.section-rail [data-section="send"]').click()
            # First connect a source normally, so the linked IN has a real cable end.
            source=source_selected(page,6)
            ch3=socket(page,3)
            drag(page,source,ch3)
            check(ch3.get_attribute("aria-pressed")=="true",f"{width}: fixture CH6→CH3 did not connect")
            gain3=page.locator('#tracks .strip[data-id="3"] .wire-gain')
            edge=gain3.get_attribute("data-wire-id")
            gain3.evaluate("""el=>{
              el.value="-17";
              el.dispatchEvent(new Event("input",{bubbles:true}));
              el.dispatchEvent(new Event("change",{bubbles:true}));
            }""")
            check(gain3.input_value()=="-17",f"{width}: fixture gain not applied")
            # The live IN or socket well is the draggable cable end.
            x0,y0=center(ch3)
            x4,y4=center(socket(page,4))
            page.mouse.move(x0,y0);page.mouse.down()
            page.mouse.move((x0+x4)/2,(y0+y4)/2,steps=8)
            check(page.locator('#wireLayer .wire-floating-plug').count()==1,
                  f"{width}: floating cable plug is missing during replug")
            check(page.locator('#tracks .strip[data-id="3"] .wire-node-in.is-replug-origin').count()==1,
                  f"{width}: original socket does not look empty while dragging")
            page.mouse.move(x4,y4,steps=12)
            check(page.locator('#tracks .strip[data-id="4"] .wire-node-in.is-snap-target').count()==1,
                  f"{width}: node did not snap to receiver CH4")
            page.mouse.up()
            check(socket(page,3).get_attribute('aria-pressed')=='false',
                  f"{width}: old receiver stayed connected after replug")
            check(socket(page,4).get_attribute('aria-pressed')=='true',
                  f"{width}: target did not receive moved wire")
            gain4=page.locator('#tracks .strip[data-id="4"] .wire-gain')
            check(gain4.get_attribute("data-wire-id")==edge and gain4.input_value()=="-17",
                  f"{width}: wire ID or level changed in replug")
            # No click ghost should disconnect the freshly created destination.
            check('⇢1' in page.locator('#tracks .strip[data-id="6"]').inner_text(),
                  f"{width}: a ghost click removed the source route")

            # Moving only a few pixels must snap right back, not drop signal.
            x4,y4=center(socket(page,4))
            page.mouse.move(x4,y4);page.mouse.down()
            page.mouse.move(x4+16,y4-5,steps=7);page.mouse.up()
            check(socket(page,4).get_attribute("aria-pressed")=="true",
                  f"{width}: near-source movement unplugged audio")
            # A short intentional pull past the magnetic threshold unplugs.
            page.mouse.move(x4,y4);page.mouse.down()
            page.mouse.move(x4,y4-56,steps=11)
            check(page.locator('#wireLayer .wire-floating-plug').count()==1,
                  f"{width}: loose cable tip is not drawn away from its socket")
            page.mouse.up()
            check(socket(page,4).get_attribute("aria-pressed")=="false",
                  f"{width}: off-socket release failed to disconnect")
            check('⇢1' not in page.locator('#tracks .strip[data-id="6"]').inner_text(),
                  f"{width}: detached edge still reported active")

            # Already-connected destination must never receive a duplicate route.
            drag(page,source_selected(page,6),socket(page,3))
            drag(page,source_selected(page,6),socket(page,4))
            x3,y3=center(socket(page,3));x4,y4=center(socket(page,4))
            page.mouse.move(x3,y3);page.mouse.down()
            page.mouse.move(x4,y4,steps=12);page.mouse.up()
            check(socket(page,3).get_attribute("aria-pressed")=="true" and
                  socket(page,4).get_attribute("aria-pressed")=="true",
                  f"{width}: rejected duplicate destroyed either existing cable")
            check('REPLUG REJECTED' in page.locator('#info').inner_text(),
                  f"{width}: duplicate should report rejected operation")
            check(not errors,f"{width}: JavaScript exceptions: {errors}")
            page.screenshot(path=str(ROOT/f"preview-v18-replug-{width}.png"))
            page.close()
        browser.close()
        print("PASS V18 Chromium: linked-IN cable follows pointer, magnetic replug, gain/ID held, gentle release, rejected duplicates; 1366/1680/1920")

if __name__=="__main__":main()
