#!/usr/bin/env python3
"""V53 FX/rail operator contract: real Chromium DOM, reference-only, no native PCM claims."""
from pathlib import Path
from playwright.sync_api import sync_playwright

import subprocess,sys,tempfile
from approved_publication_fixture import published_console
ROOT=Path(__file__).resolve().parents[1]
_current_gui=tempfile.TemporaryDirectory(prefix="arsonmix-approved-interaction-")
subprocess.run([sys.executable,str(ROOT/'scripts/build_pages.py'),'--out',_current_gui.name],cwd=ROOT,check=True,capture_output=True)
PAGE=published_console(Path(_current_gui.name))
BREAKPOINTS=((1366,768),(1378,911),(1680,900),(1920,1080))
PRESENTATIONS=("default","wire-folded","refocused")

def model(page,channel="1"):
 """Read only the public rendered rack; never reach private JS closure state."""
 return page.locator(f'#tracks .strip[data-id="{channel}"] .fxslot-row').evaluate_all(
  """rows=>rows.map(row=>({
     slot:Number(row.dataset.slot),id:row.dataset.insertId||"",
     name:row.querySelector('.fx-pick')?.textContent?.trim()||"",
     bypassed:row.classList.contains('is-bypassed')
   })).filter(row=>row.id)""")

def check_geometry(page,width,presentation):
 root=page.locator('#tracks .strip[data-id="1"]')
 rack=root.locator('.fx-section')
 plate=rack.locator('.subhead')
 well=rack.locator('.fxslot-list')
 p=plate.bounding_box();w=well.bounding_box()
 assert p and w and p["height"]>0 and w["height"]>0,(width,presentation,p,w)
 assert abs(p["x"]-w["x"])<2 and abs(p["width"]-w["width"])<2,\
        (width,presentation,"FX plate and inset well must share side edges",p,w)
 assert abs((p["y"]+p["height"])-w["y"])<=2.1,\
        (width,presentation,"FX header disconnected from processor well",p,w)
 skin=plate.evaluate("""el=>({bg:getComputedStyle(el).backgroundImage,
     border:getComputedStyle(el).borderBottomWidth})""")
 assert skin["bg"]!="none" and skin["border"]=="1px",(width,presentation,skin)
 assert page.locator('.section-rail .rail-cell.fx .rail-svg-icon').evaluate(
  "el=>Math.round(el.getBoundingClientRect().width)")==16,(width,presentation,"oversized FX rail icon")
 assert page.locator("#master .strip .fx-section").count()==1, \
        (width,presentation,"pinned Master has no rack")
 for channel in ('1',):
  root=page.locator(f'#tracks .strip[data-id="{channel}"]')
  assert root.locator('.fx-grip').count()==0,(width,presentation,"obsolete grip visible")
  for row in root.locator('.fxslot-row').all():
   assert row.get_attribute("draggable") is None,(width,presentation,"native HTML DnD must stay disabled")
  for row in root.locator('.fxslot-row.is-active,.fxslot-row.is-bypassed').all():
   power=row.locator('[data-action="slotPower"]')
   remove=row.locator('[data-action="removeFx"]')
   name=row.locator('.fx-pick')
   assert power.count()==remove.count()==name.count()==1
   p=power.bounding_box();r=remove.bounding_box();n=name.bounding_box()
   assert p and n and r and p["x"]+p["width"]<=n["x"]+1 and n["x"]+n["width"]<=r["x"]+1
   assert name.get_attribute("draggable") is None
   assert name.get_attribute("aria-label"),"FX name retains accessible editor activation"
   assert row.evaluate("e=>getComputedStyle(e).cursor")=="grab"
 rail=page.locator('.section-rail [data-section="fx"]')
 assert rail.get_attribute("aria-expanded")=="true"
 # V29 has one comfort density: actual FX rail disclosure must remain
 # reversible across every operator state without swapping FX slot IDs.
 rail.click()
 assert rail.get_attribute("aria-expanded")=="false",(width,presentation,"FX collapse aria broken")
 rail.click()
 assert rail.get_attribute("aria-expanded")=="true",(width,presentation,"FX expand aria broken")

def check_transactions(page):
 root=page.locator('#tracks .strip[data-id="1"]')
 # Insert an actual second effect via the existing public quick FX Picker.
 # Do not mutate inaccessible IIFE-local tracks or fake an accepted graph.
 assert root.locator('.fx-pick[data-slot="1"]').count()==1,"Missing empty insert"
 root.locator('.fx-pick[data-slot="1"]').click()
 effect=page.locator('#fastResults .fast-effect').filter(has_text="Dynamics Processing").first
 assert effect.is_visible(),"Quick FX Picker did not expose Dynamics Processing"
 effect.click()
 assert root.locator('.fxslot-row:not(.is-empty)').count()>=2,"Second FX not accepted by GUI"
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
 # Focusable plugin name also exposes keyboard reorder, without a handle.
 first=root.locator('.fx-pick[data-slot="0"]')
 first.focus();first.press('Alt+ArrowDown')
 after=model(page)
 assert [x["name"] for x in after[:2]]==[
   x["name"] for x in initial[:2][::-1]],("keyboard reorder not committed",after)
 assert [x["id"] for x in after[:2]]==[
   x["id"] for x in initial[:2][::-1]],"insert identity lost"
 assert [x["bypassed"] for x in after[:2]]==[
   x["bypassed"] for x in initial[:2][::-1]],"bypass moved separately"
 root.locator('.fx-pick:not(.empty)').first.focus()
 root.locator('.fx-pick:not(.empty)').first.press('Control+z')
 assert model(page)==initial,"plugin-name Ctrl+Z must restore order, bypass and identity"
 # Real pointer Drag&Drop on row-wide pointer; non-draggable rows cannot reorder.
 root.locator('.fx-pick[data-slot="0"]').drag_to(root.locator('.fxslot-row[data-slot="1"]'),target_position={'x':40,'y':18})
 dragged=model(page)
 # Slot indices are physical positions and MUST remain 0/1 after moving.
 # Compare stable insert payload separately from its new position.
 assert [r["slot"] for r in dragged[:2]]==[0,1],("invalid DnD positions",dragged)
 for key in ("id","name","bypassed"):
  assert [r[key] for r in dragged[:2]]==[
   r[key] for r in initial[:2][::-1]],("row DnD changed "+key,dragged)
 # Cross-channel pointer transfer preserves token/bypass and compacts
 # both chains. Native transfer has separate accepted PCM coverage.
 before=model(page)
 second=model(page,"2")
 other=page.locator('#tracks .strip[data-id="2"]')
 root.locator('.fx-pick[data-slot="0"]').drag_to(other.locator('.fxslot-row[data-slot="0"]'))
 assert len([r for r in model(page) if r['name']!='None'])==len([r for r in before if r['name']!='None'])-1
 assert len([r for r in model(page,"2") if r['name']!='None'])==len([r for r in second if r['name']!='None'])+1
 other.locator('.fx-pick:not(.empty)').first.focus()
 other.locator('.fx-pick:not(.empty)').first.press('Control+z')
 assert model(page)==before and model(page,"2")==second,"transfer undo lost native-style identity"
 root.locator('.fx-pick:not(.empty)').first.focus()
 root.locator('.fx-pick:not(.empty)').first.press('Control+z')
 assert model(page)==initial,"preceding reorder undo lost"
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
   # Composed /preview/ deliberately has one hidden legacy density ID.
   # Keep twelve real viewport/operator-state cases without resurrecting it.
   assert page.locator("#densitySelect").is_hidden()
   assert page.locator(".console-body").get_attribute("data-density")=="comfort"
   for presentation in PRESENTATIONS:
    body=page.locator(".console-body")
    if presentation=="wire-folded":
     if "collapsed-send" not in (body.get_attribute("class") or ""):
      page.locator('.section-rail [data-section="send"]').click()
     assert "collapsed-send" in (body.get_attribute("class") or "")
    elif presentation=="refocused":
     if "collapsed-send" in (body.get_attribute("class") or ""):
      page.locator('.section-rail [data-section="send"]').click()
     page.locator('#tracks .strip[data-id="2"] .strip-identity-top').click()
     page.locator('#tracks .strip[data-id="1"] .strip-identity-top').click()
    if "collapsed-fx" in (body.get_attribute("class") or ""):
     page.locator('.section-rail [data-section="fx"]').click()
    check_geometry(page,width,presentation)
    if width==1680 and presentation=="default":check_transactions(page)
   assert page.locator(".console-body").get_attribute("data-density")=="comfort"
   page.screenshot(path=f"pages-v53-fx-rail-{width}.png",full_page=True)
   assert not errors,(width,errors)
   page.close()
  browser.close()
 print("PASS V53 row-wide pointer + reversible FX reorder, visual frame and rail on 4 sizes × 3 operator states")

if __name__=="__main__":main()