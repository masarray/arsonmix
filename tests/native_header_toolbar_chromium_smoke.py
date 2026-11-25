#!/usr/bin/env python3
"""Native-only desktop toolbar: real assembled WebView DOM, no DSP simulation.

Verify breathing room, semantic group separators, no overlapped controls and
the original file/menu actions at ordinary and minimum Windows widths.
"""
import importlib.util
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright
from native_event_fixture import NATIVE_EVENTS

ROOT = Path(__file__).resolve().parents[1]


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
                document.querySelectorAll('.command-bar .mixer-file-tool').length===2 &&
                document.querySelectorAll('.command-bar .armix-toolbar-divider').length===3 &&
                !document.querySelector('.command-bar [data-native-monitor]') &&
                !!document.querySelector('[data-native-emergency-output]') &&
                !!document.getElementById('playerToggle')""")
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
              const visible=[...bar.children].filter(el=>{
                 const r=el.getBoundingClientRect(),cs=getComputedStyle(el);
                 return r.width>0 && r.height>0 && cs.display!=='none' &&
                     !el.classList.contains('armix-toolbar-divider') &&
                     !el.classList.contains('sep');
              }).map(el=>({id:el.id||el.textContent.trim().slice(0,12),
                  left:el.getBoundingClientRect().left,
                  right:el.getBoundingClientRect().right}));
              return {bar:{height:b.height,left:b.left,right:b.right},
                more:{left:more.left,right:more.right},
                visible,dividers:[...bar.querySelectorAll('.armix-toolbar-divider')]
                 .map(el=>({hidden:getComputedStyle(el).display==='none',
                           height:el.getBoundingClientRect().height}))};
            }""")
            assert metrics["bar"]["height"] == 43, (width, "incorrect native toolbar height", metrics)
            assert metrics["more"]["right"] <= width + 1, (
                width, "overflow More menu outside window", metrics)
            assert metrics["dividers"][0]["height"] in (0, 21), (
                width, "divider layout changed", metrics)
            visible = metrics["visible"]
            for a, b in zip(visible, visible[1:]):
                # More can be pushed to the far right, never overlaid on controls.
                assert a["right"] <= b["left"] + 1, (
                    width, "toolbar controls overlap", a, b)
            for item in visible:
                assert item["left"] >= -1 and item["right"] <= width+1, (
                    width, "toolbar control outside viewport", item)
            for caption in ("Save", "Open"):
                button = page.locator('.command-bar .mixer-file-tool').filter(has_text=caption)
                assert button.count() == 1 and button.is_visible(), (
                    width, "native file control not usable", caption)
                button.click()
                assert page.evaluate("()=>window.__events.at(-1).payload.action") == (
                    "nativeMixerFile"+caption), (width, "file action disconnected", caption)
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
            assert not errors, (width, "native JavaScript errors", errors)
            print("NATIVE_TOOLBAR", width, metrics["bar"], len(visible), flush=True)
            page.close()
        browser.close()
    print("PASS native toolbar: spacing, group borders, actions, four viewports")


if __name__ == "__main__":
    main()
