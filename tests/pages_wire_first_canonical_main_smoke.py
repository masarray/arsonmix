#!/usr/bin/env python3
"""WIRE-first A: native-execution NOT claimed. Real Chromium control-plane integration."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"site/preview-v2/index.html"

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

def run():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
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
            assert page.locator('#master .wire-node-in').get_attribute("data-wire-main-edge")==initial
            assert control.get_attribute("aria-pressed")=="true"
            # Distinct disconnect is reversible and only affects this channel.
            control.click()
            assert edge(page,5,"out") is None,(width,"MAIN disconnect not real")
            assert edge(page,6,"out"),(width,"neighbor MAIN edge removed")
            assert page.locator('#master .wire-node-in').get_attribute("data-wire-main-edge") is None
            page.locator("#wireNavigatorToggle").click()
            page.locator("#wireWsUndo").click()
            assert edge(page,5,"out")==initial,(width,"Undo changed identity")
            page.locator("#wireNavigatorToggle").click()
            page.locator("#wireConfigClose").click()
            # The accepted WIRE to another channel must preserve the MAIN route.
            if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                page.locator('.section-rail [data-section="send"]').click()
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
        page.locator("#sceneSafe").click()
        page.locator("#sceneSelect").select_option("1")
        page.locator("#recallScene").click()
        assert edge(page,5,"out") is None,"Snapshot SAFE overwrote live MAIN route"
        page.close()
        browser.close()
    print("PASS WIRE A: real Chrome defaults, paired MAIN IDs, detach/undo, cycle guard, Scene/SAFE, 3 widths")

if __name__=="__main__":
    run()
