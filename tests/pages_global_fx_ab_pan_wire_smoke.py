"""Physical mixer UX: PAN extremes, contextual GAIN/WIRE, global FX A/B.

Runs the one compiled public /preview/ renderer. Browser sound is simulated;
actual native PCM bypass must pass the separate JUCE/Windows contracts.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE = Path(__file__).resolve().parents[1] / "dist-pages/preview/index.html"


def pan(page, value):
    return page.locator('#tracks .strip[data-id="1"] .pan-section').evaluate(
        """(row,value)=>{
          const slider=row.querySelector('input.center-pan');
          slider.value=String(value);
          slider.dispatchEvent(new Event('input',{bubbles:true}));
          slider.dispatchEvent(new Event('change',{bubbles:true}));
          const text=row.querySelector('output'), r=text.getBoundingClientRect(),
                track=row.querySelector('.pan-control').getBoundingClientRect(),
                strip=row.closest('.strip').getBoundingClientRect();
          return {text:text.textContent, clipped:text.scrollWidth>text.clientWidth+1,
            overflow:r.right>strip.right+0.8, right:r.right, strip:strip.right,
            font:parseFloat(getComputedStyle(text).fontSize),
            panFont:parseFloat(getComputedStyle(row.querySelector('.pan-icon')).fontSize),
            gap:r.left-track.right,sliderWidth:track.width,readoutWidth:r.width,
            centerError:Math.abs((track.left+track.right)/2-(strip.left+strip.right)/2),
            sliderCenter:(track.left+track.right)/2,stripCenter:(strip.left+strip.right)/2};
        }""", value)


def rack(page, channel):
    return page.locator(f'#tracks .strip[data-id="{channel}"] .fx-section').evaluate(
        """el=>({rack:el.dataset.fxChainEnabled,global:el.dataset.globalFxEnabled,
          slots:[...el.querySelectorAll('.fxslot-row:not(.is-empty)')].map(row=>({
            id:row.dataset.insertId,individual:row.dataset.fxSlotEnabled,
            effective:row.dataset.fxEffective
          }))})""")


def run(page, width, height):
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.goto(PAGE.as_uri())
    page.locator('#tracks .strip[data-id="1"]').wait_for()
    # Legacy density select is hidden in the approved operator GUI.
    # Dispatch the actual reducer event rather than a user gesture on a
    # hidden element or a value-only assignment that never updates layout.
    for density in ("comfort","standard","compact"):
        page.locator("#densitySelect").evaluate(
            "(select,value)=>{select.value=value;select.dispatchEvent(new Event('change',{bubbles:true}));}",density)
        for value,label in [(-100,"L100"),(0,"CEN"),(100,"R100")]:
            seen=pan(page,value)
            assert seen["text"]==label,(width,density,label,seen)
            assert not seen["clipped"] and not seen["overflow"],(width,density,label,seen)
            assert 8.9<=seen["font"]<=9.1,(width,density,"caption font",seen)
            assert abs(seen["font"]-seen["panFont"])<.2,(width,density,"PAN typography match",seen)
            assert seen["gap"]>=0 and seen["sliderWidth"]>=30,(width,density,"PAN collision",seen)
            if label in ("L100","R100"):
                assert 24<=seen["readoutWidth"]<=26,(width,density,"caption yields to slider",seen)
            if label=="CEN":
                assert 12<=seen["readoutWidth"]<25,(width,density,"CEN remains inside gutter",seen)
            assert seen["centerError"]<=1,(width,density,"slider must be centered",seen)
    page.locator("#densitySelect").evaluate(
        "(select)=>{select.value='comfort';select.dispatchEvent(new Event('change',{bubbles:true}));}")
    # The operator's folded WIRE intent must be restored on FX mode.
    body=page.locator('.console-body')
    assert "collapsed-send" in (body.get_attribute("class") or "")
    page.locator('#gainView').click()
    assert "collapsed-send" not in (body.get_attribute("class") or "")
    assert page.locator('.section-rail [data-section="send"]').get_attribute(
        "aria-expanded")=="true"
    page.locator('#fxView').click()
    assert "collapsed-send" in (body.get_attribute("class") or "")
    # A manual WIRE rail choice in GAIN is a stronger operator intent.
    page.locator('#gainView').click()
    page.locator('.section-rail [data-section="send"]').click()
    page.locator('.section-rail [data-section="send"]').click()
    page.locator('#fxView').click()
    assert "collapsed-send" not in (body.get_attribute("class") or "")
    # Global A/B overrides effective FX only; insert IDs, individual rack
    # and slot flags remain preserved. Master is included, not just CH01.
    ch_before=rack(page,1)
    main_before=page.locator('.strip.master .fx-section').evaluate(
        "el=>el.dataset.fxChainEnabled")
    assert ch_before["slots"] and ch_before["slots"][0]["effective"]=="true"
    assert main_before=="true"
    toggle=page.locator('#globalFxToggle')
    power=toggle.locator('.global-fx-power-icon')
    assert toggle.inner_text().strip()=="FX"
    assert power.evaluate("el=>el.tagName.toLowerCase()")=="svg"
    assert toggle.locator("button").count()==0
    assert power.evaluate("el=>getComputedStyle(el).backgroundImage")=="none"
    def check_fx_vertical_center(state):
        # Compare rendered text glyph positions with the real neighbouring
        # toolbar captions. Checking only the FX button's own center failed:
        # a special 14px linebox/1px offset had moved its baseline.
        positions=toggle.evaluate("""button=>{
          const selectors=['#gainView','#fxView','#sofToggle'];
          const center=r=>(r.top+r.bottom)/2;
          const measure=(element,caption)=>{
            const buttonRect=element.getBoundingClientRect(),range=document.createRange();
            range.selectNodeContents(caption);
            const ink=range.getBoundingClientRect(),style=getComputedStyle(caption);
            return {local:center(ink)-center(buttonRect),absolute:center(ink),
              size:style.fontSize,weight:style.fontWeight,line:style.lineHeight,
              font:style.fontFamily};
          };
          const caption=button.querySelector('.global-fx-label');
          const icon=button.querySelector('.global-fx-power-icon').getBoundingClientRect();
          return {fx:measure(button,caption),
            controls:selectors.map(s=>{let el=document.querySelector(s);return {id:s,...measure(el,el)}}),
            iconOffset:center(icon)-center(button.getBoundingClientRect()),
            labelTransform:getComputedStyle(caption).transform};
        }""")
        assert positions["labelTransform"]=="none",(width,state,positions)
        assert abs(positions["iconOffset"])<=.75,(width,state,positions)
        for ref in positions["controls"]:
            assert positions["fx"]["size"]==ref["size"],(width,state,"font size",positions)
            assert positions["fx"]["weight"]==ref["weight"],(width,state,"font weight",positions)
            assert positions["fx"]["line"]==ref["line"],(width,state,"line-height",positions)
            assert positions["fx"]["font"]==ref["font"],(width,state,"font family",positions)
            assert abs(positions["fx"]["local"]-ref["local"])<=.6,(
                width,state,"FX caption must share toolbar caption baseline",positions)
    check_fx_vertical_center("ON")
    # Approved V6 uses a translucent green pill, not the retired metallic
    # gradient. The full face and both child inks must follow accepted state.
    on_face=toggle.evaluate("""el=>({
      bg:getComputedStyle(el).backgroundColor,
      ink:getComputedStyle(el).color,
      label:getComputedStyle(el.querySelector('.global-fx-label')).color,
      glyph:getComputedStyle(el.querySelector('svg.global-fx-power-icon')).color
    })""")
    assert on_face["bg"]=="rgba(70, 183, 137, 0.125)",on_face
    assert on_face["ink"]==on_face["label"]==on_face["glyph"]=="rgb(166, 231, 202)",on_face
    slot_power=page.locator('#tracks .strip[data-id="1"] [data-util-kind="fx-slot"]').first
    assert slot_power.evaluate("el=>getComputedStyle(el).getPropertyValue('--fx-led').trim()")=="#00b6d5"
    toggle.click()
    assert toggle.get_attribute("aria-pressed")=="false"
    assert toggle.get_attribute("data-fx-power-state")=="bypassed"
    page.mouse.move(0,height-1)
    check_fx_vertical_center("BYPASS")
    off_face=toggle.evaluate("""el=>({
      bg:getComputedStyle(el).backgroundColor,
      ink:getComputedStyle(el).color,
      label:getComputedStyle(el.querySelector('.global-fx-label')).color,
      glyph:getComputedStyle(el.querySelector('svg.global-fx-power-icon')).color
    })""")
    assert off_face["bg"]=="rgba(0, 0, 0, 0)" and off_face["bg"]!=on_face["bg"],off_face
    assert off_face["ink"]==off_face["label"]==off_face["glyph"],off_face
    assert slot_power.evaluate("el=>getComputedStyle(el).getPropertyValue('--fx-led').trim()")=="#34393b"
    assert page.locator(".console-body").evaluate("el=>el.classList.contains('global-fx-bypassed')")
    during=rack(page,1)
    assert during["global"]=="false"
    assert [v["id"] for v in during["slots"]]==[v["id"] for v in ch_before["slots"]]
    assert [v["individual"] for v in during["slots"]]==[
        v["individual"] for v in ch_before["slots"]]
    assert all(v["effective"]=="false" for v in during["slots"])
    master=page.locator('.strip.master .fx-section')
    assert master.get_attribute("data-global-fx-enabled")=="false"
    assert master.get_attribute("data-fx-chain-enabled")=="true"
    page.screenshot(path=f"arsonmix-global-fx-{width}-bypassed.png")
    toggle.click()
    after=rack(page,1)
    assert after==ch_before | {"global":"true"},(ch_before,after)
    assert toggle.evaluate("el=>getComputedStyle(el).backgroundColor")==on_face["bg"]
    assert power.evaluate("el=>getComputedStyle(el).color")==on_face["glyph"]
    assert slot_power.evaluate("el=>getComputedStyle(el).getPropertyValue('--fx-led').trim()")=="#00b6d5"
    page.screenshot(path=f"arsonmix-global-fx-{width}-enabled.png")
    # Individual OFF remains OFF across the master A/B roundtrip.
    slot_power.click()
    assert slot_power.get_attribute("aria-pressed")=="false"
    toggle.click()
    toggle.click()
    assert slot_power.evaluate("el=>getComputedStyle(el).getPropertyValue('--fx-led').trim()")=="#34393b"
    assert rack(page,1)["slots"][0]["individual"]=="false"
    assert not errors,errors
    page.close()


def main():
    assert PAGE.is_file(),"Publish Pages into dist-pages/ before browser test"
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        try:
            for width,height in [(390,844),(1366,768),(1680,900)]:
                page=browser.new_page(viewport={"width":width,"height":height})
                run(page,width,height)
        finally:
            browser.close()
    print("PASS PAN L100/R100 + GAIN/WIRE + non-destructive GLOBAL FX A/B")


if __name__=="__main__":
    main()
