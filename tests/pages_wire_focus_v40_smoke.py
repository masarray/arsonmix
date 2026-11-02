#!/usr/bin/env python3
"""V40 WIRE UX: real public Pages HTML, no native audio qualification."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"site/preview-v2/index.html"

def main():
 with sync_playwright() as pw:
  browser=pw.chromium.launch(headless=True)
  for width in (1366,1680):
   page=browser.new_page(viewport={"width":width,"height":900})
   errors=[]
   page.on("pageerror",lambda e:errors.append(str(e)))
   page.goto(PAGE.as_uri())
   body=page.locator(".console-body")
   # The public console ships with the WIRE section collapsed by default.
   # Exercise actual exposed jack hardware, not hidden miniature placeholders.
   if "collapsed-send" in (body.get_attribute("class") or ""):
    page.locator('.section-rail [data-section="send"]').click()
   assert "collapsed-send" not in (body.get_attribute("class") or "")
   assert "wire-intent" not in (body.get_attribute("class") or "")
   # Entire tray, including unused metal, selects its own channel.
   strip=page.locator('#tracks .strip[data-id="2"]')
   tray=strip.locator(".wire-tray")
   tray.scroll_into_view_if_needed()
   r=tray.bounding_box()
   page.mouse.click(r["x"]+5,r["y"]+r["height"]/2)
   assert "selected" in (strip.get_attribute("class") or ""), "blank WIRE tray did not select its strip"
   rim=strip.evaluate("""s=>{const a=getComputedStyle(s,'::after');return {width:a.borderTopWidth,opacity:Number(a.opacity)}}""")
   assert rim["width"]=="1px" and .4<=rim["opacity"]<.8,rim
   out=strip.locator(".wire-node-out")
   rect=out.bounding_box()
   # Approach outside 28px node while still inside the tray: no click yet.
   page.mouse.move(rect["x"]-5,rect["y"]+rect["height"]/2)
   assert "is-near" in (out.get_attribute("class") or ""), "OUT fails near-pointer cue"
   assert out.get_attribute("aria-pressed")=="false"
   # The halo is interactive even when clicked OUTSIDE its visible circle.
   page.mouse.click(rect["x"]-5,rect["y"]+rect["height"]/2)
   assert "wire-intent" in (body.get_attribute("class") or ""), "near-OUT click did not arm patch"
   assert strip.locator(".wire-node-out").get_attribute("aria-pressed")=="true"
   receiver=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
   assert receiver.is_enabled(), "IN must be ready when source is pinned"
   # A different mixer section is not a WIRE control and releases focus.
   page.locator('#tracks .strip[data-id="3"] .strip-identity-top').click()
   assert "wire-intent" not in (body.get_attribute("class") or "")
   assert page.locator('#tracks .strip[data-id="2"] .wire-node-out').get_attribute("aria-pressed")=="false"
   inactive=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
   idle=inactive.evaluate("""el=>{const s=getComputedStyle(el);return {bg:s.backgroundColor,color:s.color}}""")
   assert idle["bg"]=="rgb(27, 43, 53)",idle
   assert not errors,errors
   page.close()
  browser.close()
 print("PASS V40: OUT proximity, explicit arm/disarm, tray selection, graphite IN and translucent rim")

if __name__=="__main__":main()
