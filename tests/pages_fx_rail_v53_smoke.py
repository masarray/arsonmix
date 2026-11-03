#!/usr/bin/env python3
"""V53 FX/rail operator contract: real Chromium DOM, reference-only, no native PCM claims."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"site/preview-v2/index.html"
BREAKPOINTS=((1366,768),(1378,911),(1680,900),(1920,1080))
DENSITIES=("comfort","standard","compact")

def model(page):
 return page.evaluate("""()=>{
  const t=tracks[0];ensureFxInsertIds(t);
  return {fx:t.fx.slice(),bypass:t.slotBypass.slice(),
   ids:t.fxInsertIds.map(x=>x?.id||null)};
 }""")

def check_geometry(page,width,density):
 root=page.locator('#tracks .strip[data-id="1"]')
 rack=root.locator('.fx-section')
 plate=rack.locator('.subhead')
 well=rack.locator('.fxslot-list')
 p=plate.bounding_box();w=well.bounding_box()
 assert p and w and p["height"]>0 and w["height"]>0,(width,density,p,w)
 assert abs(p["x"]-w["x"])<2 and abs(p["width"]-w["width"])<2,\
        (width,density,"FX plate and inset well must share side edges",p,w)
 assert abs((p["y"]+p["height"])-w["y"])<=2.1,\
        (width,density,"FX header disconnected from processor well",p,w)
 skin=plate.evaluate("""el=>({bg:getComputedStyle(el).backgroundImage,
     border:getComputedStyle(el).borderBottomWidth})""")
 assert skin["bg"]!="none" and skin["border"]=="1px",(width,density,skin)
 assert page.locator('.section-rail .rail-cell.fx .rail-svg-icon').evaluate(
  "el=>Math.round(el.getBoundingClientRect().width)")==16,(width,density,"oversized FX rail icon")
 assert page.locator("#master .strip .fx-section").count()==1, \
        (width,density,"pinned Master has no rack")
 for channel in ('1',):
  root=page.locator(f'#tracks .strip[data-id="{channel}"]')
  for row in root.locator('.fxslot-row').all():
   assert row.get_attribute("draggable") is None,(width,density,"row-wide drag regressed")
  for row in root.locator('.fxslot-row.is-active,.fxslot-row.is-bypassed').all():
   grip=row.locator('.fx-grip')
   remove=row.locator('[data-action="removeFx"]')
   assert grip.count()==remove.count()==1
   g=grip.bounding_box();r=remove.bounding_box()
   assert g and r and g["x"]+g["width"] <= r["x"]+.5,\
          (width,density,"grip must precede REMOVE",g,r)
   assert grip.get_attribute("draggable")=="true"
   assert grip.get_attribute("aria-label"),"FX grip must be keyboard accessible"
   assert row.locator('.fx-pick').get_attribute("draggable") is None
 rail=page.locator('.section-rail [data-section="fx"]')
 assert rail.get_attribute("aria-expanded")=="true"
 rail.click()
 assert rail.get_attribute("aria-expanded")=="false",(width,density,"FX collapse aria broken")
 rail.click()
 assert rail.get_attribute("aria-expanded")=="true",(width,density,"FX expand aria broken")

def check_transactions(page):
 page.evaluate("""()=>{
  const t=tracks[0];
  t.fx[0]='Parametric Equalizer';t.fx[1]='Dynamics Processing';
  t.slotBypass[0]=true;t.slotBypass[1]=false;
  refreshStrip(t);
 }""")
 root=page.locator('#tracks .strip[data-id="1"]')
 initial=model(page)
 assert initial["fx"][:2]==["Parametric Equalizer","Dynamics Processing"]
 for selector in ('.fx-pick[data-slot="0"]','[data-action="slotPower"][data-slot="0"]',
                  '[data-action="removeFx"][data-slot="0"]'):
  assert root.locator(selector).get_attribute("draggable") is None
 # Keyboard must reorder the selected INSERT identity, not copy its name only.
 first=root.locator('.fx-grip[data-slot="0"]')
 first.focus();first.press('Alt+ArrowDown')
 after=model(page)
 assert after["fx"][:2]==initial["fx"][:2][::-1],("keyboard reorder not committed",after)
 assert after["bypass"][:2]==initial["bypass"][:2][::-1],"bypass moved separately"
 assert after["ids"][:2]==initial["ids"][:2][::-1],"insert identity lost"
 root.locator('.fx-grip').first.focus()
 root.locator('.fx-grip').first.press('Control+z')
 assert model(page)==initial,"grip Ctrl+Z must restore order, bypass and identity"
 # DnD moves ONLY from its handle; dropping a valid occupied slot commits once.
 root.locator('.fx-grip[data-slot="0"]').drag_to(root.locator('.fxslot-row[data-slot="1"]'))
 dragged=model(page)
 assert dragged["fx"][:2]==initial["fx"][:2][::-1],("grip DnD failed",dragged)
 assert dragged["ids"][:2]==initial["ids"][:2][::-1],"DnD changed instance IDs"
 # The existing other channel's processing never becomes a name-only VST3 copy.
 other=page.locator('#tracks .strip[data-id="2"]')
 before=model(page)
 second=page.evaluate("()=>tracks[1].fx.slice()")
 root.locator('.fx-grip[data-slot="0"]').drag_to(other.locator('.fxslot-row[data-slot="0"]'))
 assert model(page)==before,"cross-channel drop corrupted source"
 assert page.evaluate("()=>tracks[1].fx.slice()")==second,"unsafe cross-channel FX copied"
 root.locator('.fx-grip').first.focus()
 root.locator('.fx-grip').first.press('Control+z')
 assert model(page)==initial,"invalid drag lost preceding single-step Undo"
 # The physical OFF control is independently operable, never a drag handle.
 root.locator('[data-action="slotPower"][data-slot="0"]').click()
 assert model(page)["fx"]==initial["fx"],"FX power click reordered rack"

def main():
 with sync_playwright() as pw:
  browser=pw.chromium.launch(headless=True)
  for width,height in BREAKPOINTS:
   page=browser.new_page(viewport={"width":width,"height":height},device_scale_factor=1)
   errors=[];page.on("pageerror",lambda error:errors.append(str(error)))
   page.goto(PAGE.as_uri())
   for density in DENSITIES:
    page.locator("#densitySelect").select_option(density)
    if "collapsed-fx" in (page.locator(".console-body").get_attribute("class") or ""):
     page.locator('.section-rail [data-section="fx"]').click()
    check_geometry(page,width,density)
    if width==1680 and density=="comfort":check_transactions(page)
   page.locator("#densitySelect").select_option("comfort")
   page.screenshot(path=f"pages-v53-fx-rail-{width}.png",full_page=True)
   assert not errors,(width,errors)
   page.close()
  browser.close()
 print("PASS V53 dedicated grip + reversible FX reorder, visual frame and rail on 4 sizes × 3 densities")

if __name__=="__main__":main()
