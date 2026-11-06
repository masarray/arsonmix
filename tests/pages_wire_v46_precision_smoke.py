#!/usr/bin/env python3
"""V46: physical dot, socket and cable share exact x/y; LVL owns compact footprint.

Uses generated public /preview/, never only a source or mock renderer.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright
from pages_wire_incoming_v43_smoke import verify_approved_wire_bundle

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
CSS=ROOT/"site/preview-v2/assets/mixer-wire-dots.css"

def center(loc):
 box=loc.bounding_box()
 assert box and box["width"]>0 and box["height"]>0,("missing physical element",loc)
 return (box["x"]+box["width"]/2,box["y"]+box["height"]/2)

def points(svg_path):
 path=svg_path.get_attribute("d") or ""
 numbers=[float(v) for v in re.findall(r"-?\d+(?:\.\d+)?",path)]
 assert len(numbers)>=4,("unreadable SVG path",path)
 return (numbers[0],numbers[1]),(numbers[-2],numbers[-1])

def exact(actual,expected,width,why,tolerance=.28):
 assert abs(actual[0]-expected[0])<tolerance and abs(actual[1]-expected[1])<tolerance,(
  width,why,actual,expected)

def local_point(loc,origin):
 x,y=center(loc)
 return x-origin[0],y-origin[1]

def incoming_colors(sock):
 return sock.evaluate("""el=>{
  const pin=el.querySelector('.wire-arrow');
  return {connected:el.getAttribute('data-wire-arrival')==='true',
   ring:getComputedStyle(el).borderTopColor,
   center:getComputedStyle(pin).backgroundColor,
   opacity:Number(getComputedStyle(pin).opacity)};
 }""")

def main():
 with tempfile.TemporaryDirectory(prefix="arsonmix-v46-") as tmp:
  dist=Path(tmp)/"dist"
  subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
    "--out",str(dist),"--sha","v46-dot-anchors","--branch","v46"],
    cwd=ROOT,capture_output=True,text=True,check=True)
  # Stage C can prune redundant alignment declarations. Verify the compiled
  # alignment owner and runtime digest, then exercise actual dot geometry.
  public=verify_approved_wire_bundle(
      dist,extra_owners=("mixer-wire-out-align-v48.css",))

  with sync_playwright() as pw:
   browser=pw.chromium.launch(headless=True)
   for width in (1366,1680,1920):
    page=browser.new_page(viewport={"width":width,"height":900})
    errors=[];page.on("pageerror",lambda e:errors.append(str(e)))
    page.goto(public.as_uri())
    if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
     page.locator('.section-rail [data-section="send"]').click()
    src=page.locator('#tracks .strip[data-id="5"] .wire-node-out')
    src_pin=src.locator('.wire-pin')
    # V47 shifts the OUT connector and its caption as one unit, not the
    # wire independently. Keep the complete button in its reserved column.
    spacing=src.evaluate("""el=>{
      const css=getComputedStyle(el);return {margin:parseFloat(css.marginInlineStart),
        inner:parseFloat(css.paddingLeft)};}""")
    assert abs(spacing["margin"]-4)<.1,(width,"OUT box margin changed",spacing)
    # V48 shifts the actual dot and caption +3px within the unchanged button
    # hit rectangle, keeping magnetic pointer region and IN spacing stable.
    assert abs(spacing["inner"]-4)<.1,(width,"OUT dot inset should be 4px",spacing)
    caption=src.locator('small')
    if caption.is_visible():
     p=src_pin.bounding_box();t=caption.bounding_box();button=src.bounding_box()
     assert abs(p["x"]-button["x"]-4)<.7,(width,"physical OUT pin not inset 4px",p,button)
     assert abs((t["y"]+t["height"]/2)-(p["y"]+p["height"]/2))<.6,(
         width,"OUT caption not vertically centered with 12px dot",p,t)
     assert t["x"] >= p["x"]+p["width"]+1,(width,"OUT caption overlaps dot",p,t)
     assert t["x"]+t["width"] < page.locator('#tracks .strip[data-id="5"] .wire-node-in').bounding_box()["x"],(width,"OUT caption collides with IN")
    dest=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
    dest_pin=dest.locator('.wire-arrow')
    for channel in (3,5,6):
     labels=page.locator(f'#tracks .strip[data-id="{channel}"] .wire-gain-caption')
     assert labels.count()==1 and labels.inner_text()=="LVL",(width,channel,"LVL caption")

    # Outer ring, INNER dot and magnet center must be the same regardless of
    # V45 3-column layout, caption widths or source/target interaction state.
    exact(center(dest_pin),center(dest),width,"IN dot not at socket circle center",.32)
    vp=page.locator('#viewport').bounding_box()
    assert vp and vp['width']>0 and vp['height']>0,(width,'missing viewport')
    origin=(vp['x'],vp['y'])
    source_center=local_point(src_pin,origin)
    target_center=local_point(dest_pin,origin)
    # A parent button includes OUT label. Its center is NOT the cable anchor.
    button=center(src);pin=center(src_pin)
    if width>=1680:
     assert abs(button[0]-pin[0])>4,(width,"OUT label has no offset; test not exercising V45 layout",button,pin)

    # Begin on actual physical OUT pin, not on its text-bearing button center.
    x0,y0=pin;x1,y1=center(dest_pin)
    page.mouse.move(x0,y0);page.mouse.down()
    page.mouse.move((x0+x1)/2,y0+52,steps=7)
    drag=page.locator('#wireLayer path.wire-drag-line').last
    exact(points(drag)[0],source_center,width,"drag wire starts away from OUT pin")
    assert page.locator('#wireLayer .wire-floating-plug').count()==1,"drag dot follower missing"
    page.mouse.move(x1,y1,steps=7)
    assert "is-snap-target" in (dest.get_attribute("class") or ""),(width,"snap receiver inactive")
    exact(points(drag)[1],target_center,width,"magnetic drag endpoint misses IN center")
    assert page.locator('#wireLayer .wire-floating-plug').count()==0,"snap follower must be hidden"
    page.mouse.up()
    page.mouse.move(x0,y0-72)
    page.wait_for_function("""()=>document.querySelector('#tracks .strip[data-id="3"] .wire-node-in')?.getAttribute('data-wire-arrival')==='true'""")
    path=page.locator('#wireLayer path.wire-outgoing-line[data-from="5"][data-to="3"]')
    page.wait_for_function("""()=>document.querySelector('#wireLayer path.wire-outgoing-line[data-from="5"][data-to="3"]')?.getAttribute('d')?.includes(' C ')""",timeout=5000)
    exact(points(path)[0],source_center,width,"committed OUT wire misses its actual pin")
    exact(points(path)[1],target_center,width,"committed IN wire misses inner socket dot")
    colors=incoming_colors(dest)
    assert colors["connected"] and colors["ring"]!="rgb(57, 74, 85)",(width,"IN ring did not illuminate",colors)
    assert colors["center"]==colors["ring"] and colors["opacity"]==1,(width,"IN dot not lit with circle",colors)

    # Source focus change re-renders the same cable as an INCOMING path;
    # neither physical anchor may shift with SVG class or opacity.
    page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
    incoming=page.locator('#wireLayer path.wire-incoming-line[data-from="5"][data-to="3"]')
    assert incoming.count()==1,(width,"incoming wire disappeared")
    exact(points(incoming)[0],local_point(src_pin,origin),width,"incoming source pin mismatch")
    exact(points(incoming)[1],local_point(dest_pin,origin),width,"incoming socket dot mismatch")
    exact(center(dest_pin),center(dest),width,"socket dot shifted on incoming display",.32)

    # A focus change may hide the cable but never lies about a committed
    # enabled route: socket remains connected while graph is unchanged.
    page.locator('#tracks .strip[data-id="2"] .strip-identity-top').click()
    idle=incoming_colors(dest)
    assert idle["connected"],(width,"focus must not remove accepted graph edge",idle)
    assert idle["ring"]=="rgb(57, 74, 85)" and idle["center"]=="rgb(75, 83, 91)",(width,"hidden cable must leave both ring and center graphite",idle)
    page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
    lit=incoming_colors(dest)
    assert lit["connected"] and lit["center"]==lit["ring"] and lit["center"]!="rgb(75, 83, 91)",(width,"focus return must restore accepted visible endpoint",lit)
    if width==1366:
     page.locator('#densitySelect').select_option(label="Compact")
     exact(center(dest_pin),center(dest),width,"compact socket no longer concentric",.32)
    assert not errors,(width,errors)
    page.close()
   browser.close()
 print("PASS V46: published page exact OUT/IN physical path anchors, concentric colored/idle IN and LVL")

if __name__=="__main__":
 main()
