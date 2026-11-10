#!/usr/bin/env python3
"""Native-only desktop toolbar: real assembled WebView DOM, no DSP simulation.

Verify breathing room, semantic group separators, no overlapped controls and
the original file/menu actions at ordinary and minimum Windows widths.
"""
import importlib.util
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright

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
            page.add_init_script("""window.__nativeListeners=new Map();window.__events=[];
                window.__JUCE__={backend:{
                    emitEvent(name,payload){window.__events.push({name,payload});},
                    addEventListener(name,callback){window.__nativeListeners.set(name,callback);}
                }};""")
            page.goto((dst / "index.html").as_uri())
            page.wait_for_function("""()=>document.documentElement.classList.contains('armix-native-desktop') &&
                document.querySelectorAll('.command-bar .mixer-file-tool').length===2 &&
                document.querySelectorAll('.command-bar .armix-toolbar-divider').length===4 &&
                !!document.getElementById('playerToggle')""")
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
