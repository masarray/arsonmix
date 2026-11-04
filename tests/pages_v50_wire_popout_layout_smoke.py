#!/usr/bin/env python3
"""V50 operator-first routing popouts and compact meter scale (browser reference only)."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HTML=Path(__file__).resolve().parents[1]/"site/preview/index.html"

def launch(page,kind):
    ids={"routes":"wireNavigatorToggle","matrix":"wireMatrixToggle","config":"wireConfigToggle"}
    button=page.locator("#"+ids[kind])
    if button.is_visible():
        button.click()
    else:
        menu=page.locator("#moreTools")
        if not menu.evaluate("e=>e.open"):
            menu.locator("summary").click()
        menu.locator('[data-wire-open="'+kind+'"]').click()

def inspect(page,width,height):
    page.goto(HTML.as_uri())
    page.wait_for_selector("#tracks .strip[data-id='1']")
    title=page.locator(".mixer-title")
    assert abs(title.bounding_box()["height"]-27)<=1.5,(width,height,"title height")
    assert page.locator("#mixerTitleToggle").count()==0
    assert title.locator("#wireMode,#wireMaster,#wireView,#wireNavigatorToggle,#wireMatrixToggle").count()==0
    assert page.locator("#wireLegend").evaluate(
        "e=>!!e.closest('#wireConfigWindow')"
    ),"quick patch and dynamic WIRE context must live inside Config"
    span=page.locator(".strip .meter-box .vu-tick.major span").first
    assert span.count()==1
    edges=span.evaluate("e=>{const s=getComputedStyle(e);return [s.left,s.right]}")
    assert edges==["7px","9px"],(width,height,edges)
    # On smaller displays the same command-bar tools are available from More.
    launch(page,"config")
    assert page.locator("#wireConfigWindow").is_visible()
    assert page.locator("#wireWorkspace").is_hidden()
    cfg=page.locator("#wireConfigWindow")
    assert cfg.locator("#wireMode,#wireMaster,#wireView").count()==3
    cfg.locator("#wireMode").select_option("split")
    cfg.locator("#wireView").select_option("out")
    assert cfg.locator("#wireMode").input_value()=="split"
    assert cfg.locator("#wireView").input_value()=="out"
    page.keyboard.press("Escape")
    assert cfg.is_hidden(),"Escape did not dismiss WIRE configuration"

    launch(page,"routes")
    workspace=page.locator("#wireWorkspace")
    assert workspace.is_visible() and not workspace.evaluate("e=>e.classList.contains('is-matrix')")
    launch(page,"matrix")
    assert workspace.is_visible() and workspace.evaluate("e=>e.classList.contains('is-matrix')")
    launch(page,"config")
    assert workspace.is_hidden() and cfg.is_visible(),"floating tools overlapped one another"
    cfg.locator("#wireConfigClose").click()
    assert cfg.is_hidden()

    # In-strip patching remains direct; config display is cosmetic, never a patch action.
    out=page.locator('#tracks .strip[data-id="1"] [data-action="wireSource"]')
    if not out.is_visible():
        page.locator('.section-rail [data-section="send"]').click()
    out.click()
    page.locator('#tracks .strip[data-id="4"] [data-action="wireRoute"]').click()
    launch(page,"routes")
    # A new direct jack patch can intentionally clear the transient OUT arm.
    # Select the source in the real Navigator before reading committed routes.
    workspace.locator('#wireWsSourceSelect').select_option('1')
    workspace.locator('[data-ws-mode="routes"]').click()
    assert workspace.locator('[data-ws-inspect="1:4"]').count()>=1,(
        width,height,"committed WIRE missing from route inspector")
    workspace.locator("#wireWsClose").click()
    launch(page,"config")
    assert cfg.locator("#wireView").input_value()=="out"
    assert cfg.locator("#wireMode").input_value()=="split"
    cfg.locator("#wireConfigClose").click()
    assert abs(title.bounding_box()["height"]-27)<=1.5

def main():
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for width,height in ((1024,768),(1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda error:errors.append(str(error)))
            inspect(page,width,height)
            assert not errors,(width,height,errors)
            if width==1366:
                page.screenshot(path="pages-v50-compact-console-1366.png")
            page.close()
        browser.close()
    print("PASS V50: Routes/Matrix/WIRE Setup popouts, 27px status title, 7/9px dBFS ticks, direct patch intact")

if __name__=="__main__":
    main()
