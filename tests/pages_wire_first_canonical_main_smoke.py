#!/usr/bin/env python3
"""WIRE-first A: native-execution NOT claimed. Real Chromium control-plane integration."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"site/preview/index.html"

def edge(page, channel, endpoint):
    selector=(f'#tracks .strip[data-id="{channel}"] .wire-node-out'
              if endpoint=="out" else '#master .wire-node-in')
    return page.locator(selector).get_attribute("data-wire-main-edge")

def main_button(page, channel):
    # Selection establishes the MAIN context; an armed OUT is not required.
    page.locator(f'#tracks .strip[data-id="{channel}"] .strip-identity-top').click()
    page.locator("#wireConfigToggle").click()
    control=page.locator("#wireMaster")
    assert control.is_enabled(), ("MAIN button disabled",channel)
    return control


def physical_main_plug(page):
    """Exercise the actual pinned MAIN jack pointer gesture, not a toggle button."""
    page.locator('#tracks .strip[data-id="5"] .strip-identity-top').click()
    body=page.locator(".console-body")
    if "collapsed-send" in (body.get_attribute("class") or ""):
        page.locator('.section-rail [data-section="send"]').click()
    jack=page.locator("#master .wire-node-in")
    source=page.locator('#tracks .strip[data-id="5"] .wire-node-out')
    original=source.get_attribute("data-wire-main-edge")
    assert original and jack.get_attribute("data-wire-main-edge")==original
    # The empty WIRE bay is an intentional detach target; avoid magnetic jacks.
    blank=page.evaluate("""() => {
      const bay=document.querySelector('#tracks .strip .send-section');
      const vp=document.getElementById('viewport');
      if(!bay||!vp)return null;
      const b=bay.getBoundingClientRect(),v=vp.getBoundingClientRect();
      const sockets=[...document.querySelectorAll('.wire-node-in')]
        .map(el=>el.getBoundingClientRect()).filter(r=>r.width&&r.height);
      for(let y=b.top+Math.min(20,b.height/2);y<b.bottom-3;y+=9)
        for(let x=v.left+18;x<v.right-18;x+=12)
          if(sockets.every(r=>Math.hypot(x-r.left-r.width/2,y-r.top-r.height/2)>42))
            return {x,y};
      return null;
    }""")
    assert blank, "no safe blank WIRE bay release position"
    def drag_to(point,escape=False):
        r=jack.bounding_box()
        assert r, "MAIN jack hidden"
        start=(r["x"]+r["width"]/2,r["y"]+r["height"]/2)
        page.mouse.move(*start)
        page.mouse.down()
        page.mouse.move(point["x"],point["y"],steps=12)
        if escape:
            page.keyboard.press("Escape")
        page.mouse.up()
    # Escape returns the original graph and edge identity unchanged.
    drag_to(blank,escape=True)
    assert source.get_attribute("data-wire-main-edge")==original,"Escape changed MAIN edge"
    assert jack.get_attribute("data-wire-main-edge")==original
    # Deliberate physical unplug disconnects ONLY the focused source.
    drag_to(blank)
    assert source.get_attribute("data-wire-main-edge") is None,"MAIN plug drag failed to unplug CH5"
    assert page.locator('#tracks .strip[data-id="6"] .wire-node-out').get_attribute("data-wire-main-edge"),"MAIN drag disconnected neighbor"
    page.locator("#wireNavigatorToggle").click()
    page.locator("#wireWsUndo").click()
    page.locator("#wireNavigatorToggle").click()
    assert source.get_attribute("data-wire-main-edge")==original,"Undo did not restore same MAIN edge"
    # Direct MAIN->CH3 replug preserves the edgeId and updates receiving socket.
    dest=page.locator('#tracks .strip[data-id="3"] .wire-node-in').bounding_box()
    assert dest, "CH3 receiver hidden"
    drag_to({"x":dest["x"]+dest["width"]/2,"y":dest["y"]+dest["height"]/2})
    assert source.get_attribute("data-wire-main-edge") is None,"Replug left duplicate direct MAIN edge"
    recv=page.locator('#tracks .strip[data-id="3"] .wire-gain')
    assert recv.count()==1,"Retargeted exact MAIN edge not reflected at receiving jack"
    assert recv.get_attribute("data-wire-id")==original,"Replug lost stable edge identity"
    page.locator("#wireNavigatorToggle").click()
    page.locator("#wireWsUndo").click()
    assert source.get_attribute("data-wire-main-edge")==original,"Retarget Undo did not restore MAIN"


def audit_visual_matrix(browser):
    """Nine real Chromium renders: viewport, density, focus, collapse, plug."""
    for width,height in ((1366,768),(1680,900),(1920,1080)):
        for density in ("comfort","standard","compact"):
            page=browser.new_page(viewport={"width":width,"height":height},
                                  reduced_motion="reduce")
            errors=[]
            page.on("pageerror",lambda exc: errors.append(str(exc)))
            page.goto(PAGE.as_uri())
            page.locator("#densitySelect").select_option(density)
            body=page.locator(".console-body")
            # Narrow/Compact intentionally locks the WIRE bay folded to keep
            # 23px physical geometry. It has graph-bound compact contacts,
            # not a full cable overlay; the rail refuses expansion by design.
            if density=="compact":
                assert "collapsed-send" in (body.get_attribute("class") or "")
                initial=edge(page,1,"out")
                assert initial, "folded view must retain the canonical MAIN graph edge"
                assert page.locator(
                    '#tracks .strip[data-id="1"] .wire-fold-summary'
                ).get_attribute("data-wire-main-edge") is None
                assert page.locator(
                    '#master .wire-fold-summary'
                ).get_attribute("data-wire-main-edge") is None
                assert page.locator('#tracks .strip[data-id="1"] .wire-fold-summary .has-out').count()==0
                assert page.locator(".wire-main-fragment").count()==0
                page.screenshot(path=str(PAGE.parents[2]/
                    f"pages-wire-main-{width}x{height}-{density}.png"))
                assert not errors,(width,height,density,errors)
                page.close()
                continue
            if "collapsed-send" in (body.get_attribute("class") or ""):
                page.locator('.section-rail [data-section="send"]').click()
            pair=page.evaluate("""() => {
              const src=document.querySelector('#tracks .strip[data-id="1"]');
              const main=document.querySelector('#master');
              const fragment=src?.querySelector('.wire-main-fragment');
              const incoming=main?.querySelector('.wire-main-fragment');
              const dot=src?.querySelector('.wire-node-out .wire-pin');
              const jack=main?.querySelector('.wire-node-in .wire-arrow');
              function position(svg,pin){
                if(!svg||!pin)return null;
                const r=svg.getBoundingClientRect(),p=pin.getBoundingClientRect();
                const c=svg.querySelector('.wire-main-plug');
                const x=Number(c?.getAttribute('cx')),y=Number(c?.getAttribute('cy'));
                if(!Number.isFinite(x)||!Number.isFinite(y))return null;
                return {error:Math.hypot(r.left+x-(p.left+p.width/2),
                      r.top+y-(p.top+p.height/2)),
                    bounded:x>0&&x<r.width&&y>0&&y<r.height,
                    cubic:svg.querySelector('.wire-main-jacket')?.getAttribute('d')?.includes(' C'),
                    width:r.width,height:r.height};
              }
              const strips=[...document.querySelectorAll('#tracks .strip')];
              return {source:fragment?.dataset.edgeId,main:incoming?.dataset.edgeId,
                connected:src?.querySelector('.wire-node-out')?.getAttribute('data-wire-main-edge'),
                sourceGeometry:position(fragment,dot),mainGeometry:position(incoming,jack),
                sources:strips.filter(x=>x.querySelector('.wire-node-out[data-wire-main-edge]')).length,
                painted:strips.filter(x=>x.querySelector('.wire-main-fragment')).length};
            }""")
            assert pair["source"] and pair["source"]==pair["main"]==pair["connected"],(
                width,height,density,"MAIN fragments do not share exact graph edge",pair)
            assert pair["sources"]>=1 and pair["painted"]==1,(
                width,height,density,"only selected channel may paint MAIN fragment",pair)
            for side in ("sourceGeometry","mainGeometry"):
                geom=pair[side]
                assert geom and geom["bounded"] and geom["cubic"] and geom["error"]<=1.1,(
                    width,height,density,side,"misaligned or clipped MAIN plug",geom)
            # Selection moves the SINGLE physical source fragment, never graph edges.
            page.locator('#tracks .strip[data-id="5"] .strip-identity-top').click()
            assert page.locator('#tracks .strip[data-id="1"] .wire-main-fragment').count()==0
            assert page.locator('#tracks .wire-main-fragment').count()==1
            focused=page.locator('#master .wire-main-fragment').get_attribute("data-edge-id")
            assert focused==edge(page,5,"out") and focused!=pair["source"],(
                width,height,density,"focused MAIN edge selection")
            page.locator('.section-rail [data-section="send"]').click()
            assert page.locator('.wire-main-fragment').count()==0,(
                width,height,density,"collapsed MAIN fragments not retired")
            page.locator('.section-rail [data-section="send"]').click()
            assert page.locator('#master .wire-main-fragment').get_attribute("data-edge-id")==focused
            page.screenshot(path=str(PAGE.parents[2]/
                f"pages-wire-main-{width}x{height}-{density}.png"))
            assert not errors,(width,height,density,errors)
            page.close()

def run():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        audit_visual_matrix(browser)
        for width in (1366,1680,1920):
            page=browser.new_page(viewport={"width":width,"height":900})
            errors=[]
            page.on("pageerror", lambda exc: errors.append(str(exc)))
            page.goto(PAGE.as_uri())
            assert not errors,("startup exception",width,errors)
            # Every initial channel has a single real default MAIN edge.
            initial=edge(page,5,"out")
            assert initial and initial.isdigit(),(width,"no canonical MAIN edge")
            assert not edge(page,5,"out")==edge(page,6,"out"),(width,"nonunique edge IDs")
            # MAIN is pinned; its second stub follows the uniquely focused CH5.
            control=main_button(page,5)
            assert control.get_attribute("aria-pressed")=="true"
            if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                # Folded view suppresses physical MAIN fragments; graph remains.
                assert page.locator('#master .wire-node-in').get_attribute("data-wire-main-edge") is None
                page.locator('.section-rail [data-section="send"]').click()
            assert page.locator('#master .wire-node-in').get_attribute("data-wire-main-edge")==initial
            # One graph edge paints TWO measured local cubic cables, never a
            # cross-bank line. Geometry and edge identity survive all widths.
            source_stub=page.locator('#tracks .strip[data-id="5"] .wire-main-fragment')
            main_stub=page.locator('#master .wire-main-fragment')
            assert source_stub.count()==1 and main_stub.count()==1,(
                width,"missing paired physical MAIN cables")
            assert source_stub.get_attribute("data-edge-id")==initial
            assert main_stub.get_attribute("data-edge-id")==initial
            assert " C" in source_stub.locator(".wire-main-jacket").get_attribute("d")
            assert " C" in main_stub.locator(".wire-main-jacket").get_attribute("d")
            assert page.locator('#tracks .strip[data-id="6"] .wire-main-fragment').count()==0

            # Distinct disconnect is reversible and only affects this channel.
            control.click()
            assert edge(page,5,"out") is None,(width,"MAIN disconnect not real")
            assert edge(page,6,"out"),(width,"neighbor MAIN edge removed")
            assert page.locator('#master .wire-node-in').get_attribute("data-wire-main-edge") is None
            page.locator("#wireNavigatorToggle").click()
            page.locator("#wireWsUndo").click()
            assert edge(page,5,"out")==initial,(width,"Undo changed identity")
            page.locator("#wireNavigatorToggle").click()
            # Navigator closes Wire Setup automatically; no hidden close click.
            # The accepted WIRE to another channel must preserve the MAIN route.
            if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                page.locator('.section-rail [data-section="send"]').click()
            # MAIN is a many-input sink: explicitly focus the one source
            # whose plug is being handled, after Navigator and disclosure UI.
            page.locator('#tracks .strip[data-id="5"] .strip-identity-top').click()
            assert page.locator('#master .wire-node-in').get_attribute(
                "data-wire-main-edge")==initial,"MAIN focus lost exact-edge identity"
            # Actual pointer from pinned MAIN socket must remove CH05 only.
            # Choose the furthest safe point inside the *measured* MAIN well.
            if width==1680:
                jack=page.locator('#master .wire-node-in').bounding_box()
                bay=page.locator('#master .send-section').bounding_box()
                assert jack and bay,(width,"physical MAIN jack geometry")
                x0=jack["x"]+jack["width"]/2
                y0=jack["y"]+jack["height"]/2
                corners=[(bay["x"]+9,bay["y"]+9),
                         (bay["x"]+bay["width"]-9,bay["y"]+9),
                         (bay["x"]+9,bay["y"]+bay["height"]-9),
                         (bay["x"]+bay["width"]-9,bay["y"]+bay["height"]-9)]
                x1,y1=max(corners,key=lambda p:(p[0]-x0)**2+(p[1]-y0)**2)
                assert (x1-x0)**2+(y1-y0)**2>34**2,"No legitimate MAIN unplug drop area"
                page.evaluate("""() => {
                    window.__wirePointerProbe=[];
                    for(const event of ['pointerdown','pointermove','pointerup'])
                      window.addEventListener(event, e => {
                        if(window.__wirePointerProbe.length<40)
                          window.__wirePointerProbe.push({
                            event,tag:e.target.tagName,cls:String(e.target.className?.baseVal||e.target.className||''),
                            x:Math.round(e.clientX),y:Math.round(e.clientY),
                            hits:[...document.elementsFromPoint(e.clientX,e.clientY)].slice(0,3)
                              .map(el=>el.tagName+'.'+String(el.className?.baseVal||el.className||''))
                          });
                      },true);
                  }""")
                page.mouse.move(x0,y0)
                page.mouse.down()
                page.mouse.move(x1,y1,steps=12)
                page.mouse.up()
                assert edge(page,5,"out") is None,(
                    "MAIN pointer drag failed",page.locator("#info").inner_text(),
                    "socket",jack,"bay",bay,"drop",(x1,y1),
                    "main-disabled",page.locator('#master .wire-node-in').get_attribute("disabled"),
                    "master-highlight",page.locator('#master .wire-node-in').get_attribute("class"),
                    "event-probe",page.evaluate("window.__wirePointerProbe"))
                assert edge(page,6,"out"),"MAIN drag unplugged other source"
                page.locator("#wireNavigatorToggle").click()
                page.locator("#wireWsUndo").click()
                assert edge(page,5,"out")==initial,"MAIN pointer undo changed edge identity"
                page.locator("#wireNavigatorToggle").click()
                page.screenshot(path=str(PAGE.parents[2]/"pages-wire-main-1680.png"))
            page.locator('#tracks .strip[data-id="5"] .wire-node-out').click()
            page.locator('#tracks .strip[data-id="3"] .wire-node-in').click()
            assert page.locator('#tracks .strip[data-id="3"] .wire-gain').count()==1
            assert edge(page,5,"out")==initial,(width,"parallel SEND removed MAIN")
            # A reverse edge would close the directed cycle: reject atomically.
            page.locator('#tracks .strip[data-id="3"] .wire-node-out').click()
            page.locator('#tracks .strip[data-id="5"] .wire-node-in').click()
            assert page.locator('#tracks .strip[data-id="5"] .wire-gain').count()==0
            assert page.locator('#tracks .strip[data-id="3"] .wire-gain').count()==1
            assert edge(page,5,"out")==initial
            assert not errors,(width,errors)
            page.close()
        # The pinned MAIN jack itself must be draggable, cancellable and reversible.
        physical_page=browser.new_page(viewport={"width":1680,"height":900})
        physical_page.goto(PAGE.as_uri())
        physical_main_plug(physical_page)
        physical_page.close()
        # A versioned Scene must remember that a MAIN wire was unplugged.
        page=browser.new_page(viewport={"width":1680,"height":900})
        page.goto(PAGE.as_uri())
        control=main_button(page,5)
        control.click()
        page.locator("#saveScene").click()
        assert edge(page,5,"out") is None
        control.click()
        assert edge(page,5,"out")
        page.locator("#recallScene").click()
        assert edge(page,5,"out") is None,"Scene recreated an intentionally unplugged MAIN"
        # Snapshot SAFE preserves the current unpatched source against old scene.
        control.click()
        page.locator("#saveScene").click()
        control.click()
        page.locator("#moreTools summary").click()
        page.locator("#sceneSafe").click()
        page.locator("#moreTools summary").click()
        page.locator("#sceneSelect").select_option("1")
        page.locator("#recallScene").click()
        assert edge(page,5,"out") is None,"Snapshot SAFE overwrote live MAIN route"
        page.close()
        browser.close()
    print("PASS WIRE A: nine Chromium viewport/density cable audits plus exact-edge MAIN gesture, Scene/SAFE, Undo")

if __name__=="__main__":
    run()
