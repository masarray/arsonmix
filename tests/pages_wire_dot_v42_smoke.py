#!/usr/bin/env python3
"""Public ArSonMix V42: original OUT pin fill, drag follower and magnetic IN."""
from pathlib import Path
from playwright.sync_api import sync_playwright
PAGE=Path(__file__).resolve().parents[1]/"site/preview-v2/index.html"
WIRE_STYLE=PAGE.with_name("assets")/"mixer-wire-dots.css"
assert 'assets/mixer-wire-dots.css?v=v48-css-v16' in PAGE.read_text(encoding="utf-8")
assert 'wire-node-out .wire-pin' in WIRE_STYLE.read_text(encoding="utf-8")
def check(ok,msg): assert ok,msg
def styles(loc):
 return loc.evaluate("""el=>{const s=getComputedStyle(el);return {bg:s.backgroundColor,border:s.borderTopColor,shadow:s.boxShadow}}""")
def center(loc):
 r=loc.bounding_box();check(bool(r),"no layout rect");return (r["x"]+r["width"]/2,r["y"]+r["height"]/2)
def main():
 with sync_playwright() as pw:
  b=pw.chromium.launch(headless=True)
  for width in (1366,1680):
   p=b.new_page(viewport={"width":width,"height":900},device_scale_factor=1)
   errors=[];p.on("pageerror",lambda err:errors.append(str(err)))
   p.goto(PAGE.as_uri())
   body=p.locator(".console-body")
   if "collapsed-send" in (body.get_attribute("class") or ""):
    p.locator('.section-rail [data-section="send"]').click()
   src=p.locator('#tracks .strip[data-id="5"] .wire-node-out')
   pin=src.locator(".wire-pin")
   dest=p.locator('#tracks .strip[data-id="3"] .wire-node-in')
   dest_inner=dest.locator(".wire-arrow")
   check(src.is_visible() and dest.is_visible(),"test requires exposed jacks")
   initial=styles(pin)
   check(initial["bg"]=="rgb(75, 83, 91)",("OUT must start graphite",initial))
   socket_idle=styles(dest_inner)["bg"]
   check(socket_idle=="rgb(75, 83, 91)",("IN must start graphite",socket_idle))
   x0,y0=center(src);x1,y1=center(dest)
   # Approach OUT, without touching it: the same tiny pin changes fill;
   # its 24px housing stays transparent (no new glowing outer ring).
   p.mouse.move(x0-17,y0)
   check("is-near" in (src.get_attribute("class") or ""),"OUT proximity missing")
   approach=styles(pin)
   check(approach["bg"]!=initial["bg"],("OUT fill did not illuminate",approach))
   check(styles(src)["bg"]=="rgba(0, 0, 0, 0)",("unexpected outer circle",styles(src)))
   p.mouse.move(x0+80,y0-20)
   check(styles(pin)["bg"]==initial["bg"],"OUT remains white/illuminated when unselected")
   # The dragging cursor must carry a colored DOT at the end of the wire,
   # including NEW wires (not only replugging existing ones).
   p.mouse.move(x0,y0);p.mouse.down()
   # Travel through empty WIRE deck space, away from intervening IN jacks.
   # A midpoint on the socket row may legitimately magnet-snap to CH04.
   px=(x0+x1)/2;py=y0+54
   p.mouse.move(px,py,steps=6)
   check(p.locator('.wire-node-in.is-snap-target').count()==0,
         'empty-deck pointer unexpectedly close to magnetic IN')
   plug=p.locator("#wireLayer circle.wire-floating-plug")
   check(plug.count()==1,"fresh drag missing floating dot")
   check(p.locator("#wireLayer path.wire-drag-line").count()==2,"drag cable missing")
   geom=plug.evaluate("""el=>({x:Number(el.getAttribute('cx')),y:Number(el.getAttribute('cy')),fill:el.getAttribute('fill')})""")
   viewport=p.locator("#viewport").bounding_box()
   # The existing renderer bounds the cable tip to the mixer viewport.
   # Compare against the same bounded pointer, not raw screen coordinates.
   ex=max(2,min(viewport["width"]-2,px-viewport["x"]))
   ey=max(2,min(viewport["height"]-2,py-viewport["y"]))
   check(abs(geom["x"]-ex)<1.5 and abs(geom["y"]-ey)<1.5,
         ("dot is not following bounded pointer",geom,(ex,ey),viewport))
   # Magnetic snap docks the cable; the extra follower disappears INTO the real socket.
   p.mouse.move(x1-7,y1,steps=5)
   check("is-snap-target" in (dest.get_attribute("class") or ""),"magnet did not snap to IN")
   snap_inner=styles(dest_inner)
   check(snap_inner["bg"]!=socket_idle,("snap target remains dark",snap_inner))
   check(plug.count()==0,"extra floating dot must vanish while snapped into socket")
   # Departing snap radius removes highlight; snapping back and releasing
   # connects normally without an extra click or lost old snap handling.
   p.mouse.move(x1-48,y1,steps=3)
   check("is-snap-target" not in (dest.get_attribute("class") or ""),"snap latched outside magnet")
   check(plug.count()==1,"floating dot must reappear after unsnapping")
   p.mouse.move(x1,y1,steps=4);p.mouse.up()
   check(p.locator("#wireLayer circle.wire-floating-plug").count()==0,"floating dot left behind after release")
   # OUT still has a live routing context until click-away.
   p.locator('#tracks .strip[data-id="1"] .strip-identity-top').click()
   check("wire-intent" not in (body.get_attribute("class") or ""),"routing focus failed to release")
   check(styles(src.locator(".wire-pin"))["bg"]==initial["bg"],"OUT failed to return graphite")
   check(styles(dest.locator(".wire-arrow"))["bg"]==socket_idle,"inactive IN failed to return graphite")
   check(not errors,errors)
   p.close()
  b.close()
 print("PASS V42: charcoal idle pin, single lit fill, floating OUT dot, magnetic snap and restored IN")
if __name__=="__main__":main()
