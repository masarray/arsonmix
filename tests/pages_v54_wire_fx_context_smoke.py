#!/usr/bin/env python3
"""V54: physical WIRE truth, individual/chain FX bypass, native fail-closed."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"site/preview-v2/index.html"

def wire(page,ch,direction):
    node=".wire-node-out" if direction=="out" else ".wire-node-in"
    dot=".wire-pin" if direction=="out" else ".wire-arrow"
    return page.locator(f'#tracks .strip[data-id="{ch}"] {node}').evaluate(
        """(el,dot)=>({
          connected:el.getAttribute('data-wire-connected'),
          arrival:el.getAttribute('data-wire-arrival'),
          visible:el.getAttribute('data-wire-visible'),
          color:getComputedStyle(el.querySelector(dot)).backgroundColor,
          stale:el.style.getPropertyValue('--wire-edge-color')
        })""",dot)

def fx(page,ch):
    return page.locator(f'#tracks .strip[data-id="{ch}"] .fx-section').evaluate(
        """root=>({
          enabled:root.dataset.fxChainEnabled,
          bypassed:root.classList.contains('is-chain-bypassed'),
          header:root.querySelector('[data-action="fxOn"]').getAttribute('aria-pressed'),
          slots:[...root.querySelectorAll('.fxslot-row:not(.is-empty)')].map(row=>({
            id:row.dataset.insertId,enabled:row.dataset.fxSlotEnabled,
            effective:row.dataset.fxEffective,
            power:row.querySelector('[data-action="slotPower"]')?.getAttribute('aria-pressed')
          }))
        })""")

def scenario(page,w,density):
    page.locator("#densitySelect").select_option(density)
    if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
        page.locator('.section-rail [data-section="send"]').click()
    # In WIRE-first sessions every CH has one *real* default MAIN edge.
    # IN remains idle. Default MAIN is a real graph edge but never a
    # custom OUT indicator, even before a channel is selected.
    for ch in (1,2):
        incoming=wire(page,ch,"in")
        outgoing=wire(page,ch,"out")
        assert incoming["connected"] is None and incoming["arrival"] is None and not incoming["stale"],(
            w,density,"phantom incoming graph route",ch,incoming)
        assert incoming["color"]=="rgb(75, 83, 91)",(w,density,"idle IN dot isn't graphite",ch,incoming)
        assert outgoing["connected"] is None,(w,density,"default MAIN falsely lit OUT",ch,outgoing)
        assert page.locator(f'#tracks .strip[data-id="{ch}"] .wire-node-out').get_attribute(
            "data-wire-main-edge"),(w,density,"MAIN graph edge absent",ch)
        assert outgoing["stale"]=="",(w,density,"stale OUT paint",ch,outgoing)
    # Compact/Standard can intentionally fold SEND/WIRE controls to protect
    # the fixed meter/fader. Validate their passive material in every density;
    # exercise operator patch gestures only in Comfort where jacks are exposed.
    if density!="comfort":return
    # Graph connection lights ONLY the selected edge's source and receiver.
    page.locator('#tracks .strip[data-id="5"] .wire-node-out').click()
    page.locator('#tracks .strip[data-id="3"] .wire-node-in').click()
    assert wire(page,5,"out")["connected"]=="true",(w,density,"missing OUT")
    assert wire(page,3,"in")["arrival"]=="true",(w,density,"missing IN")
    for ch in (1,2):
        assert wire(page,ch,"out")["connected"] is None,(w,density,"default MAIN falsely lit OUT",ch)
    page.locator("#wireNavigatorToggle").click()
    undo=page.locator("#wireWsUndo")
    assert undo.is_enabled(),(w,density,"undo not available")
    undo.click()
    assert wire(page,5,"out")["connected"] is None,(w,density,"Undo left custom OUT lit")
    assert page.locator('#tracks .strip[data-id="5"] .wire-node-out').get_attribute(
        "data-wire-main-edge"),"Undo removed MAIN graph edge"
    assert wire(page,3,"in")["arrival"] is None,(w,density,"ghost input after undo")
    page.locator("#wireNavigatorToggle").click()
    before=fx(page,1)
    assert before["enabled"]=="true" and len(before["slots"])==1,(w,density,before)
    # Add real second GUI demo slot using UI picker; no direct private model access.
    page.locator('#tracks .strip[data-id="1"] .fx-pick.empty').click()
    page.locator('#fastResults [data-fast-id]').first.click()
    two=fx(page,1);other=fx(page,2)
    assert len(two["slots"])==2 and all(s["effective"]=="true" for s in two["slots"]),(
        w,density,"two FX setup",two)
    header=page.locator('#tracks .strip[data-id="1"] [data-action="fxOn"]')
    header.click()
    off=fx(page,1)
    assert off["enabled"]=="false" and off["bypassed"] and off["header"]=="false",(w,density,off)
    assert all(s["enabled"]=="true" and s["effective"]=="false" for s in off["slots"]),(
        w,density,"header incorrectly toggled slot bypass",off)
    assert fx(page,2)==other,(w,density,"channel FX bypass leaked to CH2")
    slot=page.locator('#tracks .strip[data-id="1"] .fxslot-row[data-slot="0"] [data-action="slotPower"]')
    slot.click()
    solo_off=fx(page,1)
    assert solo_off["slots"][0]["enabled"]=="false" and solo_off["slots"][1]["enabled"]=="true",(
        w,density,"slot bypass touched sibling",solo_off)
    header.click()
    restored=fx(page,1)
    assert restored["enabled"]=="true" and restored["slots"][0]["effective"]=="false" and restored["slots"][1]["effective"]=="true",(w,density,"chain restore",restored)
    assert [s["id"] for s in two["slots"]]==[s["id"] for s in restored["slots"]],(
        w,density,"bypass lost stable demo insert IDs")
    if density=="comfort":
        page.screenshot(path=f"pages-v54-wire-fx-{w}.png")

def native_guard(browser):
    page=browser.new_page(viewport={"width":1680,"height":900})
    page.add_init_script("window.arMixConsoleIntent=function(){}")
    page.goto(PAGE.as_uri())
    initial=fx(page,1)
    root='#tracks .strip[data-id="1"] '
    page.locator(root+'[data-action="fxOn"]').click()
    page.locator(root+'[data-action="slotPower"]').first.click()
    page.locator(root+'[data-action="removeFx"]').first.click()
    assert fx(page,1)==initial,("Native bridge must not paint un-ACKed FX state")
    page.close()

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            for density in ("comfort","standard","compact"):
                page=browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror",lambda exc:errors.append(str(exc)))
                page.goto(PAGE.as_uri())
                scenario(page,width,density)
                assert not errors,(width,density,errors)
                page.close()
        native_guard(browser)
        browser.close()
    print("PASS V54: 0/1 WIRE/Undo, CH1/CH2 dark, two FX bypass levels, native ACK guard, 3x3")

if __name__=="__main__":
    main()
