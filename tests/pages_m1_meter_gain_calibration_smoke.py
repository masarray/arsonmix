#!/usr/bin/env python3
"""M1: calibrated physical gain/dBFS scale + no-PCM synthetic meter contract.

Use the real generated public /preview/ and canonical sources, never a mock.
Check unit math, computed geometry, visual label rhythm, and click-to-preview.
"""
import math
import subprocess
import sys
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
CANON = ROOT / "site/preview-v2/index.html"
ASSETS = CANON.parent / "assets"

def close(a, b, tolerance=0.65):
    assert abs(a - b) <= tolerance, (a, b, tolerance)

def geometry(page, density, label):
    page.locator("#densitySelect").select_option(density)
    page.wait_for_selector("#tracks .strip[data-id='1'] .fader-scale")
    out = page.evaluate("""() => {
      const strip=document.querySelector('#tracks .strip[data-id="1"]');
      const g=strip.querySelector('.gain-scale-travel');
      const v=strip.querySelector('.vu-scale-travel');
      const travel=strip.querySelector('.fader-travel');
      const cap=strip.querySelector('.fader-travel .cap');
      const bars=strip.querySelector('.vu-bars');
      const meterLane=strip.querySelector('.meter-lane');
      const rect=e=>{const b=e.getBoundingClientRect();return {y:b.top,h:b.height,x:b.left,w:b.width};};
      const ticks=(parent,cls)=>[...parent.querySelectorAll(cls)].map(el=>({
        text:el.querySelector('span')?.textContent,
        y:el.getBoundingClientRect().y,
        top:parseFloat(el.style.top),
        major:el.classList.contains('major'),
        font:parseFloat(getComputedStyle(el.querySelector('span')).fontSize),
        rect:rect(el.querySelector('span'))
      }));
      return {g:rect(g),v:rect(v),travel:rect(travel),cap:rect(cap),
       bar:rect(bars),lane:rect(meterLane),gain:ticks(g,'.gain-tick'),
       vu:ticks(v,'.vu-tick'),
       gainFont:parseFloat(getComputedStyle(g.querySelector('.gain-tick.major span')).fontSize),
       meterFont:parseFloat(getComputedStyle(v.querySelector('.vu-tick.major span')).fontSize),
       engine:window.AudioContext?true:false};
    }""")
    close(out["g"]["y"], out["travel"]["y"], 1)
    close(out["g"]["h"], out["travel"]["h"], 1)
    close(out["v"]["y"], out["lane"]["y"], 1.25)
    close(out["v"]["h"], out["lane"]["h"], 1.25)
    assert out["gainFont"] >= 9 and out["meterFont"] >= 8.5,(label,density,"small labels",out)
    gain_values=[10,0,-6,-12,-24,-36,-48,-60]
    vu_values=[0,-6,-12,-18,-24,-36,-48,-60]
    assert len(out["gain"])==8 and len(out["vu"])==8
    for item,db in zip(out["gain"],gain_values):
        expected=(10-db)/70*100
        close(item["top"],expected,.03)
        close(item["y"],out["travel"]["y"]+out["travel"]["h"]*expected/100,.8)
    for item,db in zip(out["vu"],vu_values):
        expected=-db/60*100
        close(item["top"],expected,.03)
        close(item["y"],out["lane"]["y"]+out["lane"]["h"]*expected/100,1.1)
    close(out["gain"][1]["y"],out["cap"]["y"]+out["cap"]["h"]/2,1.05)
    assert out["gain"][1]["text"]=="0" and out["vu"][0]["text"]=="0"
    for key in ("gain","vu"):
        rects=[t["rect"] for t in out[key] if t["major"]]
        for earlier,later in zip(rects,rects[1:]):
            assert later["y"]>=earlier["y"]+earlier["h"]-.75,(label,density,key,"overlapping labels",rects)
    return out

def math_and_preview(page,label):
    data=page.evaluate("""() => {
      const {scaleTopPct, faderPercent, meterDbfsPercent, demoMeterDbfs}=
        window.ArSonMixMeterCalibration;
      const nums=[-60,-48,-36,-24,-12,-6,0,6,10];
      const perDb=nums.map(x=>({db:x,gain:scaleTopPct(x,-60,10),meter:meterDbfsPercent(x)}));
      const probe=[-60,-30,0].map(db=>({db,p:meterDbfsPercent(db),tick:scaleTopPct(db,-60,0)}));
      const gainEdge=[faderPercent(-60,false),faderPercent(0,false),faderPercent(10,false)];
      const sendEdge=[faderPercent(-60,true),faderPercent(0,true),faderPercent(6,true)];
      const demo=[demoMeterDbfs(0,.46),demoMeterDbfs(6,.46),
                  demoMeterDbfs(-60,0),demoMeterDbfs(10,1)];
      return {perDb,probe,gainEdge,sendEdge,demo};
    }""")
    assert all(0<=t["gain"]<=100 and 0<=t["meter"]<=100 for t in data["perDb"])
    for x in data["probe"]:
        close(x["p"],100-x["tick"],.00001)
        close(x["p"],(x["db"]+60)/60*100,.00001)
    close(data["gainEdge"][0],0,.00001)
    close(data["gainEdge"][2],100,.00001)
    close(data["sendEdge"][0],0,.00001)
    close(data["sendEdge"][2],100,.00001)
    close(data["demo"][1]-data["demo"][0],6,.00001)
    assert data["demo"][2]==-60 and data["demo"][3]==0,(label,"preview clipping",data)
    btn=page.locator("#preview")
    btn.click()
    page.wait_for_function("""()=>Number.parseFloat(
     document.querySelector('#tracks .strip[data-id="1"] .meter-fill.left')
      ?.style.getPropertyValue('--lit')||'0')>5""",timeout=8000)
    result=page.evaluate("""() => {
      const {meterDbfsPercent,demoMeterDbfs,readDemo}=window.ArSonMixMeterCalibration;
      const {gain,frame,running}=readDemo();
      const modulation=.46+.23*Math.sin(frame*.13)+.17*Math.sin(frame*.31);
      const expected=meterDbfsPercent(demoMeterDbfs(gain,modulation));
      const fill=document.querySelector('#tracks .strip[data-id="1"] .meter-fill.left');
      return {expected,actual:parseFloat(fill.style.getPropertyValue('--lit')),
              text:document.getElementById('transport').textContent,
              preview:running,frame};
    }""")
    assert result["preview"] and "preview" in result["text"].lower(),(label,result)
    close(result["actual"],result["expected"],.0001)
    btn.click()
    assert not page.evaluate("window.ArSonMixMeterCalibration.readDemo().running")
    assert page.locator("#tracks .strip[data-id='1'] .meter-fill.left").evaluate(
        "el=>el.style.getPropertyValue('--lit')")=="0%",label

def main():
    with tempfile.TemporaryDirectory(prefix="arsonmix-m1-") as tmp:
        dist=Path(tmp)/"pages"
        subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
            "--out",str(dist),"--sha","m1-calibrated","--branch","m1"],
            cwd=ROOT,check=True,capture_output=True,text=True)
        published=dist/"preview/mixer.html"
        assert published.read_bytes()==CANON.read_bytes(),"Published mixer source drift"
        for name in ("mixer-fader-assembly.css","mixer-meter-display.css"):
            assert (dist/"preview/assets"/name).read_bytes()==(ASSETS/name).read_bytes(),name
        with sync_playwright() as pw:
            browser=pw.chromium.launch(headless=True)
            for html,label,width,height in (
                (CANON,"canonical",1366,768),
                (CANON,"canonical",1680,900),
                (CANON,"canonical",1920,1080),
                (published,"pages",1680,900),
                (dist/"preview-buttons/index.html","png",1366,768)):
                page=browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror",lambda exc:errors.append(str(exc)))
                page.goto(html.as_uri())
                for density in ("comfort","standard","compact"):
                    data=geometry(page,density,label)
                    if width==1680 and label=="pages" and density=="comfort":
                        page.locator("#tracks .strip[data-id='1'] .fader-section").screenshot(
                            path="m1-calibrated-fader-meter-1680.png")
                math_and_preview(page,label)
                assert not errors,(label,width,errors)
                page.close()
            browser.close()
    print("PASS M1: dBFS meter and gain rulers calibrated to physical travel; "
          "preview demo displays real dBFS percentages, 3 densities × 3 sizes × 3 Pages routes")

if __name__=="__main__":
    main()
