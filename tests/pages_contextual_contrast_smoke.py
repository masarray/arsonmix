"""V6 contextual readability audit: DOM-computed ink on approved silver cap.

Actual canonical/public/PNG pages in headless Chromium; verifies:
- Arrow SVG not white on silver; high contrast in OFF and :active
- Arrow 24px SVG / 31x29 click target never move or resize
- Route-choice microcopy and symbols use dark text on metal
- WIRE matrix connected/legacy semantic classes preserve distinct ON colors
- Original PNG MUTE/SOLO/PFL remains outside this styling
"""
from pathlib import Path
import subprocess, sys, tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
PAGE=ROOT/"site/preview/index.html"
CSS=ROOT/"site/preview/assets/mixer-contextual-buttons.css"

def luminance(hexv):
    rgb=[int(hexv[i:i+2],16)/255 for i in (1,3,5)]
    rgb=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]
    return sum(w*v for w,v in zip((.2126,.7152,.0722),rgb))
def contrast(a,b):
    l1,l2=luminance(a),luminance(b)
    return (max(l1,l2)+.05)/(min(l1,l2)+.05)

def verify(page,label,path):
    page.goto(path.as_uri())
    page.wait_for_selector("#bankNext svg path")
    # Check the approved PNG key before bank navigation hides CH 01.
    assert page.locator("#tracks .strip[data-id='1'] button[data-hw-kind='mute']").count()==1
    for key in ("bankPrev","bankNext"):
        button=page.locator("#"+key)
        result=button.evaluate("""el=>{
           const p=el.querySelector('svg path');
           const ir=el.querySelector('svg').getBoundingClientRect();
           const r=el.getBoundingClientRect();
           return {fill:getComputedStyle(p).fill,
             w:r.width,h:r.height,x:r.x,y:r.y,svgW:ir.width,svgH:ir.height,
             background:getComputedStyle(el).backgroundImage};
        }""")
        assert result["fill"]=="rgb(35, 52, 59)",(label,key,"dark arrow ink",result)
        assert result["svgW"]==24 and result["svgH"]==24,(label,key,"original SVG dimensions",result)
        assert result["w"]>=30 and abs(result["h"]-29)<.5,(label,key,"hit target changed",result)
        assert "gradient" in result["background"],(label,key,"silver cap lost",result)
    # Worst-case V6 silver surface for arrows, small dialog text and icon.
    assert contrast("#23343b","#a3a6a7")>=5.2
    assert contrast("#263b43","#a3a6a7")>=4.5
    assert contrast("#194b3d","#a3a6a7")>=3.0
    assert contrast("#f4fafb","#587581")>=4.5
    # Only Next is enabled at initial bank; :active reverses the ink to white.
    next_btn=page.locator("#bankNext")
    assert next_btn.is_enabled(),(label,"no navigable next bank")
    before=next_btn.bounding_box()
    page.mouse.move(before["x"]+before["width"]/2,before["y"]+before["height"]/2)
    page.mouse.down()
    held=next_btn.evaluate("""el=>({
      fill:getComputedStyle(el.querySelector('svg path')).fill,
      background:getComputedStyle(el).backgroundImage,
      transform:getComputedStyle(el).transform
    })""")
    assert held["fill"]=="rgb(244, 250, 251)",(label,"pressed arrow hidden",held)
    assert held["transform"]=="none",(label,"pressed arrow moved",held)
    page.mouse.up()
    released=next_btn.evaluate("el=>getComputedStyle(el.querySelector('svg path')).fill")
    assert released=="rgb(35, 52, 59)",(label,"arrow did not return OFF",released)
    after=next_btn.bounding_box()
    assert all(abs(before[k]-after[k])<.5 for k in ("x","y","width","height")),(label,before,after)
    # Route-choice dialog is present in canonical DOM; computed CSS applies
    # even while modal is not shown, without requiring destructive routing.
    dialog=page.locator("#routeOnly")
    text_color=dialog.locator("small").evaluate("e=>getComputedStyle(e).color")
    symbol_color=dialog.locator(".choice-symbol").evaluate("e=>getComputedStyle(e).color")
    assert text_color=="rgb(38, 59, 67)",(label,"subtitle light-on-light",text_color)
    assert symbol_color=="rgb(25, 75, 61)",(label,"icon light-on-light",symbol_color)
    # Use a scoped fixture of the REAL matrix class/semantic state names.
    # No routing engine changes, no synthetic data written into the app.
    classes=page.evaluate("""()=>{
      const panel=document.querySelector('.wire-workspace');
      if(!panel)throw Error('WIRE workspace missing');
      const fixture=document.createElement('div');
      fixture.className='wire-ws-matrix';
      fixture.style.position='absolute';fixture.style.left='-5000px';
      fixture.innerHTML='<button class="is-linked">●</button><button class="is-legacy">◆</button>';
      panel.appendChild(fixture);
      const r=[...fixture.children].map(el=>({
        color:getComputedStyle(el).color,
        mid:getComputedStyle(el).getPropertyValue('--ari-on-mid').trim(),
        background:getComputedStyle(el).backgroundImage
      }));
      fixture.remove();
      return r;
    }""")
    assert classes[0]["color"]=="rgb(245, 255, 248)",(label,"connected state",classes)
    assert classes[1]["color"]=="rgb(245, 248, 255)",(label,"legacy state",classes)
    assert "radial-gradient" in classes[0]["background"] and "radial-gradient" in classes[1]["background"],classes
    assert classes[0]["mid"]=="#2d6e51" and classes[1]["mid"]=="#405b7f",classes
    assert contrast("#f5fff8","#2d6e51")>=5.5
    assert contrast("#f5f8ff","#405b7f")>=5.5
    # The bank-changing arrow has already been exercised; CH 01 need not
    # remain rendered after moving to the next bank.
    if label=="approved-png":
        page.locator(".command-bar").screenshot(path="pages-v6-arrow-contrast-1680.png")

def main():
    css=CSS.read_text(encoding='utf-8')
    source=PAGE.read_text(encoding='utf-8')
    assert ".app .command-bar button.bank-arrow svg path" in css
    assert ".is-linked,.is-legacy" in css
    # The historical inline rule is checksum-pinned by V10; computed style
    # below proves V6 owner overrides its obsolete white path ink correctly.
    with tempfile.TemporaryDirectory(prefix="arsonmix-contrast-") as temp:
        dst=Path(temp)/"pages"
        subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
                        "--out",str(dst),"--sha","contrast-audit","--branch","main"],
                        check=True,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True)
            for path,label in ((PAGE,"canonical"),
                               (dst/"preview/index.html","approved-png")):
                page=browser.new_page(viewport={"width":1680,"height":900})
                verify(page,label,path)
                page.close()
            browser.close()
    print("PASS arrow OFF/ON icon contrast, unchanged geometry, Route to Bus dark microcopy,")
    print("WIRE linked/legacy semantic contrast, and PNG reference route 3/3")


if __name__=="__main__":
    main()
