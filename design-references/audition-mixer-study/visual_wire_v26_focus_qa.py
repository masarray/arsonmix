#!/usr/bin/env python3
"""V26 Focus: selected-channel receives behind sends + hue-aware IN/OUT legend."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html")

def ensure(ok,why):
    if not ok: raise AssertionError(why)

def main():
  with sync_playwright() as pw:
    browser=pw.chromium.launch(headless=True)
    for width,height in ((1366,768),(1680,900),(1920,1080)):
      page=browser.new_page(viewport={"width":width,"height":height})
      errors=[]
      page.on("pageerror",lambda e:errors.append(str(e)))
      page.goto(PAGE.as_uri())
      body=page.locator('.console-body')
      wire=page.locator('.section-rail [data-section="send"]')
      wire.click()
      def node(ch,suffix):
        return page.locator(f'#tracks .strip[data-id="{ch}"] {suffix}')
      def select(ch):
        node(ch,'.strip-identity-top').click()
      def connect(src,dst):
        select(src)
        receiver=node(dst,'.wire-node-in')
        receiver.click()
        ensure(receiver.get_attribute('aria-pressed')=='true',
               f'{width}: failed CH{src:02}→CH{dst:02} fixture')
      connect(1,3)
      connect(2,3)
      connect(3,4)
      # Select through normal channel strip, not via an internal test shortcut.
      select(3)
      legend=page.locator('#wireLegend')
      in_ids=legend.locator('.wire-map-group.is-in .wire-map-chip span').all_inner_texts()
      out_ids=legend.locator('.wire-map-group.is-out .wire-map-chip span').all_inner_texts()
      ensure(in_ids==['CH01','CH02'] and out_ids==['CH04'],
             f'{width}: two-way badge routing wrong: {in_ids} / {out_ids}')
      ensure(legend.locator('.wire-map-focus').inner_text()=='CH03',
             f'{width}: focus did not follow selected channel')
      layer=page.locator('#wireLayer')
      incoming=layer.locator('path.wire-incoming-line')
      outgoing=layer.locator('path.wire-outgoing-line')
      ensure(incoming.count()==2 and outgoing.count()==1,
             f'{width}: expected 2 received behind 1 sent, got {incoming.count()}/{outgoing.count()}')
      ensure(set(incoming.evaluate_all('(els)=>els.map(e=>e.dataset.from)'))=={'1','2'},
             f'{width}: incorrect incoming source channels')
      ensure(outgoing.first.get_attribute('data-to')=='4',
             f'{width}: outgoing destination wrong')
      order=layer.evaluate("""el=>Array.from(el.querySelectorAll('path')).map(p=>
          p.classList.contains('wire-incoming-line')?'in':
          p.classList.contains('wire-outgoing-line')?'out':'shadow')""")
      ensure(order[:2]==['in','in'] and order[-1]=='out',
             f'{width}: incoming is NOT drawn behind bright outgoing: {order}')
      ensure(float(incoming.first.get_attribute('opacity')) <
             float(outgoing.first.get_attribute('opacity')),
             f'{width}: incoming line not softer than outgoing')
      for ch in (1,2):
        expected=node(ch,'.strip-identity-top').evaluate(
            "el=>el.closest('.strip').style.getPropertyValue('--ch')")
        stroke=layer.locator(f'path.wire-incoming-line[data-from="{ch}"]').get_attribute('stroke')
        ensure(stroke.lower()==expected.lower(),
               f'{width}: incoming CH{ch:02} wire does not use source hue')
        color=legend.locator(f'.wire-map-group.is-in .wire-map-chip:has-text("CH{ch:02}")').evaluate(
            'el=>el.style.getPropertyValue("--wire-hue")')
        ensure(color.lower()==expected.lower(),
               f'{width}: IN badge missing actual source hue')
      color4=node(4,'.strip-identity-top').evaluate(
          "el=>el.closest('.strip').style.getPropertyValue('--ch')")
      ensure(outgoing.first.get_attribute('stroke').lower()==color4.lower(),
             f'{width}: outgoing CH04 hue changed')
      # Collapse is ALWAYS cables-off; detailed routing remains in compact badges.
      wire.click()
      ensure('collapsed-send' in body.get_attribute('class'),f'{width}: fold failed')
      ensure(layer.locator('path').count()==0,f'{width}: cables leaked into collapsed WIRE')
      ensure(legend.locator('.wire-map-group.is-in .wire-map-chip').count()==2,
             f'{width}: badge visibility lost in fold')
      ensure(not node(3,'.wire-tray').is_visible(),f'{width}: sockets leaked into PAN')
      wire.click()
      # A rail expansion may schedule the SVG render in the next animation
      # frame. Wait for the actual 2-IN/1-OUT contract, not an arbitrary sleep.
      page.wait_for_function("""()=>
          document.querySelectorAll('#wireLayer path.wire-incoming-line').length===2 &&
          document.querySelectorAll('#wireLayer path.wire-outgoing-line').length===1
      """,timeout=1500)
      ensure(incoming.count()==2 and outgoing.count()==1,
             f'{width}: expanded focus did not restore two directions')
      # Legacy Sends must also appear when the bus is selected, even if their
      # origin strips are off-bank (never invent off-screen cable coordinates).
      bank=page.locator('#bankSelect')
      bank.select_option(str(bank.locator('option').count()-1))
      bus=node(11,'.strip-identity-top')
      ensure(bus.count()==1,f'{width}: Bus A missing from last bank')
      bus.click()
      bus_in=legend.locator('.wire-map-group.is-in .wire-map-chip span').all_inner_texts()
      ensure(bus_in==['CH01','CH02'],f'{width}: legacy Bus receive badges missing {bus_in}')
      ensure(legend.locator('.wire-map-focus').inner_text()=='CH11',
             f'{width}: Bus A did not become focused channel')
      ensure(incoming.count()==0,
             f'{width}: renderer invented offscreen CH01/02 cable coordinates')
      # V27 real one-to-many physical OUT ferrule: 1 stem, 1 junction,
      # 2 independent destinations leave exactly the same branch coordinate.
      bank.select_option('0')
      connect(1,2) # CH01 already sends to CH03 (fixture above).
      select(1)
      branch=layer.locator('path.wire-outgoing-line')
      stems=layer.locator('path.wire-origin-stem')
      joins=layer.locator('circle.wire-origin-junction')
      ensure(branch.count()==2 and stems.count()==1 and joins.count()==1,
             f'{width}: overlapping duplicate cable stems or missing branches')
      starts=[d.split(' C ')[0] for d in branch.evaluate_all(
          '(els)=>els.map(e=>e.getAttribute("d"))')]
      ensure(len(set(starts))==1,
             f'{width}: 1-to-many cable starts diverge at OUT: {starts}')
      ensure(stems.first.get_attribute('d').startswith('M '),
             f'{width}: shared physical stem path missing')
      if width==1366:
        # 1244px owner screenshot: do not overlap labels and toolbar buttons.
        page.set_viewport_size({'width':1244,'height':900})
        hidden=page.locator('.command-bar>.toolbar-label').evaluate_all(
            '(els)=>els.every(e=>getComputedStyle(e).display==="none")')
        ensure(hidden,'1244: toolbar labels still clash with actual controls')
        controls=page.locator('.command-bar > :is(button,select,details)')
        boxes=controls.evaluate_all("""els=>els.map(el=>{
            const r=el.getBoundingClientRect();
            return [r.left,r.right,getComputedStyle(el).display];
        }).filter(e=>e[2]!=='none')""")
        ensure(all(boxes[i][1]<=boxes[i+1][0]+1 for i in range(len(boxes)-1)),
               f'1244: toolbar controls overlap {boxes}')
      ensure(not errors,f'{width}: JavaScript exceptions {errors}')
      page.screenshot(path=str(ROOT/f"preview-v26-focus-{width}.png"))
      page.close()
    browser.close()
    print("PASS V26 1366/1680/1920: selected IN behind OUT, source hue, both badges, collapse, Bus legacy across banks")

if __name__=="__main__":main()
