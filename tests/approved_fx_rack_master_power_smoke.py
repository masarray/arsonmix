"""P0 FX rack: header bypass overrides effective power, never saved slots.

Chromium exercises ONE actual built /preview/ GUI (also used by native JUCE).
Public Pages only demonstrates the control state; real DSP is checked in
native engine contracts/Windows CTest and physical hardware FAT separately.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).resolve().parents[1]/"dist-pages/preview/index.html"


def snapshot(page, channel=1):
    root=page.locator(f'#tracks .strip[data-id="{channel}"] .fx-section')
    return root.evaluate("""el=>({
      rack:el.dataset.fxChainEnabled,
      header:el.querySelector('[data-action="fxOn"]').getAttribute('aria-pressed'),
      slots:[...el.querySelectorAll('.fxslot-row:not(.is-empty)')].map(row=>({
        id:row.dataset.insertId,
        individual:row.dataset.fxSlotEnabled,
        effective:row.dataset.fxEffective,
        power:row.querySelector('[data-action="slotPower"]')
          ?.getAttribute('aria-pressed'),
        bypassed:row.classList.contains('is-bypassed')
      }))
    })""")


def main():
    assert PAGE.is_file(), "build official /preview/ first"
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        try:
            for width,height in [(390,844),(1366,768),(1680,900)]:
                page=browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror",lambda e:errors.append(str(e)))
                page.goto(PAGE.as_uri())
                page.locator("#tracks .strip[data-id='1']").wait_for()
                root=page.locator('#tracks .strip[data-id="1"]')
                initial=snapshot(page)
                assert len(initial["slots"])==1 and initial["slots"][0]["power"]=="true",initial
                # Load the second effect using the actual UI picker.
                root.locator('.fx-pick.empty').click()
                page.locator('#fastResults [data-fast-id]').first.click()
                both=snapshot(page)
                assert len(both["slots"])==2 and all(
                    x["power"]=="true" for x in both["slots"]),both
                # Individual OFF is a stored preference, never overwritten.
                root.locator('[data-action="slotPower"][data-slot="0"]').click()
                individual=snapshot(page)
                assert individual["slots"][0]["individual"]=="false",individual
                assert individual["slots"][1]["individual"]=="true",individual
                other=snapshot(page,2)
                root.locator('[data-action="fxOn"]').click()
                off=snapshot(page)
                assert off["header"]==off["rack"]=="false",off
                assert all(row["power"]=="false" and
                           row["effective"]=="false" and row["bypassed"]
                           for row in off["slots"]),off
                assert [r["individual"] for r in off["slots"]]==["false","true"],off
                assert [r["id"] for r in off["slots"]]==[r["id"] for r in individual["slots"]],off
                assert snapshot(page,2)==other,"Rack power leaked into other channel"
                root.locator('[data-action="fxOn"]').click()
                restored=snapshot(page)
                assert restored["header"]==restored["rack"]=="true",restored
                assert [r["power"] for r in restored["slots"]]==["false","true"],restored
                assert [r["effective"] for r in restored["slots"]]==["false","true"],restored
                assert [r["individual"] for r in restored["slots"]]==["false","true"],restored
                assert [r["id"] for r in restored["slots"]]==[r["id"] for r in individual["slots"]],restored
                assert not errors,errors
                print("PASS FX rack master power + saved individual state",width,height)
                page.close()
        finally:
            browser.close()


if __name__=="__main__":
    main()
