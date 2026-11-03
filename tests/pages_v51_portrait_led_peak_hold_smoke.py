#!/usr/bin/env python3
"""V51 owner reference: portrait segmented LEDs, stepped dBFS, color-matched peak hold and quiet fader shadow.

This tests SIMULATED Pages values only, never physical PCM or JUCE telemetry.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
HTML=ROOT/"site/preview-v2/index.html"
CSS=ROOT/"site/preview-v2/assets/mixer-meter-display.css"

def inspect_geometry(page,density,w):
    page.locator("#densitySelect").select_option(density)
    page.wait_for_selector('#tracks .strip[data-id="1"] .meter-peak-hold')
    data=page.evaluate("""() => {
      const elems=[document.querySelector('#tracks .strip[data-id="1"]'),
                   document.querySelector('#tracks .strip .meter-box.stereo')?.closest('.strip'),
                   document.querySelector('#master .strip')].filter(Boolean);
      return elems.map(root=>{
        const bar=root.querySelector('.meter-box');
        const lane=root.querySelector('.meter-lane');
        const fill=lane.querySelector('.meter-fill');
        const hold=lane.querySelector('.meter-peak-hold');
        const cap=root.querySelector('.fader-travel .cap');
        const css=getComputedStyle(fill),shell=getComputedStyle(lane);
        return {boxW:bar.getBoundingClientRect().width,
          laneW:lane.getBoundingClientRect().width,fillW:fill.getBoundingClientRect().width,
          fillInset:css.left,fillRight:css.right,
          ramp:css.backgroundImage,peakRamp:getComputedStyle(hold).backgroundImage,
          idlePeak:getComputedStyle(hold).opacity,
          offCells:getComputedStyle(lane,'::before').backgroundImage,
          segmentMask:getComputedStyle(lane,'::after').backgroundImage,
          faderShadow:getComputedStyle(cap).boxShadow,
          faderFilter:getComputedStyle(cap).filter};
      });
    }""")
    assert len(data)>=2,(w,density,'missing source or master',data)
    for e in data:
        assert abs(e['boxW']-70)<.7,(w,density,'M3 housing drift',e)
        assert abs(e['laneW']-10)<.8,(w,density,'M3 lane drift',e)
        assert abs(e['fillW']-6)<.8,(w,density,'portrait width',e)
        assert e['fillInset']=='2px' and e['fillRight']=='2px',(w,density,'optical well',e)
        assert e['ramp']==e['peakRamp'],(w,density,'peak must match full-height gradient',e)
        assert '12px' in e['offCells'] and '14px' in e['offCells'],(w,density,'portrait pitch',e)
        assert '12px' in e['segmentMask'] and '14px' in e['segmentMask'],(w,density,'step separation',e)
        assert e['faderFilter']=='none' and e['faderShadow']!='none',(w,density,'quiet thumb shadow',e)
    return data

def inspect_motion(page):
    menu=page.locator('#moreTools')
    menu.locator('summary').click()
    page.locator('#preview').click()
    lane=page.locator('#tracks .strip[data-id="1"] .meter-lane').first
    fill=lane.locator('.meter-fill')
    hold=lane.locator('.meter-peak-hold')
    page.wait_for_function("""() => {
      const e=document.querySelector('#tracks .strip[data-id="1"] .meter-peak-hold');
      return e&&getComputedStyle(e).opacity==='1'
        && Number.parseFloat(e.style.getPropertyValue('--peak-extent'))>10;
    }""",timeout=9000)
    measured=lane.evaluate("""el=>{
      const fill=el.querySelector('.meter-fill'),marker=el.querySelector('.meter-peak-hold');
      const h=el.clientHeight;
      const pct=parseFloat(fill.style.getPropertyValue('--lit'));
      const lit=pct*h/100;
      return {h,lit,rows:Math.floor(h/14),peak:parseFloat(marker.style.getPropertyValue('--peak-extent'))};
    }""")
    assert measured['rows']>=8 and measured['lit']>0,measured
    assert abs(measured['lit']/14-round(measured['lit']/14))<.06,(
        'meter uses half/fractional LED',measured)
    assert measured['peak']>=measured['lit'],('peak not above level',measured)
    assert abs((measured['peak']+2)/14-round((measured['peak']+2)/14))<.05,(
        'peak marker is not aligned to a full portrait LED cell',measured)
    assert '12px' in hold.evaluate("e=>getComputedStyle(e).clipPath"),(
        'peak hold must illuminate full 12px cell, not a thin line')
    page.screenshot(path='pages-v51-portrait-meter-1680.png')
    # Holding the physical MUTE must turn off live level without painting a
    # fake zero-dB CLIP, while peak LED remains for a short genuine hold.
    page.locator('#tracks .strip[data-id="1"] [data-hw-kind="mute"]').click()
    page.wait_for_timeout(220)
    held=lane.evaluate("""el=>({
      pct:parseFloat(el.querySelector('.meter-fill').style.getPropertyValue('--lit')),
      peak:parseFloat(el.querySelector('.meter-peak-hold').style.getPropertyValue('--peak-extent')),
      shown:getComputedStyle(el.querySelector('.meter-peak-hold')).opacity
    })""")
    assert held['pct']==0,('MUTE did not quench meter',held)
    assert held['shown']=='1',('peak-hold vanished instantly rather than decaying',held)
    page.wait_for_timeout(1600)
    released=float(hold.evaluate("e=>parseFloat(e.style.getPropertyValue('--peak-extent'))"))
    assert released<=held['peak'],('peak never descends after release',held,released)
    assert page.locator('#tracks .strip[data-id="1"] .meter-box .peak-led').count()>=1,(
       'Independent CLIP lamps lost')
    page.screenshot(path='pages-v51-peak-hold-release-1680.png')
    # Stop MUST clear both LED level and held marker, without leftover RAF.
    menu.locator('summary').click()
    page.locator('#preview').click()
    assert hold.evaluate("e=>getComputedStyle(e).opacity")=='0',(
       'stopped demo still shows peak hold')

def main():
    css=CSS.read_text(encoding='utf-8')
    assert '--meter-led-ramp' in css and '.meter-peak-hold' in css
    with sync_playwright() as p:
        browser=p.chromium.launch(headless=True)
        for w,h in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={'width':w,'height':h})
            errors=[]
            page.on('pageerror',lambda e:errors.append(str(e)))
            page.goto(HTML.as_uri())
            page.wait_for_selector('#tracks .strip[data-id="1"]')
            for density in ('comfort','standard','compact'):
                inspect_geometry(page,density,w)
            page.locator('#densitySelect').select_option('comfort')
            if w==1680:inspect_motion(page)
            assert not errors,(w,h,'JavaScript errors',errors)
            page.close()
        browser.close()
    print('PASS V51: 6x12 portrait segments, 14px snap, L/R gradient-aligned peak-hold, light original thumb shadow, calibrated M3 housing')

if __name__=='__main__':
    main()
