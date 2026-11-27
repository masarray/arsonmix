#!/usr/bin/env python3
"""Native-only desktop toolbar: real assembled WebView DOM, no DSP simulation.

Verify breathing room, semantic group separators, no overlapped controls and
the original file/menu actions at ordinary and minimum Windows widths.
"""
import base64
import math
import importlib.util
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS

ROOT = Path(__file__).resolve().parents[1]


def assert_ribbon_seam(page, width, *, check_pixels=False, evidence_suffix=""):
    # Test the painted SVG and its summary, not just the details wrapper.
    geometry = page.evaluate("""()=>{
      const box=el=>{const r=el.getBoundingClientRect();
        return {x:r.x,y:r.y,right:r.right,bottom:r.bottom,w:r.width,h:r.height};};
      const menu=document.querySelector('#moreTools>summary');
      const tab=document.querySelector('#ribbon-tab-mixer');
      const nav=document.querySelector('.armix-ribbon-nav');
      return {menu:box(menu),face:box(menu.querySelector('.armix-ribbon-menu-shape')),
        tab:box(tab),tabFace:box(tab.querySelector('svg')),nav:box(nav),
        rail:parseFloat(getComputedStyle(nav).borderBottomWidth),
        max:getComputedStyle(menu).maxHeight,
        transform:getComputedStyle(menu).transform};
    }""")
    m, f, t, tf, n = (geometry[key] for key in ("menu","face","tab","tabFace","nav"))
    assert geometry["max"]=="32px" and geometry["transform"]=="none", (width, geometry)
    assert all(abs(r["h"]-32)<0.1 for r in (m,f,t,tf)), (width, geometry)
    assert max(r["y"] for r in (m,f,t,tf))-min(r["y"] for r in (m,f,t,tf))<0.25, (width, geometry)
    assert all(abs(r["bottom"]-(n["bottom"]-geometry["rail"]))<0.25 for r in (m,f,t,tf)), (width, geometry)
    assert abs(m["right"]-t["x"]-22)<0.25, (width, geometry)
    if check_pixels:
        # Sample inside the lower diagonal where menu overlaps active Mixer.
        # Before correction the overlap sample was cyan, not dark like the menu.
        x0=math.floor(m["x"])
        y0=math.floor(m["y"])
        clip={"x":x0,"y":y0,"width":math.ceil(t["right"])-x0,
              "height":math.ceil(t["bottom"])-y0}
        png=page.screenshot(clip=clip,scale="css")
        samples=page.evaluate("""async ({png,points})=>{
          const im=new Image();im.src='data:image/png;base64,'+png;await im.decode();
          const c=document.createElement('canvas');c.width=im.width;c.height=im.height;
          const ctx=c.getContext('2d');ctx.drawImage(im,0,0);
          return points.map(([x,y])=>Array.from(ctx.getImageData(x,y,1,1).data).slice(0,3));
        }""",{"png":base64.b64encode(png).decode("ascii"),"points":[
          [math.floor(m["x"]+45)-x0,math.floor(m["bottom"])-2-y0],
          [math.floor(m["right"]-12)-x0,math.floor(m["bottom"])-2-y0],
          [math.floor(t["x"]+45)-x0,math.floor(t["bottom"])-2-y0]]})
        left,seam,active=samples
        distance=lambda a,b:sum(abs(x-y) for x,y in zip(a,b))
        assert distance(left,seam)<=18 and distance(seam,active)>=45,(width,geometry,samples)
        Path(f"operator-ux-toolbar-seam-{width}{evidence_suffix}.png").write_bytes(png)

def assert_menu_color_role(page, workflow, opened):
    # Read the rendered SVG stop colors, not merely authored CSS tokens.
    # Native <details> remains the disclosure authority; one tab is selected.
    state=page.evaluate("""()=>{
      const menu=document.querySelector('#moreTools');
      const summary=menu.querySelector('summary');
      const selected=[...document.querySelectorAll('.armix-ribbon-tabs [aria-selected=true]')];
      const stops=el=>[...el.querySelectorAll('stop')].map(s=>getComputedStyle(s).stopColor);
      return {open:menu.open,selected:selected.map(t=>t.id),
        menuColors:stops(summary),tabColors:selected.length===1?stops(selected[0]):[],
        label:getComputedStyle(summary).color,
        brand:getComputedStyle(summary.querySelector('.armix-menu-brand-accent')).color,
        closedGlyph:getComputedStyle(summary.querySelector('.armix-menu-icon-closed')).display,
        openGlyph:getComputedStyle(summary.querySelector('.armix-menu-icon-open')).display};
    }""")
    assert state["open"]==opened and state["selected"]==["ribbon-tab-"+workflow],state
    assert state["tabColors"]==["rgb(54, 184, 218)","rgb(32, 161, 195)","rgb(8, 127, 159)"],state
    assert state["menuColors"]!=state["tabColors"],state
    def rgb(color):
        return [int(v) for v in color.removeprefix("rgb(").removesuffix(")").split(",")]
    def luminance(color):
        channels=[v/255 for v in rgb(color)]
        return sum(w*(v/12.92 if v<=0.04045 else ((v+0.055)/1.055)**2.4)
                   for w,v in zip((.2126,.7152,.0722),channels))
    for stop in state["menuColors"]:
        red,green,blue=rgb(stop)
        assert blue>red>green,("amethyst role lost",state)
        for fg in (state["label"],state["brand"]):
            ratio=(max(luminance(fg),luminance(stop))+.05)/(min(luminance(fg),luminance(stop))+.05)
            assert ratio>=4.5,("menu text contrast",ratio,state)
    assert (state["openGlyph"]!="none")==opened,state
    assert (state["closedGlyph"]!="none")== (not opened),state
    return state

def main():
    spec = importlib.util.spec_from_file_location(
        "native_bundle", ROOT / "scripts/build_approved_native_gui.py")
    bundle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with tempfile.TemporaryDirectory(prefix="arsonmix-toolbar-") as folder, sync_playwright() as pw:
        dst = Path(folder)
        bundle.stage(dst)
        browser = pw.chromium.launch(headless=True)
        for width, height in ((1680, 900), (1366, 768), (1200, 740), (1100, 650)):
            page = browser.new_page(viewport={"width": width, "height": height})
            errors = []
            page.on("pageerror", lambda err: errors.append(str(err)))
            page.add_init_script(NATIVE_EVENTS + """window.__events=[];
                window.__JUCE__={backend:{
                    emitEvent(name,payload){window.__events.push({name,payload});},
                    ...window.__nativeListenerMethods
                }};""")
            page.goto((dst / "index.html").as_uri())
            page.wait_for_function("""()=>document.documentElement.classList.contains('armix-native-desktop') &&
                document.querySelectorAll('.command-bar .mixer-file-tool').length===3 &&
                document.querySelectorAll('.command-bar .armix-toolbar-divider').length===3 &&
                !document.querySelector('.command-bar [data-native-monitor]') &&
                !!document.querySelector('[data-native-emergency-output]') &&
                !!document.getElementById('playerToggle')""")
            # Actual assembled controls, with native event readback fixture.
            cap=page.locator(".armix-ribbon-window-controls")
            assert cap.is_visible() and cap.locator("button").count()==3
            assert cap.locator("button:disabled").count()==3
            assert page.evaluate("()=>window.__events.some(e=>e.name==='arMixRibbonWindow'&&e.payload.operation==='READY')")
            page.evaluate("()=>window.__nativeListeners.get('arMixRibbonWindowState')({active:true,maximised:false,fullscreen:false,minimised:false})")
            assert cap.locator("button:enabled").count()==3
            geometry=cap.locator("button").evaluate_all("""nodes=>nodes.map(e=>{const r=e.getBoundingClientRect();return {x:r.x,right:r.right,y:r.y,w:r.width,h:r.height}})""")
            assert all(g["w"]==34 and g["h"]==32 for g in geometry),(width,geometry)
            assert all(abs(a["right"]-b["x"])<0.1 for a,b in zip(geometry,geometry[1:])),(width,geometry)
            assert abs(geometry[-1]["right"]-page.locator(".armix-ribbon-nav").bounding_box()["width"]-page.locator(".armix-ribbon-nav").bounding_box()["x"]+9)<1,(width,geometry)
            for ident,operation in [("ribbonWinMin","MINIMISE"),("ribbonWinMax","TOGGLE_MAXIMISE"),("ribbonWinClose","CLOSE")]:
                page.locator("#"+ident).click()
                assert page.evaluate("()=>window.__events.at(-1)")=={"name":"arMixRibbonWindow","payload":{"operation":operation}}
            page.evaluate("()=>window.__nativeListeners.get('arMixRibbonWindowState')({active:true,maximised:true,fullscreen:false,minimised:false})")
            assert page.locator("#ribbonWinMax").get_attribute("aria-label")=="Restore"
            page.evaluate("()=>window.__nativeListeners.get('arMixRibbonWindowState')({active:true,maximised:false,fullscreen:false,minimised:false})")
            assert page.locator("#ribbonWinMax").get_attribute("aria-label")=="Maximize"
            page.locator("#ribbonWinClose").hover()
            page.wait_for_function("""()=>getComputedStyle(document.getElementById('ribbonWinClose')).backgroundColor==='rgb(180, 66, 72)'""")
            page.mouse.move(10,100)
            if width==1366:
                page.screenshot(path="operator-ux-toolbar-window-controls-1366.png",clip={"x":width-400,"y":0,"width":400,"height":80})
            assert page.locator(".command-bar [data-native-extra-output], .command-bar [data-native-first-sound]").count() == 0
            assert page.locator(".command-bar").get_by_text("Mute Output", exact=True).count() == 0
            assert page.locator(".command-bar").get_by_text("Resume Output", exact=True).count() == 0
            # UX-1 visual regression: evaluate the ACTUAL native assembled
            # /preview/ CSSOM, not source-only checks against retired HTML.
            # Real Chromium must resolve the clock/CPU/RT/RAM SVG masks and
            # sober normal/attention/critical colors at every desktop width.
            footer = page.evaluate("""()=>{
              const left=document.getElementById('transport');
              const right=document.querySelector('.status-bar .warning');
              const oldLeft=left.textContent, oldRight=right.textContent;
              const oldLeftMode=left.getAttribute('data-telemetry');
              const oldRightMode=right.getAttribute('data-telemetry');
              left.textContent='LAT ~20.0 ms';
              left.dataset.telemetry='true';
              right.dataset.telemetry='true';
              const spans=['cpu','rt','ram'].map((name,index)=>{
                const node=document.createElement('span');
                node.className='armix-health-stat';
                node.dataset.metric=name;
                node.dataset.tone=['normal','attention','critical'][index];
                node.textContent=['CPU App 4.2%','RT last 84%','RAM 318 MiB'][index];
                return node;
              });
              right.replaceChildren(...spans);
              const mask=el=>{
                const style=getComputedStyle(el,'::before');
                return {content:style.content,mask:style.webkitMaskImage||style.maskImage,
                  width:style.width};
              };
              const colors=spans.map(el=>getComputedStyle(el).color);
              const result={left:mask(left),icons:spans.map(mask),colors,
                footerHeight:document.querySelector('.status-bar').getBoundingClientRect().height,
                labelHeight:left.getBoundingClientRect().height};
              left.textContent=oldLeft;right.textContent=oldRight;
              if(oldLeftMode===null)left.removeAttribute('data-telemetry');
              else left.setAttribute('data-telemetry',oldLeftMode);
              if(oldRightMode===null)right.removeAttribute('data-telemetry');
              else right.setAttribute('data-telemetry',oldRightMode);
              return result;
            }""")
            for icon in (footer["left"], *footer["icons"]):
                assert "data:image/svg+xml" in icon["mask"] and icon["width"] == "11px", (
                    width, "UX-1 native status icon is missing from compiled CSS", footer)
            assert len(set(footer["colors"])) == 3, (
                width, "native status bar has lost normal/attention/critical colors", footer)
            assert footer["footerHeight"] == 20, (
                width, "native footer must remain a compact 20px strip", footer)
            metrics = page.evaluate("""()=>{
              const bar=document.querySelector('.command-bar');
              const b=bar.getBoundingClientRect();
              const more=document.querySelector('#moreTools').getBoundingClientRect();
              const nav=document.querySelector('.armix-ribbon-nav').getBoundingClientRect();
              const visible=[...bar.children].filter(el=>{
                 const r=el.getBoundingClientRect(),cs=getComputedStyle(el);
                 return el.id!=='moreTools' && r.width>0 && r.height>0 && cs.display!=='none' && cs.visibility!=='hidden' &&
                     !el.classList.contains('armix-toolbar-divider') &&
                     !el.classList.contains('sep');
              }).map(el=>({id:el.id||el.textContent.trim().slice(0,12),
                  left:el.getBoundingClientRect().left,
                  right:el.getBoundingClientRect().right}));
              const tail=[...bar.children].slice(-3).map(el=>el.id||
                (el.hasAttribute('data-native-io')?'audioIO':'unknown'));
              const css=getComputedStyle(bar);
              return {bar:{height:b.height,left:b.left,right:b.right},
                gap:parseFloat(css.columnGap),paddingRight:parseFloat(css.paddingRight),
                tail,more:{left:more.left,right:more.right,bottom:more.bottom},nav:{height:nav.height,bottom:nav.bottom},
                visible,dividers:[...bar.querySelectorAll('.armix-toolbar-divider')]
                 .map(el=>({hidden:getComputedStyle(el).display==='none',
                           height:el.getBoundingClientRect().height}))};
            }""")
            assert metrics["bar"]["height"] == 38, (width, "incorrect native toolbar height", metrics)
            assert metrics["more"]["right"] <= width + 1, (
                width, "overflow More menu outside window", metrics)
            assert metrics["dividers"][0]["height"] in (0, 21), (
                width, "divider layout changed", metrics)
            visible = metrics["visible"]
            # V6 menu belongs to the navigation row, not the command row.
            assert metrics['nav']['height']==40 and abs(metrics['more']['bottom']-metrics['nav']['bottom'])<=3,(width,metrics)
            assert page.locator('.armix-ribbon-tabs [role=tab]').count()==6
            assert_ribbon_seam(page,width,check_pixels=True)
            closed_color=assert_menu_color_role(page,"mixer",False)
            tabs=page.locator('.armix-ribbon-tabs [role=tab]').evaluate_all("""nodes=>nodes.map(el=>({z:Number(getComputedStyle(el).zIndex),x:el.getBoundingClientRect().x,w:el.getBoundingClientRect().width,path:el.querySelector('path').getAttribute('d')}))""")
            assert all(tabs[i]["z"]>tabs[i+1]["z"] for i in range(5)),(width,tabs)
            assert all(abs(tabs[i]["x"]+tabs[i]["w"]-tabs[i+1]["x"]-22)<=1 for i in range(5)),(width,tabs)
            assert all("H102 Q109 0 115 12" in t["path"] for t in tabs),(width,tabs)
            assert page.locator('.armix-ribbon-nav .logo').is_hidden()
            assert page.locator('.armix-ribbon-nav .file-name').is_hidden()
            assert page.locator('.armix-ribbon-nav #trackCount').is_hidden()
            ids=[x["id"] for x in visible]
            assert ids.index("bankPrev")<ids.index("bankSelect")<ids.index("bankNext")<ids.index("viewFilter")<ids.index("addTrack")<ids.index("pluginsButton"),(width,ids)
            assert not page.locator("#playerToggle").is_visible()
            assert page.locator("#ribbonQuickPlayer").is_visible()
            assert page.locator("#viewFilter").count()==1
            assert ids[-2:]==["mixContext","ribbonEmergency"],(width,ids)
            assert page.locator("#ribbonEmergency").is_disabled()  # no hardware in this fixture
            assert page.locator("[data-native-emergency-output]").get_attribute("data-emergency-mode")=="stop"
            assert page.locator("#pluginsButton").evaluate("el=>getComputedStyle(el,'::before').maskImage")!="none"
            assert page.locator("#mixContext").evaluate("el=>getComputedStyle(el,'::before').maskImage")!="none"
            if width==1366:
                page.screenshot(path="operator-ux-toolbar-mixer-1366.png")
            for a, b in zip(visible, visible[1:]):
                # More can be pushed to the far right, never overlaid on controls.
                assert a["right"] <= b["left"] + 1, (
                    width, "toolbar controls overlap", a, b)
            for item in visible:
                assert item["left"] >= -1 and item["right"] <= width+1, (
                    width, "toolbar control outside viewport", item)
            # Real file group remains in the original menu DOM.
            menu=page.locator("#moreTools")
            assert menu.evaluate("(el)=>el===el.parentElement.firstElementChild"), width
            assert page.locator(".command-bar > .mixer-file-tool").count()==0
            menu.locator(":scope > summary").click()
            assert_ribbon_seam(page,width,check_pixels=True,evidence_suffix="-menu-open")
            open_color=assert_menu_color_role(page,"mixer",True)
            assert open_color["menuColors"]!=closed_color["menuColors"]
            file_group = menu.locator(".mixer-file-tool")
            assert file_group.all_text_contents() == ["New", "Save", "Open"], width
            new = menu.locator("[data-native-new-project]")
            assert new.count() == 1 and new.is_visible(), width
            panel=menu.locator(".more-tools-panel")
            assert panel.bounding_box()["height"]<=300,(width,panel.bounding_box())
            assert panel.locator(":scope > .armix-menu-category:visible").count()==5
            assert panel.locator(":scope > [data-armix-menu-active=false]:visible").count()==0
            assert menu.locator("[data-native-emergency-output]").is_visible()
            assert not panel.get_by_text("Copy Audio Report",exact=True).is_visible()
            page.screenshot(path=f"operator-ux-application-menu-{width}.png")
            panel.locator('[data-menu-category="diagnostics"]').click()
            assert panel.get_by_text("Copy Audio Report",exact=True).is_visible()
            assert not new.is_visible()
            page.screenshot(path=f"operator-ux-menu-diagnostics-{width}.png")
            panel.get_by_text("Copy Audio Report",exact=True).click()
            assert page.evaluate("()=>window.__events.at(-1).payload.action")=="copyDiagnosticReport"
            panel.locator(".armix-menu-back").click()
            assert new.is_visible()
            for category in ("channel","view","scenes","settings"):
                panel.locator('[data-menu-category="'+category+'"]').click()
                assert panel.locator(".armix-menu-back").is_visible()
                assert panel.locator(":scope > [data-armix-menu-active=false]:visible").count()==0
                page.keyboard.press("Escape")
                assert new.is_visible()
            assert panel.bounding_box()["height"]<=300
            assert new.get_by_role("img").count() == 0  # decorative icon
            image = new.locator("img")
            assert image.get_attribute("alt") == ""
            assert image.get_attribute("src") == "assets/mixer-new.svg"
            page.wait_for_function("()=>{const i=document.querySelector('[data-native-new-project] img');return i.complete&&i.naturalWidth>0}")
            assert image.bounding_box()["width"] == 16 and image.bounding_box()["height"] == 16, width
            for caption in ("Save", "Open"):
                button = page.locator('.command-bar .mixer-file-tool').filter(has_text=caption)
                assert button.count() == 1 and button.is_visible(), (
                    width, "native file control not usable", caption)
                button.click()
                assert page.evaluate("()=>window.__events.at(-1).payload.action") == (
                    "nativeMixerFile"+caption), (width, "file action disconnected", caption)
            menu.locator(":scope > summary").click()
            # Native-only status presenter must not expose internal FAT,
            # PCM graph revision counters, or simulation copy in the UI.
            operator_footer = page.evaluate("""async()=>{
              document.getElementById('info').textContent =
                'NATIVE PCM GRAPH ACCEPTED · FAT PENDING · device 1 · graph 3 · roster 94';
              document.querySelector('.status-bar .warning').textContent =
                'NATIVE PCM · HARDWARE FAT PENDING';
              document.getElementById('transport').textContent =
                'MONITOR ON · OUT Headphones · NATIVE PCM GRAPH ACCEPTED · FAT PENDING';
              // MutationObserver reactions run at the microtask checkpoint;
              // a frame guarantees presentation has completed.
              await Promise.resolve();
              await new Promise(requestAnimationFrame);
              return {
                info: document.getElementById('info').textContent,
                warning: document.querySelector('.status-bar .warning').textContent,
                transport: document.getElementById('transport').textContent
              };
            }""")
            visible_footer = " ".join(str(v) for v in operator_footer.values())
            assert operator_footer["info"] == "Mixer ready", (width,operator_footer)
            assert operator_footer["warning"] == "Mixer ready", (width,operator_footer)
            assert operator_footer["transport"] == "Output enabled", (width,operator_footer)
            assert not __import__("re").search(
                r"FAT|PCM|NATIVE|GRAPH|ROSTER|SIMULATED|MOCKUP",
                visible_footer,__import__("re").IGNORECASE), (width,operator_footer)
            more = page.locator("#moreTools>summary")
            more.click()
            assert page.locator("#moreTools").get_attribute("open") is not None, (
                width, "More menu did not open")
            more.click()
            assert_ribbon_seam(page,width)
            assert_menu_color_role(page,"mixer",False)
            # Keyboard disclosure and Escape preserve the current workspace.
            more.focus()
            page.keyboard.press("Enter")
            assert_menu_color_role(page,"mixer",True)
            page.keyboard.press("Escape")
            assert_menu_color_role(page,"mixer",False)
            more.hover()
            page.mouse.down()
            try:
                assert_ribbon_seam(page,width)
            finally:
                page.mouse.move(width-10,height-10)
                page.mouse.up()
            if width==1366:
                for workflow in ("player","fx","routing","scenes","io"):
                    page.locator("#ribbon-tab-"+workflow).click()
                    assert page.locator("#ribbon-tab-"+workflow).get_attribute("aria-selected")=="true"
                    assert page.locator("#ribbonEmergency").is_visible()
                    if workflow=="io":
                        assert page.locator("[data-ribbon-audio-summary]").is_visible()
                        assert page.locator("[data-ribbon-audio-summary] [data-audio-value]").all_text_contents()==["No device","—","—","—"]
                    page.screenshot(path=f"operator-ux-toolbar-{workflow}-1366.png")
                    assert_menu_color_role(page,workflow,False)
                    more.click()
                    assert_menu_color_role(page,workflow,True)
                    page.screenshot(path=f"operator-ux-menu-{workflow}-1366.png")
                    page.keyboard.press("Escape")
                    assert_menu_color_role(page,workflow,False)
            assert not errors, (width, "native JavaScript errors", errors)
            print("NATIVE_TOOLBAR", width, metrics["bar"], len(visible), flush=True)
            page.close()
        # Fractional Windows display scaling must retain the same baseline.
        for scale in (1.25,1.5):
            page=browser.new_page(viewport={"width":1366,"height":768},device_scale_factor=scale)
            page.add_init_script(NATIVE_EVENTS + """window.__events=[];
                window.__JUCE__={backend:{
                    emitEvent(name,payload){window.__events.push({name,payload});},
                    ...window.__nativeListenerMethods
                }};""")
            page.goto((dst/"index.html").as_uri())
            page.locator("#ribbon-tab-mixer").wait_for(state="visible")
            assert_ribbon_seam(page,1366)
            page.screenshot(path=f"operator-ux-toolbar-seam-dpi-{scale}.png",
                            clip={"x":0,"y":0,"width":310,"height":44})
            page.close()
        browser.close()
    print("PASS native toolbar: spacing, group borders, actions, four viewports and ribbon seam DPI checks")


if __name__ == "__main__":
    main()
