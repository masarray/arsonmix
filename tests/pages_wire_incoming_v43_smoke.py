#!/usr/bin/env python3
"""V43: end-to-end generated /preview/ plus existing wires, incoming portals and socket wave."""
from pathlib import Path
import subprocess,sys,tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
CANONICAL=ROOT/"site/preview-v2/index.html"
WIRE_CSS=ROOT/"site/preview-v2/assets/mixer-wire-dots.css"

def center(locator):
 r=locator.bounding_box()
 assert r and r["width"]>0 and r["height"]>0,r
 return r["x"]+r["width"]*.5,r["y"]+r["height"]*.5

def main():
 with tempfile.TemporaryDirectory(prefix="arsonmix-v43-") as dirname:
  dst=Path(dirname)/"dist"
  subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),"--out",str(dst),
                  "--sha","v43-ci-source","--branch","main"],check=True,
                 cwd=ROOT,capture_output=True,text=True)
  public=dst/"preview/mixer.html"
  shared=dst/"preview/assets/mixer-wire-dots.css"
  assert public.read_bytes()==CANONICAL.read_bytes(),"Public iframe still uses stale GUI"
  assert shared.read_bytes()==WIRE_CSS.read_bytes(),"Public iframe WIRE CSS was not published"
  assert 'assets/mixer-wire-dots.css?v=v43' in public.read_text("utf-8")
  with sync_playwright() as pw:
   browser=pw.chromium.launch(headless=True)
   for width in (1366,1680):
    page=browser.new_page(viewport={"width":width,"height":900})
    errors=[]
    page.on("pageerror",lambda err:errors.append(str(err)))
    page.goto(public.as_uri())
    body=page.locator(".console-body")
    if "collapsed-send" in (body.get_attribute("class") or ""):
     page.locator('.section-rail [data-section="send"]').click()
    src=page.locator('#tracks .strip[data-id="5"] .wire-node-out')
    pin=src.locator(".wire-pin")
    receiver=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
    assert src.is_visible() and receiver.is_visible()
    assert pin.evaluate("el=>getComputedStyle(el).backgroundColor")=="rgb(75, 83, 91)",            "PUBLIC /preview/ OUT must be graphite, not white"
    x0,y0=center(src)
    page.mouse.move(x0-17,y0)
    assert "is-near" in (src.get_attribute("class") or ""),"OUT proximity failed on PUBLIC /preview/"
    assert pin.evaluate("el=>getComputedStyle(el).backgroundColor")!="rgb(75, 83, 91)",            "PUBLIC /preview/ fill did not light on approach"
    # Start a REAL physical OUT drag. The very same physical pin enlarges;
    # no extra concentric ring, and the free cable carries a follower dot.
    x1,y1=center(receiver)
    page.mouse.move(x0,y0);page.mouse.down()
    page.mouse.move((x0+x1)/2,y0+48,steps=5)
    assert "is-drag-origin" in (src.get_attribute("class") or ""),"source pin not marked during drag"
    scale=pin.evaluate("""el=>getComputedStyle(el).transform""")
    assert scale.startswith("matrix(1.27,") or scale.startswith("matrix(1.269"),scale
    plug=page.locator("#wireLayer .wire-floating-plug")
    assert plug.count()==1 and plug.get_attribute("r")=="7.5",            "one enlarged follower dot must be visible on cable"
    page.mouse.move(x1,y1,steps=6)
    assert "is-snap-target" in (receiver.get_attribute("class") or ""),            "magnetic IN snap/glow regressed"
    page.mouse.up()
    assert page.locator("#wireLayer .wire-floating-plug").count()==0
    assert page.evaluate("()=>wireMotion.token")>0,            "socket impact was not triggered on accepted cable"
    # Clicking the receiver ends transient patch intent but MUST keep the
    # accepted incoming cable on-screen and show sender on physical IN.
    page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
    assert "wire-intent" not in (body.get_attribute("class") or "")
    incoming=page.locator('#wireLayer path.wire-incoming-line[data-from="5"][data-to="3"]')
    assert incoming.count()==1,"accepted incoming cable disappeared after click-away"
    assert float(incoming.get_attribute("opacity"))>=.7,"incoming cable is too dim"
    # An off-bank sender is still a visible physical cable, entering through
    # a labeled portal, never an invented strip position.
    portal_id=page.evaluate("""()=>{
      const off=tracks.find(t=>t.kind!=='master' && t.id!==3 &&
        !document.querySelector('#tracks .strip[data-id="'+t.id+'"]'));
      if(!off)throw Error('No off-bank fixture');
      wireEdges.push({id:nextWireId++,sourceId:off.id,destId:3,
                      kind:'send',mode:'send',levelDb:0,enabled:true});
      renderWireRouting();return off.id;
    }""")
    off=page.locator(f'#wireLayer path.wire-incoming-line[data-from="{portal_id}"][data-to="3"]')
    assert off.count()==1,f"Missing off-bank incoming cable from CH{portal_id}"
    assert page.locator("#wireLayer .wire-offbank-label").count()>0
    assert not errors,errors
    page.close()
   # Ensure the actual impact travels FROM socket (t=1) TO source (t=0),
   # not global sag shaking or a permanent animation loop.
   p=browser.new_page()
   p.goto(public.as_uri())
   waves=p.evaluate("""()=>{
     const sag=20+Math.sqrt(400)*1.55, c=sag*4/3;
     function peak(frame){
      const path=wireImpactPathData(20,20,420,20,75,frame);
      const coords=[...path.matchAll(/(?:M|L) (-?[0-9.]+) (-?[0-9.]+)/g)]
        .map(m=>[+m[1],+m[2]]);
      let top={at:0,delta:0};
      for(let i=1;i<coords.length;i++){
       const t=i/56,u=1-t;
       const x=u*u*u*20+3*u*u*t*20+3*u*t*t*420+t*t*t*420;
       const y=u*u*u*20+3*u*u*t*(20+c)+3*u*t*t*(20+c)+t*t*t*20;
       const delta=Math.hypot(coords[i][0]-x,coords[i][1]-y);
       if(delta>top.delta)top={at:t,delta};
      }
      return top;
     }
     return {nearSocket:peak(6),nearSource:peak(27)};
   }""")
   assert waves["nearSocket"]["delta"]>1.0,waves
   assert waves["nearSource"]["delta"]>.6,waves
   assert waves["nearSocket"]["at"]>waves["nearSource"]["at"]+.35,waves
   p.close()
   browser.close()
 print("PASS V43: real /preview/ graphite OUT, hover, larger original pin, magnetic drag, persistent same-bank/off-bank incoming, socket-to-source impact")

if __name__=="__main__":main()
