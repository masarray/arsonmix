#!/usr/bin/env python3
"""V52 fader bevel/ruler/raised static thumb-shadow; V51 meter is frozen.

Browser GUI reference only. Never treats simulated meter data as native PCM.
"""
import hashlib
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
FADER=ROOT/"site/preview-v2/assets/mixer-fader-assembly.css"
METER=ROOT/"site/preview-v2/assets/mixer-meter-display.css"
APPROVED_V27_METER_BLOB="e168fe3fde3b176ce46f2c637a17db65bc510944"

def git_blob(path):
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()

def audit_checkpoint():
    assert git_blob(METER)==APPROVED_V27_METER_BLOB, (
        "V27-preserved, owner-approved V51 meter changed; restore the V27 visual baseline"
    )
    html=HTML.read_text(encoding="utf-8")
    assert "function displayMeterDbfs(" in html
    assert "meter-peak-hold" in html and "METER_HOLD_MS=850" in html
    css=FADER.read_text(encoding="utf-8")
    assert "filter:none!important" in css
    assert ".console-body .strip .fader-workspace" in css
    assert "#263944" in css
    assert "1px 8px 9px -3px" in css and "0 13px 12px -7px" in css
    assert "linear-gradient(90deg," in css

def inspect(page,width,height,density):
    page.locator("#densitySelect").select_option(density)
    vals=page.evaluate("""() => {
      const roots=[document.querySelector('#tracks .strip[data-id="1"]'),
                   document.querySelector('#master .strip')];
      return roots.map(root=>{
        const el=sel=>root.querySelector(sel);
        const w=el('.fader-workspace'),box=el('.fader-box'),
              before=getComputedStyle(box,'::before'),
              slot=getComputedStyle(box,'::after'),
              scale=el('.fader-scale'),cap=el('.fader-travel .cap'),
              meter=el('.meter-box'),fill=el('.meter-fill');
        const ws=getComputedStyle(w),p=getComputedStyle(box),
              sc=getComputedStyle(scale),c=getComputedStyle(cap);
        const rect=node=>node.getBoundingClientRect();
        return {workColor:ws.backgroundColor,rulerColor:sc.backgroundColor,
          boxWidth:rect(box).width,capWidth:rect(cap).width,capHeight:rect(cap).height,
          rim:p.boxShadow,pocket:before.backgroundImage,depth:before.boxShadow,
          slotWidth:slot.width,slotBackground:slot.backgroundImage,
          thumbShadow:c.boxShadow,thumbFilter:c.filter,thumbTransition:c.transitionDuration,
          meterWidth:rect(meter).width,meterFillWidth:rect(fill).width,
          gainRulerGap:rect(scale).left-rect(box).right,
          meterPocketGap:rect(box).left-rect(meter).right,
          borderRadius:p.borderRadius,
          count:el('.gain-scale-travel').querySelectorAll('.gain-tick').length};
      });
    }""")
    assert len(vals)==2,(width,density,'missing channel/master')
    targetBox,targetCap={"comfort":(29,27),"standard":(27,27),"compact":(25,25)}[density]
    for v in vals:
        assert abs(v["boxWidth"]-targetBox)<.75,(width,density,"pocket width",v)
        assert abs(v["capWidth"]-targetCap)<.75,(width,density,"PNG cap width",v)
        assert 0<=v["boxWidth"]-v["capWidth"]<=2.6,(width,density,"width delta",v)
        assert v["workColor"]==v["rulerColor"]=="rgb(38, 57, 68)",(
            width,density,"ambiguous gain ruler surface",v)
        assert "linear-gradient" in v["pocket"] and "inset" in v["depth"],(
            width,density,"recess lost",v)
        assert "inset" in v["rim"],(width,density,"machined rim lost",v)
        assert abs(float(v["slotWidth"].replace("px",""))-5)<.6,(width,density,"dark slot drift",v)
        assert "rgb(1, 4, 7)" in v["slotBackground"],(
            width,density,"center aperture not near-black",v)
        assert v["thumbShadow"]!="none" and "9px" in v["thumbShadow"] and "13px" in v["thumbShadow"],(
            width,density,"visible low-cost lower thumb shadow missing",v)
        assert v["thumbFilter"]=="none",(width,density,"GPU filter forbidden",v)
        assert v["thumbTransition"]=="0s",(width,density,"animated shadow forbidden",v)
        assert abs(v["meterWidth"]-70)<.7 and abs(v["meterFillWidth"]-6)<.8,(
            width,density,"approved V51 meter geometry changed",v)
        assert 1<=v["gainRulerGap"]<=4.5 and 1<=v["meterPocketGap"]<=4.5,(
            width,density,"fader/meters collision",v)
        assert v["count"]>=8,(width,density,"gain ruler labels missing",v)

def pointer_drag(page):
    fader=page.locator('#tracks .strip[data-id="1"] .fader-box')
    native=page.locator('#tracks .strip[data-id="1"] input.fader')
    bb=fader.bounding_box()
    assert bb and bb['height']>100,bb
    x=bb['x']+bb['width']*.5
    y0=bb['y']+bb['height']*.72
    y1=bb['y']+bb['height']*.32
    page.mouse.move(x,y0)
    page.mouse.down()
    page.mouse.move(x,y1,steps=5)
    page.mouse.up()
    assert float(native.input_value())>-20,('pointer-to-dB drag disconnected',native.input_value())

def main():
    audit_checkpoint()
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda err:errors.append(str(err)))
            page.goto(HTML.as_uri())
            page.wait_for_selector('#tracks .strip[data-id="1"] .fader-travel .cap')
            for density in ("comfort","standard","compact"):
                inspect(page,width,height,density)
            page.locator("#densitySelect").select_option("comfort")
            pointer_drag(page)
            if width==1680:
                page.screenshot(path="pages-v52-fader-well-console-1680.png")
                page.locator('#tracks .strip[data-id="1"] .fader-workspace').screenshot(
                    path="pages-v52-fader-well-detail-1680.png")
            assert not errors,(width,height,errors)
            page.close()
        browser.close()
    print("PASS V52: V51 meter frozen, flat ruler/chassis, inset pocket, 29/27/25px box,"
          " black 5px slot, three-layer shadow depth and real pointer drag")

if __name__=="__main__":
    main()
