"""Real pointer/keyboard contract for single approved /preview/ FX picker and Parametric EQ.
Browser-only visual demo is not audible PCM or hardware FAT.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"dist-pages/preview/index.html"

def main():
    assert PAGE.is_file(), "Build approved /preview/ first"
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        try:
            for width,height in [(390,844),(1366,768),(1680,900)]:
                page=browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror", lambda e: errors.append(str(e)))
                page.goto(PAGE.as_uri())
                strip=page.locator('#tracks .strip[data-id="1"]')
                strip.wait_for()
                assert page.locator('#mxBuiltinEq').is_hidden()
                strip.locator('.fx-pick.empty').click()
                picker=page.locator('#fastPicker')
                assert picker.is_visible()
                choice=page.locator('#fastResults [data-fast-builtin-eq]')
                assert choice.count()==1 and "Parametric EQ" in choice.inner_text()
                choice.click()
                assert picker.is_hidden()
                editor=page.locator('#mxBuiltinEq')
                assert editor.is_hidden(), "Selecting a plugin cannot auto-open the editor"
                row=strip.locator('.fxslot-row.is-builtin')
                assert row.count()==1 and "Parametric EQ" in row.inner_text()
                assert row.get_attribute('data-fx-effective')=="true"
                assert "FX 2" in strip.locator('.fx-section .hint').inner_text()
                row.locator('[data-action="openBuiltinEq"]').click()
                assert editor.is_visible()
                assert editor.locator('.mx-eq-mode').inner_text()=="UI DEMO · NO AUDIO"
                title=editor.locator('.mx-eq-top')
                rect=editor.bounding_box()
                bar=title.bounding_box()
                assert rect and bar
                x=bar["x"]+min(110,bar["width"]/3)
                y=bar["y"]+bar["height"]/2
                page.mouse.move(x,y)
                page.mouse.down()
                page.mouse.move(x+37,y+23,steps=5)
                page.mouse.up()
                moved=editor.bounding_box()
                assert moved and moved["x"]>=rect["x"]+23 and moved["y"]>=rect["y"]+13,(rect,moved)
                band=editor.locator('.mx-eq-band[data-slot="3"]')
                band.click()
                assert "selected" in band.get_attribute("class")
                editor.locator('.mx-eq-switch').click()
                assert editor.locator('.mx-eq-switch').get_attribute('aria-pressed')=="true"
                freq=editor.get_by_label('FREQUENCY · Hz')
                freq.fill("735")
                freq.press("Tab")
                assert freq.input_value()=="735"
                gain=editor.get_by_label('GAIN · dB')
                gain.fill("4.0")
                gain.press("Tab")
                assert gain.input_value()=="4.0"
                editor.locator('.mx-eq-close').click()
                assert editor.is_hidden()
                row.locator('[data-action="openBuiltinEq"]').click()
                assert editor.is_visible()
                assert editor.get_by_label('FREQUENCY · Hz').input_value()=="735"
                editor.locator('.mx-eq-close').click()
                row.locator('[data-action="builtinEqRemove"]').click()
                assert strip.locator('.fxslot-row.is-builtin').count()==0
                assert not errors, errors
                print("PASS Parametric EQ pick/insert/open/drag/edit/reopen/remove",width,height)
                page.close()
        finally:
            browser.close()

if __name__=="__main__":
    main()
