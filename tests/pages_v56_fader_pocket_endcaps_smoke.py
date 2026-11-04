#!/usr/bin/env python3
"""V56: accepted V51 meter untouched; moderate fader recess and overtravel.

Checks the rendered geometry on real Chromium, not just selector text.
Pages is a GUI reference; this does not claim a native audio signal.
"""
from pathlib import Path
import hashlib
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
METER=ROOT/"site/preview-v2/assets/mixer-meter-display.css"
FADER=ROOT/"site/preview-v2/assets/mixer-fader-assembly.css"
METER_LOCK="e168fe3fde3b176ce46f2c637a17db65bc510944"

def blob_sha(path):
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\x00"+raw).hexdigest()

def geometry(page,width,density):
    page.locator("#densitySelect").select_option(density)
    data=page.evaluate("""() => {
      return [document.querySelector('#tracks .strip[data-id="1"]'),
              document.querySelector('#master .strip')].map(root=>{
        const q=selector=>root.querySelector(selector);
        const rect=el=>el.getBoundingClientRect();
        const box=q('.fader-box'),r=rect(box),cap=q('.fader-travel .cap'),
              cr=rect(cap),meter=rect(q('.meter-box')),ruler=rect(q('.fader-scale')),
              p=getComputedStyle(box,'::before'),slot=getComputedStyle(box,'::after'),
              surface=getComputedStyle(q('.fader-workspace')),
              scale=getComputedStyle(q('.fader-scale')),
              boxStyle=getComputedStyle(box),thumb=getComputedStyle(cap);
        return {
          boxWidth:r.width,capWidth:cr.width,boxTop:r.top,boxBottom:r.bottom,
          pocketTop:parseFloat(p.top),pocketBottom:parseFloat(p.bottom),
          pocketWidth:parseFloat(p.width),pocketBg:p.backgroundImage,
          pocketShadow:p.boxShadow,pocketRadius:p.borderRadius,
          boxShadow:boxStyle.boxShadow,boxBg:boxStyle.backgroundColor,
          surfaceBg:surface.backgroundColor,scaleBg:scale.backgroundColor,
          slotTop:parseFloat(slot.top),slotBottom:parseFloat(slot.bottom),
          slotWidth:parseFloat(slot.width),slotBg:slot.backgroundImage,
          thumbShadow:thumb.boxShadow,filter:thumb.filter,
          thumbTransition:thumb.transitionDuration,
          meterWidth:meter.width,meterGap:r.left-meter.right,
          scaleGap:ruler.left-r.right,scaleOverflow:ruler.right-rect(q('.fader-workspace')).right
        };
      });
    }""")
    target={"comfort":(29,27),"standard":(29,27),"compact":(27,25)}[density]
    for item in data:
        assert abs(item["boxWidth"]-target[0])<.8,(width,density,"pocket",item)
        assert abs(item["capWidth"]-target[1])<.8,(width,density,"original cap",item)
        assert abs(item["pocketWidth"]-item["boxWidth"])<1,(width,density,"well width",item)
        assert item["pocketTop"]==item["pocketBottom"]==10,(width,density,"endcaps escape panel",item)
        assert item["slotTop"]>=item["pocketTop"] and item["slotBottom"]>=item["pocketBottom"]
        assert abs(item["slotWidth"]-5)<.8,(width,density,"physical groove",item)
        assert item["boxShadow"]=="none",(width,density,"separate block seam",item)
        assert "linear-gradient" in item["pocketBg"] and "inset" in item["pocketShadow"],(width,density,"moderate bevel",item)
        assert "rgb(30, 48, 58)" in item["pocketBg"],(width,density,"flat middle missing",item)
        assert "rgb(36, 126, 170)" in item["slotBg"],(width,density,"soft cyan tail missing",item)
        assert item["surfaceBg"]==item["scaleBg"]==item["boxBg"]=="rgb(38, 57, 68)",(
            width,density,"disjoint ruler/console surfaces",item)
        assert item["thumbShadow"]!="none" and item["filter"]=="none" and item["thumbTransition"]=="0s",(
            width,density,"shadow must be static",item)
        assert abs(item["meterWidth"]-70)<.8,(width,density,"V51 meter was resized",item)
        assert 1<=item["meterGap"]<=4.5 and 1<=item["scaleGap"]<=4.5,(width,density,"horizontal spacing",item)
        assert item["scaleOverflow"]<=1.5,(width,density,"cropped scale",item)

def overtravel(page):
    # The pocket ends 10px within its instrument but the metal cap may ride
    # past both ends without being clipped by a rounded rectangle.
    strip=page.locator('#tracks .strip[data-id="1"]')
    box=strip.locator('.fader-box')
    cap=strip.locator('.fader-travel .cap')
    ctl=strip.locator('input.fader')
    b=box.bounding_box()
    x=b["x"]+b["width"]/2
    page.mouse.click(x,b["y"]+3)
    assert float(ctl.input_value())>=9.9,("top detent",ctl.input_value())
    top=cap.bounding_box()
    assert top["y"]<=b["y"]+2,("cap cannot cross inset top lip",top,b)
    page.mouse.click(x,b["y"]+b["height"]-3)
    assert float(ctl.input_value())<=-59.9,("bottom detent",ctl.input_value())
    bottom=cap.bounding_box()
    assert bottom["y"]+bottom["height"]>=b["y"]+b["height"]-2,(
        "cap cannot cross inset lower lip",bottom,b)
    page.mouse.click(x,b["y"]+b["height"]*.50)
    assert -60<float(ctl.input_value())<10,("normal fader range broke",ctl.input_value())

def main():
    assert blob_sha(METER)==METER_LOCK,"Owner-approved V51 LED and peak hold changed"
    css=FADER.read_text(encoding="utf-8")
    assert "top:10px!important;bottom:10px!important" in css
    assert "filter:none!important" in css
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.goto(HTML.as_uri())
            page.wait_for_selector('#tracks .strip[data-id="1"] .fader-box')
            for density in ("comfort","standard","compact"):
                geometry(page,width,density)
                if width==1366:
                    page.locator('#tracks .strip[data-id="1"] .fader-workspace').screenshot(
                        path=f"pages-v56-pocket-{density}-1366.png")
            page.locator("#densitySelect").select_option("comfort")
            overtravel(page)
            if width in (1366,1680):
                page.screenshot(path=f"pages-v56-console-{width}.png")
            assert not errors,(width,height,errors)
            page.close()
        browser.close()
    print("PASS V56: calibrated meter unchanged, 10px inset ends, thumb+2px recess,"
          " continuous graphite scale, cyan tail, cap overtravel and static shadows")

if __name__=="__main__":
    main()
