"""V18 GUI visual engineering: 1:1 micro buttons, stable Google Material
icon states, aligned PAN/SEND thumb, and calibrated non-overlapping dB scales.
Tests canonical + two published routes using REAL Chromium / mixer controls.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"site/preview-v2/index.html"
ASSETS=ROOT/"site/preview-v2/assets"

def same_box(a,b):
    return all(abs(a[k]-b[k])<=0.6 for k in ("x","y","width","height"))

def check(page,path,label):
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.goto(path.as_uri())
    page.wait_for_selector("#tracks .strip[data-id='1'] .fxslot-row button[data-util-kind='fx-remove']")
    channel="#tracks .strip[data-id='1']"
    # Never replace the separately approved physical PNG MUTE/SOLO/PFL.
    assert page.locator(channel+" button[data-hw-kind='mute']").count()==1
    buttons=["fx-main","fx-slot","fx-remove","phase"]
    for kind in buttons:
        button=page.locator(channel+" button.mx-util-key[data-util-kind='"+kind+"']").first
        dims=button.bounding_box()
        assert dims is not None and abs(dims["width"]-17)<.6 and abs(dims["height"]-17)<.6,(
            label,kind,"micro icon button is not square 17x17",dims)
        icon=button.locator("svg.mi-svg")
        assert icon.count()==1,(label,kind,"SVG Material icon missing")
        assert icon.get_attribute("viewBox")=="0 0 24 24"
    phase=page.locator(channel+" button.mx-util-key[data-util-kind='phase']")
    icon_path=phase.locator("svg path").get_attribute("d")
    assert icon_path.startswith("M6.99 11L3 15"),(label,"wrong Material swap_horiz icon")
    old=phase.get_attribute("aria-pressed")
    b0=phase.bounding_box()
    page.locator(channel+" button.mx-util-key[data-util-kind='phase']").click()
    phase=page.locator(channel+" button.mx-util-key[data-util-kind='phase']")
    assert phase.get_attribute("aria-pressed")!=old,(label,"phase did not toggle")
    assert phase.locator("svg path").get_attribute("d")==icon_path,(
        label,"phase ON/OFF must retain identical icon geometry")
    assert same_box(b0,phase.bounding_box()),(label,"phase toggled with layout movement")
    phase.click()
    assert page.locator(channel+" button.mx-util-key[data-util-kind='phase']").get_attribute("aria-pressed")==old
    trash=page.locator(channel+" button.mx-util-key[data-util-kind='fx-remove']").first
    assert trash.locator("svg path").get_attribute("d").startswith("M16 9v10H8V9"),(
        label,"FX remove is not Google Material delete icon")
    assert trash.get_attribute("aria-pressed") is None,(label,"remove must be momentary")

    # No dB scale positions or real input values change: only typography and
    # major/minor rendering. The two physical ruler ends match the cap travel.
    for density in ("comfort","standard","compact"):
        page.locator("#densitySelect").select_option(density)
        page.wait_for_selector(channel+" .gain-scale-travel")
        tick=page.evaluate("""(sel)=>{
           const strip=document.querySelector(sel);
           const r=e=>{const x=e.getBoundingClientRect();return {top:x.top,bottom:x.bottom,h:x.height}};
           const gain=strip.querySelector('.gain-scale-travel');
           const cap=strip.querySelector('.fader-travel');
           const vu=strip.querySelector('.vu-scale-travel');
           const bars=strip.querySelector('.vu-bars');
           const majorRects=(parent)=>[...parent.querySelectorAll('.major span')].map(r);
           const overlap=rects=>rects.slice(1).some((v,i)=>v.top<rects[i].bottom-.6);
           const gainMarks=[...gain.querySelectorAll('.gain-tick')];
           const vuMarks=[...vu.querySelectorAll('.vu-tick')];
           return {
             gain:r(gain),cap:r(cap),vu:r(vu),bars:r(bars),
             gainCount:gainMarks.length,vuCount:vuMarks.length,
             gainMajor:gain.querySelectorAll('.gain-tick.major span').length,
             vuMajor:vu.querySelectorAll('.vu-tick.major span').length,
             gainOverlap:overlap(majorRects(gain)),vuOverlap:overlap(majorRects(vu)),
             gainZero:gain.querySelector('.gain-tick.unity')?.querySelector('span')?.textContent,
             vuZero:vu.querySelector('.vu-tick.zero')?.querySelector('span')?.textContent,
             value:strip.querySelector('input.fader')?.value,
             panHeight:strip.querySelector('.center-pan').getBoundingClientRect().height
           };
        }""",channel)
        assert tick["gainCount"]==8 and tick["vuCount"]==8,(label,density,"scale marks missing",tick)
        assert tick["gainMajor"]==7 and tick["vuMajor"]==6,(label,density,"scale density incorrect",tick)
        assert tick["gainZero"]=="0" and tick["vuZero"]=="0",(label,density,"unity/zero mark missing",tick)
        assert abs(tick["gain"]["top"]-tick["cap"]["top"])<=1 and abs(
            tick["gain"]["bottom"]-tick["cap"]["bottom"])<=1,(label,density,"gain ruler miscalibrated",tick)
        assert not tick["gainOverlap"] and not tick["vuOverlap"],(
            label,density,"printed scale numbers collide",tick)
        assert tick["value"]=="0",(label,density,"fader numeric value changed",tick)
        assert abs(tick["panHeight"]-40)<.6,(label,density,"PAN input hitbox moved",tick["panHeight"])
        if density=="comfort" and label=="png":
            page.locator(channel+" .fader-section").screenshot(path="pages-v18-fader-scale-1680.png")
            page.locator(channel+" .fx-section").screenshot(path="pages-v18-fx-micro-1680.png")
    assert not errors,(label,errors)
    page.close()

def main():
    html=SOURCE.read_text("utf-8")
    assert 'materialIcon("delete")' in html
    assert 'materialIcon("swap_horiz")' in html
    assert "(t.phase?'Ø':'⊘')" not in html
    assert "v%12===0" in html and "v!==-6" in html
    css=(ASSETS/"mixer-pan-controls.css").read_text("utf-8")
    assert ".center-pan::-webkit-slider-thumb{margin-top:-12px!important}" in css
    assert ".ari-horizontal::-webkit-slider-thumb{margin-top:-10px!important}" in css
    with tempfile.TemporaryDirectory(prefix="arsonmix-visual-v18-") as temp:
        dist=Path(temp)/"dist"
        subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
                        "--out",str(dist),"--sha","v18-ui-test","--branch","main"],
                       cwd=ROOT,check=True,capture_output=True,text=True)
        with sync_playwright() as playwright:
            browser=playwright.chromium.launch(headless=True)
            for path,label in ((SOURCE,"canonical"),
                               (dist/"preview/mixer.html","public"),
                               (dist/"preview-buttons/index.html","png")):
                page=browser.new_page(viewport={"width":1680,"height":900},device_scale_factor=1)
                check(page,path,label)
            browser.close()
    print("PASS V18: Material delete+phase, 17px square buttons, calibrated rulers,")
    print("no label overlaps across densities, PNG preserved, 3 real preview routes")

if __name__=="__main__":
    main()
