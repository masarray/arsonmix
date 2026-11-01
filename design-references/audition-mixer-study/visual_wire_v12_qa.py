#!/usr/bin/env python3
"""V12 real-Chromium screenshot and geometry acceptance for the isolated ArSonMix
operator-first wire routing reference. NOT a native audio functional test.

Run locally from a checkout (no GitHub Actions required):
    python -m pip install playwright
    python -m playwright install chromium
    python visual_wire_v12_qa.py --screenshots
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from playwright.sync_api import sync_playwright

SOURCE = Path(__file__).with_name("index.html")
VIEWPORTS = [(1366, 768), (1680, 900), (1920, 1080)]
DENSITIES = ("comfort", "standard", "compact")
MEASURE = r"""() => {
 const root=document.querySelector('.console-body'),rail=root.querySelector('.section-rail');
 const strips=[...document.querySelectorAll('#tracks .strip')];
 const master=document.querySelector('#master .strip');
 const bounds=el=>el?.getBoundingClientRect();
 const problems=[],rt=el=>Math.round(el*10)/10;
 const names=[['route','.route-section'],['fx','.fx-section'],
              ['send','.send-section'],['pan','.pan-section'],
              ['switch','.switch-section']];
 if(!strips.length||!master)return {problems:['Missing mixer strips or master']};
 for(const [key,sel] of names){
  const left=bounds(rail.querySelector('.rail-cell.'+key));
  const ch=bounds(strips[0].querySelector(sel)),m=bounds(master.querySelector(sel));
  if(!left||!ch||!m){problems.push('Missing '+key+' physical section');continue;}
  if(Math.abs(left.top-ch.top)>2||Math.abs(left.bottom-ch.bottom)>2||
     Math.abs(ch.top-m.top)>2||Math.abs(ch.bottom-m.bottom)>2)
    problems.push('Section alignment: '+key+' rail '+rt(left.top)+'-'+rt(left.bottom)
      +' channel '+rt(ch.top)+'-'+rt(ch.bottom));
 }
 const f=bounds(strips[0].querySelector('.fader-section'));
 const fx=bounds(strips[0].querySelector('.fx-section'));
 const send=bounds(strips[0].querySelector('.send-section'));
 const wire=bounds(strips[0].querySelector('.wire-tray'));
 if(wire&&send&&(wire.top<send.top-1||wire.bottom>send.bottom+1))
  problems.push('Wire row exits its section');
 if(root.classList.contains('collapsed-send')&&wire&&wire.height<23)
  problems.push('Wire row hidden by collapsed advanced SEND');
 if(document.getElementById('viewport').scrollWidth>
     document.getElementById('viewport').clientWidth+2)
  problems.push('Horizontal source bank overflow');
 const width=bounds(strips[0]).width,masterWidth=bounds(master).width;
 if(Math.abs(masterWidth-width)>1.5)problems.push('Pinned MAIN not equal width');
 return {problems,railWidth:rt(bounds(rail).width),width:rt(width),
  faderHeight:rt(f.height),fxHeight:rt(fx.height),sendHeight:rt(send.height),
  wireHeight:wire?rt(wire.height):0,
  cablePathCount:document.querySelectorAll('#wireLayer path').length,
  links:document.querySelectorAll('#tracks .wire-tray.linked').length,
  sourceIndicator:document.getElementById('wireLegend').textContent,
  collapse:root.className,mode:root.dataset.density};
}"""

def metrics(page, label, failures):
    result=page.evaluate(MEASURE)
    for problem in result["problems"]:
        failures.append(f"{label}: {problem}")
    print(json.dumps({"scenario":label, **result},ensure_ascii=False))
    return result

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--screenshots",action="store_true")
    p.add_argument("--out",type=Path,default=Path(__file__).with_name("screenshots-v12"))
    opt=p.parse_args()
    if not SOURCE.is_file():raise SystemExit(f"Missing {SOURCE}")
    if opt.screenshots:opt.out.mkdir(parents=True,exist_ok=True)
    failures=[]
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in VIEWPORTS:
            page=browser.new_page(viewport={"width":width,"height":height},
                                  device_scale_factor=1,reduced_motion="reduce")
            page.on("pageerror",lambda error:failures.append("JS runtime "+str(error)))
            for density in DENSITIES:
                page.goto(SOURCE.resolve().as_uri(),wait_until="load")
                page.locator("#densitySelect").select_option(density)
                if density=="compact" and page.locator(".console-body").get_attribute("class").find("narrow")>=0:
                    # Compact is intentionally just a view size, not a routing mode.
                    pass
                page.locator('#bankSelect').select_option('0')
                prefix=f"{width}x{height}-{density}"
                before=metrics(page,prefix+"-default",failures)
                if opt.screenshots:
                    page.screenshot(path=str(opt.out/(prefix+"-default.png")))
                # Cross-section reclaim: route collapse gives space to FX, not fader.
                page.locator('.section-rail [data-section="route"]').click()
                closed=metrics(page,prefix+"-route-collapsed",failures)
                if abs(closed["faderHeight"]-before["faderHeight"])>2:
                    failures.append(prefix+": route collapsed moved fader")
                if closed["fxHeight"]<before["fxHeight"]+20:
                    failures.append(prefix+": route collapsed did not enlarge FX rack")
                page.locator('.section-rail [data-section="route"]').click()
                # Normal routing and two-click shift-range.
                page.locator('#tracks .strip[data-id="1"] .strip-identity-top').click()
                page.locator('#tracks .strip[data-id="3"] .wire-socket').click()
                page.locator('#tracks .strip[data-id="6"] .wire-socket').click(modifiers=['Shift'])
                wired=metrics(page,prefix+"-CH1-to-CH3-6",failures)
                if wired["links"]!=4:
                    failures.append(prefix+": four links were not created")
                if wired["cablePathCount"]<8:
                    failures.append(prefix+": expected 4 two-layer SVG cables")
                # Gain independent; source must not change when operating the pot.
                page.locator('#tracks .strip[data-id="3"] .wire-gain').focus()
                page.keyboard.press('ArrowDown')
                gain=page.locator('#tracks .strip[data-id="3"] .wire-gain').input_value()
                if float(gain)!=-1:failures.append(prefix+": keyboard gain step failed: "+gain)
                if "CH01" not in page.locator("#wireLegend").inner_text():
                    failures.append(prefix+": gain changed wire source")
                if opt.screenshots:
                    page.screenshot(path=str(opt.out/(prefix+"-four-wires.png")))
                # Source direct Main toggles independently from wire links.
                page.locator('#wireMaster').click()
                if not page.locator('#wireMaster').inner_text().endswith('OFF'):
                    failures.append(prefix+": explicit Master detach failed")
                if page.locator('#tracks .wire-tray.linked').count()!=4:
                    failures.append(prefix+": Master toggle cleared wire sends")
                page.close()
                page=browser.new_page(viewport={"width":width,"height":height},
                                       device_scale_factor=1,reduced_motion="reduce")
                page.on("pageerror",lambda err:failures.append("JS runtime "+str(err)))
            page.close()
        browser.close()
    if failures:
        print("\nFAIL",len(failures),"rendered problems")
        for err in failures:print(" -",err)
        return 1
    print("\nPASS rendered Chromium geometry and routing interactions.")
    print("Screenshots still require human assessment for aesthetics; audio is simulated.")
    return 0

if __name__=="__main__":raise SystemExit(main())
