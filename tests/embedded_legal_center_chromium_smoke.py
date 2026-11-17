#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Real Chromium audit of the native-only embedded legal center.

Runs the exact assembled desktop DOM with a JUCE transport stub. The tiny
local HTTP server supplies the same exact-source notices that JUCE BinaryData
serves in the production executable. It does not simulate audio or PCM.
"""
from __future__ import annotations

import importlib.util
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread
from tempfile import TemporaryDirectory
from urllib.parse import urlsplit

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTS = {
    "overview": "LICENSE-NOTICE.txt",
    "agpl": "LICENSE",
    "hrir": "docs/THIRD_PARTY_8D_SONKU_HRTF.md",
    "signalsmith-stretch": "third_party/signalsmith/LICENSE-STRETCH.txt",
    "signalsmith-linear": "third_party/signalsmith/LICENSE-LINEAR.txt",
    "inter": "web/assets/fonts/OFL-LICENSE-Inter.txt",
    "vb-cable": "installer/VB-CABLE-NOTICE.txt",
}


def main() -> None:
    spec = importlib.util.spec_from_file_location(
        "native_bundle", ROOT / "scripts/build_approved_native_gui.py"
    )
    assert spec is not None and spec.loader is not None
    bundle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    with TemporaryDirectory(prefix="arsonmix-legal-") as folder:
        stage = Path(folder)
        bundle.stage(stage)

        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=str(stage), **kwargs)

            def log_message(self, fmt, *args):
                pass

            def do_GET(self):
                route = urlsplit(self.path).path
                if route.startswith("/legal/"):
                    path = DOCUMENTS.get(route.removeprefix("/legal/"))
                    if path is None:
                        self.send_error(404)
                        return
                    data = (ROOT / path).read_bytes()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
                super().do_GET()

        host = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = Thread(target=host.serve_forever, daemon=True)
        worker.start()
        try:
            url = f"http://127.0.0.1:{host.server_port}/index.html"
            with sync_playwright() as playwright:
                browser = playwright.chromium.launch(headless=True)
                for width, height in ((1680, 900), (1100, 650)):
                    page = browser.new_page(viewport={"width": width, "height": height})
                    errors: list[str] = []
                    page.on("pageerror", lambda error: errors.append(str(error)))
                    page.add_init_script("""window.__legalEvents=[];
                      window.__JUCE__={backend:{
                        emitEvent(name,payload){window.__legalEvents.push({name,payload});},
                        addEventListener(){ }
                      }};""")
                    page.goto(url)
                    entry = page.locator("#armix-open-legal")
                    entry.wait_for(state="attached", timeout=15000)
                    backdrop = page.locator("#armix-legal-backdrop")
                    assert not backdrop.is_visible(), "Legal modal must not appear at startup"
                    assert page.locator("#armix-open-legal").count() == 1
                    page.locator("#moreTools > summary").click()
                    entry.click()
                    assert backdrop.is_visible(), "About & Licenses did not open"
                    assert page.locator('#armix-legal-panel[role="dialog"][aria-modal="true"]').count() == 1
                    page.wait_for_function("""()=>document.querySelector('#armix-legal-text')
                      ?.textContent.includes('ArSonMix')""")
                    page.locator('.armix-legal-tab[data-doc="agpl"]').click()
                    page.wait_for_function("""()=>document.querySelector('#armix-legal-text')
                      ?.textContent.includes('END OF TERMS AND CONDITIONS')""")
                    assert page.locator("#armix-legal-text").text_content().__len__() > 30000
                    page.locator('.armix-legal-tab[data-doc="hrir"]').click()
                    page.wait_for_function("""()=>document.querySelector('#armix-legal-text')
                      ?.textContent.includes('Bill Gardner')""")
                    page.locator('.armix-legal-tab[data-doc="inter"]').click()
                    page.wait_for_function("""()=>document.querySelector('#armix-legal-text')
                      ?.textContent.includes('SIL OPEN FONT LICENSE')""")
                    page.locator('.armix-legal-tab[data-doc="overview"]').click()
                    page.wait_for_function("""()=>document.querySelector('#armix-legal-text')
                      ?.textContent.includes('WITHOUT ANY WARRANTY')""")
                    page.locator("#armix-legal-footer button").first.click()
                    assert page.evaluate("""()=>window.__legalEvents.some(
                      event=>event.name==='arMixOpenLegalSource')""")
                    geometry = page.evaluate("""()=>{
                      const p=document.getElementById('armix-legal-panel')
                        .getBoundingClientRect();
                      const nav=document.getElementById('armix-legal-nav')
                        .getBoundingClientRect();
                      const b=document.getElementById('armix-legal-backdrop')
                        .getBoundingClientRect();
                      return {panel:p.toJSON(),nav:nav.toJSON(),backdrop:b.toJSON(),
                        width:innerWidth,height:innerHeight};
                    }""")
                    assert geometry["panel"]["left"] >= 0, geometry
                    assert geometry["panel"]["right"] <= width, geometry
                    assert geometry["panel"]["bottom"] <= height, geometry
                    assert geometry["nav"]["right"] < geometry["panel"]["right"], geometry
                    page.screenshot(path=str(ROOT / f"arsonmix-legal-center-{width}.png"),
                                    full_page=False)
                    page.keyboard.press("Escape")
                    assert not backdrop.is_visible(), "Escape did not close legal modal"
                    assert page.evaluate("""()=>document.activeElement===
                      document.querySelector('#moreTools > summary')"""), (
                          "Close must return keyboard focus to a visible control"
                      )
                    assert not errors, (width, errors)
                    page.close()
                browser.close()
        finally:
            host.shutdown()
            host.server_close()
            worker.join(timeout=3)
    print("PASS: native-only legal center, full notices, two Chromium sizes and accessible close")


if __name__ == "__main__":
    main()
