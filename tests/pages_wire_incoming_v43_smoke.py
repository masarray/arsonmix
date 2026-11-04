#!/usr/bin/env python3
"""V43: generated public /preview/ parity, persistent incoming cables, and physical socket impact."""
from pathlib import Path
import math,re,subprocess,sys,tempfile
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
CANONICAL=ROOT/"site/preview-v2/index.html"
WIRE_CSS=ROOT/"site/preview-v2/assets/mixer-wire-dots.css"

def center(el):
 r=el.bounding_box()
 assert r and r["width"]>0 and r["height"]>0,r
 return r["x"]+r["width"]*.5,r["y"]+r["height"]*.5

def impact_peak(path,baseline):
 values=[float(v) for v in re.findall(r"-?\d+(?:\.\d+)?",path)]
 base=[float(v) for v in re.findall(r"-?\d+(?:\.\d+)?",baseline)]
 assert " L " in path and len(values)==114,(len(values),path[:90])
 assert " C " in baseline and len(base)==8,(len(base),baseline)
 x0,y0,c0x,c0y,c1x,c1y,x1,y1=base
 max_point=(0.,0.)
 for i in range(1,57):
  t=i/56.;u=1-t
  x=u*u*u*x0+3*u*u*t*c0x+3*u*t*t*c1x+t*t*t*x1
  y=u*u*u*y0+3*u*u*t*c0y+3*u*t*t*c1y+t*t*t*y1
  dist=math.hypot(values[i*2]-x,values[i*2+1]-y)
  if dist>max_point[1]:max_point=(t,dist)
 return max_point

def impact_motion(path,baseline):
 # Distributed squared-deformation centroid: unlike the single highest
 # oscillation crest, this follows the entire slow traveling wave packet.
 values=[float(v) for v in re.findall(r"-?\d+(?:\.\d+)?",path)]
 base=[float(v) for v in re.findall(r"-?\d+(?:\.\d+)?",baseline)]
 assert " L " in path and len(values)==114,(len(values),path[:90])
 assert " C " in baseline and len(base)==8,(len(base),baseline)
 x0,y0,c0x,c0y,c1x,c1y,x1,y1=base
 energy=0.
 weighted_position=0.
 for i in range(1,57):
  t=i/56.;u=1-t
  x=u*u*u*x0+3*u*u*t*c0x+3*u*t*t*c1x+t*t*t*x1
  y=u*u*u*y0+3*u*u*t*c0y+3*u*t*t*c1y+t*t*t*y1
  weight=(values[i*2]-x)**2+(values[i*2+1]-y)**2
  energy+=weight
  weighted_position+=t*weight
 return weighted_position/max(energy,1e-12),math.sqrt(energy)

def main():
 with tempfile.TemporaryDirectory(prefix="arsonmix-v43-") as dirname:
  dst=Path(dirname)/"dist"
  subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),"--out",str(dst),
                  "--sha","v43-ci-source","--branch","main"],check=True,
                 cwd=ROOT,capture_output=True,text=True)
  public=dst/"preview/mixer.html"
  shared=dst/"preview/assets/mixer-wire-dots.css"
  assert public.read_bytes()==CANONICAL.read_bytes(),"Public iframe still uses stale GUI"
  assert shared.read_bytes()==WIRE_CSS.read_bytes(),"Public iframe did not publish the effective WIRE CSS"
  assert 'assets/mixer-wire-dots.css?v=v55-accepted-wire-paint' in public.read_text("utf-8")
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
    # WIRE-first default CH→MAIN is real. Disconnect this particular OUT
    # using the operator UI so legacy neutral-pin physics remain testable.
    page.locator('#tracks .strip[data-id="5"] .strip-identity-top').click()
    page.locator("#wireConfigToggle").click()
    assert page.locator("#wireMaster").get_attribute("aria-pressed")=="true"
    page.locator("#wireMaster").click()
    page.locator("#wireConfigClose").click()
    src=page.locator('#tracks .strip[data-id="5"] .wire-node-out')
    pin=src.locator(".wire-pin")
    receiver=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
    assert src.is_visible() and receiver.is_visible()
    assert pin.evaluate("el=>getComputedStyle(el).backgroundColor")=="rgb(75, 83, 91)", \
           "PUBLIC /preview/ OUT must be graphite, not white"
    x0,y0=center(src)
    page.mouse.move(x0-17,y0)
    assert "is-near" in (src.get_attribute("class") or ""),"OUT proximity failed on /preview/"
    assert pin.evaluate("el=>getComputedStyle(el).backgroundColor")!="rgb(75, 83, 91)", \
           "PUBLIC /preview/ original dot fill did not light on approach"
    # Observe the REAL accepted cable path across the subsequent SVG refresh;
    # no test access to implementation-private JS variables is required.
    page.evaluate("""()=>{
      window.__v43Wave=[];
      window.__v43Frames=[];
      const layer=document.getElementById('wireLayer');
      new MutationObserver(records=>{
        for(const r of records){
          const path=r.target;
          if(r.type!=='attributes'||!path.matches?.('path.wire-outgoing-line[data-cable-key="5-3"]'))continue;
          const d=path.getAttribute('d');
          if(d?.includes(' L ')&&window.__v43Wave.at(-1)!==d){
            window.__v43Wave.push(d);
            window.__v43Frames.push({d,ms:performance.now()});
          }
        }
      }).observe(layer,{attributes:true,attributeFilter:['d'],childList:true,subtree:true});
    }""")
    x1,y1=center(receiver)
    page.mouse.move(x0,y0);page.mouse.down()
    page.mouse.move((x0+x1)/2,y0+48,steps=5)
    assert "is-drag-origin" in (src.get_attribute("class") or ""),"source pin not growing during drag"
    scale=pin.evaluate("el=>getComputedStyle(el).transform")
    assert scale.startswith("matrix(1.27,") or scale.startswith("matrix(1.269"),scale
    plug=page.locator("#wireLayer .wire-floating-plug")
    assert plug.count()==1 and plug.get_attribute("r")=="7.5", \
           "enlarged follower dot missing during cable drag"
    page.mouse.move(x1,y1,steps=6)
    assert "is-snap-target" in (receiver.get_attribute("class") or ""), \
           "magnetic IN snap/glow regressed"
    page.mouse.up()
    assert page.locator("#wireLayer .wire-floating-plug").count()==0
    # A successful drop reconstructs the SVG once. The impact animation must
    # reacquire the NEW live path and update it rather than a detached path.
    page.wait_for_function("()=>window.__v43Wave.length>=16",timeout=4000)
    live=page.locator('#wireLayer path.wire-outgoing-line[data-cable-key="5-3"]')
    # Wait for the accepted cable to settle into its original cubic (not
    # frame-count-based indexing; actual temporal damping is ~1600ms).
    page.wait_for_function("""()=>{
      const p=document.querySelector('#wireLayer path.wire-outgoing-line[data-cable-key="5-3"]');
      return p&&p.getAttribute('d')?.includes(' C ')&&window.__v43Frames.length>=16;
    }""",timeout=4500)
    baseline=live.get_attribute("d")
    frames=page.evaluate("()=>window.__v43Frames")
    trace=[f["d"] for f in frames]
    assert len(set(trace))>=16,("no visible traveling wave on accepted cable",len(trace))
    t0=frames[0]["ms"]
    # Use elapsed time and packet energy, not the highest sine crest; that
    # crest changes phase while the flexible cable visibly settles.
    def window_motion(lo,hi):
     subset=[impact_motion(f["d"],baseline) for f in frames
             if lo <= f["ms"]-t0 <= hi]
     assert len(subset)>=3,("not enough timed animation samples",lo,hi,len(subset))
     return (sum(x[0] for x in subset)/len(subset),
             sum(x[1] for x in subset)/len(subset))
    early=window_motion(150,400)
    late=window_motion(950,1250)
    assert early[1]>2.0 and late[1]>.35,(early,late)
    assert early[0]-late[0]>.20,("energy packet did not travel IN to OUT",early,late)
    assert late[1]<early[1]*.65,("cable motion did not damp",early,late)
    # Unarming WIRE must not delete the already committed incoming cable.
    page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
    assert "wire-intent" not in (body.get_attribute("class") or "")
    incoming=page.locator('#wireLayer path.wire-incoming-line[data-from="5"][data-to="3"]')
    assert incoming.count()==1,"incoming cable vanished after selection/click-away"
    assert float(incoming.get_attribute("opacity"))>=.7
    # Move to a different mixer bank and create an actual cross-bank source,
    # using the UI-only patch selector (no fake graph injection).
    page.locator("#bankNext").click()
    bus=page.locator('#tracks .strip[data-id="11"] .wire-node-out')
    assert bus.is_visible(),"Bus A source must be on next bank"
    bus.click()
    # V50: same real cross-bank selector, now inside Config.
    page.locator('#wireConfigToggle').click()
    picker=page.locator('#wireConfigWindow #wireLegend [data-wire-destination]')
    picker.select_option("3")
    page.locator('#wireConfigClose').click()
    page.locator("#bankPrev").click()
    page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
    off=page.locator('#wireLayer path.wire-incoming-line[data-from="11"][data-to="3"]')
    assert off.count()==1,"off-bank source cable missing from selected receiver"
    assert page.locator('#wireLayer .wire-offbank-label').count()>0
    assert not errors,errors
    page.close()
   browser.close()
 print("PASS V43: real published /preview/ dot fill, drag growth, preserved incoming/cross-bank cables, wave travels from IN socket")

if __name__=="__main__":main()
