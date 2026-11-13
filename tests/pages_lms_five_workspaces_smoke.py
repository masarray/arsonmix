#!/usr/bin/env python3
"""Real Chromium UX FAT for the ONE canonical ArSonMix /preview/ LMS workspace.
No fake ASIO, audio, protection, or hardware ACK claims.
"""
from pathlib import Path
import subprocess,sys,tempfile
from playwright.sync_api import sync_playwright
from approved_publication_fixture import published_console

ROOT=Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix="arsonmix-lms-ui-") as name:
        target=Path(name)/"dist"
        subprocess.run([sys.executable,str(ROOT/"scripts/build_pages.py"),
                        "--out",str(target),"--sha","lms-ui-exact-source",
                        "--branch","lms"],cwd=ROOT,check=True,capture_output=True,text=True)
        public=published_console(target)
        with sync_playwright() as pw:
            browser=pw.chromium.launch(headless=True)
            for width in (1366,1680):
                page=browser.new_page(viewport={"width":width,"height":900})
                errors=[]
                page.on("pageerror",lambda e:errors.append(str(e)))
                page.goto(public.as_uri())
                speaker=page.locator("#lmsOpen")
                assert speaker.count()==1,"one LMS entrypoint inside approved mixer"
                speaker.click()
                body=page.locator("#lmsWorkspace")
                assert body.is_visible() and page.locator(".app.lms-active").count()==1
                assert body.locator(".lms-nav button").count()==5
                assert "DESIGN ONLY" in body.inner_text()
                assert "OUTPUT DISCONNECTED" in body.inner_text()
                assert page.locator(".console-pane").is_hidden()
                assert not body.evaluate("(el)=>!!el.querySelector('audio,video,iframe')"), \
                    "LMS cannot instantiate any competing audio graph or renderer"

                body.locator('button[data-page="crossover"]').click()
                assert body.locator(".lms-frequency path").count()==3
                before=body.locator(".lms-frequency path").first.get_attribute("d")
                body.locator("#lms-x0").fill("180")
                body.locator("#lms-x0").press("Tab")
                after=body.locator(".lms-frequency path").first.get_attribute("d")
                assert before!=after,"Crossover must plot recalculated LR4, not static rectangles"
                assert "48 kHz" in body.inner_text()
                if width==1680:
                    page.screenshot(path="pages-lms-crossover-1680.png",full_page=False)
                body.locator("#lms-way").select_option("4")
                assert "Review structural change" in body.inner_text()
                body.locator('[data-action="confirm"]').click()
                assert body.locator(".lms-frequency path").count()==4

                body.locator('button[data-page="matrix"]').click()
                matrix=body.locator(".lms-matrix")
                assert matrix.locator("tbody tr").count()==8
                first=matrix.locator('[data-route-port="DEMO-01"][data-route-section="L:1"]')
                first.click()
                assert matrix.locator('[data-route-port="DEMO-01"][data-route-section="L:1"]').get_attribute("aria-pressed")=="true"
                another=matrix.locator('[data-route-port="DEMO-01"][data-route-section="L:2"]')
                another.click()
                assert matrix.locator('[data-route-port="DEMO-01"][data-route-section="L:2"]').get_attribute("aria-pressed")=="true"
                assert matrix.locator('[data-route-port="DEMO-01"][data-route-section="L:1"]').get_attribute("aria-pressed")=="false", \
                    "one physical destination cannot silently sum two crossover sections"
                body.locator('button[data-page="processing"]').click()
                assert "Parametric EQ" in body.inner_text()
                body.locator('[data-action="addPeq"]').click()
                assert body.locator('[data-select-band="0"]').count()==1
                before=body.locator(".lms-frequency path").first.get_attribute("d")
                body.locator('[data-peq-field="gainDb"]').fill("6")
                body.locator('[data-peq-field="gainDb"]').press("Tab")
                after=body.locator(".lms-frequency path").first.get_attribute("d")
                assert before!=after,"EQ must plot real biquad response"
                if width==1680:
                    page.screenshot(path="pages-lms-eq-1680.png",full_page=False)
                body.locator('[data-dsp-tab="protection"]').click()
                assert "NOT CALIBRATED" in body.inner_text()

                body.locator('button[data-page="alignment"]').click()
                assert body.locator("#lms-room-svg").count()==1
                assert "Suggested" in body.inner_text()
                body.locator("#lms-align-port").select_option("DEMO-06")
                assert "ms" in body.inner_text()
                body.locator('[data-action="planDelay"]').click()
                assert "Review structural change" in body.inner_text()
                body.locator('[data-action="cancel"]').click()
                body.locator('[data-action="planDelay"]').click()
                body.locator('[data-action="confirm"]').click()
                if width==1680:
                    page.screenshot(path="pages-lms-alignment-1680.png",full_page=False)
                body.locator('button[data-page="processing"]').click()
                body.locator('[data-dsp-tab="level"]').click()
                delay=float(body.locator("#lms-delay").input_value())
                assert delay>0,"Nearer speaker requires positive extra delay draft"
                assert "DESIGN ONLY" in body.inner_text()
                assert not errors,(width,errors)
                speaker.click()
                assert body.is_hidden() and page.locator(".app.lms-active").count()==0
                assert page.locator(".console-pane").is_visible()
                page.close()
            browser.close()
    print("PASS LMS canonical /preview/: 5 pages, calculated LR4/PEQ curves, exclusive patch,"
          " draft geometry, fail-closed no native ACK; 2 viewport Chromium UX")

if __name__=="__main__":
    main()
