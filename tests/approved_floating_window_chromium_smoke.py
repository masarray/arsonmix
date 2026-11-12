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
    # A core mixer operation must be discoverable without opening More or
    # affecting playback: add a channel, then rename in place by typing once.
    add=page.locator("#addTrack")
    assert add.count()==1 and add.is_visible(),(width,"Add Audio is hidden")
    assert page.locator("#moreTools #addTrack").count()==0,"Add Audio must not be buried"
    add.click()
    assert "11 audio" in page.locator("#trackCount").inner_text(),(width,"channel not added")
    new_track=page.locator("#tracks .strip[data-id='13']")
    assert new_track.is_visible(),(width,"new channel not revealed in its bank")
    new_track.locator(".strip-identity-top").dblclick()
    editor=page.locator("#channelIdentity")
    assert editor.is_visible(),(width,"rename editor not opened")
    name=page.locator("#identityName")
    selection=name.evaluate("""e=>({value:e.value,start:e.selectionStart,end:e.selectionEnd,
      focused:document.activeElement===e})""")
    assert (selection["focused"] and selection["start"]==0 and
            selection["end"]==len(selection["value"])),selection
    name.fill("Lead Vocal")
    name.press("Enter")
    assert editor.is_hidden(),(width,"Enter did not close accepted rename")
    assert new_track.locator(".identity-name").inner_text()=="Lead Vocal"
    # Restore the starting bank so subsequent WIRE/FX smoke uses CH01.
    page.locator("#bankSelect").select_option("0")
    page.wait_for_selector("#tracks .strip[data-id='1']")
    # Real operator command path, not synthetic DOM positioning.
    page.locator("#playerToggle").click()
    player=bounds(page,"#arsonPlayer")
    player_close=bounds(page,"#playerClose")
    assert 0 <= player["x"]+player["width"]-player_close["x"]-player_close["width"] <= 16,(
        "Player Close must align with right edge",player,player_close)
    page.locator("#playerClose").click()
    open_wire(page,"routes")
    move_panel(page,"#wireWorkspace","#wireWorkspace .wire-ws-head")
    # Changing from Routes to wider Matrix must not strand the window.
    open_wire(page,"matrix")
    box=bounds(page,"#wireWorkspace")
    assert box["x"]>=7 and box["x"]+box["width"]<=width+1,("matrix viewport",box)
    matrix_close=page.locator("#wireWsClose")
    icon=matrix_close.evaluate("e=>getComputedStyle(e,'::after').backgroundImage")
    assert "M6.4,19L5,17.6" in icon,icon
    matrix_close.hover()
    assert "rgb(99, 56, 64)" in matrix_close.evaluate("e=>getComputedStyle(e).backgroundImage")
    page.mouse.down()
    pressed=matrix_close.evaluate("""e=>({
        icon:getComputedStyle(e,'::after').backgroundImage,
        height:getComputedStyle(e,'::after').height,
        face:getComputedStyle(e).backgroundImage
    })""")
    assert pressed["icon"]==icon and pressed["height"]=="22px",pressed
    assert "rgb(239, 62, 80)" in pressed["face"],pressed
    page.mouse.up()
    open_wire(page,"config")
    assert page.locator("#wireWorkspace").is_hidden()
    move_panel(page,"#wireConfigWindow","#wireConfigWindow .wire-config-head")
    close=page.locator("#wireConfigClose")
    geometry=close.evaluate("""e=>({
      width:e.getBoundingClientRect().width,height:e.getBoundingClientRect().height,
      symbol:getComputedStyle(e,'::after').backgroundImage
    })""")
    assert geometry["width"]==24 and geometry["height"]==24,geometry
    assert "M6.4,19L5,17.6" in geometry["symbol"],geometry
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
