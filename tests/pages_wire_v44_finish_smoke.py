#!/usr/bin/env python3
"""V44: browser proof on the exact generated public Pages /preview/ artifact."""
from pathlib import Path
import subprocess,sys,tempfile
from playwright.sync_api import sync_playwright
from pages_wire_incoming_v43_smoke import center, impact_peak

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"site/preview-v2/index.html"
CSS=ROOT/"site/preview-v2/assets/mixer-wire-dots.css"

def box(loc):
 r=loc.bounding_box()
 assert r and r["width"]>0 and r["height"]>0,r
 return r

def main():
 with tempfile.TemporaryDirectory(prefix="arsonmix-v44-") as tmp:
  dst=Path(tmp)/"dist"
  subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
                  "--out",str(dst),"--sha","v44-ui-smoke","--branch","gui-v44"],
                 cwd=ROOT,capture_output=True,text=True,check=True)
  public=dst/"preview/mixer.html"
  assert public.read_bytes()==SRC.read_bytes(),"published /preview/ differs from canonical GUI"
  assert (dst/"preview/assets/mixer-wire-dots.css").read_bytes()==CSS.read_bytes(),"published WIRE style missing"
  assert 'assets/mixer-wire-dots.css?v=v45' in public.read_text("utf-8")
  with sync_playwright() as pw:
   browser=pw.chromium.launch(headless=True)
   for width in (1366,1680):
    page=browser.new_page(viewport={"width":width,"height":900})
    errors=[]
    page.on("pageerror",lambda err:errors.append(str(err)))
    page.goto(public.as_uri())
    body=page.locator(".console-body")
    assert "collapsed-send" in (body.get_attribute("class") or ""),"default folded WIRE drift"
    rail=box(page.locator('.section-rail [data-section="send"]'))
    collapsed=box(page.locator('#tracks .strip[data-id="5"] .wire-section'))
    assert abs(collapsed["height"]-27)<1.5,(width,"fold row",collapsed)
    assert abs(rail["height"]-collapsed["height"])<1.5,(width,"rail alignment",rail,collapsed)
    page.locator('.section-rail [data-section="send"]').click()
    src=page.locator('#tracks .strip[data-id="5"] .wire-node-out')
    pin=src.locator('.wire-pin')
    dest=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
    socket=dest.locator('.wire-arrow')
    # No disembodied labels: caption is to the RIGHT of its actual dot/socket.
    out_label=src.locator('small')
    in_label=dest.locator('.wire-label')
    level=page.locator('#tracks .strip[data-id="5"] .wire-gain-caption')
    # V45: captions intentionally fold away only when the actual channel
    # cannot safely fit icon + text; do not punish responsive dense banks.
    if out_label.is_visible():
     assert box(out_label)["x"] >= box(pin)["x"]+box(pin)["width"]-1,(width,"OUT label overlaps pin")
     assert box(in_label)["x"] >= box(dest)["x"]+box(dest)["width"]-1,(width,"IN label not right")
     assert box(level)["x"] >= box(page.locator('#tracks .strip[data-id="5"] .wire-pot'))["x"]+20,(width,"LEVEL not right")
    else:
     assert not in_label.is_visible() and not level.is_visible(),"caption fallback must be all or none"
    assert pin.evaluate("el=>getComputedStyle(el).backgroundColor")=="rgb(75, 83, 91)","idle OUT is not graphite"
    x0,y0=center(src);x1,y1=center(dest)
    page.evaluate("""()=>{
      window.__v44Impact=[];
      new MutationObserver(records=>{
        for(const r of records){
          const p=r.target;
          if(r.type!=='attributes'||!p.matches?.('path.wire-outgoing-line[data-cable-key="5-3"]'))continue;
          const d=p.getAttribute('d');
          if(d?.includes(' L ')&&window.__v44Impact.at(-1)!==d)
            window.__v44Impact.push(d);
        }
      }).observe(document.getElementById('wireLayer'),{subtree:true,attributes:true,attributeFilter:['d']});
    }""")
    page.mouse.move(x0,y0);page.mouse.down()
    page.mouse.move((x0+x1)/2,y0+55,steps=7)
    plug=page.locator('#wireLayer .wire-floating-plug')
    assert plug.count()==1,"drag follower not attached"
    assert "is-drag-origin" in (src.get_attribute("class") or "")
    assert pin.evaluate("el=>getComputedStyle(el).transform").startswith("matrix(1.27,")
    drag=page.locator('#wireLayer path.wire-drag-line').last
    assert drag.get_attribute("stroke")==plug.get_attribute("fill"),"drag cable/dot color disagreement"
    assert drag.get_attribute("opacity")==plug.get_attribute("opacity"),"drag cable/dot alpha disagreement"
    page.mouse.move(x1,y1,steps=6)
    assert "is-snap-target" in (dest.get_attribute("class") or ""),"socket magnet lost"
    assert "is-snap-locked" in (src.get_attribute("class") or ""),"source pin not docked"
    assert plug.count()==0,"extra follower must disappear on socket snap"
    assert pin.evaluate("el=>getComputedStyle(el).transform") in ("none","matrix(1, 0, 0, 1, 0, 0)"),"source pin stayed enlarged at snap"
    page.mouse.move(x1-55,y1,steps=4)
    assert plug.count()==1,"pointer follower not restored on unsnap"
    assert "is-snap-locked" not in (src.get_attribute("class") or "")
    page.mouse.move(x1,y1,steps=5);page.mouse.up()
    assert plug.count()==0,"orphan floating dot after drop"
    page.wait_for_function("()=>window.__v44Impact.length>=18",timeout=5000)
    page.wait_for_timeout(550)
    data=page.evaluate("()=>window.__v44Impact")
    rendered=page.locator('#wireLayer path.wire-outgoing-line[data-cable-key="5-3"]')
    assert rendered.count()==1,"committed outgoing cable invisible"
    page.wait_for_function("""()=>document.querySelector('#wireLayer path.wire-outgoing-line[data-cable-key="5-3"]')?.getAttribute('d')?.includes(' C ')""")
    base=rendered.get_attribute('d')
    peaks=[impact_peak(d,base) for d in data]
    assert max(v for t,v in peaks)>5.0,("wave should look like a broad cable jolt",max(peaks,key=lambda p:p[1]))
    # Click-away changes focus only; the incoming cable and its physical dots
    # must remain rendered and take the EXACT SVG source hue+opacity.
    page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
    incoming=page.locator('#wireLayer path.wire-incoming-line[data-from="5"][data-to="3"]')
    assert incoming.count()==1,"incoming disappeared after focus change"
    color=incoming.get_attribute('stroke');alpha=float(incoming.get_attribute('opacity'))
    assert src.get_attribute("data-wire-visible")=="true"
    assert dest.get_attribute("data-wire-visible")=="true"
    for dot in (pin,socket):
     props=dot.evaluate("""e=>({c:getComputedStyle(e).backgroundColor,alpha:parseFloat(getComputedStyle(e).opacity),
       wire:getComputedStyle(e.parentElement).getPropertyValue('--wire-edge-color').trim()})""")
     assert props["wire"]==color,(width,"dot hue differs from its actual wire",props,color)
     assert abs(props["alpha"]-alpha)<.001,(width,"dot opacity differs from wire",props,alpha)
    assert not errors,(width,errors)
    page.close()
   browser.close()
 print("PASS V44: published Pages, 27px fold, right captions, snap/unsnap, matching cable hardware and broad impact")

if __name__=="__main__":main()
