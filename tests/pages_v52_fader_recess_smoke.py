#!/usr/bin/env python3
"""Owner-requested exact rollback to approved pre-V52 fader; V51 meter stays frozen.

Preserve canonical /preview-v2/, /preview/ and PNG public preview source parity.
Browser GUI reference only: no PCM, VST3, routing model, or gain logic change.
"""
import hashlib
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
FADER=ROOT/"site/preview-v2/assets/mixer-fader-assembly.css"
METER=ROOT/"site/preview-v2/assets/mixer-meter-display.css"
APPROVED_V27_METER_BLOB="e168fe3fde3b176ce46f2c637a17db65bc510944"
APPROVED_PRE_V52_FADER_BLOB="8a2eac41b3508e35d8a6c49855512e8a0868b0e6"

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
    assert git_blob(FADER)==APPROVED_PRE_V52_FADER_BLOB, (
        "Owner rejected V52/V54/V55 fader visuals: restore ONLY exact pre-V52 V27 owner CSS"
    )
    css=FADER.read_text(encoding="utf-8")
    assert "filter:none!important" in css
    assert "color-mix(in srgb,var(--track-color,#6fb4c6) 56%,#164959)" in css, (
        "Original under-thumb teal tail light must be retained"
    )
    assert "0 2px 3px -2px #000c,0 4px 3px -4px #000a" in css, (
        "Original low-cost V51 fader shadow must not be replaced by V55 three heavy layers"
    )
    assert '#14232e!important' in css
    assert "mixer-fader-assembly.css?v=css-owner-v27-fader-rollback-pre-v52-tail" in html
    assert "REFERENCE_WORKFLOW_QA_ONLY" in (
        ROOT/"site/preview/candidate.json").read_text(encoding="utf-8"), (
        "Public /preview/ must keep canonical GUI source, not a parallel stale demo"
    )

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
        return {workspaceGradient:ws.backgroundImage,workColor:ws.backgroundColor,rulerColor:sc.backgroundColor,
          boxWidth:rect(box).width,capWidth:rect(cap).width,capHeight:rect(cap).height,
          rim:p.boxShadow,pocket:before.backgroundImage,pocketColor:before.backgroundColor,depth:before.boxShadow,
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
    targetBox,targetCap={"comfort":(32,27),"standard":(27,27),"compact":(25,25)}[density]
    for v in vals:
        assert abs(v["boxWidth"]-targetBox)<.75,(width,density,"pocket width",v)
        assert abs(v["capWidth"]-targetCap)<.75,(width,density,"PNG cap width",v)
        assert abs((v["boxWidth"]-v["capWidth"])-(targetBox-targetCap))<.7,(
            width,density,"original physical fader width drift",v)
        assert "linear-gradient" in v["workspaceGradient"],(
            width,density,"original low-key chassis gradient missing",v)
        assert v["rulerColor"] in ("rgba(0, 0, 0, 0)","transparent"),(
            width,density,"old translucent gain ruler should not be boxed",v)
        assert v["pocket"]=="none" and v["pocketColor"]=="rgb(14, 27, 36)",(
            width,density,"original flat graphite well was replaced",v)
        assert "inset" in v["depth"],(width,density,"original narrow cavity sidewall missing",v)
        assert abs(float(v["slotWidth"].replace("px",""))-5)<.6,(width,density,"dark slot drift",v)
        assert "linear-gradient" in v["slotBackground"],(
            width,density,"original reactive teal tail light was replaced",v)
        assert v["thumbShadow"]!="none" and "2px 3px -2px" in v["thumbShadow"] and "4px 3px -4px" in v["thumbShadow"],(
            width,density,"approved subtle V51 thumb shadow missing",v)
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
                page.screenshot(path="pages-fader-rollback-console-1680.png")
                page.locator('#tracks .strip[data-id="1"] .fader-workspace').screenshot(
                    path="pages-fader-rollback-detail-1680.png")
            assert not errors,(width,height,errors)
            page.close()
        browser.close()
    print("PASS FADER ROLLBACK: V51 meter frozen, exact pre-V52 graphite chassis, 32/27/25px well,"
          " original teal tail, two lightweight shadows and original pointer drag")

if __name__=="__main__":
    main()
