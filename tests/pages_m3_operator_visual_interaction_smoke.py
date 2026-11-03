#!/usr/bin/env python3
"""M3 operator console: geometry, overlays, FX, clip and multi-wire browser contract."""
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"

def physical(page,density,width):
    page.locator("#densitySelect").select_option(density)
    data=page.evaluate("""() => {
      const roots=[document.querySelector('#tracks .strip[data-id="1"]'),
                   document.querySelector('#master .strip')];
      return roots.map(root=>{
        const r=selector=>root.querySelector(selector)?.getBoundingClientRect();
        const head=r('.fader-head'),meter=r('.meter-box'),box=r('.fader-box');
        const ruler=r('.fader-scale'),led=r('.meter-lane');
        const workspace=r('.fader-workspace');
        const captions=[...root.querySelectorAll('.vu-tick.major span')]
          .filter(e=>e.textContent.trim()).map(el=>{
            const range=document.createRange();range.selectNodeContents(el);
            return {left:range.getBoundingClientRect().left,
                    right:range.getBoundingClientRect().right};
          });
        return {headX:head.x,meterX:meter.x,meterW:meter.width,
                meterGap:box.left-meter.right,rulerGap:ruler.left-box.right,
                scaleRight:ruler.right,workspaceRight:workspace.right,
                ledWidth:led.width,
                labelClipped:captions.some(x=>x.left<meter.left-1||x.right>meter.right+1),
                peakLamps:root.querySelectorAll('.peak-led').length,
                mono:root.querySelector('.meter-box').classList.contains('mono')};
      });
    }""")
    assert len(data)==2,(width,density,data)
    for rig in data:
        assert abs(rig["meterX"]-rig["headX"])<=1.5,(width,density,"head alignment",rig)
        assert rig["meterW"]>=69.5 and abs(rig["ledWidth"]-10)<.7,(width,density,"meter geometry",rig)
        assert 1<=rig["meterGap"]<=4.5,(width,density,"meter/fader collision",rig)
        assert 1<=rig["rulerGap"]<=4.5,(width,density,"gain ruler collision",rig)
        assert rig["scaleRight"]<=rig["workspaceRight"]+1.5,(width,density,"assembly overflow",rig)
        assert not rig["labelClipped"],(width,density,"dBFS ink overflow",rig)
        assert rig["peakLamps"]==(1 if rig["mono"] else 2),(width,density,"missing peak lamp",rig)
    # Stereo must carry two independent peak lamps even though CH1 is mono.
    stereo=page.locator('#tracks .strip .meter-box.stereo .peak-led')
    assert stereo.count()>=2,(width,density,"stereo lamps")

def interactions(page):
    title=page.locator(".mixer-title")
    toggle=page.locator("#mixerTitleToggle")
    expected=52 if page.viewport_size["height"]<=800 else 66
    assert abs(title.bounding_box()["height"]-expected)<=2,("M3 header height",page.viewport_size)
    assert toggle.get_attribute("aria-expanded")=="true"
    toggle.click()
    assert toggle.get_attribute("aria-expanded")=="false"
    assert title.bounding_box()["height"]<=32
    toggle.click()
    assert toggle.get_attribute("aria-expanded")=="true"

    menu=page.locator("#moreTools")
    menu.locator("summary").click()
    assert menu.evaluate("e=>e.open")
    page.locator(".mixer-title .panel-name").click()
    assert not menu.evaluate("e=>e.open")
    menu.locator("summary").click()
    page.keyboard.press("Escape")
    assert not menu.evaluate("e=>e.open")
    menu.locator("summary").click()
    page.locator("#densitySelect").focus()
    assert not menu.evaluate("e=>e.open")

    empty=page.locator('#tracks .strip[data-id="1"] .fxslot-row:has(.fx-pick.empty)').first
    occupied=page.locator('#tracks .strip[data-id="1"] .fxslot-row:not(:has(.fx-pick.empty))').first
    assert empty.locator('button[data-action="slotPower"]').count()==0
    assert empty.locator(".fx-slot-placeholder").count()==1
    assert occupied.locator('button[data-action="slotPower"]').count()==1
    assert abs(empty.locator('.fx-pick').bounding_box()['x']-
               occupied.locator('.fx-pick').bounding_box()['x'])<=1.25

    # Drive the real operator UI, not private IIFE variables. The display
    # preview is simulated; no native audio device or DSP is involved.
    fader=page.locator('#tracks .strip[data-id="1"] input.fader')
    fader.evaluate("""e => {e.value='10';e.dispatchEvent(new Event('input',{bubbles:true}));
                         e.dispatchEvent(new Event('change',{bubbles:true}));}""")
    if not page.evaluate("window.ArSonMixMeterCalibration.readDemo().running"):
        page.locator("#moreTools summary").click()
        page.locator("#preview").click()
    peak=page.locator('#tracks .strip[data-id="1"] .peak')
    page.wait_for_function("""() => document.querySelector(
      '#tracks .strip[data-id="1"] .peak')?.classList.contains('clipping')""",
      timeout=10000)
    assert 'clipping' in peak.get_attribute("class")
    if not page.locator("#moreTools").evaluate("e=>e.open"):
        page.locator("#moreTools summary").click()
    page.locator("#preview").click()  # stop re-latching before reset
    page.locator('#tracks .strip[data-id="1"] button[data-action="resetClip"]').click()
    assert 'clipping' not in page.locator('#tracks .strip[data-id="1"] .peak').get_attribute("class")

    # The reference console can boot with SEND collapsed. Expose the real
    # patch-bay through its rail before interacting with physical jacks.
    out_jack=page.locator('#tracks .strip[data-id="1"] [data-action="wireSource"]')
    if not out_jack.is_visible():
        page.locator('.section-rail [data-section="send"]').click()
    assert out_jack.is_visible(),"WIRE OUT still hidden after expanding SEND rail"

    # Two real operator connections, independently created by OUT -> IN.
    # A receiving socket must stay usable after both edges already exist.
    for src in (1,2):
        page.locator(f'#tracks .strip[data-id="{src}"] [data-action="wireSource"]').click()
        receiver=page.locator('#tracks .strip[data-id="4"] [data-action="wireRoute"]')
        assert receiver.is_enabled(),("receiving jack disabled while adding second wire",src)
        receiver.click()
    receiver=page.locator('#tracks .strip[data-id="4"] .wire-node-in')
    assert receiver.is_enabled(),"multi-wire receiving IN unexpectedly disabled"
    assert "2 incoming" in receiver.get_attribute("title"),receiver.get_attribute("title")
    receiver.click()
    assert not page.locator("#wireWorkspace").is_hidden(),"receiver inspector did not open"
    routes=page.locator('#wireWsList [data-ws-inspect]')
    keys=routes.evaluate_all("(els)=>els.map(e=>e.dataset.wsInspect)")
    assert '1:4' in keys and '2:4' in keys,("incoming edges not individually inspectable",keys)
    page.locator('#wireWsList [data-ws-inspect="1:4"]').click()
    inspector=page.locator("#wireWsInspector")
    assert inspector.is_visible(),"individual incoming route inspection missing"
    assert "CH01" in inspector.inner_text() and "CH04" in inspector.inner_text()
    assert page.locator('#wireWsList [data-ws-inspect="2:4"]').count()==1,(
        "opening a cable inspector must not remove the other incoming wire")

def main():
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda err:errors.append(str(err)))
            page.goto(HTML.as_uri())
            page.wait_for_selector('#tracks .strip[data-id="1"] .meter-box')
            for density in ("comfort","standard","compact"):
                physical(page,density,width)
            page.locator("#densitySelect").select_option("comfort")
            interactions(page)
            if width==1680:page.screenshot(path="pages-m3-operator-meter-wire-1680.png")
            assert not errors,(width,errors)
            page.close()
        browser.close()
    print("PASS M3: 70px meter, aligned fader, stereo clip, FX, menu/header and multi-wire inspector")

if __name__=="__main__":
    main()
