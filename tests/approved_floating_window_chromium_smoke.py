#!/usr/bin/env python3
"""Real Chromium smoke of approved modeless workspace gestures; no audio device."""
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site"/"preview-v2"/"index.html"

def bounds(page,selector):
    box=page.locator(selector).bounding_box()
    assert box,(selector,"missing visible bounds")
    return box

def move_panel(page,panel,header,dx=-110,dy=70):
    before=bounds(page,panel)
    handle=bounds(page,header)
    x,y=handle["x"]+15,handle["y"]+handle["height"]/2
    page.mouse.move(x,y)
    page.mouse.down()
    page.mouse.move(x+dx,y+dy,steps=6)
    page.mouse.up()
    after=bounds(page,panel)
    assert page.locator(panel).evaluate("e=>e.classList.contains('armix-floating-moved')"),panel
    assert after["x"]!=before["x"] or after["y"]!=before["y"],(panel,before,after)
    assert after["x"]>=7 and after["y"]>=7,(panel,"negative / offscreen",after)
    viewport=page.viewport_size
    assert after["x"]+after["width"]<=viewport["width"]+1,(panel,"right edge",after)
    assert after["y"]+after["height"]<=viewport["height"]+1,(panel,"bottom edge",after)
    return after

def open_wire(page,kind):
    names={"routes":"wireNavigatorToggle","matrix":"wireMatrixToggle","config":"wireConfigToggle"}
    button=page.locator("#"+names[kind])
    if button.is_visible():
        button.click()
    else:
        more=page.locator("#moreTools")
        if not more.evaluate("e=>e.open"):
            more.locator("summary").click()
        more.locator('[data-wire-open="'+kind+'"]').click()

def run(page,width,height):
    errors=[]
    page.on("pageerror",lambda err:errors.append(str(err)))
    page.goto(HTML.as_uri())
    page.wait_for_selector("#tracks .strip[data-id='1']")
    # Real operator command path, not synthetic DOM positioning.
    open_wire(page,"routes")
    move_panel(page,"#wireWorkspace","#wireWorkspace .wire-ws-head")
    # Changing from Routes to wider Matrix must not strand the window.
    open_wire(page,"matrix")
    box=bounds(page,"#wireWorkspace")
    assert box["x"]>=7 and box["x"]+box["width"]<=width+1,("matrix viewport",box)
    open_wire(page,"config")
    assert page.locator("#wireWorkspace").is_hidden()
    move_panel(page,"#wireConfigWindow","#wireConfigWindow .wire-config-head")
    close=page.locator("#wireConfigClose")
    geometry=close.evaluate("""e=>({
      width:e.getBoundingClientRect().width,height:e.getBoundingClientRect().height,
      symbol:getComputedStyle(e,'::after').backgroundImage
    })""")
    assert geometry["width"]==24 and geometry["height"]==24,geometry
    assert "gradient" in geometry["symbol"],geometry
    close.click()
    assert page.locator("#wireConfigWindow").is_hidden()
    # Quick FX entry: existing source slot activation must still be clickable.
    slot=page.locator("#tracks .strip[data-id='1'] [data-action='openPlugin']").first
    slot.click()
    assert page.locator("#fastPicker").is_visible(),"Quick FX popup failed to open"
    move_panel(page,"#fastPicker","#fastPicker .fast-head",dx=-50,dy=45)
    action=page.locator("#fastManage")
    styling=action.evaluate("""e=>({
      height:e.getBoundingClientRect().height,
      border:getComputedStyle(e).borderTopStyle,
      bg:getComputedStyle(e).backgroundImage
    })""")
    assert styling["height"]>=28 and styling["border"]=="solid" and "gradient" in styling["bg"],styling
    page.locator("#fastClose").click()
    assert page.locator("#fastPicker").is_hidden()
    # Route Choice has a shared close/drag surface without changing real routing.
    page.evaluate("document.getElementById('routeChoice').hidden=false")
    move_panel(page,"#routeChoice","#routeChoice .route-choice-head",dx=-50,dy=35)
    page.locator("#routeChoiceClose").click()
    assert page.locator("#routeChoice").is_hidden()
    assert not errors,(width,height,errors)
    page.screenshot(path=f"arsonmix-floating-ux-{width}x{height}.png",full_page=False)

def main():
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for width,height in ((1366,768),(1024,576)):
            page=browser.new_page(viewport={"width":width,"height":height})
            try: run(page,width,height)
            finally: page.close()
        browser.close()
    print("PASS approved floating UX: move/close/Matrix resize/picker actions across two viewports")

if __name__=="__main__":
    main()
