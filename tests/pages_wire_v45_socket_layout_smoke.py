#!/usr/bin/env python3
"""V45 physical WIRE bay: destination-hued arrival ring and zero caption collisions.

Tests the actually published /preview/ bundle, not just /preview-v2 source.
"""
from pathlib import Path
import subprocess,sys,tempfile
from playwright.sync_api import sync_playwright
from pages_wire_incoming_v43_smoke import center

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
STYLE=ROOT/"site/preview-v2/assets/mixer-wire-dots.css"

def rect(loc):
 box=loc.bounding_box()
 assert box and box["width"]>0 and box["height"]>0,("missing visual control",loc)
 return box

def right(box):
 return box["x"]+box["width"]

def channel_ring(sock):
 return sock.evaluate("""el=>{
  const colors=getComputedStyle(el),probe=document.createElement('span');
  probe.style.color='var(--ch)';el.appendChild(probe);
  const channel=getComputedStyle(probe).color;probe.remove();
  return {arrival:el.getAttribute('data-wire-arrival'),
   border:colors.borderTopColor,background:colors.backgroundColor,channel};
 }""")

def label_geometry(page,width):
 for idx in (2,3,5):
  base=f'#tracks .strip[data-id="{idx}"] '
  tray=page.locator(base+'.wire-tray')
  out=page.locator(base+'.wire-node-out')
  pin=out.locator('.wire-pin')
  socket=page.locator(base+'.wire-node-in')
  knob=page.locator(base+'.wire-pot')
  labels=(out.locator('small'),socket.locator('.wire-label'),
          page.locator(base+'.wire-gain-caption'))
  assert out.is_visible() and socket.is_visible() and knob.is_visible()
  assert rect(pin)["width"]>=11 and rect(socket)["width"]>=27
  vis=[label.is_visible() for label in labels]
  assert vis==[True,True,True] or vis==[False,False,False],(width,idx,"partial labels",vis)
  if all(vis):
   a,b,c=(rect(x) for x in labels)
   assert a["x"]>=right(rect(pin))-1,(width,idx,"OUT caption covers dot",a)
   assert right(a)+1<=rect(socket)["x"],(width,idx,"OUT caption touches IN ring",a,rect(socket))
   assert b["x"]>=right(rect(socket))-1,(width,idx,"IN caption covers socket",b)
   assert right(b)+1<=rect(knob)["x"],(width,idx,"IN caption touches LVL knob",b,rect(knob))
   assert c["x"]>=rect(knob)["x"]+20,(width,idx,"LVL caption covers knob",c)
   assert right(c)<=right(rect(tray))+1,(width,idx,"LVL caption clipped",c,rect(tray))
  else:
   # Labels are optional on a dense strip; never shrink the live jack itself.
   assert rect(out)["width"]>=23 and rect(socket)["width"]>=27

def main():
 with tempfile.TemporaryDirectory(prefix="arsonmix-v45-") as tmp:
  dist=Path(tmp)/"dist"
  subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
    "--out",str(dist),"--sha","v45-browser","--branch","v45"],
    cwd=ROOT,capture_output=True,text=True,check=True)
  public=dist/"preview/mixer.html"
  assert public.read_bytes()==HTML.read_bytes(),"Pages did not publish canonical V45 console"
  assert (dist/"preview/assets/mixer-wire-dots.css").read_bytes()==STYLE.read_bytes(),"Pages lost WIRE CSS"
  assert "assets/mixer-wire-dots.css?v=v23-wire-v50-config" in public.read_text("utf-8")
  with sync_playwright() as pw:
   browser=pw.chromium.launch(headless=True)
   for width in (1366,1680,1920):
    p=browser.new_page(viewport={"width":width,"height":900})
    errors=[];p.on("pageerror",lambda e:errors.append(str(e)))
    p.goto(public.as_uri())
    body=p.locator('.console-body')
    if "collapsed-send" in (body.get_attribute("class") or ""):
     p.locator('.section-rail [data-section="send"]').click()
    # A channel with no visible incoming cable must not have a colored ring.
    p.locator('#tracks .strip[data-id="2"] .strip-identity-top').click()
    dest=p.locator('#tracks .strip[data-id="3"] .wire-node-in')
    idle=channel_ring(dest)
    assert idle["arrival"] is None,(width,"unwired IN marked as plugged",idle)
    assert idle["border"]=="rgb(57, 74, 85)",(width,"unwired socket not graphite",idle)
    label_geometry(p,width)
    source=p.locator('#tracks .strip[data-id="5"] .wire-node-out')
    x0,y0=center(source);x1,y1=center(dest)
    p.mouse.move(x0,y0);p.mouse.down()
    p.mouse.move((x0+x1)/2,y0+50,steps=6)
    assert p.locator('#wireLayer .wire-floating-plug').count()==1
    p.mouse.move(x1,y1,steps=5)
    assert "is-snap-target" in (dest.get_attribute('class') or ""),(width,"magnet lost")
    p.mouse.up()
    p.mouse.move(x0,y0-70)
    p.wait_for_function("""()=>document.querySelector('#tracks .strip[data-id="3"] .wire-node-in')?.getAttribute('data-wire-arrival')==='true'""")
    connected=channel_ring(dest)
    assert connected["border"]==connected["channel"],(width,"socket ring is not CH03 orange",connected)
    assert connected["background"]!=idle["background"],(width,"connected receiver still looks idle",connected)
    path=p.locator('#wireLayer path.wire-outgoing-line[data-from="5"][data-to="3"]')
    assert path.count()==1,(width,"route cable lost on snap")
    # The same connected graph can stay stored while the displayed focus
    # moves to an unrelated strip. No drawn input means a graphite ring again.
    p.locator('#tracks .strip[data-id="2"] .strip-identity-top').click()
    assert p.locator('#wireLayer path.wire-outgoing-line[data-from="5"][data-to="3"]').count()==0
    hidden=channel_ring(dest)
    assert hidden["arrival"] is None,(width,"hidden route still illuminates socket",hidden)
    assert hidden["border"]==idle["border"] and hidden["background"]==idle["background"],(width,"socket not restored to graphite",hidden)
    # Compact is a spatial fallback, not a different hit-test or route state.
    if width==1366:
     p.locator('#densitySelect').select_option(label="Compact")
     label_geometry(p,width)
    assert not errors,(width,errors)
    p.close()
   browser.close()
 print("PASS V45: destination-colored real arrival, dark hidden/empty IN, zero collisions, compact fallback on published Pages")

if __name__=="__main__":main()
