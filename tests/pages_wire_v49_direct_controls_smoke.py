#!/usr/bin/env python3
"""V49 operator interaction: real Chromium Pages DOM, simulated GUI, no PCM claims."""
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
PAGE=ROOT/"site/preview/index.html"

def center(box):
    assert box and box["width"] and box["height"], box
    return box["x"]+box["width"]/2,box["y"]+box["height"]/2

def run():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width in (1366,1680,1920):
            page=browser.new_page(viewport={"width":width,"height":900})
            errors=[]
            page.on("pageerror",lambda exc:errors.append(str(exc)))
            page.goto(PAGE.as_uri())
            body=page.locator(".console-body")
            if "collapsed-send" in (body.get_attribute("class") or ""):
                page.locator('.section-rail [data-section="send"]').click()
            # WIRE-first sessions start connected to MAIN. Explicitly unplug
            # CH05 to exercise a *truly* empty original V49 OUT jack.
            page.locator('#tracks .strip[data-id="5"] .strip-identity-top').click()
            page.locator("#wireConfigToggle").click()
            assert page.locator("#wireMaster").get_attribute("aria-pressed")=="true"
            page.locator("#wireMaster").click()
            page.locator("#wireConfigClose").click()
            source=page.locator('#tracks .strip[data-id="5"] .wire-node-out')
            destination=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
            # An actually unpatched OUT stays neutral even if its channel is selected.
            idle=source.evaluate("""node=>({
               graph:node.hasAttribute('data-wire-connected'),
               paint:getComputedStyle(node.querySelector('.wire-pin')).backgroundColor
            })""")
            assert not idle["graph"],("phantom source route",width,idle)
            source.click()
            destination.click()
            gain=page.locator('#tracks .strip[data-id="3"] .wire-gain')
            assert gain.count()==1,("missing committed wire gain",width)
            # Selecting the cable is an actual SVG pointer operation, not an
            # invisible broad overlay or a guessed channel index.
            hit=page.locator('#wireLayer path.wire-interaction-hit').first
            assert hit.count()>=1,(width,"no selectable cable")
            assert hit.get_attribute("data-edge-id"),(width,"missing canonical edge id")
            # Click at the SVG *curve* midpoint, not the bounding-box center
            # (which is often off-stroke for a hanging cable).
            mid=hit.evaluate("""el=>{
                const pt=el.getPointAtLength(el.getTotalLength()*.5);
                const svg=el.ownerSVGElement, rect=svg.getBoundingClientRect();
                const box=svg.viewBox.baseVal;
                return {x:rect.left+(pt.x-box.x)*rect.width/box.width,
                        y:rect.top+(pt.y-box.y)*rect.height/box.height};
            }""")
            page.mouse.click(mid["x"],mid["y"])
            assert page.locator('#wireLayer path.wire-interaction-hit.is-wire-selected').count()>=1,(width,"cable not selected")
            # Leave patch mode. Existing route remains a directly editable knob.
            page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
            gain=page.locator('#tracks .strip[data-id="3"] .wire-gain')
            assert gain.count()==1,(width,"gain requires source OUT arming")
            old=float(gain.input_value())
            page.locator('#tracks .strip[data-id="3"] .wire-pot').hover()
            page.mouse.wheel(0,-120)
            new=float(gain.input_value())
            assert new>old,(width,"wire gain not editable via hover wheel",old,new)
            assert abs(new-old)<=1,(width,"wheel gain jumped too far",old,new)
            # Wheel on recessed physical fader; hidden input must still follow.
            faderbox=page.locator('#tracks .strip[data-id="3"] .fader-box')
            fader=page.locator('#tracks .strip[data-id="3"] .fader')
            old=float(fader.input_value());faderbox.hover();page.mouse.wheel(0,-120)
            assert float(fader.input_value())>old,(width,"fader wheel ignored")
            # Pan center detent must not consume every small wheel adjustment.
            pan=page.locator('#tracks .strip[data-id="3"] .center-pan')
            pan.hover();page.mouse.wheel(0,-120)
            assert float(pan.input_value())>0,(width,"PAN wheel stuck in center detent")
            # Unarmed receiving jack begins a candidate drag. Replug must
            # preserve edge ID and knob gain, and provide an exact route undo.
            edge_id=hit.get_attribute("data-edge-id")
            x0,y0=center(destination.bounding_box())
            new_dest=page.locator('#tracks .strip[data-id="6"] .wire-node-in')
            x1,y1=center(new_dest.bounding_box())
            page.mouse.move(x0,y0)
            page.mouse.down()
            page.mouse.move((x0+x1)/2,y0+38,steps=6)
            page.mouse.move(x1,y1,steps=6)
            page.mouse.up()
            new_gain=page.locator('#tracks .strip[data-id="6"] .wire-gain')
            assert new_gain.count()==1,(width,"unarmed receiver replug failed")
            assert abs(float(new_gain.input_value())-new)<.11,(width,"replug lost send gain")
            # Undo belongs to routing only, not channel fader/PAN.
            page.locator('#wireNavigatorToggle').click()
            undo=page.locator('#wireWsUndo')
            assert undo.is_enabled(),(width,"routing undo not recorded")
            undo.click()
            assert page.locator('#tracks .strip[data-id="3"] .wire-gain').count()==1,(width,"undo did not restore edge")
            assert page.locator('#tracks .strip[data-id="6"] .wire-gain').count()==0,(width,"undo left ghost route")
            # A selected cable owns its gain even while an unrelated OUT
            # remains armed. No editing the first/armed route by accident.
            page.locator('#wireNavigatorToggle').click()
            page.locator('#tracks .strip[data-id="7"] .wire-node-out').click()
            destination.click()
            cable_b=page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="7-3"]')
            assert cable_b.count()==1,(width,"second incoming route absent")
            edge_b=cable_b.get_attribute("data-edge-id")
            cable_b.evaluate("el=>el.dispatchEvent(new MouseEvent('click',{bubbles:true}))")
            page.locator('#tracks .strip[data-id="5"] .wire-node-out').click()
            active=page.locator('#tracks .strip[data-id="3"] .wire-gain')
            assert active.get_attribute("data-wire-id")==edge_b,(width,"armed OUT stole explicitly selected gain")
            b_before=float(active.input_value())
            page.locator('#tracks .strip[data-id="3"] .wire-pot').hover()
            page.mouse.wheel(0,-120)
            b_after=float(active.input_value())
            assert b_after>b_before,(width,"selected route B wheel did not adjust B")
            cable_a=page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="5-3"]')
            assert cable_a.count()==1,(width,"original route A disappeared")
            cable_a.evaluate("el=>el.dispatchEvent(new MouseEvent('click',{bubbles:true}))")
            a_input=page.locator('#tracks .strip[data-id="3"] .wire-gain')
            assert a_input.get_attribute("data-wire-id")==edge_id,(width,"route A identity lost")
            assert abs(float(a_input.input_value())-new)<.11,(width,"route A gain changed while editing B")
            assert not errors,(width,errors)
            page.close()
        browser.close()
    print("PASS V49 selectable WIRE, unarmed knob, wheel, replug and scoped undo")

if __name__=="__main__":
    run()
