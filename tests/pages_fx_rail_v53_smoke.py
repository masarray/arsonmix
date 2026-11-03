#!/usr/bin/env python3
"""V53 FX/rail operator contract: real Chromium DOM, reference-only, no native PCM claims."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"site/preview-v2/index.html"
BREAKPOINTS=((1366,768),(1378,911),(1680,900),(1920,1080))
DENSITIES=("comfort","standard","compact")

def model(page,channel="1"):
 """Read only the public rendered rack; never reach private JS closure state."""
 return page.locator(f'#tracks .strip[data-id="{channel}"] .fxslot-row').evaluate_all(
  """rows=>rows.map(row=>({
     slot:Number(row.dataset.slot),id:row.dataset.insertId||"",
     name:row.querySelector('.fx-pick')?.textContent?.trim()||"",
     bypassed:row.classList.contains('is-bypassed')
   })).filter(row=>row.id)""")

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
 if density=="compact":
  # Approved Narrow mode explicitly refuses FX/SEND disclosure changes to
  # protect the calibrated vertical chassis. Verify zero accidental mutation.
  before=page.locator(".console-body").get_attribute("class")
  rail.click()
  assert rail.get_attribute("aria-expanded")=="true",(
    width,density,"Narrow mode changed FX disclosure unexpectedly")
  assert page.locator(".console-body").get_attribute("class")==before,(
    width,density,"Narrow mode shifted rack/fader geometry")
 else:
  rail.click()
  assert rail.get_attribute("aria-expanded")=="false",(width,density,"FX collapse aria broken")
  rail.click()
  assert rail.get_attribute("aria-expanded")=="true",(width,density,"FX expand aria broken")

def check_transactions(page):
 root=page.locator('#tracks .strip[data-id="1"]')
 # Insert an actual second effect via the existing public quick FX Picker.
 # Do not mutate inaccessible IIFE-local tracks or fake an accepted graph.
 assert root.locator('.fx-pick[data-slot="1"]').count()==1,"Missing empty insert"
 root.locator('.fx-pick[data-slot="1"]').click()
 effect=page.locator('#fastResults .fast-effect').filter(has_text="Dynamics Processing").first
 assert effect.is_visible(),"Quick FX Picker did not expose Dynamics Processing"
 effect.click()
 assert root.locator('.fx-grip[data-slot="1"]').count()==1,"Second FX not accepted by GUI"
 # Independently bypass first plugin; the move must carry bypass with identity.
 root.locator('[data-action="slotPower"][data-slot="0"]').click()
 initial=model(page)
 assert [x["name"] for x in initial[:2]]==[
   "Parametric Equalizer","Dynamics Processing"],initial
 assert initial[0]["bypassed"] and not initial[1]["bypassed"],initial
 assert len({x["id"] for x in initial})==2,"Insert IDs must be unique"
 for selector in ('.fx-pick[data-slot="0"]','[data-action="slotPower"][data-slot="0"]',
                  '[data-action="removeFx"][data-slot="0"]'):
  assert root.locator(selector).get_attribute("draggable") is None
 # Focusable grip is the only drag actuator. Keyboard moves one real insert.
 first=root.locator('.fx-grip[data-slot="0"]')
 first.focus();first.press('Alt+ArrowDown')
 after=model(page)
 assert [x["name"] for x in after[:2]]==[
   x["name"] for x in initial[:2][::-1]],("keyboard reorder not committed",after)
 assert [x["id"] for x in after[:2]]==[
   x["id"] for x in initial[:2][::-1]],"insert identity lost"
 assert [x["bypassed"] for x in after[:2]]==[
   x["bypassed"] for x in initial[:2][::-1]],"bypass moved separately"
 root.locator('.fx-grip').first.focus()
 root.locator('.fx-grip').first.press('Control+z')
 assert model(page)==initial,"grip Ctrl+Z must restore order, bypass and identity"
 # Real pointer Drag&Drop on dedicated grip; non-draggable rows cannot reorder.
 root.locator('.fx-grip[data-slot="0"]').drag_to(root.locator('.fxslot-row[data-slot="1"]'))
 dragged=model(page)
 # Slot indices are physical positions and MUST remain 0/1 after moving.
 # Compare stable insert payload separately from its new position.
 assert [r["slot"] for r in dragged[:2]]==[0,1],("invalid DnD positions",dragged)
 for key in ("id","name","bypassed"):
  assert [r[key] for r in dragged[:2]]==[
   r[key] for r in initial[:2][::-1]],("grip DnD changed "+key,dragged)
 # Cross-channel FX lease cannot become a blind name-only clone.
 before=model(page)
 second=model(page,"2")
 other=page.locator('#tracks .strip[data-id="2"]')
 root.locator('.fx-grip[data-slot="0"]').drag_to(other.locator('.fxslot-row[data-slot="0"]'))
 assert model(page)==before,"cross-channel drop corrupted source FX"
 assert model(page,"2")==second,"unsafe cross-channel FX copied"
 root.locator('.fx-grip').first.focus()
 root.locator('.fx-grip').first.press('Control+z')
 assert model(page)==initial,"invalid drag erased preceding Undo transaction"
 # The separate OFF button is never a drag handle; it flips only bypass.
 root.locator('[data-action="slotPower"][data-slot="0"]').click()
 after_power=model(page)
 assert [x["name"] for x in after_power]==[x["name"] for x in initial],after_power
 assert [x["id"] for x in after_power]==[x["id"] for x in initial],after_power
 assert after_power[0]["bypassed"]!=initial[0]["bypassed"],"FX OFF did not toggle"

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
