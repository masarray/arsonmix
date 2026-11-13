"""V60 approved mixer: tactile unity fader and physical full-span WIRE.
Fast --source contract runs without Playwright; browser mode tests actual DOM,
mouse gestures, responsive rail/Master geometry and SVG path animation.
This is reference GUI evidence, never native PCM or ASIO FAT.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
HTML=(ROOT/"site/preview-v2/index.html").read_text("utf-8")
FADER=(ROOT/"site/preview-v2/assets/mixer-fader-assembly.css").read_text("utf-8")
WIRE=(ROOT/"site/preview-v2/assets/mixer-wire-dots.css").read_text("utf-8")


def source_contract():
    for text in ("FADER_UNITY_CAPTURE_PX=7,FADER_UNITY_RELEASE_PX=12",
                 "gesture.unityLatched=true", "if(!ev.altKey&&Math.abs",
                 "handleField({target:g.input,type:'change'",
                 "WIRE_IMPACT_TRAVEL_MS=460,WIRE_IMPACT_SETTLE_MS=190",
                 "const center=.035+.93*sweep",
                 "const tether=Math.min(1,t/.035,(1-t)/.035)",
                 "line.style.setProperty('--wire-selected-hue'"):
        assert text in HTML,("missing tactile contract",text)
    assert "elapsed<1600" not in HTML
    assert "--fader-fixed-h:clamp(270px,35vh,390px)" in FADER
    assert "--fader-fixed-h:clamp(175px,32vh,210px)" in FADER
    assert "stroke:transparent!important;" in WIRE
    assert "stroke-width:5.6px!important;opacity:1!important;" in WIRE
    assert "stroke-dasharray:none!important;" in WIRE
    print("PASS V60 fast source contract: detent / tall rail / full-span impulse / selected jacket")


def point_at_midpoint(path):
    return path.evaluate("""el=>{
        const pt=el.getPointAtLength(el.getTotalLength()*.48);
        const svg=el.ownerSVGElement,r=svg.getBoundingClientRect(),v=svg.viewBox.baseVal;
        return {x:r.left+(pt.x-v.x)*r.width/v.width,
                y:r.top+(pt.y-v.y)*r.height/v.height};
    }""")


def exercise(page,width,height):
    page.goto((ROOT/"dist-pages/preview/index.html").as_uri())
    page.locator('#tracks .strip[data-id="1"] .fader-box').wait_for()
    layout=page.evaluate("""()=> {
      const r=q=>{const n=document.querySelector(q);if(!n)return null;
        const b=n.getBoundingClientRect();return {y:b.y,h:b.height,bottom:b.bottom};};
      return {fader:r('#tracks .strip[data-id="1"] .fader-section'),
              master:r('#master .strip .fader-section'),
              rail:r('.section-rail .rail-cell.fader'),
              travel:r('#tracks .strip[data-id="1"] .fader-travel'),
              fx:r('#tracks .strip[data-id="1"] .fx-section'),
              footer:r('#tracks .strip[data-id="1"] .bottom-section'),
              token:getComputedStyle(document.querySelector('.console-body')).getPropertyValue('--fader-fixed-h').trim(),
              flex:getComputedStyle(document.querySelector('#tracks .strip[data-id="1"] .fader-section')).flexBasis};
    }""")
    a,b,c=layout["fader"],layout["master"],layout["rail"]
    assert all(x and x["h"]>0 for x in (a,b,c)),layout
    assert abs(a["h"]-b["h"])<2 and abs(a["h"]-c["h"])<2,(width,layout)
    min_height=300 if height>=1000 else (275 if height>=850 else 235)
    assert a["h"]>=min_height,(width,"tall hardware fader lower bound",layout)
    print("V60 geometry",width,height,"fader",a["h"],"CSS token",layout["token"],"flex",layout["flex"])
    assert layout["travel"]["h"]>a["h"]-110,(width,layout)
    assert layout["fx"]["h"]>=23,(width,"FX cell disappeared",layout)
    assert a["bottom"]<layout["footer"]["bottom"],(width,"fader overlaps footer",layout)

    strip=page.locator('#tracks .strip[data-id="1"]')
    box=strip.locator(".fader-box").bounding_box()
    travel=strip.locator(".fader-travel").bounding_box()
    fader=strip.locator('input[data-field="level"]')
    x=box["x"]+box["width"]/2
    zero=travel["y"]+travel["height"]*(10/70)
    page.mouse.move(x,zero+5)
    page.mouse.down()
    assert abs(float(fader.input_value()))<.001,(width,"unity detent failed")
    page.mouse.move(x,zero+10)
    assert abs(float(fader.input_value()))<.001,(width,"unity detent hold failed")
    page.mouse.up()
    assert abs(float(fader.input_value()))<.001,(width,"unity release failed")
    page.mouse.move(x,zero+5)
    page.mouse.down()
    page.mouse.move(x,zero+22)
    page.mouse.up()
    assert float(fader.input_value())<-.3,(width,"fader trapped at zero")
    page.keyboard.down("Alt")
    page.mouse.move(x,zero+5)
    page.mouse.down()
    page.mouse.up()
    page.keyboard.up("Alt")
    assert abs(float(fader.input_value()))>.25,(width,"Alt must bypass detent")
    # Restored unity can be stored and re-rendered without display rounding.
    page.mouse.move(x,zero)
    page.mouse.down()
    page.mouse.up()
    assert abs(float(fader.input_value()))<.001
    assert "0.0" in strip.locator(".fader-readout").inner_text()
    return layout


def test_wire(page):
    # The SEND bay can be folded by default on the assembled preview.
    # Use the actual operator rail to reveal the physical sockets.
    body=page.locator(".console-body")
    if "collapsed-send" in (body.get_attribute("class") or ""):
        page.locator('.section-rail [data-section="send"]').click()
    source=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
    target=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
    assert source.count()==target.count()==1
    # Listen before a user-originated WIRE acceptance. The observer records
    # only real outgoing jacket path mutations, no synthetic second renderer.
    page.evaluate("""()=>{
      window.__v60WireSamples=[];
      const svg=document.querySelector('#wireLayer');
      window.__v60Observer=new MutationObserver(records=>{
        for(const record of records){
          const el=record.target;
          if(!el.matches?.('path.wire-outgoing-line[data-from="1"][data-to="3"]'))continue;
          const d=el.getAttribute('d'),len=el.getTotalLength();
          const p=el.getPointAtLength(len*.90),a=el.getPointAtLength(0),
                b=el.getPointAtLength(len);
          window.__v60WireSamples.push({d,near:[p.x,p.y],start:[a.x,a.y],end:[b.x,b.y]});
        }
      });
      window.__v60Observer.observe(svg,{subtree:true,attributes:true,attributeFilter:['d']});
    }""")
    # CH01 -> CH03: simple explicit parallel graph patch, not PCM.
    source.click()
    target.click()
    cable=page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="3"]')
    cable.wait_for(timeout=8000)
    page.wait_for_timeout(850)
    samples=page.evaluate("window.__v60WireSamples")
    assert len(samples)>=5,("no finite spring movement",len(samples))
    assert any(" L " in x["d"] for x in samples),("no cable impact",len(samples))
    settled=cable.get_attribute("d")
    assert " C " in settled,("must return to exact canonical cubic",settled[:90])
    canonical=cable.evaluate("""el=>{const p=el.getPointAtLength(el.getTotalLength()*.90);
                                  return [p.x,p.y];}""")
    far=max(((x["near"][0]-canonical[0])**2+
             (x["near"][1]-canonical[1])**2)**.5 for x in samples)
    assert far>1.0,("wave never reached receiving end",far)
    for axis in ("start","end"):
        coords=[x[axis] for x in samples]
        assert max(abs(p[j]-coords[0][j]) for p in coords for j in (0,1))<.3,\
            ("motion detached endpoint",axis,coords[:3])

    hit=page.locator('#wireLayer path.wire-interaction-hit[data-cable-key="1-3"]')
    assert hit.count()==1
    mid=point_at_midpoint(hit)
    page.mouse.click(mid["x"],mid["y"])
    selected=page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="3"].is-wire-selected')
    assert selected.count()==1,"Selected edge must brighten its real visible jacket"
    values=selected.evaluate("""el=>{
      const c=getComputedStyle(el);
      const hit=document.querySelector('#wireLayer .wire-interaction-hit.is-wire-selected');
      return {width:parseFloat(c.strokeWidth),stroke:c.stroke,
              original:el.getAttribute('stroke'),dash:c.strokeDasharray,
              filter:c.filter,hit:getComputedStyle(hit).stroke};
    }""")
    assert values["width"]>=5.5 and values["dash"]=="none",values
    assert values["stroke"]!=values["original"],values
    assert values["filter"]=="none",values
    assert values["hit"] in ("transparent","rgba(0, 0, 0, 0)"),values
    # Re-selecting does not alter source/destination graph identity.
    assert hit.get_attribute("data-edge-id")
    page.evaluate("window.__v60Observer.disconnect()")


def run():
    from playwright.sync_api import sync_playwright
    subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
                    "--out",str(ROOT/"dist-pages"),"--sha","v60-tactile-test",
                    "--branch","gui-test"],cwd=ROOT,check=True,capture_output=True,text=True)
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        try:
            for width,height in ((640,1005),(1680,900),(1366,768)):
                page=browser.new_page(viewport={"width":width,"height":height})
                errs=[]
                page.on("pageerror",lambda error:errs.append(str(error)))
                exercise(page,width,height)
                if width==1680:test_wire(page)
                if width in (640,1680):
                    page.screenshot(path=f"arsonmix-v60-fader-wire-{width}.png",full_page=False)
                assert not errs,(width,errs)
                page.close()
        finally:
            browser.close()
    print("PASS V60 Chromium: taller source/Master, unity detent, fast full-span WIRE, no selected halo")


if __name__=="__main__":
    source_contract()
    if "--source" not in sys.argv:run()
